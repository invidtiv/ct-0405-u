#include "TabletDriver.h"
#include <iostream>

namespace ct0405 {

TabletDriver::TabletDriver() {
    m_config = ConfigManager::LoadConfig();
    m_decoder.SetCustomBounds(m_config.tablet_max_x, m_config.tablet_max_y, m_config.tablet_max_pressure, m_config.auto_detect_bounds);
    m_processor.UpdateConfig(m_config);
    m_mapper.UpdateConfig(m_config);
    m_injector.UpdateConfig(m_config);

    m_hid_device.SetPacketCallback([this](const uint8_t* buf, size_t len) {
        this->OnRawPacket(buf, len);
    });
}

TabletDriver::~TabletDriver() {
    Stop();
}

bool TabletDriver::Start() {
    if (m_running) return true;

    m_running = true;
    m_injector.Initialize(m_config.use_windows_ink);

    // Initial connection attempt on start
    DiscoveredDevice dev;
    if (DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        if (m_hid_device.Open(dev)) {
            m_current_device = dev;
            m_decoder.SetDeviceModel(dev.model);
            m_processor.SetTabletCapabilities(m_decoder.GetCapabilities());
            m_hid_device.StartReading();

            if (m_connection_callback) {
                m_connection_callback(true, dev.product_name);
            }
        }
    }

    m_watcher_thread = std::thread(&TabletDriver::DeviceWatcherThread, this);
    return true;
}

void TabletDriver::Stop() {
    if (!m_running) return;

    m_running = false;
    m_hid_device.Close();
    m_injector.Shutdown();

    if (m_watcher_thread.joinable()) {
        m_watcher_thread.join();
    }
}

void TabletDriver::SetConfig(const DriverConfig& config) {
    m_config = config;
    m_decoder.SetCustomBounds(config.tablet_max_x, config.tablet_max_y, config.tablet_max_pressure, config.auto_detect_bounds);
    m_processor.UpdateConfig(config);
    m_mapper.UpdateConfig(config);
    m_injector.UpdateConfig(config);
    ConfigManager::SaveConfig(config);
}

void TabletDriver::SetStateCallback(StateCallback callback) {
    m_state_callback = callback;
}

void TabletDriver::SetConnectionCallback(ConnectionCallback callback) {
    m_connection_callback = callback;
    // Notify immediately of current connection status
    if (m_hid_device.IsOpen() && m_connection_callback) {
        m_connection_callback(true, m_current_device.product_name);
    }
}

TabletProcessedState TabletDriver::GetCurrentState() {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_latest_processed;
}

TabletRawState TabletDriver::GetCurrentRawState() {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_latest_raw;
}

void TabletDriver::DeviceWatcherThread() {
    bool previously_connected = m_hid_device.IsOpen();

    while (m_running) {
        if (!m_hid_device.IsOpen()) {
            DiscoveredDevice dev;
            if (DeviceEnumerator::FindFirstSupportedTablet(dev)) {
                if (m_hid_device.Open(dev)) {
                    m_current_device = dev;
                    m_decoder.SetDeviceModel(dev.model);
                    m_processor.SetTabletCapabilities(m_decoder.GetCapabilities());
                    m_hid_device.StartReading();

                    previously_connected = true;
                    if (m_connection_callback) {
                        m_connection_callback(true, dev.product_name);
                    }
                }
            } else if (previously_connected) {
                previously_connected = false;
                if (m_connection_callback) {
                    m_connection_callback(false, L"Disconnected");
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void TabletDriver::ProcessRawPacket(const uint8_t* buffer, size_t length) {
    OnRawPacket(buffer, length);
}

void TabletDriver::OnRawPacket(const uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return;

    TabletRawState raw;
    bool decoded = m_decoder.DecodePacket(buffer, length, raw);
    TabletProcessedState processed{};

    if (decoded) {
        processed = m_processor.Process(raw);
        
        if (processed.in_proximity) {
            m_mapper.MapToScreen(processed.normalized_x, processed.normalized_y, processed.screen_x, processed.screen_y);
        }

        // Inject input into Windows
        m_injector.Inject(processed);
    } else {
        raw.raw_hex = m_decoder.FormatHex(buffer, length);
        raw.timestamp_us = GetCurrentTimestampUs();
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_latest_raw = raw;
        if (decoded) {
            m_latest_processed = processed;
        }
    }

    if (m_state_callback) {
        m_state_callback(raw, processed);
    }
}

} // namespace ct0405
