#include "CoordinateMapper.h"
#include <algorithm>

namespace ct0405 {

CoordinateMapper::CoordinateMapper() {
    RefreshMonitors();
}

void CoordinateMapper::UpdateConfig(const DriverConfig& config) {
    m_config = config;
    RecalculateTargetBounds();
}

BOOL CALLBACK CoordinateMapper::MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) {
    auto* self = reinterpret_cast<CoordinateMapper*>(dwData);
    if (!self || !lprcMonitor) return TRUE;

    MONITORINFOEXW mi;
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorInfo info;
        info.index = static_cast<int>(self->m_monitors.size());
        info.name = mi.szDevice;
        info.device_id = mi.szDevice;
        info.rect = mi.rcMonitor;
        info.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        self->m_monitors.push_back(info);
    }
    return TRUE;
}

void CoordinateMapper::RefreshMonitors() {
    m_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(this));

    m_virtual_desktop_rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_virtual_desktop_rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_virtual_desktop_rect.right = m_virtual_desktop_rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_virtual_desktop_rect.bottom = m_virtual_desktop_rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    RecalculateTargetBounds();
}

void CoordinateMapper::RecalculateTargetBounds() {
    switch (m_config.mapping_mode) {
        case MappingMode::AllMonitors:
            m_target_screen_rect = m_virtual_desktop_rect;
            break;

        case MappingMode::PrimaryMonitor: {
            RECT primary_rect{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
            for (const auto& mon : m_monitors) {
                if (mon.is_primary) {
                    primary_rect = mon.rect;
                    break;
                }
            }
            m_target_screen_rect = primary_rect;
            break;
        }

        case MappingMode::SpecificMonitor: {
            if (m_config.target_monitor_index >= 0 && m_config.target_monitor_index < static_cast<int>(m_monitors.size())) {
                m_target_screen_rect = m_monitors[m_config.target_monitor_index].rect;
            } else {
                m_target_screen_rect = m_virtual_desktop_rect;
            }
            break;
        }

        case MappingMode::CustomArea:
            m_target_screen_rect = m_config.custom_screen_rect;
            break;

        default:
            m_target_screen_rect = m_virtual_desktop_rect;
            break;
    }
}

void CoordinateMapper::MapToScreen(double norm_x, double norm_y, int32_t& out_screen_x, int32_t& out_screen_y) const {
    // 1. Remap tablet active area crop
    double area_w = m_config.tablet_area_right - m_config.tablet_area_left;
    double area_h = m_config.tablet_area_bottom - m_config.tablet_area_top;
    if (area_w < 0.05) area_w = 1.0;
    if (area_h < 0.05) area_h = 1.0;

    double mapped_x = (norm_x - m_config.tablet_area_left) / area_w;
    double mapped_y = (norm_y - m_config.tablet_area_top) / area_h;

    mapped_x = std::clamp(mapped_x, 0.0, 1.0);
    mapped_y = std::clamp(mapped_y, 0.0, 1.0);

    // 2. Compute screen target rectangle with optional aspect ratio preservation
    int32_t screen_w = m_target_screen_rect.right - m_target_screen_rect.left;
    int32_t screen_h = m_target_screen_rect.bottom - m_target_screen_rect.top;

    double eff_screen_x = static_cast<double>(m_target_screen_rect.left);
    double eff_screen_y = static_cast<double>(m_target_screen_rect.top);
    double eff_screen_w = static_cast<double>(screen_w);
    double eff_screen_h = static_cast<double>(screen_h);

    if (m_config.lock_aspect_ratio && screen_w > 0 && screen_h > 0) {
        double max_x = m_config.tablet_max_x > 0 ? static_cast<double>(m_config.tablet_max_x) : 5040.0;
        double max_y = m_config.tablet_max_y > 0 ? static_cast<double>(m_config.tablet_max_y) : 3780.0;
        double tablet_aspect = (max_x * area_w) / (max_y * area_h);
        double screen_aspect = eff_screen_w / eff_screen_h;

        if (screen_aspect > tablet_aspect) {
            // Screen is wider than tablet area: letterbox horizontally (pillarbox)
            double target_w = eff_screen_h * tablet_aspect;
            eff_screen_x += (eff_screen_w - target_w) / 2.0;
            eff_screen_w = target_w;
        } else {
            // Screen is taller: letterbox vertically
            double target_h = eff_screen_w / tablet_aspect;
            eff_screen_y += (eff_screen_h - target_h) / 2.0;
            eff_screen_h = target_h;
        }
    }

    // 3. Calculate final screen coordinate
    out_screen_x = static_cast<int32_t>(eff_screen_x + mapped_x * eff_screen_w + 0.5);
    out_screen_y = static_cast<int32_t>(eff_screen_y + mapped_y * eff_screen_h + 0.5);
}

void CoordinateMapper::ScreenToNormalized(int32_t screen_x, int32_t screen_y, double& out_norm_x, double& out_norm_y) const {
    int32_t screen_w = m_target_screen_rect.right - m_target_screen_rect.left;
    int32_t screen_h = m_target_screen_rect.bottom - m_target_screen_rect.top;

    if (screen_w <= 0 || screen_h <= 0) {
        out_norm_x = 0.0;
        out_norm_y = 0.0;
        return;
    }

    out_norm_x = static_cast<double>(screen_x - m_target_screen_rect.left) / static_cast<double>(screen_w);
    out_norm_y = static_cast<double>(screen_y - m_target_screen_rect.top) / static_cast<double>(screen_h);
    out_norm_x = std::clamp(out_norm_x, 0.0, 1.0);
    out_norm_y = std::clamp(out_norm_y, 0.0, 1.0);
}

} // namespace ct0405
