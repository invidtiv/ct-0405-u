#include "HidDevice.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "hid.lib")

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

    m_info = device_info;

    // 1. Try Shared Read/Write
    m_handle = CreateFileW(
        device_info.device_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    // 2. Try Shared Read only
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

    // 3. Try standard query access
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_handle = CreateFileW(
            device_info.device_path.c_str(),
            0,
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

    if (m_handle != INVALID_HANDLE_VALUE) {
        CancelIo(m_handle);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool HidDevice::StartReading() {
    if (m_handle == INVALID_HANDLE_VALUE || m_is_reading) {
        return false;
    }

    m_is_reading = true;
    ResetEvent(m_stop_event);
    m_reader_thread = std::thread(&HidDevice::ReadWorkerThread, this);
    return true;
}

void HidDevice::StopReading() {
    if (m_is_reading) {
        m_is_reading = false;
        if (m_stop_event) {
            SetEvent(m_stop_event);
        }
        if (m_handle != INVALID_HANDLE_VALUE) {
            CancelIo(m_handle);
        }
        if (m_reader_thread.joinable()) {
            m_reader_thread.join();
        }
    }
}

void HidDevice::ReadWorkerThread() {
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        m_is_reading = false;
        return;
    }

    const DWORD bytes_to_read = static_cast<DWORD>(std::max<size_t>(64, m_info.input_report_byte_length));
    std::vector<uint8_t> buffer(bytes_to_read, 0);

    HANDLE wait_handles[2] = { m_stop_event, overlapped.hEvent };

    while (m_is_reading && m_handle != INVALID_HANDLE_VALUE) {
        ResetEvent(overlapped.hEvent);
        DWORD bytes_read = 0;

        BOOL read_status = ReadFile(
            m_handle,
            buffer.data(),
            bytes_to_read,
            &bytes_read,
            &overlapped
        );

        if (!read_status) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, 200);
                if (wait_res == WAIT_OBJECT_0) {
                    CancelIo(m_handle);
                    break;
                } else if (wait_res == WAIT_OBJECT_0 + 1) {
                    if (GetOverlappedResult(m_handle, &overlapped, &bytes_read, FALSE)) {
                        if (bytes_read > 0 && m_packet_callback) {
                            m_packet_callback(buffer.data(), bytes_read);
                        }
                    }
                }
            } else if (error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_HANDLE_EOF || error == ERROR_INVALID_HANDLE) {
                break;
            } else {
                Sleep(20);
            }
        } else {
            if (bytes_read > 0 && m_packet_callback) {
                m_packet_callback(buffer.data(), bytes_read);
            }
        }
    }

    CloseHandle(overlapped.hEvent);
    m_is_reading = false;
}

} // namespace ct0405
