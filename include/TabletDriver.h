#pragma once

#include "Common.h"
#include "HidDevice.h"
#include "DeviceEnumerator.h"
#include "PacketDecoder.h"
#include "SignalProcessor.h"
#include "CoordinateMapper.h"
#include "InputInjector.h"
#include "ConfigManager.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

namespace ct0405 {

class TabletDriver {
public:
    TabletDriver();
    ~TabletDriver();

    bool Start();
    void Stop();

    bool IsRunning() const { return m_running; }
    bool IsConnected() const { return m_hid_device.IsOpen(); }

    const DiscoveredDevice& GetCurrentDevice() const { return m_current_device; }
    const DriverConfig& GetConfig() const { return m_config; }
    void SetConfig(const DriverConfig& config);

    // Register UI / Debug callbacks
    void SetStateCallback(StateCallback callback);
    void SetConnectionCallback(ConnectionCallback callback);

    // Accessors for UI visualizers
    TabletProcessedState GetCurrentState();
    TabletRawState GetCurrentRawState();
    CoordinateMapper& GetCoordinateMapper() { return m_mapper; }
    HidDevice& GetHidDevice() { return m_hid_device; }
    PacketDecoder& GetPacketDecoder() { return m_decoder; }

    // Direct packet injection for simulation / testing
    void ProcessRawPacket(const uint8_t* buffer, size_t length);

private:
    void DeviceWatcherThread();
    void OnRawPacket(const uint8_t* buffer, size_t length);

    DriverConfig m_config;
    DiscoveredDevice m_current_device;

    HidDevice m_hid_device;
    PacketDecoder m_decoder;
    SignalProcessor m_processor;
    CoordinateMapper m_mapper;
    InputInjector m_injector;

    std::atomic<bool> m_running{ false };
    std::thread m_watcher_thread;

    std::mutex m_state_mutex;
    TabletRawState m_latest_raw;
    TabletProcessedState m_latest_processed;

    StateCallback m_state_callback;
    ConnectionCallback m_connection_callback;
};

} // namespace ct0405
