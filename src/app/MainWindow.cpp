#include "MainWindow.h"
#include "ConfigManager.h"
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace ct0405 {

// Posted by driver threads; handled on the UI thread. Nothing that runs on a
// driver thread may touch a window directly.
constexpr UINT WM_APP_TABLET_STATE = WM_APP + 1;
constexpr UINT WM_APP_CONNECTION   = WM_APP + 2;

constexpr UINT_PTR ID_TIMER_REFRESH = 1;
constexpr UINT_PTR ID_TIMER_SAVE = 2;

// Config is applied to the live driver immediately but only written to disk
// after the user stops fiddling, so dragging a slider no longer performs
// hundreds of file writes.
constexpr UINT SAVE_DEBOUNCE_MS = 700;
constexpr UINT REFRESH_INTERVAL_MS = 33;   // ~30Hz is ample for a live readout

constexpr int ID_COMBO_TABLET_PRESET = 120;
constexpr int ID_EDIT_MAX_X = 121;
constexpr int ID_EDIT_MAX_Y = 122;
constexpr int ID_CHECK_AUTODETECT = 123;
constexpr int ID_BTN_CALIBRATE = 124;
constexpr int ID_BTN_RESET_AREA = 125;
constexpr int ID_BTN_SET_SCREEN_AREA = 126;
constexpr int ID_COMBO_MONITOR = 127;
constexpr int ID_COMBO_TIP = 128;

constexpr int ID_COMBO_MAPPING = 101;
constexpr int ID_CHECK_ASPECT = 102;
constexpr int ID_CHECK_INK = 103;

constexpr int ID_COMBO_CURVE = 104;
constexpr int ID_SLIDER_DEADZONE = 105;
constexpr int ID_CHECK_SMOOTHING = 106;
constexpr int ID_SLIDER_SMOOTHING = 107;

constexpr int ID_COMBO_BARREL1 = 108;
constexpr int ID_COMBO_BARREL2 = 109;
constexpr int ID_CHECK_STARTUP = 110;
constexpr int ID_CHECK_MINIMIZE_TRAY = 111;
constexpr int ID_BTN_APPLY = 112;

constexpr UINT_PTR SUBCLASS_CHECKBOX_ID = 2001;
constexpr UINT_PTR SUBCLASS_BUTTON_ID   = 2002;
constexpr UINT_PTR SUBCLASS_EDIT_ID     = 2003;

// Logical (96 DPI) layout. Every use is passed through Scale().
constexpr int LOGICAL_CLIENT_W = 480;
constexpr int LOGICAL_CLIENT_H = 755;

namespace {

// Per-monitor DPI entry points are Windows 10 1607+; the manifest still allows
// running on 8.1, so they are resolved dynamically.
using PFN_GetDpiForWindow = UINT(WINAPI*)(HWND);
using PFN_AdjustWindowRectExForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

PFN_GetDpiForWindow GetGetDpiForWindow() {
    static PFN_GetDpiForWindow fn = []() -> PFN_GetDpiForWindow {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<PFN_GetDpiForWindow>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow"))) : nullptr;
    }();
    return fn;
}

PFN_AdjustWindowRectExForDpi GetAdjustWindowRectExForDpi() {
    static PFN_AdjustWindowRectExForDpi fn = []() -> PFN_AdjustWindowRectExForDpi {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<PFN_AdjustWindowRectExForDpi>(
            reinterpret_cast<void*>(GetProcAddress(user32, "AdjustWindowRectExForDpi"))) : nullptr;
    }();
    return fn;
}

UINT QueryWindowDpi(HWND hWnd) {
    if (auto fn = GetGetDpiForWindow(); fn && hWnd) {
        const UINT dpi = fn(hWnd);
        if (dpi >= 48) return dpi;
    }
    HDC hdc = GetDC(nullptr);
    const UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi >= 48 ? dpi : 96;
}

void AddRoundedRectangle(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float radius) {
    float d = radius * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

// Combo entries are ordered to match the ButtonAction enum exactly, so the
// selection index *is* the enum value. The previous hand-written index->enum
// ladders were where barrel assignments silently changed meaning.
const wchar_t* const BUTTON_ACTION_LABELS[] = {
    L"Default (Pen Barrel)",
    L"Left Click",
    L"Right Click",
    L"Middle Click",
    L"Eraser Toggle",
    L"Undo (Ctrl+Z)",
    L"Redo (Ctrl+Y)",
    L"Pan / Scroll Drag",
    L"Disabled"
};
constexpr int BUTTON_ACTION_COUNT = static_cast<int>(std::size(BUTTON_ACTION_LABELS));

const wchar_t* const CURVE_LABELS[] = {
    L"Linear", L"Soft", L"Very Soft", L"Firm", L"Hard", L"Custom Bezier"
};
constexpr int CURVE_COUNT = static_cast<int>(std::size(CURVE_LABELS));

void FillCombo(HWND combo, const wchar_t* const* labels, int count, int select) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i]));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(select, 0, count - 1)), 0);
}

int ComboSelection(HWND combo, int fallback) {
    if (!combo) return fallback;
    const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return (sel == CB_ERR) ? fallback : static_cast<int>(sel);
}

} // namespace

MainWindow::MainWindow(HINSTANCE hInstance, TabletDriver& driver)
    : m_hInstance(hInstance), m_driver(driver) {
    InitializeGdiPlus();

    m_hbrBackground = CreateSolidBrush(RGB(18, 21, 28));
    m_hbrCard       = CreateSolidBrush(RGB(27, 31, 42));
    m_hbrEdit       = CreateSolidBrush(RGB(20, 23, 31));
}

MainWindow::~MainWindow() {
    // Detach from the driver before this object dies, so a late callback can
    // never reach a destroyed window.
    m_driver.SetStateCallback(nullptr);
    m_driver.SetConnectionCallback(nullptr);

    if (m_hbrBackground) DeleteObject(m_hbrBackground);
    if (m_hbrCard) DeleteObject(m_hbrCard);
    if (m_hbrEdit) DeleteObject(m_hbrEdit);

    DestroyFonts();
    ShutdownGdiPlus();
}

void MainWindow::InitializeGdiPlus() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
}

void MainWindow::ShutdownGdiPlus() {
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

void MainWindow::CreateFonts() {
    DestroyFonts();
    m_hFontNormal = CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void MainWindow::DestroyFonts() {
    if (m_hFontNormal) { DeleteObject(m_hFontNormal); m_hFontNormal = nullptr; }
}

void MainWindow::SetupDarkMode() {
    if (!m_hWnd) return;
    BOOL darkMode = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE on 1903+, 19 on earlier builds.
    DwmSetWindowAttribute(m_hWnd, 20, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(m_hWnd, 19, &darkMode, sizeof(darkMode));
}

void MainWindow::ApplyDpi(UINT dpi) {
    if (dpi < 48) dpi = 96;
    m_dpi = dpi;
    CreateFonts();

    const HWND controls[] = {
        m_hComboTabletPreset, m_hEditMaxX, m_hEditMaxY, m_hCheckAutoDetect, m_hBtnCalibrate, m_hBtnResetArea,
        m_hComboMapping, m_hComboMonitor, m_hBtnSetScreenArea, m_hCheckAspect, m_hCheckInk,
        m_hComboCurve, m_hSliderDeadzone, m_hCheckSmoothing, m_hSliderSmoothing,
        m_hComboTip, m_hComboBarrel1, m_hComboBarrel2, m_hCheckStartup, m_hCheckMinimizeTray, m_hBtnApply
    };
    for (HWND hCtrl : controls) {
        if (hCtrl) SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFontNormal), TRUE);
    }

    LayoutControls();
}

bool MainWindow::Create(int nCmdShow, bool start_minimized) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    HICON hAppIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(101));
    if (!hAppIcon) {
        hAppIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    const wchar_t CLASS_NAME[] = L"CT0405_ControlPanel_Class";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_hbrBackground;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_dpi = QueryWindowDpi(nullptr);
    CreateFonts();

    const DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rcDesired{ 0, 0, Scale(LOGICAL_CLIENT_W), Scale(LOGICAL_CLIENT_H) };
    if (auto fn = GetAdjustWindowRectExForDpi()) {
        fn(&rcDesired, dwStyle, FALSE, 0, m_dpi);
    } else {
        AdjustWindowRect(&rcDesired, dwStyle, FALSE);
    }

    const int winW = rcDesired.right - rcDesired.left;
    const int winH = rcDesired.bottom - rcDesired.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    m_hWnd = CreateWindowExW(
        0, CLASS_NAME, L"Wacom CT-0405-U Driver", dwStyle,
        x, y, winW, winH, nullptr, nullptr, m_hInstance, this);

    if (!m_hWnd) return false;

    // The window may have landed on a monitor with a different scale factor.
    ApplyDpi(QueryWindowDpi(m_hWnd));
    SetupDarkMode();

    SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hAppIcon));
    SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hAppIcon));

    CreateControls();
    LoadConfigToUI();

    m_trayIcon.Initialize(m_hWnd, WM_TRAYICON, hAppIcon, L"Wacom CT-0405-U Driver");

    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        m_is_connected = m_driver.IsConnected();
        if (m_is_connected) {
            m_device_name = m_driver.GetCurrentDevice().product_name;
        }
    }

    m_driver.SetStateCallback([this](const TabletRawState& raw, const TabletProcessedState& processed) {
        this->OnStateUpdate(raw, processed);
    });

    m_driver.SetConnectionCallback([this](bool connected, const std::wstring& name) {
        this->OnConnectionUpdate(connected, name);
    });

    SetTimer(m_hWnd, ID_TIMER_REFRESH, REFRESH_INTERVAL_MS, nullptr);

    if (!start_minimized) {
        ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);
    } else {
        ShowWindow(m_hWnd, SW_HIDE);
        KillTimer(m_hWnd, ID_TIMER_REFRESH);   // nothing to repaint while hidden
    }

    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (result == -1) break;
        if (!IsDialogMessageW(m_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        if (pThis) pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (uMsg == WM_NCDESTROY) {
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// --- Subclass procedures ---------------------------------------------------

LRESULT CALLBACK MainWindow::CheckboxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    // Hover state is tracked per window. A single `static bool` here meant
    // hovering one checkbox drew all six as hovered.
    static HWND hHoveredCheckbox = nullptr;

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            if (hHoveredCheckbox != hWnd) {
                HWND previous = hHoveredCheckbox;
                hHoveredCheckbox = hWnd;
                if (previous) InvalidateRect(previous, nullptr, FALSE);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (hHoveredCheckbox == hWnd) {
                hHoveredCheckbox = nullptr;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        }
        case WM_NCDESTROY:
            if (hHoveredCheckbox == hWnd) hHoveredCheckbox = nullptr;
            RemoveWindowSubclass(hWnd, CheckboxSubclassProc, SUBCLASS_CHECKBOX_ID);
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            const int width = rc.right - rc.left;
            const int height = rc.bottom - rc.top;
            if (width <= 0 || height <= 0) { EndPaint(hWnd, &ps); return 0; }

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = hdcMem ? CreateCompatibleBitmap(hdc, width, height) : nullptr;
            if (!hdcMem || !hbmMem) {
                if (hbmMem) DeleteObject(hbmMem);
                if (hdcMem) DeleteDC(hdcMem);
                EndPaint(hWnd, &ps);
                return 0;
            }
            HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 27, 31, 42));
            g.FillRectangle(&bgBrush, 0, 0, width, height);

            const bool isChecked = (SendMessageW(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
            const bool isHovered = (hHoveredCheckbox == hWnd);

            const float boxSize = static_cast<float>(std::min(height - 2, 16 * height / 22));
            const float boxX = 2.0f;
            const float boxY = (static_cast<float>(height) - boxSize) / 2.0f;

            Gdiplus::GraphicsPath boxPath;
            AddRoundedRectangle(boxPath, boxX, boxY, boxSize, boxSize, boxSize * 0.19f);

            if (isChecked) {
                Gdiplus::SolidBrush checkBg(Gdiplus::Color(255, 31, 111, 235));
                Gdiplus::Pen checkBorder(Gdiplus::Color(255, 88, 166, 255), 1.2f);
                g.FillPath(&checkBg, &boxPath);
                g.DrawPath(&checkBorder, &boxPath);

                Gdiplus::Pen markPen(Gdiplus::Color(255, 255, 255, 255), boxSize * 0.11f);
                markPen.SetStartCap(Gdiplus::LineCapRound);
                markPen.SetEndCap(Gdiplus::LineCapRound);

                Gdiplus::PointF p1(boxX + boxSize * 0.22f, boxY + boxSize * 0.50f);
                Gdiplus::PointF p2(boxX + boxSize * 0.41f, boxY + boxSize * 0.72f);
                Gdiplus::PointF p3(boxX + boxSize * 0.78f, boxY + boxSize * 0.28f);

                g.DrawLine(&markPen, p1, p2);
                g.DrawLine(&markPen, p2, p3);
            } else {
                Gdiplus::SolidBrush uncheckBg(Gdiplus::Color(255, 18, 21, 28));
                Gdiplus::Pen uncheckBorder(isHovered ? Gdiplus::Color(255, 88, 166, 255)
                                                     : Gdiplus::Color(255, 75, 85, 110), 1.2f);
                g.FillPath(&uncheckBg, &boxPath);
                g.DrawPath(&uncheckBorder, &boxPath);
            }

            wchar_t text[256]{ 0 };
            GetWindowTextW(hWnd, text, 256);

            Gdiplus::Font textFont(L"Segoe UI", static_cast<float>(height) * 0.52f,
                                   Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 230, 235, 245));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF layoutRect(boxX + boxSize + 8.0f, 0.0f,
                                      static_cast<float>(width) - (boxX + boxSize + 9.0f),
                                      static_cast<float>(height));
            g.DrawString(text, -1, &textFont, layoutRect, &format, &textBrush);

            BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            EndPaint(hWnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    static HWND hHoveredWnd = nullptr;
    static HWND hPressedWnd = nullptr;
    const bool isPrimary = (dwRefData == 1);

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            if (hHoveredWnd != hWnd) {
                hHoveredWnd = hWnd;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (hHoveredWnd == hWnd) {
                hHoveredWnd = nullptr;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            hPressedWnd = hWnd;
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        case WM_LBUTTONUP: {
            if (hPressedWnd == hWnd) {
                ReleaseCapture();
                hPressedWnd = nullptr;
                InvalidateRect(hWnd, nullptr, FALSE);
                RECT rc;
                GetClientRect(hWnd, &rc);
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (PtInRect(&rc, pt)) {
                    HWND hParent = GetParent(hWnd);
                    const int id = GetDlgCtrlID(hWnd);
                    SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                                 reinterpret_cast<LPARAM>(hWnd));
                }
            }
            break;
        }
        case WM_NCDESTROY:
            if (hHoveredWnd == hWnd) hHoveredWnd = nullptr;
            if (hPressedWnd == hWnd) hPressedWnd = nullptr;
            RemoveWindowSubclass(hWnd, ButtonSubclassProc, SUBCLASS_BUTTON_ID);
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            const int width = rc.right - rc.left;
            const int height = rc.bottom - rc.top;
            if (width <= 0 || height <= 0) { EndPaint(hWnd, &ps); return 0; }

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = hdcMem ? CreateCompatibleBitmap(hdc, width, height) : nullptr;
            if (!hdcMem || !hbmMem) {
                if (hbmMem) DeleteObject(hbmMem);
                if (hdcMem) DeleteDC(hdcMem);
                EndPaint(hWnd, &ps);
                return 0;
            }
            HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 27, 31, 42));
            g.FillRectangle(&bgBrush, 0, 0, width, height);

            const bool isHovered = (hHoveredWnd == hWnd);
            const bool isPressed = (hPressedWnd == hWnd);

            Gdiplus::GraphicsPath btnPath;
            AddRoundedRectangle(btnPath, 1.0f, 1.0f,
                                static_cast<float>(width) - 2.0f,
                                static_cast<float>(height) - 2.0f, 4.0f);

            if (isPrimary) {
                Gdiplus::Color topColor = isPressed ? Gdiplus::Color(255, 20, 75, 165) :
                    (isHovered ? Gdiplus::Color(255, 45, 130, 255) : Gdiplus::Color(255, 31, 111, 235));
                Gdiplus::Color bottomColor = isPressed ? Gdiplus::Color(255, 15, 60, 135) :
                    (isHovered ? Gdiplus::Color(255, 31, 111, 235) : Gdiplus::Color(255, 22, 85, 185));

                Gdiplus::LinearGradientBrush grad(
                    Gdiplus::PointF(0, 0), Gdiplus::PointF(0, static_cast<float>(height)),
                    topColor, bottomColor);
                g.FillPath(&grad, &btnPath);

                Gdiplus::Pen borderPen(Gdiplus::Color(255, 88, 166, 255), 1.2f);
                g.DrawPath(&borderPen, &btnPath);
            } else {
                Gdiplus::Color fillCol = isPressed ? Gdiplus::Color(255, 22, 25, 34) :
                    (isHovered ? Gdiplus::Color(255, 46, 53, 72) : Gdiplus::Color(255, 36, 41, 56));
                Gdiplus::Color borderCol = isHovered ? Gdiplus::Color(255, 88, 166, 255)
                                                     : Gdiplus::Color(255, 65, 75, 100);

                Gdiplus::SolidBrush fillBrush(fillCol);
                Gdiplus::Pen borderPen(borderCol, 1.0f);
                g.FillPath(&fillBrush, &btnPath);
                g.DrawPath(&borderPen, &btnPath);
            }

            wchar_t text[256]{ 0 };
            GetWindowTextW(hWnd, text, 256);

            Gdiplus::Font textFont(L"Segoe UI",
                                   static_cast<float>(height) * (isPrimary ? 0.38f : 0.46f),
                                   isPrimary ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                                   Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            g.DrawString(text, -1, &textFont,
                         Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)),
                         &format, &textBrush);

            BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            EndPaint(hWnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, EditSubclassProc, SUBCLASS_EDIT_ID);
    } else if (uMsg == WM_NCPAINT) {
        const LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        HDC hdc = GetWindowDC(hWnd);
        if (hdc) {
            RECT rc;
            GetWindowRect(hWnd, &rc);
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(65, 75, 100));
            HGDIOBJ hOld = SelectObject(hdc, hPen);
            HGDIOBJ hOldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top);
            SelectObject(hdc, hOldBr);
            SelectObject(hdc, hOld);
            DeleteObject(hPen);
            ReleaseDC(hWnd, hdc);
        }
        return res;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// --- Control creation & layout ---------------------------------------------

void MainWindow::CreateControls() {
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
        return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                               0, 0, 10, 10, m_hWnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_hInstance, nullptr);
    };

    const DWORD comboStyle = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;

    m_hComboTabletPreset = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_TABLET_PRESET);
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"CT-0405-U default (5040 x 3780)"));
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom / calibrated bounds..."));
    SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 0, 0);

    m_hEditMaxX = make(L"EDIT", L"5040", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, ID_EDIT_MAX_X, WS_EX_CLIENTEDGE);
    m_hEditMaxY = make(L"EDIT", L"3780", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, ID_EDIT_MAX_Y, WS_EX_CLIENTEDGE);
    m_hBtnResetArea = make(L"BUTTON", L"Reset Bounds", BS_PUSHBUTTON | WS_TABSTOP, ID_BTN_RESET_AREA);
    m_hCheckAutoDetect = make(L"BUTTON", L"Auto-Expand Bounds", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_AUTODETECT);
    m_hBtnCalibrate = make(L"BUTTON", L"Calibrate Area", BS_PUSHBUTTON | WS_TABSTOP, ID_BTN_CALIBRATE);

    m_hComboMapping = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_MAPPING);
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Virtual Desktop"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary Monitor"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Specific Monitor"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom Area (Overlay)"));
    SendMessageW(m_hComboMapping, CB_SETCURSEL, 1, 0);

    // Occupies the same slot as the overlay button; whichever is relevant to
    // the selected mapping mode is shown. "Specific Monitor" previously had no
    // way at all to choose which monitor.
    m_hComboMonitor = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_MONITOR);
    m_hBtnSetScreenArea = make(L"BUTTON", L"Set Area Overlay...", BS_PUSHBUTTON | WS_TABSTOP, ID_BTN_SET_SCREEN_AREA);

    m_hCheckAspect = make(L"BUTTON", L"Lock 1:1 Aspect Ratio", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_ASPECT);
    m_hCheckInk = make(L"BUTTON", L"Enable Windows Ink (Pen Mode)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_INK);

    m_hComboCurve = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_CURVE);
    FillCombo(m_hComboCurve, CURVE_LABELS, CURVE_COUNT, 0);

    m_hSliderDeadzone = make(TRACKBAR_CLASSW, nullptr, TBS_AUTOTICKS | TBS_NOTICKS | WS_TABSTOP, ID_SLIDER_DEADZONE);
    SendMessageW(m_hSliderDeadzone, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
    SendMessageW(m_hSliderDeadzone, TBM_SETPOS, TRUE, 2);

    m_hCheckSmoothing = make(L"BUTTON", L"Enable 1-Euro Smoothing Filter", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_SMOOTHING);
    m_hSliderSmoothing = make(TRACKBAR_CLASSW, nullptr, TBS_AUTOTICKS | TBS_NOTICKS | WS_TABSTOP, ID_SLIDER_SMOOTHING);
    SendMessageW(m_hSliderSmoothing, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessageW(m_hSliderSmoothing, TBM_SETPOS, TRUE, 12);

    m_hComboTip = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_TIP);
    FillCombo(m_hComboTip, BUTTON_ACTION_LABELS, BUTTON_ACTION_COUNT,
              static_cast<int>(ButtonAction::LeftClick));

    m_hComboBarrel1 = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_BARREL1);
    FillCombo(m_hComboBarrel1, BUTTON_ACTION_LABELS, BUTTON_ACTION_COUNT,
              static_cast<int>(ButtonAction::RightClick));

    m_hComboBarrel2 = make(L"COMBOBOX", nullptr, comboStyle, ID_COMBO_BARREL2);
    FillCombo(m_hComboBarrel2, BUTTON_ACTION_LABELS, BUTTON_ACTION_COUNT,
              static_cast<int>(ButtonAction::EraserToggle));

    m_hCheckStartup = make(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_STARTUP);
    m_hCheckMinimizeTray = make(L"BUTTON", L"Close to Tray", BS_AUTOCHECKBOX | WS_TABSTOP, ID_CHECK_MINIMIZE_TRAY);
    m_hBtnApply = make(L"BUTTON", L"Save & Apply Settings", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_BTN_APPLY);

    const HWND checkboxes[] = {
        m_hCheckAutoDetect, m_hCheckAspect, m_hCheckInk, m_hCheckSmoothing,
        m_hCheckStartup, m_hCheckMinimizeTray
    };
    for (HWND hChk : checkboxes) {
        if (hChk) SetWindowSubclass(hChk, CheckboxSubclassProc, SUBCLASS_CHECKBOX_ID, 0);
    }

    const HWND secondaryButtons[] = { m_hBtnCalibrate, m_hBtnResetArea, m_hBtnSetScreenArea };
    for (HWND hBtn : secondaryButtons) {
        if (hBtn) SetWindowSubclass(hBtn, ButtonSubclassProc, SUBCLASS_BUTTON_ID, 0);
    }
    if (m_hBtnApply) SetWindowSubclass(m_hBtnApply, ButtonSubclassProc, SUBCLASS_BUTTON_ID, 1);

    if (m_hEditMaxX) SetWindowSubclass(m_hEditMaxX, EditSubclassProc, SUBCLASS_EDIT_ID, 0);
    if (m_hEditMaxY) SetWindowSubclass(m_hEditMaxY, EditSubclassProc, SUBCLASS_EDIT_ID, 0);

    ApplyDpi(m_dpi);
}

void MainWindow::LayoutControls() {
    // Positions are authored in logical 96-DPI units and scaled here, so the
    // same table is correct at every scale factor.
    auto place = [&](HWND hWnd, int x, int y, int w, int h) {
        if (hWnd) {
            SetWindowPos(hWnd, nullptr, Scale(x), Scale(y), Scale(w), Scale(h),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };

    place(m_hComboTabletPreset, 30, 174, 420, 200);
    place(m_hEditMaxX,          78, 206,  85,  22);
    place(m_hEditMaxY,         226, 206,  85,  22);
    place(m_hBtnResetArea,     330, 205, 120,  24);
    place(m_hCheckAutoDetect,   30, 235, 175,  22);
    place(m_hBtnCalibrate,     220, 234, 230,  25);

    place(m_hComboMapping,      30, 304, 230, 200);
    place(m_hComboMonitor,     270, 303, 180, 200);
    place(m_hBtnSetScreenArea, 270, 303, 180,  25);
    place(m_hCheckAspect,       30, 338, 190,  22);
    place(m_hCheckInk,         230, 338, 220,  22);

    place(m_hComboCurve,        30, 416, 160, 200);
    place(m_hSliderDeadzone,    30, 464, 160,  20);

    place(m_hCheckSmoothing,    30, 526, 210,  22);
    place(m_hSliderSmoothing,   30, 550, 420,  22);

    place(m_hComboTip,          30, 632, 134, 200);
    place(m_hComboBarrel1,     172, 632, 134, 200);
    place(m_hComboBarrel2,     314, 632, 136, 200);

    place(m_hCheckStartup,      30, 674, 190,  22);
    place(m_hCheckMinimizeTray,250, 674, 190,  22);
    place(m_hBtnApply,          30, 704, 420,  34);

    UpdateMappingModeControls();
}

void MainWindow::UpdateMappingModeControls() {
    const auto mode = static_cast<MappingMode>(
        ComboSelection(m_hComboMapping, static_cast<int>(MappingMode::PrimaryMonitor)));

    const bool show_monitor = (mode == MappingMode::SpecificMonitor);
    const bool show_area = (mode == MappingMode::CustomArea);

    if (m_hComboMonitor) ShowWindow(m_hComboMonitor, show_monitor ? SW_SHOW : SW_HIDE);
    if (m_hBtnSetScreenArea) ShowWindow(m_hBtnSetScreenArea, show_area ? SW_SHOW : SW_HIDE);
}

void MainWindow::PopulateMonitorCombo() {
    if (!m_hComboMonitor) return;

    const int previous = ComboSelection(m_hComboMonitor, 0);
    const auto monitors = m_driver.GetCoordinateMapper().GetMonitors();

    SendMessageW(m_hComboMonitor, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& mon = monitors[i];
        std::wostringstream label;
        label << L"Monitor " << (i + 1)
              << L" (" << (mon.rect.right - mon.rect.left) << L"x" << (mon.rect.bottom - mon.rect.top) << L")"
              << (mon.is_primary ? L" *" : L"");
        SendMessageW(m_hComboMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.str().c_str()));
    }
    if (monitors.empty()) {
        SendMessageW(m_hComboMonitor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No monitors found"));
    }

    const int count = static_cast<int>(std::max<size_t>(1, monitors.size()));
    SendMessageW(m_hComboMonitor, CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(previous, 0, count - 1)), 0);
}

// --- Config <-> UI ---------------------------------------------------------

void MainWindow::LoadConfigToUI() {
    const DriverConfig c = m_driver.GetConfig();

    // Programmatic control updates must not be mistaken for user edits.
    m_suppress_ui_events = true;

    const bool is_stock_bounds = (c.tablet_max_x == CT0405U_MAX_X && c.tablet_max_y == CT0405U_MAX_Y);
    SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, is_stock_bounds ? 0 : 1, 0);

    SetWindowTextW(m_hEditMaxX, std::to_wstring(c.tablet_max_x).c_str());
    SetWindowTextW(m_hEditMaxY, std::to_wstring(c.tablet_max_y).c_str());
    SendMessageW(m_hCheckAutoDetect, BM_SETCHECK, c.auto_detect_bounds ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(m_hComboMapping, CB_SETCURSEL, static_cast<WPARAM>(c.mapping_mode), 0);
    PopulateMonitorCombo();
    SendMessageW(m_hComboMonitor, CB_SETCURSEL, static_cast<WPARAM>(std::max(0, c.target_monitor_index)), 0);

    SendMessageW(m_hCheckAspect, BM_SETCHECK, c.lock_aspect_ratio ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hCheckInk, BM_SETCHECK, c.use_windows_ink ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hComboCurve, CB_SETCURSEL, static_cast<WPARAM>(c.curve_type), 0);

    SendMessageW(m_hSliderDeadzone, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(c.pressure_min_threshold * 100.0)));

    SendMessageW(m_hCheckSmoothing, BM_SETCHECK, c.enable_smoothing ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hSliderSmoothing, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(c.filter_min_cutoff * 10.0)));

    // These two were simply missing before, so a saved barrel assignment was
    // shown as the default and then written back over the stored value.
    SendMessageW(m_hComboTip, CB_SETCURSEL, static_cast<WPARAM>(c.tip_action), 0);
    SendMessageW(m_hComboBarrel1, CB_SETCURSEL, static_cast<WPARAM>(c.barrel_1_action), 0);
    SendMessageW(m_hComboBarrel2, CB_SETCURSEL, static_cast<WPARAM>(c.barrel_2_action), 0);

    SendMessageW(m_hCheckStartup, BM_SETCHECK, c.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hCheckMinimizeTray, BM_SETCHECK, c.minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);

    m_minimize_to_tray = c.minimize_to_tray;
    m_last_autostart_applied = c.start_with_windows;

    UpdateMappingModeControls();
    m_suppress_ui_events = false;
}

DriverConfig MainWindow::ReadConfigFromUI() const {
    DriverConfig c = m_driver.GetConfig();

    wchar_t bufX[32]{ 0 };
    wchar_t bufY[32]{ 0 };
    GetWindowTextW(m_hEditMaxX, bufX, 32);
    GetWindowTextW(m_hEditMaxY, bufY, 32);

    uint32_t max_x = c.tablet_max_x;
    uint32_t max_y = c.tablet_max_y;
    try { max_x = static_cast<uint32_t>(std::stoul(bufX)); } catch (...) {}
    try { max_y = static_cast<uint32_t>(std::stoul(bufY)); } catch (...) {}

    if (max_x < MIN_SANE_TABLET_BOUND) max_x = c.tablet_max_x;
    if (max_y < MIN_SANE_TABLET_BOUND) max_y = c.tablet_max_y;

    // Editing the bounds is an explicit user decision, which stops device
    // detection from overwriting them on the next connect.
    if (max_x != c.tablet_max_x || max_y != c.tablet_max_y) {
        c.bounds_source = BoundsSource::UserSet;
    }
    c.tablet_max_x = max_x;
    c.tablet_max_y = max_y;

    c.auto_detect_bounds = (SendMessageW(m_hCheckAutoDetect, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.mapping_mode = static_cast<MappingMode>(
        ComboSelection(m_hComboMapping, static_cast<int>(c.mapping_mode)));
    c.target_monitor_index = ComboSelection(m_hComboMonitor, c.target_monitor_index);
    c.lock_aspect_ratio = (SendMessageW(m_hCheckAspect, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.use_windows_ink = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.curve_type = static_cast<PressureCurveType>(
        ComboSelection(m_hComboCurve, static_cast<int>(c.curve_type)));

    c.pressure_min_threshold =
        static_cast<double>(SendMessageW(m_hSliderDeadzone, TBM_GETPOS, 0, 0)) / 100.0;

    c.enable_smoothing = (SendMessageW(m_hCheckSmoothing, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.filter_min_cutoff =
        static_cast<double>(SendMessageW(m_hSliderSmoothing, TBM_GETPOS, 0, 0)) / 10.0;

    // Combo order matches the ButtonAction enum, so the index is the value.
    c.tip_action = ClampEnum(ComboSelection(m_hComboTip, static_cast<int>(c.tip_action)),
                             ButtonAction::Disabled, ButtonAction::LeftClick);
    c.barrel_1_action = ClampEnum(ComboSelection(m_hComboBarrel1, static_cast<int>(c.barrel_1_action)),
                                  ButtonAction::Disabled, ButtonAction::RightClick);
    c.barrel_2_action = ClampEnum(ComboSelection(m_hComboBarrel2, static_cast<int>(c.barrel_2_action)),
                                  ButtonAction::Disabled, ButtonAction::EraserToggle);

    c.start_with_windows = (SendMessageW(m_hCheckStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.minimize_to_tray = (SendMessageW(m_hCheckMinimizeTray, BM_GETCHECK, 0, 0) == BST_CHECKED);

    return c;
}

void MainWindow::ApplyConfigFromUI(bool persist_now) {
    if (m_suppress_ui_events) return;

    const DriverConfig c = ReadConfigFromUI();
    m_minimize_to_tray = c.minimize_to_tray;

    // Apply to the running driver immediately; persist on a debounce.
    m_driver.SetConfig(c, false);

    // The Run key is only touched when the checkbox actually changes, rather
    // than on every settings write.
    if (c.start_with_windows != m_last_autostart_applied) {
        if (ConfigManager::SetAutoStart(c.start_with_windows)) {
            m_last_autostart_applied = c.start_with_windows;
        } else {
            m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                                   L"Could not update the Windows startup entry.", NIIF_WARNING);
        }
    }

    if (persist_now) {
        PersistConfigNow();
    } else {
        MarkConfigDirty();
    }
}

void MainWindow::MarkConfigDirty() {
    m_config_dirty = true;
    if (m_hWnd) {
        SetTimer(m_hWnd, ID_TIMER_SAVE, SAVE_DEBOUNCE_MS, nullptr);
    }
}

void MainWindow::PersistConfigNow() {
    if (m_hWnd) KillTimer(m_hWnd, ID_TIMER_SAVE);
    m_config_dirty = false;

    const DriverConfig c = m_driver.GetConfig();
    if (!ConfigManager::SaveConfig(c)) {
        // A failed save used to be reported as success, so settings silently
        // vanished at the next launch.
        m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                               L"Settings could not be saved. Check permissions for the config folder.",
                               NIIF_ERROR);
    }
}

// --- Driver callbacks (NOT on the UI thread) --------------------------------

void MainWindow::OnStateUpdate(const TabletRawState& raw, const TabletProcessedState& processed) {
    std::lock_guard<std::mutex> lock(m_ui_mutex);
    m_live_raw = raw;
    m_live_processed = processed;

    // Recording the sweep is pure arithmetic, so it can safely happen right
    // here on the HID thread. Nothing touches a window and nothing blocks.
    if (m_calibration_step == CalibrationStep::Sweeping && raw.in_proximity) {
        m_calib_min_x = std::min(m_calib_min_x, raw.raw_x);
        m_calib_min_y = std::min(m_calib_min_y, raw.raw_y);
        m_calib_max_x = std::max(m_calib_max_x, raw.raw_x);
        m_calib_max_y = std::max(m_calib_max_y, raw.raw_y);
        ++m_calib_samples;
    }
}

void MainWindow::OnConnectionUpdate(bool connected, const std::wstring& device_name) {
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        m_pending_connection_state = connected;
        m_pending_connection_name = connected ? device_name : L"Disconnected";
    }
    // Shell_NotifyIcon and window updates belong to the UI thread only.
    if (m_hWnd) {
        PostMessageW(m_hWnd, WM_APP_CONNECTION, 0, 0);
    }
}

// --- UI-thread handlers for the posted messages -----------------------------

void MainWindow::HandleConnectionChangeOnUiThread() {
    std::wstring name;
    bool connected;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        m_is_connected = m_pending_connection_state;
        m_device_name = m_pending_connection_name;
        connected = m_is_connected;
        name = m_device_name;
    }

    m_trayIcon.UpdateTooltip(L"Wacom CT-0405-U Driver: " + name);

    if (connected) {
        // A newly detected tablet may have brought its own bounds with it.
        LoadConfigToUI();
    }
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void MainWindow::HandleTabletStateOnUiThread() {
    // Retained for future driver-initiated UI updates; the sweep records itself.
}

std::wstring MainWindow::CalibrationHintText() const {
    uint32_t lo_x, hi_x, lo_y, hi_y, samples;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        lo_x = m_calib_min_x; hi_x = m_calib_max_x;
        lo_y = m_calib_min_y; hi_y = m_calib_max_y;
        samples = m_calib_samples;
    }
    if (samples == 0) return L"Sweep the pen around the edge of the tablet...";

    std::wostringstream ss;
    ss << L"X " << lo_x << L".." << hi_x << L"   Y " << lo_y << L".." << hi_y
       << L"   (" << samples << L" samples)";
    return ss.str();
}

// --- Actions ---------------------------------------------------------------

void MainWindow::StartCalibration() {
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        if (m_calibration_step == CalibrationStep::Sweeping) {
            return;   // handled by FinishCalibration
        }
        m_calib_min_x = 0xFFFFFFFFu;
        m_calib_min_y = 0xFFFFFFFFu;
        m_calib_max_x = 0;
        m_calib_max_y = 0;
        m_calib_samples = 0;
        m_calibration_step = CalibrationStep::Sweeping;
    }
    SetWindowTextW(m_hBtnCalibrate, L"Finish Calibration");
    m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                           L"Sweep the pen right around the edge of the drawing area, "
                           L"into all four corners, then click Finish Calibration.");
}

void MainWindow::FinishCalibration() {
    uint32_t lo_x, hi_x, lo_y, hi_y, samples;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        m_calibration_step = CalibrationStep::None;
        lo_x = m_calib_min_x; hi_x = m_calib_max_x;
        lo_y = m_calib_min_y; hi_y = m_calib_max_y;
        samples = m_calib_samples;
    }
    SetWindowTextW(m_hBtnCalibrate, L"Calibrate Area");

    // Refuse a sweep that plainly did not cover the surface, rather than
    // writing bounds that would cramp the mapping.
    const bool usable = samples >= 20 && hi_x > lo_x && hi_y > lo_y &&
                        (hi_x - lo_x) >= MIN_SANE_TABLET_BOUND &&
                        (hi_y - lo_y) >= MIN_SANE_TABLET_BOUND;
    if (!usable) {
        m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                               L"Not enough of the surface was covered. Nothing was changed - "
                               L"try again and sweep right into all four corners.",
                               NIIF_WARNING);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }

    DriverConfig c = m_driver.GetConfig();
    c.tablet_min_x = lo_x;
    c.tablet_min_y = lo_y;
    c.tablet_max_x = hi_x;
    c.tablet_max_y = hi_y;
    c.bounds_source = BoundsSource::UserSet;
    m_driver.SetConfig(c, true);
    LoadConfigToUI();

    std::wostringstream msg;
    msg << L"Calibrated from " << samples << L" samples: X " << lo_x << L".." << hi_x
        << L", Y " << lo_y << L".." << hi_y;
    m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver", msg.str());
}

void MainWindow::CancelCalibration() {
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        m_calibration_step = CalibrationStep::None;
    }
    SetWindowTextW(m_hBtnCalibrate, L"Calibrate Area");
}

void MainWindow::ResetActiveArea() {
    DriverConfig c = m_driver.GetConfig();
    c.tablet_min_x = 0;
    c.tablet_min_y = 0;
    c.tablet_max_x = CT0405U_MAX_X;
    c.tablet_max_y = CT0405U_MAX_Y;
    c.tablet_area_left = 0.0;
    c.tablet_area_top = 0.0;
    c.tablet_area_right = 1.0;
    c.tablet_area_bottom = 1.0;
    c.bounds_source = BoundsSource::Default;   // let detection take over again
    m_driver.SetConfig(c, true);

    LoadConfigToUI();
    m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                           L"Tablet bounds reset. The connected tablet's own bounds will be used.");
}

void MainWindow::OpenScreenAreaOverlay() {
    const DriverConfig c = m_driver.GetConfig();
    const TabletCapabilities caps = m_driver.GetEffectiveCapabilities();

    double tablet_aspect = 4.0 / 3.0;
    if (caps.max_y > 0) {
        tablet_aspect = static_cast<double>(caps.max_x) / static_cast<double>(caps.max_y);
    }

    m_overlay.Show(m_hInstance, m_hWnd, c.custom_screen_rect, tablet_aspect,
                   caps.max_x, caps.max_y, c.lock_aspect_ratio,
                   [this](const RECT& selected_rect) {
        DriverConfig cfg = m_driver.GetConfig();
        cfg.mapping_mode = MappingMode::CustomArea;
        cfg.custom_screen_rect = selected_rect;
        m_driver.SetConfig(cfg, true);

        m_suppress_ui_events = true;
        SendMessageW(m_hComboMapping, CB_SETCURSEL, static_cast<WPARAM>(MappingMode::CustomArea), 0);
        UpdateMappingModeControls();
        m_suppress_ui_events = false;

        m_trayIcon.ShowBalloon(L"Wacom Screen Mapping", L"Custom screen mapping area applied and saved.");
    });
}

void MainWindow::OnRefreshTimer() {
    // Repaint only the regions that actually carry live values, instead of the
    // whole window sixty times a second.
    const RECT status_card{ Scale(16), Scale(56), Scale(464), Scale(138) };
    const RECT curve_card{ Scale(205), Scale(408), Scale(456), Scale(484) };
    InvalidateRect(m_hWnd, &status_card, FALSE);
    InvalidateRect(m_hWnd, &curve_card, FALSE);
}

// --- Message handling ------------------------------------------------------

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_APP_TABLET_STATE:
            HandleTabletStateOnUiThread();
            return 0;

        case WM_APP_CONNECTION:
            HandleConnectionChangeOnUiThread();
            return 0;

        case WM_TIMER:
            if (wParam == ID_TIMER_REFRESH) {
                OnRefreshTimer();
            } else if (wParam == ID_TIMER_SAVE) {
                if (m_config_dirty) PersistConfigNow();
                KillTimer(m_hWnd, ID_TIMER_SAVE);
            }
            return 0;

        case WM_DPICHANGED: {
            ApplyDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(m_hWnd, nullptr,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            InvalidateRect(m_hWnd, nullptr, TRUE);
            return 0;
        }

        case WM_DISPLAYCHANGE:
            // Monitor layout changed: the mapper's cached geometry is stale.
            m_driver.GetCoordinateMapper().RefreshMonitors();
            m_suppress_ui_events = true;
            PopulateMonitorCombo();
            m_suppress_ui_events = false;
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;

        case WM_HSCROLL: {
            // Live-apply while dragging; the debounce timer handles the write.
            ApplyConfigFromUI(false);
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_COMMAND: {
            const int wmId = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (wmId == ID_COMBO_TABLET_PRESET && code == CBN_SELCHANGE) {
                const int sel = ComboSelection(m_hComboTabletPreset, 0);
                m_suppress_ui_events = true;
                if (sel == 0) {
                    SetWindowTextW(m_hEditMaxX, std::to_wstring(CT0405U_MAX_X).c_str());
                    SetWindowTextW(m_hEditMaxY, std::to_wstring(CT0405U_MAX_Y).c_str());
                }
                m_suppress_ui_events = false;
                if (sel == 0) {
                    DriverConfig c = ReadConfigFromUI();
                    c.bounds_source = BoundsSource::UserSet;   // an explicit choice
                    m_driver.SetConfig(c, false);
                    MarkConfigDirty();
                }
            } else if (wmId == ID_COMBO_MAPPING && code == CBN_SELCHANGE) {
                UpdateMappingModeControls();
                ApplyConfigFromUI(false);
            } else if (code == BN_CLICKED) {
                switch (wmId) {
                    case ID_BTN_CALIBRATE: {
                        bool sweeping;
                        {
                            std::lock_guard<std::mutex> lock(m_ui_mutex);
                            sweeping = (m_calibration_step == CalibrationStep::Sweeping);
                        }
                        if (sweeping) FinishCalibration(); else StartCalibration();
                        break;
                    }
                    case ID_BTN_RESET_AREA:      ResetActiveArea(); break;
                    case ID_BTN_SET_SCREEN_AREA: OpenScreenAreaOverlay(); break;
                    case ID_BTN_APPLY:
                        ApplyConfigFromUI(true);
                        m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver",
                                               L"Settings saved and applied.");
                        break;
                    default:
                        ApplyConfigFromUI(false);
                        break;
                }
            } else if (code == CBN_SELCHANGE || code == EN_KILLFOCUS) {
                ApplyConfigFromUI(false);
            }
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_CLOSE:
            ApplyConfigFromUI(true);
            // "Close to Tray" is now actually consulted. When it is off, the
            // close button closes the application instead of hiding a window
            // the user then has no obvious way to get rid of.
            if (ShouldExitOnClose()) {
                DestroyWindow(m_hWnd);
            } else {
                ShowWindow(m_hWnd, SW_HIDE);
                KillTimer(m_hWnd, ID_TIMER_REFRESH);
            }
            return 0;

        case WM_SYSCOMMAND: {
            const UINT cmd = (wParam & 0xFFF0);
            if (cmd == SC_MINIMIZE) {
                ApplyConfigFromUI(false);
                if (m_minimize_to_tray) {
                    ShowWindow(m_hWnd, SW_HIDE);
                    KillTimer(m_hWnd, ID_TIMER_REFRESH);
                    return 0;
                }
            }
            break;
        }

        case WM_SHOWWINDOW:
            // Do not run the refresh timer against a window nobody can see.
            if (wParam) {
                SetTimer(m_hWnd, ID_TIMER_REFRESH, REFRESH_INTERVAL_MS, nullptr);
            } else {
                KillTimer(m_hWnd, ID_TIMER_REFRESH);
            }
            break;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                ShowWindow(m_hWnd, SW_SHOW);
                ShowWindow(m_hWnd, SW_RESTORE);
                SetForegroundWindow(m_hWnd);
            } else if (lParam == WM_RBUTTONUP) {
                const bool is_ink = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                m_trayIcon.ShowContextMenu(m_hWnd, m_is_connected, is_ink, [this](int cmd) {
                    if (cmd == ID_TRAY_OPEN) {
                        ShowWindow(m_hWnd, SW_SHOW);
                        ShowWindow(m_hWnd, SW_RESTORE);
                        SetForegroundWindow(m_hWnd);
                    } else if (cmd == ID_TRAY_TOGGLE_INK) {
                        const bool cur = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        SendMessageW(m_hCheckInk, BM_SETCHECK, cur ? BST_UNCHECKED : BST_CHECKED, 0);
                        ApplyConfigFromUI(true);
                    } else if (cmd == ID_TRAY_EXIT) {
                        ApplyConfigFromUI(true);
                        DestroyWindow(m_hWnd);
                    }
                });
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hWnd, &ps);
            OnPaint(hdc);
            EndPaint(m_hWnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcEdit, RGB(245, 248, 255));
            SetBkColor(hdcEdit, RGB(20, 23, 31));
            return reinterpret_cast<INT_PTR>(m_hbrEdit);
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcStatic, RGB(230, 235, 245));
            SetBkMode(hdcStatic, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(m_hbrCard);
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdcList, RGB(235, 240, 250));
            SetBkColor(hdcList, RGB(27, 31, 42));
            return reinterpret_cast<INT_PTR>(m_hbrCard);
        }

        case WM_DESTROY:
            // Stop the driver calling back into a window that is going away.
            m_driver.SetStateCallback(nullptr);
            m_driver.SetConnectionCallback(nullptr);
            m_overlay.Close();
            if (m_config_dirty) PersistConfigNow();
            m_trayIcon.Remove();
            KillTimer(m_hWnd, ID_TIMER_REFRESH);
            KillTimer(m_hWnd, ID_TIMER_SAVE);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

// --- Painting --------------------------------------------------------------

void MainWindow::OnPaint(HDC hdc) {
    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    const int width = rcClient.right - rcClient.left;
    const int height = rcClient.bottom - rcClient.top;
    if (width <= 0 || height <= 0) return;

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = hdcMem ? CreateCompatibleBitmap(hdc, width, height) : nullptr;
    if (!hdcMem || !hbmMem) {
        if (hbmMem) DeleteObject(hbmMem);
        if (hdcMem) DeleteDC(hdcMem);
        return;
    }
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

    Gdiplus::Graphics g(hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 18, 21, 28));
    g.FillRectangle(&bgBrush, 0, 0, width, height);

    RenderHeader(g, width);
    RenderCards(g, width, height);

    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

void MainWindow::RenderHeader(Gdiplus::Graphics& g, int width) {
    Gdiplus::Font titleFont(L"Segoe UI", FontPx(11.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font badgeFont(L"Segoe UI", FontPx(8.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
    g.DrawString(L"WACOM CT-0405-U DRIVER", -1, &titleFont,
                 Gdiplus::PointF(ScaleF(16.0f), ScaleF(15.0f)), &titleBrush);

    bool connected;
    std::wstring dev_name;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        connected = m_is_connected;
        dev_name = m_device_name;
    }

    const float badgeW = ScaleF(200.0f);
    const float badgeH = ScaleF(24.0f);
    const float badgeX = static_cast<float>(width) - badgeW - ScaleF(16.0f);
    const float badgeY = ScaleF(14.0f);

    Gdiplus::GraphicsPath badgePath;
    AddRoundedRectangle(badgePath, badgeX, badgeY, badgeW, badgeH, ScaleF(12.0f));

    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    if (connected) {
        Gdiplus::SolidBrush bBg(Gdiplus::Color(255, 19, 45, 35));
        Gdiplus::Pen bBorder(Gdiplus::Color(255, 35, 134, 54), 1.2f);
        Gdiplus::SolidBrush bText(Gdiplus::Color(255, 63, 185, 80));
        g.FillPath(&bBg, &badgePath);
        g.DrawPath(&bBorder, &badgePath);
        const std::wstring text = L"CONNECTED: " + dev_name;
        g.DrawString(text.c_str(), -1, &badgeFont,
                     Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &fmt, &bText);
    } else {
        Gdiplus::SolidBrush bBg(Gdiplus::Color(255, 45, 22, 25));
        Gdiplus::Pen bBorder(Gdiplus::Color(255, 218, 54, 51), 1.2f);
        Gdiplus::SolidBrush bText(Gdiplus::Color(255, 248, 81, 73));
        g.FillPath(&bBg, &badgePath);
        g.DrawPath(&bBorder, &badgePath);
        // Show why, when we know why: a recognised-but-unsupported tablet
        // reports its own name here instead of a bare "disconnected".
        const std::wstring text = (dev_name.empty() || dev_name == L"Disconnected" ||
                                   dev_name == L"Searching...")
                                  ? L"DISCONNECTED (USB)"
                                  : dev_name;
        g.DrawString(text.c_str(), -1, &badgeFont,
                     Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &fmt, &bText);
    }

    Gdiplus::Pen dividerPen(Gdiplus::Color(255, 37, 42, 56), 1.0f);
    g.DrawLine(&dividerPen, ScaleF(16.0f), ScaleF(46.0f),
               static_cast<float>(width) - ScaleF(16.0f), ScaleF(46.0f));
}

void MainWindow::RenderCards(Gdiplus::Graphics& g, int /*width*/, int /*height*/) {
    Gdiplus::Font cardHeaderFont(L"Segoe UI", FontPx(9.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font labelFont(L"Segoe UI", FontPx(8.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font valueFont(L"Segoe UI", FontPx(8.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    Gdiplus::SolidBrush headerBrush(Gdiplus::Color(255, 76, 201, 240));
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 201, 209, 217));
    Gdiplus::SolidBrush cyanBrush(Gdiplus::Color(255, 76, 201, 240));
    Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 63, 185, 80));
    Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 248, 81, 73));
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 240, 243, 250));

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 27, 31, 42));
    Gdiplus::Pen cardBorder(Gdiplus::Color(255, 46, 54, 72), 1.0f);

    auto draw_card = [&](float x, float y, float w, float h) {
        Gdiplus::GraphicsPath path;
        AddRoundedRectangle(path, ScaleF(x), ScaleF(y), ScaleF(w), ScaleF(h), ScaleF(6.0f));
        g.FillPath(&cardBg, &path);
        g.DrawPath(&cardBorder, &path);
    };
    auto text_at = [&](const wchar_t* s, Gdiplus::Font& f, float x, float y, Gdiplus::Brush& b) {
        g.DrawString(s, -1, &f, Gdiplus::PointF(ScaleF(x), ScaleF(y)), &b);
    };

    const float cardX = 16.0f;
    const float cardW = 448.0f;

    // Snapshot everything that driver threads can change, once, under the lock.
    bool connected;
    std::wstring device_name;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        connected = m_is_connected;
        device_name = m_device_name;
    }
    const DriverConfig cfg = m_driver.GetConfig();
    const TabletCapabilities caps = m_driver.GetEffectiveCapabilities();

    // --- CARD 1: DRIVER STATUS ---
    draw_card(cardX, 56.0f, cardW, 82.0f);
    text_at(L"DRIVER STATUS & HARDWARE PROFILE", cardHeaderFont, cardX + 14.0f, 66.0f, headerBrush);

    text_at(L"Device:", labelFont, cardX + 14.0f, 88.0f, labelBrush);
    const std::wstring deviceText = connected ? (device_name + L" (Active)") : L"Disconnected";
    text_at(deviceText.c_str(), valueFont, cardX + 68.0f, 88.0f, connected ? greenBrush : redBrush);

    text_at(L"Bounds:", labelFont, cardX + 235.0f, 88.0f, labelBrush);
    const std::wstring bStr =
        (caps.min_x || caps.min_y)
            ? (std::to_wstring(caps.min_x) + L"-" + std::to_wstring(caps.max_x) + L" x " +
               std::to_wstring(caps.min_y) + L"-" + std::to_wstring(caps.max_y))
            : (std::to_wstring(caps.max_x) + L" x " + std::to_wstring(caps.max_y));
    text_at(bStr.c_str(), valueFont, cardX + 295.0f, 88.0f, whiteBrush);

    text_at(L"Pointer:", labelFont, cardX + 14.0f, 110.0f, labelBrush);
    text_at(cfg.use_windows_ink ? L"Native Ink (Pen)" : L"Absolute Mouse",
            valueFont, cardX + 68.0f, 110.0f, cyanBrush);

    text_at(L"Target:", labelFont, cardX + 235.0f, 110.0f, labelBrush);
    const wchar_t* mapStr = L"Virtual Desktop";
    switch (cfg.mapping_mode) {
        case MappingMode::CustomArea:     mapStr = L"Custom Area"; break;
        case MappingMode::PrimaryMonitor: mapStr = L"Primary Display"; break;
        case MappingMode::SpecificMonitor:mapStr = L"Specific Monitor"; break;
        default: break;
    }
    text_at(mapStr, valueFont, cardX + 295.0f, 110.0f, whiteBrush);

    // --- CARD 2: TABLET SPACE & CALIBRATION ---
    draw_card(cardX, 146.0f, cardW, 122.0f);
    text_at(L"TABLET SPACE & CALIBRATION", cardHeaderFont, cardX + 14.0f, 154.0f, headerBrush);
    text_at(L"Max X:", labelFont, cardX + 14.0f, 208.0f, labelBrush);
    text_at(L"Max Y:", labelFont, cardX + 162.0f, 208.0f, labelBrush);

    bool sweeping;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        sweeping = (m_calibration_step == CalibrationStep::Sweeping);
    }
    if (sweeping) {
        text_at(CalibrationHintText().c_str(), labelFont, cardX + 14.0f, 246.0f, cyanBrush);
    }

    // --- CARD 3: SCREEN MAPPING & WINDOWS INK ---
    draw_card(cardX, 276.0f, cardW, 102.0f);
    text_at(L"SCREEN MAPPING & WINDOWS INK", cardHeaderFont, cardX + 14.0f, 284.0f, headerBrush);

    // --- CARD 4: PRESSURE & SENSITIVITY ---
    draw_card(cardX, 386.0f, cardW, 104.0f);
    text_at(L"PRESSURE & SENSITIVITY", cardHeaderFont, cardX + 14.0f, 394.0f, headerBrush);

    const int deadzone_pos = static_cast<int>(SendMessageW(m_hSliderDeadzone, TBM_GETPOS, 0, 0));
    const std::wstring deadzoneStr = std::to_wstring(deadzone_pos) + L"%";
    text_at(L"Deadzone:", labelFont, cardX + 14.0f, 444.0f, labelBrush);
    text_at(deadzoneStr.c_str(), valueFont, cardX + 140.0f, 444.0f, cyanBrush);

    RECT curve_graph_rect{
        static_cast<LONG>(Scale(static_cast<int>(cardX) + 195)), Scale(414),
        static_cast<LONG>(Scale(static_cast<int>(cardX) + 434)), Scale(478)
    };
    RenderCurveGraph(g, curve_graph_rect);

    // --- CARD 5: SMOOTHING & JITTER FILTER ---
    draw_card(cardX, 498.0f, cardW, 82.0f);
    text_at(L"SMOOTHING & JITTER FILTER", cardHeaderFont, cardX + 14.0f, 506.0f, headerBrush);

    const int smooth_pos = static_cast<int>(SendMessageW(m_hSliderSmoothing, TBM_GETPOS, 0, 0));
    std::wostringstream ssCutoff;
    ssCutoff << std::fixed << std::setprecision(2) << (static_cast<double>(smooth_pos) / 10.0) << L" Hz";
    text_at(L"Min Cutoff:", labelFont, cardX + 265.0f, 528.0f, labelBrush);
    text_at(ssCutoff.str().c_str(), valueFont, cardX + 372.0f, 528.0f, cyanBrush);

    // --- CARD 6: PEN BUTTON ACTIONS ---
    draw_card(cardX, 588.0f, cardW, 72.0f);
    text_at(L"PEN BUTTON ACTIONS", cardHeaderFont, cardX + 14.0f, 596.0f, headerBrush);
    text_at(L"Pen Tip:", labelFont, cardX + 14.0f, 614.0f, labelBrush);
    text_at(L"Barrel 1 (Lower):", labelFont, cardX + 156.0f, 614.0f, labelBrush);
    text_at(L"Barrel 2 (Upper):", labelFont, cardX + 298.0f, 614.0f, labelBrush);

    // --- CARD 7: SYSTEM & ACTIONS ---
    draw_card(cardX, 668.0f, cardW, 78.0f);
}

void MainWindow::RenderCurveGraph(Gdiplus::Graphics& g, const RECT& rect) {
    const float gx = static_cast<float>(rect.left);
    const float gy = static_cast<float>(rect.top);
    const float gw = static_cast<float>(rect.right - rect.left);
    const float gh = static_cast<float>(rect.bottom - rect.top);

    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path, gx, gy, gw, gh, ScaleF(4.0f));

    Gdiplus::SolidBrush graphBg(Gdiplus::Color(255, 18, 21, 28));
    Gdiplus::Pen graphBorder(Gdiplus::Color(255, 46, 54, 72), 1.0f);
    g.FillPath(&graphBg, &path);
    g.DrawPath(&graphBorder, &path);

    const float innerPad = ScaleF(5.0f);
    const float innerW = gw - innerPad * 2.0f;
    const float innerH = gh - innerPad * 2.0f;
    const float startX = gx + innerPad;
    const float startY = gy + gh - innerPad;

    // Reference diagonal
    Gdiplus::Pen refPen(Gdiplus::Color(70, 140, 150, 170), 1.0f);
    refPen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawLine(&refPen, startX, startY, startX + innerW, startY - innerH);

    // Curve is evaluated through the same function the driver uses, so the plot
    // cannot drift away from the behaviour it claims to show - and Custom
    // Bezier now renders instead of silently falling back to linear.
    DriverConfig cfg = m_driver.GetConfig();
    cfg.curve_type = static_cast<PressureCurveType>(
        ComboSelection(m_hComboCurve, static_cast<int>(cfg.curve_type)));

    Gdiplus::Pen curvePen(Gdiplus::Color(255, 6, 214, 160), 2.0f);
    curvePen.SetLineJoin(Gdiplus::LineJoinRound);

    Gdiplus::PointF prevPoint(startX, startY);
    constexpr int steps = 40;
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const double val = SignalProcessor::ApplyPressureCurve(t, cfg);
        Gdiplus::PointF curPoint(startX + static_cast<float>(t) * innerW,
                                 startY - static_cast<float>(val) * innerH);
        g.DrawLine(&curvePen, prevPoint, curPoint);
        prevPoint = curPoint;
    }

    // Live operating point.
    TabletProcessedState processed;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        processed = m_live_processed;
    }

    if (processed.in_proximity && processed.pressure > 0.0) {
        // X is the pressure going in, Y is what the curve returns. Plotting the
        // output on both axes pinned the marker to the diagonal, so it never
        // touched the curve it was meant to ride.
        const float markX = startX + static_cast<float>(processed.raw_normalized_pressure) * innerW;
        const float markY = startY - static_cast<float>(processed.pressure) * innerH;
        const float r = ScaleF(3.5f);
        Gdiplus::SolidBrush markBrush(Gdiplus::Color(255, 255, 159, 28));
        Gdiplus::Pen markRing(Gdiplus::Color(255, 255, 255, 255), 1.5f);
        g.FillEllipse(&markBrush, markX - r, markY - r, r * 2.0f, r * 2.0f);
        g.DrawEllipse(&markRing, markX - r, markY - r, r * 2.0f, r * 2.0f);
    }
}

} // namespace ct0405
