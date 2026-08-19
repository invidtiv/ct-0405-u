#pragma once

#include "Common.h"
#include "DeviceEnumerator.h"
#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace ct0405 {

using RawPacketCallback = std::function<void(const uint8_t* buffer, size_t length)>;

class HidDevice {
public:
    HidDevice();
    ~HidDevice();

    bool Open(const DiscoveredDevice& device_info);
    void Close();

    bool IsOpen() const { return m_handle != INVALID_HANDLE_VALUE; }
    const DiscoveredDevice& GetDeviceInfo() const { return m_info; }

    void SetPacketCallback(RawPacketCallback callback) { m_packet_callback = callback; }

    bool StartReading();
    void StopReading();

private:
    void ReadWorkerThread();

    DiscoveredDevice m_info;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    HANDLE m_stop_event = nullptr;
    std::thread m_reader_thread;
    std::atomic<bool> m_is_reading{ false };
    RawPacketCallback m_packet_callback;
};

} // namespace ct0405
