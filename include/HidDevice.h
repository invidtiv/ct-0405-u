#pragma once

#include "Common.h"
#include "DeviceEnumerator.h"
#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace ct0405 {

using RawPacketCallback = std::function<void(const uint8_t* buffer, size_t length)>;

class HidDevice {
public:
    HidDevice();
    ~HidDevice();

    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    bool Open(const DiscoveredDevice& device_info);
    void Close();

    // True only while the handle is valid AND the reader has not seen the
    // device go away. A handle that outlived its device is not "open" for any
    // purpose the caller cares about - reporting it as open is what previously
    // made reconnection impossible.
    bool IsOpen() const {
        return m_handle != INVALID_HANDLE_VALUE && !m_device_lost.load(std::memory_order_acquire);
    }

    // Set to true by the reader thread when the device stops responding.
    bool IsDeviceLost() const { return m_device_lost.load(std::memory_order_acquire); }

    const DiscoveredDevice& GetDeviceInfo() const { return m_info; }

    // Must be called before StartReading(); not safe to change while reading.
    void SetPacketCallback(RawPacketCallback callback) { m_packet_callback = std::move(callback); }

    bool StartReading();
    void StopReading();

private:
    void ReadWorkerThread();

    DiscoveredDevice m_info;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    HANDLE m_stop_event = nullptr;
    std::thread m_reader_thread;
    std::atomic<bool> m_is_reading{ false };
    std::atomic<bool> m_device_lost{ false };
    std::mutex m_lifecycle_mutex;   // serializes Open/Close/StartReading/StopReading
    RawPacketCallback m_packet_callback;
};

} // namespace ct0405
