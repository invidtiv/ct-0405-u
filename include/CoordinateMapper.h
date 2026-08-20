#pragma once

#include "Common.h"
#include <vector>
#include <memory>
#include <atomic>

namespace ct0405 {

// Immutable snapshot of the display layout. Published by RefreshMonitors() on
// the UI thread and read by the HID thread without locking - the HID thread
// only ever holds a shared_ptr to a snapshot that can no longer change.
struct MonitorSnapshot {
    std::vector<MonitorInfo> monitors;
    RECT virtual_desktop_rect{ 0, 0, 1920, 1080 };
};

using MonitorSnapshotPtr = std::shared_ptr<const MonitorSnapshot>;

class CoordinateMapper {
public:
    CoordinateMapper();

    // Re-enumerates displays. Call on startup and on WM_DISPLAYCHANGE.
    void RefreshMonitors();

    MonitorSnapshotPtr GetSnapshot() const { return m_snapshot.load(); }
    std::vector<MonitorInfo> GetMonitors() const { return m_snapshot.load()->monitors; }
    RECT GetVirtualDesktopBounds() const { return m_snapshot.load()->virtual_desktop_rect; }

    // The screen rectangle the given config maps onto, before aspect-ratio
    // letterboxing.
    RECT GetTargetScreenBounds(const DriverConfig& config) const;

    // Maps normalized tablet coordinates [0.0, 1.0] to screen coordinates.
    // `caps` supplies the true tablet dimensions so the aspect-ratio lock uses
    // the device's real geometry rather than whatever bound happens to sit in
    // the config.
    void MapToScreen(double norm_x, double norm_y,
                     const DriverConfig& config,
                     const TabletCapabilities& caps,
                     int32_t& out_screen_x, int32_t& out_screen_y) const;

    // Exact inverse of MapToScreen, including active-area crop and
    // letterboxing, so calibration round-trips instead of drifting.
    void ScreenToNormalized(int32_t screen_x, int32_t screen_y,
                            const DriverConfig& config,
                            const TabletCapabilities& caps,
                            double& out_norm_x, double& out_norm_y) const;

private:
    struct EffectiveRect {
        double x, y, w, h;
    };
    EffectiveRect ComputeEffectiveRect(const DriverConfig& config, const TabletCapabilities& caps) const;

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);

    std::atomic<MonitorSnapshotPtr> m_snapshot;
};

} // namespace ct0405
