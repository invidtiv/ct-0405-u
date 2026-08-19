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

    bool Create(int nCmdShow, bool start_minimized = false);
    int RunMessageLoop();

    HWND GetHWND() const { return m_hWnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void InitializeGdiPlus();
    void ShutdownGdiPlus();
    void CreateControls();
    void SetupDarkMode();

    void OnPaint(HDC hdc);
    void RenderHeader(Gdiplus::Graphics& g, int width);
    void RenderCards(Gdiplus::Graphics& g, int width, int height);
    void RenderStatusCard(Gdiplus::Graphics& g, const RECT& rect);
    void RenderCurveGraph(Gdiplus::Graphics& g, const RECT& rect);

    void OnTimer();
    void OnStateUpdate(const TabletRawState& raw, const TabletProcessedState& processed);
    void OnConnectionUpdate(bool connected, const std::wstring& device_name);

    void LoadConfigToUI();
    void SaveConfigFromUI();
    void StartCalibration();
    void ResetActiveArea();
    void OpenScreenAreaOverlay();

    // Custom Subclass Procedures for Modern Dark Controls
    static LRESULT CALLBACK CheckboxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    HINSTANCE m_hInstance;
    TabletDriver& m_driver;
    HWND m_hWnd = nullptr;
    ULONG_PTR m_gdiplusToken = 0;
    TrayIcon m_trayIcon;
    ScreenOverlayWindow m_overlay;

    // GDI Brushes & Fonts
    HBRUSH m_hbrBackground = nullptr;
    HBRUSH m_hbrCard = nullptr;
    HBRUSH m_hbrEdit = nullptr;
    HFONT m_hFontNormal = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontHeader = nullptr;
    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontSmall = nullptr;

    // Mapping & Tablet Space Controls (Left Column)
    HWND m_hComboTabletPreset = nullptr;
    HWND m_hEditMaxX = nullptr;
    HWND m_hEditMaxY = nullptr;
    HWND m_hCheckAutoDetect = nullptr;
    HWND m_hBtnCalibrate = nullptr;
    HWND m_hBtnResetArea = nullptr;

    HWND m_hComboMapping = nullptr;
    HWND m_hBtnSetScreenArea = nullptr;
    HWND m_hCheckAspect = nullptr;
    HWND m_hCheckInk = nullptr;

    // Pressure & Smoothing Controls (Right Column)
    HWND m_hComboCurve = nullptr;
    HWND m_hSliderDeadzone = nullptr;
    HWND m_hCheckSmoothing = nullptr;
    HWND m_hSliderSmoothing = nullptr;

    // Button & System Controls (Right Column)
    HWND m_hComboBarrel1 = nullptr;
    HWND m_hComboBarrel2 = nullptr;
    HWND m_hCheckStartup = nullptr;
    HWND m_hCheckMinimizeTray = nullptr;
    HWND m_hBtnApply = nullptr;

    // Live display state
    TabletRawState m_live_raw;
    TabletProcessedState m_live_processed;
    bool m_is_connected = false;
    std::wstring m_device_name = L"Searching...";
    std::mutex m_ui_mutex;

    // Calibration state
    CalibrationStep m_calibration_step = CalibrationStep::None;
    uint32_t m_calib_min_x = 0;
    uint32_t m_calib_min_y = 0;
    uint32_t m_calib_max_x = 5040;
    uint32_t m_calib_max_y = 3780;

    bool m_minimize_to_tray = true;
};

} // namespace ct0405
