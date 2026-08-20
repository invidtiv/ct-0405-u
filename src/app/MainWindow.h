#pragma once

#include "Common.h"
#include "TabletDriver.h"
#include "TrayIcon.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <mutex>

#include "ScreenOverlayWindow.h"

namespace ct0405 {

enum class CalibrationStep {
    None,
    WaitingForTopLeft,
    WaitingForBottomRight,
    Completed
};

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance, TabletDriver& driver);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(int nCmdShow, bool start_minimized = false);
    int RunMessageLoop();

    HWND GetHWND() const { return m_hWnd; }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void InitializeGdiPlus();
    void ShutdownGdiPlus();
    void CreateControls();
    void LayoutControls();
    void CreateFonts();
    void DestroyFonts();
    void SetupDarkMode();
    void ApplyDpi(UINT dpi);

    // --- DPI helpers -------------------------------------------------------
    // The manifest declares PerMonitorV2, which switches off the OS bitmap
    // scaling fallback: every dimension has to be scaled here or the window
    // renders at the wrong size on any display that is not 100%.
    int Scale(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    float ScaleF(float logical) const { return logical * static_cast<float>(m_dpi) / 96.0f; }
    // GDI+ font sizes in device pixels, so they do not depend on the DC's
    // reported resolution.
    float FontPx(float points) const { return points * (4.0f / 3.0f) * static_cast<float>(m_dpi) / 96.0f; }

    void OnPaint(HDC hdc);
    void RenderHeader(Gdiplus::Graphics& g, int width);
    void RenderCards(Gdiplus::Graphics& g, int width, int height);
    void RenderCurveGraph(Gdiplus::Graphics& g, const RECT& rect);

    void OnRefreshTimer();

    // Called on the HID / watcher threads. These only copy data and post a
    // message - they must never touch a window or block on the UI thread.
    void OnStateUpdate(const TabletRawState& raw, const TabletProcessedState& processed);
    void OnConnectionUpdate(bool connected, const std::wstring& device_name);

    // Called on the UI thread in response to the posted messages above.
    void HandleTabletStateOnUiThread();
    void HandleConnectionChangeOnUiThread();

    void LoadConfigToUI();
    DriverConfig ReadConfigFromUI() const;
    void ApplyConfigFromUI(bool persist_now);
    void MarkConfigDirty();
    void PersistConfigNow();
    void PopulateMonitorCombo();
    void UpdateMappingModeControls();

    void StartCalibration();
    void CancelCalibration();
    void ResetActiveArea();
    void OpenScreenAreaOverlay();
    bool ShouldExitOnClose() const { return !m_minimize_to_tray; }

    // Custom Subclass Procedures for Modern Dark Controls
    static LRESULT CALLBACK CheckboxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    HINSTANCE m_hInstance;
    TabletDriver& m_driver;
    HWND m_hWnd = nullptr;
    UINT m_dpi = 96;
    ULONG_PTR m_gdiplusToken = 0;
    TrayIcon m_trayIcon;
    ScreenOverlayWindow m_overlay;

    // GDI Brushes & Fonts
    HBRUSH m_hbrBackground = nullptr;
    HBRUSH m_hbrCard = nullptr;
    HBRUSH m_hbrEdit = nullptr;
    HFONT m_hFontNormal = nullptr;

    // Mapping & Tablet Space Controls
    HWND m_hComboTabletPreset = nullptr;
    HWND m_hEditMaxX = nullptr;
    HWND m_hEditMaxY = nullptr;
    HWND m_hCheckAutoDetect = nullptr;
    HWND m_hBtnCalibrate = nullptr;
    HWND m_hBtnResetArea = nullptr;

    HWND m_hComboMapping = nullptr;
    HWND m_hComboMonitor = nullptr;
    HWND m_hBtnSetScreenArea = nullptr;
    HWND m_hCheckAspect = nullptr;
    HWND m_hCheckInk = nullptr;

    // Pressure & Smoothing Controls
    HWND m_hComboCurve = nullptr;
    HWND m_hSliderDeadzone = nullptr;
    HWND m_hCheckSmoothing = nullptr;
    HWND m_hSliderSmoothing = nullptr;

    // Button & System Controls
    HWND m_hComboTip = nullptr;
    HWND m_hComboBarrel1 = nullptr;
    HWND m_hComboBarrel2 = nullptr;
    HWND m_hCheckStartup = nullptr;
    HWND m_hCheckMinimizeTray = nullptr;
    HWND m_hBtnApply = nullptr;

    // Live display state, written by driver threads and read during paint.
    mutable std::mutex m_ui_mutex;
    TabletRawState m_live_raw;
    TabletProcessedState m_live_processed;
    bool m_is_connected = false;
    std::wstring m_device_name = L"Searching...";
    bool m_pending_connection_state = false;
    std::wstring m_pending_connection_name;

    // Calibration state (UI thread only, except the sample handoff)
    CalibrationStep m_calibration_step = CalibrationStep::None;
    uint32_t m_calib_min_x = 0;
    uint32_t m_calib_min_y = 0;
    uint32_t m_calib_max_x = 5040;
    uint32_t m_calib_max_y = 3780;

    bool m_minimize_to_tray = true;
    bool m_last_autostart_applied = false;
    bool m_config_dirty = false;
    bool m_suppress_ui_events = false;
};

} // namespace ct0405
