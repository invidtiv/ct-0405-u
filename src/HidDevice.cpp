#include "HidDevice.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>

namespace ct0405 {

HidDevice::HidDevice() {
    m_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

HidDevice::~HidDevice() {
    Close();
    if (m_stop_event) {
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
    }
}

bool HidDevice::Open(const DiscoveredDevice& device_info) {
    Close();

    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    m_info = device_info;
    m_device_lost.store(false, std::memory_order_release);

    // Read/write first, then read-only. There is deliberately no zero-access
    // fallback: a handle opened with no access can never satisfy ReadFile, so
    // it would report a connected device that produces no packets forever.
    m_handle = CreateFileW(
        device_info.device_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (m_handle == INVALID_HANDLE_VALUE) {
        m_handle = CreateFileW(
            device_info.device_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );
    }

    if (m_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Query exact report length via HID parser
    PHIDP_PREPARSED_DATA pPreparsed = nullptr;
    if (HidD_GetPreparsedData(m_handle, &pPreparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pPreparsed, &caps) == HIDP_STATUS_SUCCESS) {
            if (caps.InputReportByteLength > 0) {
                m_info.input_report_byte_length = caps.InputReportByteLength;
            }
        }
        HidD_FreePreparsedData(pPreparsed);
    }

    ResetEvent(m_stop_event);
    return true;
}

void HidDevice::Close() {
    StopReading();

    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        // CancelIoEx cancels regardless of which thread issued the I/O;
        // plain CancelIo only cancels the calling thread's requests and was
        // therefore always a no-op here.
        CancelIoEx(m_handle, nullptr);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool HidDevice::StartReading() {
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    if (m_handle == INVALID_HANDLE_VALUE || m_is_reading.load(std::memory_order_acquire)) {
        return false;
    }

    // A previous worker may have exited on its own (device removed) leaving a
    // joinable thread object behind. Reap it before starting another.
    if (m_reader_thread.joinable()) {
        m_reader_thread.join();
    }

    m_is_reading.store(true, std::memory_order_release);
    ResetEvent(m_stop_event);
    m_reader_thread = std::thread(&HidDevice::ReadWorkerThread, this);
    return true;
}

void HidDevice::StopReading() {
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    m_is_reading.store(false, std::memory_order_release);
    if (m_stop_event) {
        SetEvent(m_stop_event);
    }

    // Join unconditionally when joinable. The old code only joined if it
    // believed the worker was still running, so a worker that exited by itself
    // left a joinable std::thread - destroying that calls std::terminate.
    if (m_reader_thread.joinable()) {
        m_reader_thread.join();
    }
}

void HidDevice::ReadWorkerThread() {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        m_is_reading.store(false, std::memory_order_release);
        m_device_lost.store(true, std::memory_order_release);
        return;
    }

    const DWORD bytes_to_read = static_cast<DWORD>(std::max<size_t>(64, m_info.input_report_byte_length));
    std::vector<uint8_t> buffer(bytes_to_read, 0);

    HANDLE wait_handles[2] = { m_stop_event, overlapped.hEvent };
    bool device_lost = false;

    // Invariants this loop maintains:
    //   1. At most one ReadFile is outstanding at any moment.
    //   2. The loop never exits leaving an I/O pending against `overlapped`
    //      or `buffer`, both of which live on this thread's stack.
    while (m_is_reading.load(std::memory_order_acquire)) {
        ResetEvent(overlapped.hEvent);
        DWORD bytes_read = 0;

        if (!ReadFile(m_handle, buffer.data(), bytes_to_read, &bytes_read, &overlapped)) {
            const DWORD error = GetLastError();

            if (error != ERROR_IO_PENDING) {
                // Anything other than "pending" means this handle is finished.
                // Retrying in a sleep loop only burned CPU on a dead device.
                device_lost = true;
                break;
            }

            // Wait forever, not 200ms: the stop event is in the wait set, so
            // it - not a timeout - is what breaks this wait. Timing out here
            // and looping was what issued a second ReadFile over a still
            // pending one, every 200ms, for as long as the pen was idle.
            const DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);

            if (wait_res == WAIT_OBJECT_0) {
                // Stop requested. Cancel and then block until the cancellation
                // is acknowledged, so no I/O outlives this stack frame.
                CancelIoEx(m_handle, &overlapped);
                GetOverlappedResult(m_handle, &overlapped, &bytes_read, TRUE);
                break;
            }

            if (wait_res != WAIT_OBJECT_0 + 1) {
                device_lost = true;   // WAIT_FAILED / WAIT_ABANDONED
                break;
            }

            if (!GetOverlappedResult(m_handle, &overlapped, &bytes_read, FALSE)) {
                const DWORD result_error = GetLastError();
                if (result_error == ERROR_OPERATION_ABORTED) {
                    break;            // cancelled during shutdown
                }
                device_lost = true;
                break;
            }
        }

        if (bytes_read > 0 && m_packet_callback) {
            m_packet_callback(buffer.data(), bytes_read);
        }
    }

    CloseHandle(overlapped.hEvent);
    m_is_reading.store(false, std::memory_order_release);

    // Publishing this is what lets IsOpen() go false and the watcher thread
    // notice the device is gone and start looking for it again.
    if (device_lost) {
        m_device_lost.store(true, std::memory_order_release);
    }
}

} // namespace ct0405
