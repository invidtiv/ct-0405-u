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
#include <condition_variable>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>

namespace ct0405 {

using CapabilitiesPtr = std::shared_ptr<const TabletCapabilities>;

class TabletDriver {
public:
    TabletDriver();
    ~TabletDriver();

    TabletDriver(const TabletDriver&) = delete;
    TabletDriver& operator=(const TabletDriver&) = delete;

    bool Start();
    void Stop();

    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }
    bool IsConnected() const { return m_hid_device.IsOpen(); }

    // Returns a copy: the watcher thread may replace the current device at any
    // time, so handing out a reference to it was a race by construction.
    DiscoveredDevice GetCurrentDevice() const;

    // Immutable snapshot. Safe to read from any thread; never changes underfoot.
    ConfigPtr GetConfigSnapshot() const { return m_config.load(); }
    DriverConfig GetConfig() const { return *m_config.load(); }
    void SetConfig(const DriverConfig& config, bool persist = true);

    // Bounds actually in use, after device detection, user overrides and
    // auto-expansion have been resolved.
    TabletCapabilities GetEffectiveCapabilities() const { return *m_effective_caps.load(); }

    // Register UI / Debug callbacks. Safe to call at any time.
    void SetStateCallback(StateCallback callback);
    void SetConnectionCallback(ConnectionCallback callback);

    // Accessors for UI visualizers
    TabletProcessedState GetCurrentState() const;
    TabletRawState GetCurrentRawState() const;
    CoordinateMapper& GetCoordinateMapper() { return m_mapper; }
    HidDevice& GetHidDevice() { return m_hid_device; }
    PacketDecoder& GetPacketDecoder() { return m_decoder; }

    // Direct packet injection for simulation / testing
    void ProcessRawPacket(const uint8_t* buffer, size_t length);

private:
    void DeviceWatcherThread();
    void OnRawPacket(const uint8_t* buffer, size_t length);

    bool TryConnect();
    void HandleDisconnect();
    void PublishConfig(const DriverConfig& config);
    TabletCapabilities ResolveEffectiveCaps(const DriverConfig& config) const;
    void RecomputeEffectiveCaps();

    void FireConnection(bool connected, const std::wstring& name);

    std::atomic<ConfigPtr> m_config;
    std::atomic<CapabilitiesPtr> m_effective_caps;
    std::atomic<CapabilitiesPtr> m_detected_caps;

    mutable std::mutex m_device_mutex;
    DiscoveredDevice m_current_device;

    HidDevice m_hid_device;
    PacketDecoder m_decoder;        // touched only by the HID thread, or by the
                                    // watcher while the HID thread is stopped
    SignalProcessor m_processor;    // same ownership rule
    CoordinateMapper m_mapper;
    InputInjector m_injector;

    std::atomic<bool> m_running{ false };
    std::thread m_watcher_thread;
    std::mutex m_watch_mutex;
    std::condition_variable m_watch_cv;

    mutable std::mutex m_state_mutex;
    TabletRawState m_latest_raw;
    TabletProcessedState m_latest_processed;

    // Callbacks are published as snapshots for the same reason config is:
    // the UI thread can replace one while the HID thread is invoking it.
    std::atomic<std::shared_ptr<const StateCallback>> m_state_callback;
    std::atomic<std::shared_ptr<const ConnectionCallback>> m_connection_callback;
};

} // namespace ct0405
