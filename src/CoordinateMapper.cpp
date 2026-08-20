#include "CoordinateMapper.h"
#include <algorithm>

namespace ct0405 {

namespace {
// An active area smaller than this is almost certainly a bad config value
// rather than an intentional crop.
constexpr double MIN_ACTIVE_AREA = 0.05;
} // namespace

CoordinateMapper::CoordinateMapper() {
    m_snapshot.store(std::make_shared<const MonitorSnapshot>());
    RefreshMonitors();
}

BOOL CALLBACK CoordinateMapper::MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
    if (!monitors || !lprcMonitor) return TRUE;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorInfo info;
        info.index = static_cast<int>(monitors->size());
        info.name = mi.szDevice;
        info.device_id = mi.szDevice;
        info.rect = mi.rcMonitor;
        info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        monitors->push_back(info);
    }
    return TRUE;
}

void CoordinateMapper::RefreshMonitors() {
    auto fresh = std::make_shared<MonitorSnapshot>();

    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&fresh->monitors));

    fresh->virtual_desktop_rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    fresh->virtual_desktop_rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    fresh->virtual_desktop_rect.right = fresh->virtual_desktop_rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    fresh->virtual_desktop_rect.bottom = fresh->virtual_desktop_rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_snapshot.store(std::static_pointer_cast<const MonitorSnapshot>(fresh));
}

RECT CoordinateMapper::GetTargetScreenBounds(const DriverConfig& config) const {
    MonitorSnapshotPtr snap = m_snapshot.load();

    switch (config.mapping_mode) {
        case MappingMode::AllMonitors:
            return snap->virtual_desktop_rect;

        case MappingMode::PrimaryMonitor: {
            for (const auto& mon : snap->monitors) {
                if (mon.is_primary) return mon.rect;
            }
            return RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        }

        case MappingMode::SpecificMonitor: {
            if (config.target_monitor_index >= 0 &&
                config.target_monitor_index < static_cast<int>(snap->monitors.size())) {
                return snap->monitors[config.target_monitor_index].rect;
            }
            return snap->virtual_desktop_rect;
        }

        case MappingMode::CustomArea:
            return config.custom_screen_rect;

        default:
            return snap->virtual_desktop_rect;
    }
}

CoordinateMapper::EffectiveRect CoordinateMapper::ComputeEffectiveRect(
    const DriverConfig& config, const TabletCapabilities& caps) const {

    const RECT target = GetTargetScreenBounds(config);

    double area_w = config.tablet_area_right - config.tablet_area_left;
    double area_h = config.tablet_area_bottom - config.tablet_area_top;
    if (area_w < MIN_ACTIVE_AREA) area_w = 1.0;
    if (area_h < MIN_ACTIVE_AREA) area_h = 1.0;

    const int32_t screen_w = target.right - target.left;
    const int32_t screen_h = target.bottom - target.top;

    EffectiveRect eff{
        static_cast<double>(target.left),
        static_cast<double>(target.top),
        static_cast<double>(screen_w),
        static_cast<double>(screen_h)
    };

    if (config.lock_aspect_ratio && screen_w > 0 && screen_h > 0) {
        // Use the device's real dimensions, not the config field. Those two
        // diverge whenever the connected tablet is not the one the config was
        // last saved for, and the aspect lock then letterboxed to the wrong
        // shape.
        const double max_x = caps.max_x > 0 ? static_cast<double>(caps.max_x) : 5040.0;
        const double max_y = caps.max_y > 0 ? static_cast<double>(caps.max_y) : 3780.0;

        const double tablet_aspect = (max_x * area_w) / (max_y * area_h);
        const double screen_aspect = eff.w / eff.h;

        if (screen_aspect > tablet_aspect) {
            // Screen is wider than the tablet area: pillarbox.
            const double target_w = eff.h * tablet_aspect;
            eff.x += (eff.w - target_w) / 2.0;
            eff.w = target_w;
        } else {
            // Screen is taller: letterbox.
            const double target_h = eff.w / tablet_aspect;
            eff.y += (eff.h - target_h) / 2.0;
            eff.h = target_h;
        }
    }

    return eff;
}

void CoordinateMapper::MapToScreen(double norm_x, double norm_y,
                                   const DriverConfig& config,
                                   const TabletCapabilities& caps,
                                   int32_t& out_screen_x, int32_t& out_screen_y) const {
    // 1. Remap tablet active area crop
    double area_w = config.tablet_area_right - config.tablet_area_left;
    double area_h = config.tablet_area_bottom - config.tablet_area_top;
    if (area_w < MIN_ACTIVE_AREA) area_w = 1.0;
    if (area_h < MIN_ACTIVE_AREA) area_h = 1.0;

    double mapped_x = (norm_x - config.tablet_area_left) / area_w;
    double mapped_y = (norm_y - config.tablet_area_top) / area_h;

    mapped_x = std::clamp(mapped_x, 0.0, 1.0);
    mapped_y = std::clamp(mapped_y, 0.0, 1.0);

    // 2. Screen target rectangle, with optional aspect-ratio preservation
    const EffectiveRect eff = ComputeEffectiveRect(config, caps);

    // 3. Final screen coordinate
    out_screen_x = static_cast<int32_t>(eff.x + mapped_x * eff.w + 0.5);
    out_screen_y = static_cast<int32_t>(eff.y + mapped_y * eff.h + 0.5);
}

void CoordinateMapper::ScreenToNormalized(int32_t screen_x, int32_t screen_y,
                                          const DriverConfig& config,
                                          const TabletCapabilities& caps,
                                          double& out_norm_x, double& out_norm_y) const {
    const EffectiveRect eff = ComputeEffectiveRect(config, caps);

    if (eff.w <= 0.0 || eff.h <= 0.0) {
        out_norm_x = 0.0;
        out_norm_y = 0.0;
        return;
    }

    double mapped_x = (static_cast<double>(screen_x) - eff.x) / eff.w;
    double mapped_y = (static_cast<double>(screen_y) - eff.y) / eff.h;
    mapped_x = std::clamp(mapped_x, 0.0, 1.0);
    mapped_y = std::clamp(mapped_y, 0.0, 1.0);

    // Undo the active-area crop so this is a true inverse of MapToScreen.
    double area_w = config.tablet_area_right - config.tablet_area_left;
    double area_h = config.tablet_area_bottom - config.tablet_area_top;
    if (area_w < MIN_ACTIVE_AREA) area_w = 1.0;
    if (area_h < MIN_ACTIVE_AREA) area_h = 1.0;

    out_norm_x = std::clamp(config.tablet_area_left + mapped_x * area_w, 0.0, 1.0);
    out_norm_y = std::clamp(config.tablet_area_top + mapped_y * area_h, 0.0, 1.0);
}

} // namespace ct0405
