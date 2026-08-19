#pragma once

#include "Common.h"
#include <vector>

namespace ct0405 {

class CoordinateMapper {
public:
    CoordinateMapper();

    void UpdateConfig(const DriverConfig& config);
    void RefreshMonitors();

    const std::vector<MonitorInfo>& GetMonitors() const { return m_monitors; }
    RECT GetTargetScreenBounds() const { return m_target_screen_rect; }
    RECT GetVirtualDesktopBounds() const { return m_virtual_desktop_rect; }

    // Maps normalized tablet coordinates [0.0, 1.0] to screen coordinates
    void MapToScreen(double norm_x, double norm_y, int32_t& out_screen_x, int32_t& out_screen_y) const;

    // Convert screen coordinates back to normalized target area (for calibration/testing)
    void ScreenToNormalized(int32_t screen_x, int32_t screen_y, double& out_norm_x, double& out_norm_y) const;

private:
    void RecalculateTargetBounds();

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);

    DriverConfig m_config;
    std::vector<MonitorInfo> m_monitors;
    RECT m_virtual_desktop_rect{ 0, 0, 1920, 1080 };
    RECT m_target_screen_rect{ 0, 0, 1920, 1080 };
};

} // namespace ct0405
