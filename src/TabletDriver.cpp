#include "TabletDriver.h"
#include <algorithm>

namespace ct0405 {

namespace {
constexpr auto WATCH_INTERVAL = std::chrono::milliseconds(500);
} // namespace

TabletDriver::TabletDriver() {
    m_config.store(std::make_shared<const DriverConfig>(ConfigManager::LoadConfig()));
    m_detected_caps.store(std::make_shared<const TabletCapabilities>(m_decoder.GetCapabilities()));
    m_state_callback.store(nullptr);
    m_connection_callback.store(nullptr);

    ConfigPtr cfg = m_config.load();
    m_decoder.SetAutoDetectBounds(cfg->auto_detect_bounds);
    m_effective_caps.store(std::make_shared<const TabletCapabilities>(ResolveEffectiveCaps(*cfg)));

    m_hid_device.SetPacketCallback([this](const uint8_t* buf, size_t len) {
        this->OnRawPacket(buf, len);
    });
}

TabletDriver::~TabletDriver() {
    Stop();
}

bool TabletDriver::Start() {
    if (m_running.exchange(true, std::memory_order_acq_rel)) {
        return true;   // already running
    }

    ConfigPtr cfg = m_config.load();
    m_injector.Initialize(cfg->use_windows_ink);

    // Initial connection attempt, then hand ongoing responsibility to the watcher.
    TryConnect();

    m_watcher_thread = std::thread(&TabletDriver::DeviceWatcherThread, this);
    return true;
}

void TabletDriver::Stop() {
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        return;   // already stopped
    }

    // Wake the watcher immediately rather than waiting out its sleep.
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);
    }
    m_watch_cv.notify_all();

    if (m_watcher_thread.joinable()) {
        m_watcher_thread.join();
    }

    // Only after the watcher is gone can nothing else reopen the device.
    m_hid_device.Close();
    m_injector.Shutdown();
}

DiscoveredDevice TabletDriver::GetCurrentDevice() const {
    std::lock_guard<std::mutex> lock(m_device_mutex);
    return m_current_device;
}

void TabletDriver::PublishConfig(const DriverConfig& config) {
    m_config.store(std::make_shared<const DriverConfig>(config));
    m_effective_caps.store(std::make_shared<const TabletCapabilities>(ResolveEffectiveCaps(config)));
}

void TabletDriver::SetConfig(const DriverConfig& config, bool persist) {
    // Publish the new snapshot first: readers switch over atomically at their
    // next packet boundary, and never observe a half-updated configuration.
    PublishConfig(config);

    m_decoder.SetAutoDetectBounds(config.auto_detect_bounds);

    // The injector owns a Windows resource, so it needs a real handoff rather
    // than a snapshot swap. Initialize() takes its own lock.
    m_injector.Initialize(config.use_windows_ink);

    if (persist) {
        ConfigManager::SaveConfig(config);
    }
}

TabletCapabilities TabletDriver::ResolveEffectiveCaps(const DriverConfig& config) const {
    CapabilitiesPtr detected = m_detected_caps.load();
    TabletCapabilities caps = detected ? *detected : TabletCapabilities{};

    // Explicit user configuration always wins over detection.
    if (config.bounds_source == BoundsSource::UserSet) {
        if (config.tablet_max_x >= MIN_SANE_TABLET_BOUND) caps.max_x = config.tablet_max_x;
        if (config.tablet_max_y >= MIN_SANE_TABLET_BOUND) caps.max_y = config.tablet_max_y;
        if (config.tablet_max_pressure > 0)               caps.max_pressure = config.tablet_max_pressure;
        if (config.tablet_min_x < caps.max_x)             caps.min_x = config.tablet_min_x;
        if (config.tablet_min_y < caps.max_y)             caps.min_y = config.tablet_min_y;
    }

    // "Auto-expand bounds" widens the ceiling when the hardware genuinely
    // reports past it. It must never fight an explicit calibration: if the user
    // measured their own corners, that number stands until they change it.
    if (config.auto_detect_bounds && config.bounds_source != BoundsSource::UserSet) {
        const uint32_t seen_x = m_decoder.GetObservedMaxX();
        const uint32_t seen_y = m_decoder.GetObservedMaxY();
        if (seen_x > 0) caps.max_x = std::max(caps.max_x, seen_x);
        if (seen_y > 0) caps.max_y = std::max(caps.max_y, seen_y);
        const int32_t seen_p = m_decoder.GetObservedMaxPressure();
        if (seen_p > 0) caps.max_pressure = std::max(caps.max_pressure, static_cast<uint32_t>(seen_p));
    }

    return caps;
}

void TabletDriver::RecomputeEffectiveCaps() {
    ConfigPtr cfg = m_config.load();
    m_effective_caps.store(std::make_shared<const TabletCapabilities>(ResolveEffectiveCaps(*cfg)));
}

void TabletDriver::SetStateCallback(StateCallback callback) {
    m_state_callback.store(callback ? std::make_shared<const StateCallback>(std::move(callback))
                                    : nullptr);
}

void TabletDriver::SetConnectionCallback(ConnectionCallback callback) {
    const bool connected = m_hid_device.IsOpen();
    std::wstring name = GetCurrentDevice().product_name;

    m_connection_callback.store(callback ? std::make_shared<const ConnectionCallback>(std::move(callback))
                                         : nullptr);

    // Report current status immediately so a late-registering UI is not stuck
    // showing "searching" for a device that is already open.
    if (connected) {
        FireConnection(true, name);
    }
}

void TabletDriver::FireConnection(bool connected, const std::wstring& name) {
    auto cb = m_connection_callback.load();
    if (cb && *cb) {
        (*cb)(connected, name);
    }
}

TabletProcessedState TabletDriver::GetCurrentState() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_latest_processed;
}

TabletRawState TabletDriver::GetCurrentRawState() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_latest_raw;
}

bool TabletDriver::TryConnect() {
    DiscoveredDevice dev;
    if (!DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        return false;
    }

    if (!m_hid_device.Open(dev)) {
        return false;
    }

    // Safe to touch the decoder here: the reader thread is not started yet.
    m_decoder.Reset();
    m_processor.Reset();

    m_detected_caps.store(std::make_shared<const TabletCapabilities>(m_decoder.GetCapabilities()));

    // Adopt the detected geometry unless the user has deliberately set their
    // own. Without this the config's 5040x3780 default silently won for every
    // device, so a larger tablet only addressed a corner of the screen.
    DriverConfig cfg = *m_config.load();
    if (cfg.bounds_source != BoundsSource::UserSet) {
        const TabletCapabilities& caps = m_decoder.GetCapabilities();
        if (cfg.tablet_max_x != caps.max_x || cfg.tablet_max_y != caps.max_y ||
            cfg.tablet_max_pressure != caps.max_pressure ||
            cfg.bounds_source != BoundsSource::Detected) {
            cfg.tablet_max_x = caps.max_x;
            cfg.tablet_max_y = caps.max_y;
            cfg.tablet_max_pressure = caps.max_pressure;
            cfg.bounds_source = BoundsSource::Detected;
            PublishConfig(cfg);
        }
    }

    RecomputeEffectiveCaps();

    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_current_device = dev;
    }

    m_hid_device.StartReading();
    FireConnection(true, dev.product_name);
    return true;
}

void TabletDriver::HandleDisconnect() {
    // Close() reaps the reader thread and releases the stale handle, which is
    // what allows IsOpen() to go false and a reconnect to be attempted. Without
    // it the handle outlived the device and the watcher looped forever.
    m_hid_device.Close();
    m_injector.ReleaseAll();

    {
        std::lock_guard<std::mutex> lock(m_device_mutex);
        m_current_device = DiscoveredDevice{};
    }

    FireConnection(false, L"Disconnected");
}

void TabletDriver::DeviceWatcherThread() {
    bool previously_connected = m_hid_device.IsOpen();
    bool reported_unsupported = false;

    while (m_running.load(std::memory_order_acquire)) {
        const bool open = m_hid_device.IsOpen();

        if (!open) {
            if (previously_connected) {
                previously_connected = false;
                HandleDisconnect();
            }

            if (TryConnect()) {
                previously_connected = true;
                reported_unsupported = false;
            } else if (!reported_unsupported) {
                // A Wacom we recognise but do not drive should say so, rather
                // than leaving the user staring at a blank "disconnected".
                DiscoveredDevice other;
                if (DeviceEnumerator::FindRecognisedUnsupportedTablet(other)) {
                    reported_unsupported = true;
                    FireConnection(false, other.product_name + L" - not supported");
                }
            }
        } else {
            previously_connected = true;
        }

        // Interruptible sleep so Stop() does not have to wait out the interval.
        std::unique_lock<std::mutex> lock(m_watch_mutex);
        m_watch_cv.wait_for(lock, WATCH_INTERVAL,
                            [this] { return !m_running.load(std::memory_order_acquire); });
    }
}

void TabletDriver::ProcessRawPacket(const uint8_t* buffer, size_t length) {
    OnRawPacket(buffer, length);
}

void TabletDriver::OnRawPacket(const uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return;

    // One load per packet. Everything downstream sees the same configuration
    // even if the UI thread publishes a new one mid-packet.
    ConfigPtr cfg = m_config.load();

    TabletRawState raw;
    const bool decoded = m_decoder.DecodePacket(buffer, length, raw);
    TabletProcessedState processed{};

    if (decoded) {
        if (cfg->auto_detect_bounds) {
            RecomputeEffectiveCaps();
        }
        CapabilitiesPtr caps = m_effective_caps.load();

        processed = m_processor.Process(raw, *cfg, *caps);

        if (processed.in_proximity) {
            m_mapper.MapToScreen(processed.normalized_x, processed.normalized_y,
                                 *cfg, *caps,
                                 processed.screen_x, processed.screen_y);
        }

        m_injector.Inject(processed, *cfg);
    } else {
        // An undecodable report is reported as-is for the packet inspector and
        // is deliberately not injected as pointer input.
        raw.raw_hex = PacketDecoder::FormatHex(buffer, length);
        raw.timestamp_us = GetCurrentTimestampUs();
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_latest_raw = raw;
        if (decoded) {
            m_latest_processed = processed;
        }
    }

    auto cb = m_state_callback.load();
    if (cb && *cb) {
        (*cb)(raw, processed);
    }
}

} // namespace ct0405
