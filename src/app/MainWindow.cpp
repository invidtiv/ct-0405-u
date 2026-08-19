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

constexpr int ID_TIMER_REFRESH = 1;

constexpr int ID_COMBO_TABLET_PRESET = 120;
constexpr int ID_EDIT_MAX_X = 121;
constexpr int ID_EDIT_MAX_Y = 122;
constexpr int ID_CHECK_AUTODETECT = 123;
constexpr int ID_BTN_CALIBRATE = 124;
constexpr int ID_BTN_RESET_AREA = 125;
constexpr int ID_BTN_SET_SCREEN_AREA = 126;

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

// Helper to draw rounded rectangle in GDI+
static void AddRoundedRectangle(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float radius) {
    float d = radius * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

MainWindow::MainWindow(HINSTANCE hInstance, TabletDriver& driver)
    : m_hInstance(hInstance), m_driver(driver) {
    InitializeGdiPlus();

    // Create theme brushes
    m_hbrBackground = CreateSolidBrush(RGB(18, 21, 28));
    m_hbrCard       = CreateSolidBrush(RGB(27, 31, 42));
    m_hbrEdit       = CreateSolidBrush(RGB(20, 23, 31));

    // Create standard Segoe UI fonts
    m_hFontNormal = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontBold = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontHeader = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontTitle = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_hFontSmall = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

MainWindow::~MainWindow() {
    if (m_hbrBackground) DeleteObject(m_hbrBackground);
    if (m_hbrCard) DeleteObject(m_hbrCard);
    if (m_hbrEdit) DeleteObject(m_hbrEdit);

    if (m_hFontNormal) DeleteObject(m_hFontNormal);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontHeader) DeleteObject(m_hFontHeader);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontSmall) DeleteObject(m_hFontSmall);

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

void MainWindow::SetupDarkMode() {
    if (!m_hWnd) return;
    // Enable immersive dark mode on Windows 10 (1809+) & Windows 11
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hWnd, 20, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(m_hWnd, 19, &darkMode, sizeof(darkMode));
}

bool MainWindow::Create(int nCmdShow, bool start_minimized) {
    INITCOMMONCONTROLSEX icex;
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

    RegisterClassExW(&wc);

    // Compact 1-Column Window (Client: 480 x 755 px)
    RECT rcDesired{ 0, 0, 480, 755 };
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rcDesired, dwStyle, FALSE);
    int winW = rcDesired.right - rcDesired.left;
    int winH = rcDesired.bottom - rcDesired.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    m_hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Wacom CT-0405-U Driver",
        dwStyle,
        x, y, winW, winH,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hWnd) return false;

    SetupDarkMode();

    SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hAppIcon));
    SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hAppIcon));

    CreateControls();
    LoadConfigToUI();

    m_trayIcon.Initialize(m_hWnd, WM_TRAYICON, hAppIcon, L"Wacom CT-0405-U Driver");

    // Initialize live connection state immediately from driver
    m_is_connected = m_driver.IsConnected();
    if (m_is_connected) {
        m_device_name = m_driver.GetCurrentDevice().product_name;
    }

    // Hook callbacks
    m_driver.SetStateCallback([this](const TabletRawState& raw, const TabletProcessedState& processed) {
        this->OnStateUpdate(raw, processed);
    });

    m_driver.SetConnectionCallback([this](bool connected, const std::wstring& name) {
        this->OnConnectionUpdate(connected, name);
    });

    // 60Hz UI refresh timer
    SetTimer(m_hWnd, ID_TIMER_REFRESH, 16, nullptr);

    if (!start_minimized) {
        ShowWindow(m_hWnd, nCmdShow);
        UpdateWindow(m_hWnd);
    } else {
        ShowWindow(m_hWnd, SW_HIDE);
    }

    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// Subclass for modern custom-drawn dark checkboxes
LRESULT CALLBACK MainWindow::CheckboxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    static bool isHovered = false;

    switch (uMsg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            if (!isHovered) {
                isHovered = true;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            isHovered = false;
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Card background
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 27, 31, 42));
            g.FillRectangle(&bgBrush, 0, 0, width, height);

            bool isChecked = (SendMessageW(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED);

            // Checkbox box geometry
            float boxSize = 16.0f;
            float boxX = 2.0f;
            float boxY = (static_cast<float>(height) - boxSize) / 2.0f;

            Gdiplus::GraphicsPath boxPath;
            AddRoundedRectangle(boxPath, boxX, boxY, boxSize, boxSize, 3.0f);

            if (isChecked) {
                Gdiplus::SolidBrush checkBg(Gdiplus::Color(255, 31, 111, 235));
                Gdiplus::Pen checkBorder(Gdiplus::Color(255, 88, 166, 255), 1.2f);
                g.FillPath(&checkBg, &boxPath);
                g.DrawPath(&checkBorder, &boxPath);

                // Draw checkmark symbol
                Gdiplus::Pen markPen(Gdiplus::Color(255, 255, 255, 255), 1.8f);
                markPen.SetStartCap(Gdiplus::LineCapRound);
                markPen.SetEndCap(Gdiplus::LineCapRound);

                Gdiplus::PointF p1(boxX + 3.5f, boxY + 8.0f);
                Gdiplus::PointF p2(boxX + 6.5f, boxY + 11.5f);
                Gdiplus::PointF p3(boxX + 12.5f, boxY + 4.5f);

                g.DrawLine(&markPen, p1, p2);
                g.DrawLine(&markPen, p2, p3);
            } else {
                Gdiplus::SolidBrush uncheckBg(Gdiplus::Color(255, 18, 21, 28));
                Gdiplus::Pen uncheckBorder(isHovered ? Gdiplus::Color(255, 88, 166, 255) : Gdiplus::Color(255, 75, 85, 110), 1.2f);
                g.FillPath(&uncheckBg, &boxPath);
                g.DrawPath(&uncheckBorder, &boxPath);
            }

            // Draw Checkbox Text Label (Bright, Crisp Light Text)
            wchar_t text[256]{ 0 };
            GetWindowTextW(hWnd, text, 256);

            Gdiplus::Font textFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleRegular);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 230, 235, 245));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF layoutRect(boxX + boxSize + 8.0f, 0.0f, static_cast<float>(width) - (boxX + boxSize + 9.0f), static_cast<float>(height));
            g.DrawString(text, -1, &textFont, layoutRect, &format, &textBrush);

            BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass for modern custom-drawn dark push buttons
LRESULT CALLBACK MainWindow::ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    static HWND hHoveredWnd = nullptr;
    static HWND hPressedWnd = nullptr;
    bool isPrimary = (dwRefData == 1);

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
                    int id = GetDlgCtrlID(hWnd);
                    SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(hWnd));
                }
            }
            break;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            // Background of card behind button
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 27, 31, 42));
            g.FillRectangle(&bgBrush, 0, 0, width, height);

            bool isHovered = (hHoveredWnd == hWnd);
            bool isPressed = (hPressedWnd == hWnd);

            float bw = static_cast<float>(width) - 2.0f;
            float bh = static_cast<float>(height) - 2.0f;
            Gdiplus::GraphicsPath btnPath;
            AddRoundedRectangle(btnPath, 1.0f, 1.0f, bw, bh, 4.0f);

            if (isPrimary) {
                // Primary Accent Button (Save & Apply Settings)
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
                // Secondary Dark Card Button
                Gdiplus::Color fillCol = isPressed ? Gdiplus::Color(255, 22, 25, 34) :
                    (isHovered ? Gdiplus::Color(255, 46, 53, 72) : Gdiplus::Color(255, 36, 41, 56));
                Gdiplus::Color borderCol = isHovered ? Gdiplus::Color(255, 88, 166, 255) : Gdiplus::Color(255, 65, 75, 100);

                Gdiplus::SolidBrush fillBrush(fillCol);
                Gdiplus::Pen borderPen(borderCol, 1.0f);
                g.FillPath(&fillBrush, &btnPath);
                g.DrawPath(&borderPen, &btnPath);
            }

            wchar_t text[256]{ 0 };
            GetWindowTextW(hWnd, text, 256);

            Gdiplus::Font textFont(L"Segoe UI", isPrimary ? 9.5f : 8.5f, isPrimary ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            g.DrawString(text, -1, &textFont, Gdiplus::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), &format, &textBrush);

            BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass for dark styled edit controls
LRESULT CALLBACK MainWindow::EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCPAINT) {
        LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        HDC hdc = GetWindowDC(hWnd);
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(65, 75, 100));
        HGDIOBJ hOld = SelectObject(hdc, hPen);
        HGDIOBJ hOldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, w, h);
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);
        ReleaseDC(hWnd, hdc);
        return res;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void MainWindow::CreateControls() {
    // =========================================================================
    // 1-COLUMN COMPACT LAYOUT (Content X = 30, Width = 420)
    // =========================================================================

    // --- CARD 2: TABLET SPACE & CALIBRATION (Y = 146) ---
    m_hComboTabletPreset = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        30, 174, 420, 200, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO_TABLET_PRESET)), m_hInstance, nullptr);
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PenPartner CT-0405-U (5040 x 3780)"));
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Graphire 1/2 (10206 x 7422)"));
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Graphire 3/4 (13918 x 10206)"));
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Volito (5104 x 3712)"));
    SendMessageW(m_hComboTabletPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom Resolution..."));
    SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 0, 0);

    m_hEditMaxX = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5040", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        78, 206, 85, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_MAX_X)), m_hInstance, nullptr);

    m_hEditMaxY = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"3780", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        226, 206, 85, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT_MAX_Y)), m_hInstance, nullptr);

    m_hBtnResetArea = CreateWindowW(L"BUTTON", L"Reset Bounds", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        330, 205, 120, 24, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_RESET_AREA)), m_hInstance, nullptr);

    m_hCheckAutoDetect = CreateWindowW(L"BUTTON", L"Auto-Expand Bounds", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        30, 235, 175, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_AUTODETECT)), m_hInstance, nullptr);

    m_hBtnCalibrate = CreateWindowW(L"BUTTON", L"Calibrate Corners", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        220, 234, 230, 25, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_CALIBRATE)), m_hInstance, nullptr);


    // --- CARD 3: SCREEN MAPPING & WINDOWS INK (Y = 276) ---
    m_hComboMapping = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        30, 304, 230, 200, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO_MAPPING)), m_hInstance, nullptr);
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Virtual Desktop"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary Monitor"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Specific Monitor"));
    SendMessageW(m_hComboMapping, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom Area (Overlay)"));
    SendMessageW(m_hComboMapping, CB_SETCURSEL, 1, 0);

    m_hBtnSetScreenArea = CreateWindowW(L"BUTTON", L"Set Area Overlay...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        270, 303, 180, 25, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_SET_SCREEN_AREA)), m_hInstance, nullptr);

    m_hCheckAspect = CreateWindowW(L"BUTTON", L"Lock 1:1 Aspect Ratio", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        30, 338, 190, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_ASPECT)), m_hInstance, nullptr);

    m_hCheckInk = CreateWindowW(L"BUTTON", L"Enable Windows Ink (Pen Mode)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        230, 338, 220, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_INK)), m_hInstance, nullptr);


    // --- CARD 4: PRESSURE & SENSITIVITY (Y = 386) ---
    m_hComboCurve = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        30, 416, 160, 200, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO_CURVE)), m_hInstance, nullptr);
    SendMessageW(m_hComboCurve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Linear"));
    SendMessageW(m_hComboCurve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Soft"));
    SendMessageW(m_hComboCurve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Very Soft"));
    SendMessageW(m_hComboCurve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Firm"));
    SendMessageW(m_hComboCurve, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hard"));
    SendMessageW(m_hComboCurve, CB_SETCURSEL, 0, 0);

    m_hSliderDeadzone = CreateWindowW(TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS | WS_TABSTOP,
        30, 464, 160, 20, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SLIDER_DEADZONE)), m_hInstance, nullptr);
    SendMessageW(m_hSliderDeadzone, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
    SendMessageW(m_hSliderDeadzone, TBM_SETPOS, TRUE, 2);


    // --- CARD 5: SMOOTHING & JITTER FILTER (Y = 498) ---
    m_hCheckSmoothing = CreateWindowW(L"BUTTON", L"Enable 1-Euro Smoothing Filter", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        30, 526, 210, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_SMOOTHING)), m_hInstance, nullptr);

    m_hSliderSmoothing = CreateWindowW(TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS | WS_TABSTOP,
        30, 550, 420, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SLIDER_SMOOTHING)), m_hInstance, nullptr);
    SendMessageW(m_hSliderSmoothing, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
    SendMessageW(m_hSliderSmoothing, TBM_SETPOS, TRUE, 12);


    // --- CARD 6: PEN BARREL BUTTON ACTIONS (Y = 588) ---
    m_hComboBarrel1 = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        30, 632, 200, 200, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO_BARREL1)), m_hInstance, nullptr);
    SendMessageW(m_hComboBarrel1, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Right Click"));
    SendMessageW(m_hComboBarrel1, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Undo (Ctrl+Z)"));
    SendMessageW(m_hComboBarrel1, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Eraser Toggle"));
    SendMessageW(m_hComboBarrel1, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Disabled"));
    SendMessageW(m_hComboBarrel1, CB_SETCURSEL, 0, 0);

    m_hComboBarrel2 = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        250, 632, 200, 200, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO_BARREL2)), m_hInstance, nullptr);
    SendMessageW(m_hComboBarrel2, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Eraser Toggle"));
    SendMessageW(m_hComboBarrel2, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Redo (Ctrl+Y)"));
    SendMessageW(m_hComboBarrel2, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Middle Click"));
    SendMessageW(m_hComboBarrel2, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Disabled"));
    SendMessageW(m_hComboBarrel2, CB_SETCURSEL, 0, 0);


    // --- CARD 7: SYSTEM & ACTIONS (Y = 668) ---
    m_hCheckStartup = CreateWindowW(L"BUTTON", L"Start with Windows", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        30, 674, 190, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_STARTUP)), m_hInstance, nullptr);

    m_hCheckMinimizeTray = CreateWindowW(L"BUTTON", L"Minimize to Tray", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        250, 674, 190, 22, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHECK_MINIMIZE_TRAY)), m_hInstance, nullptr);

    m_hBtnApply = CreateWindowW(L"BUTTON", L"Save & Apply Settings", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        30, 704, 420, 34, m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_APPLY)), m_hInstance, nullptr);


    // Apply Fonts to all controls
    HWND allControls[] = {
        m_hComboTabletPreset, m_hEditMaxX, m_hEditMaxY, m_hCheckAutoDetect, m_hBtnCalibrate, m_hBtnResetArea,
        m_hComboMapping, m_hBtnSetScreenArea, m_hCheckAspect, m_hCheckInk,
        m_hComboCurve, m_hSliderDeadzone, m_hCheckSmoothing, m_hSliderSmoothing,
        m_hComboBarrel1, m_hComboBarrel2, m_hCheckStartup, m_hCheckMinimizeTray, m_hBtnApply
    };
    for (HWND hCtrl : allControls) {
        if (hCtrl) SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFontNormal), TRUE);
    }

    // Subclass checkboxes for custom dark theme rendering (guarantees NO black text!)
    HWND checkboxes[] = {
        m_hCheckAutoDetect, m_hCheckAspect, m_hCheckInk, m_hCheckSmoothing,
        m_hCheckStartup, m_hCheckMinimizeTray
    };
    for (HWND hChk : checkboxes) {
        if (hChk) SetWindowSubclass(hChk, CheckboxSubclassProc, SUBCLASS_CHECKBOX_ID, 0);
    }

    // Subclass buttons for custom dark theme styling
    HWND secondaryButtons[] = {
        m_hBtnCalibrate, m_hBtnResetArea, m_hBtnSetScreenArea
    };
    for (HWND hBtn : secondaryButtons) {
        if (hBtn) SetWindowSubclass(hBtn, ButtonSubclassProc, SUBCLASS_BUTTON_ID, 0);
    }
    // Subclass primary Apply button (dwRefData = 1 for primary styling)
    if (m_hBtnApply) {
        SetWindowSubclass(m_hBtnApply, ButtonSubclassProc, SUBCLASS_BUTTON_ID, 1);
    }

    // Subclass Edit boxes for clean dark borders
    if (m_hEditMaxX) SetWindowSubclass(m_hEditMaxX, EditSubclassProc, SUBCLASS_EDIT_ID, 0);
    if (m_hEditMaxY) SetWindowSubclass(m_hEditMaxY, EditSubclassProc, SUBCLASS_EDIT_ID, 0);
}

void MainWindow::ResetActiveArea() {
    DriverConfig c = m_driver.GetConfig();
    c.tablet_max_x = 5040;
    c.tablet_max_y = 3780;
    c.tablet_area_left = 0.0;
    c.tablet_area_top = 0.0;
    c.tablet_area_right = 1.0;
    c.tablet_area_bottom = 1.0;
    m_driver.SetConfig(c);
    LoadConfigToUI();
    SaveConfigFromUI();
    m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver", L"Tablet active bounds reset to default 5040 x 3780!");
}

void MainWindow::OpenScreenAreaOverlay() {
    DriverConfig c = m_driver.GetConfig();
    double tablet_aspect = 4.0 / 3.0;
    if (c.tablet_max_y > 0) {
        tablet_aspect = static_cast<double>(c.tablet_max_x) / static_cast<double>(c.tablet_max_y);
    }

    m_overlay.Show(m_hInstance, m_hWnd, c.custom_screen_rect, tablet_aspect, [this](const RECT& selected_rect) {
        DriverConfig cfg = m_driver.GetConfig();
        cfg.mapping_mode = MappingMode::CustomArea;
        cfg.custom_screen_rect = selected_rect;
        m_driver.SetConfig(cfg);

        SendMessageW(m_hComboMapping, CB_SETCURSEL, 3, 0);
        SaveConfigFromUI();
        m_trayIcon.ShowBalloon(L"Wacom Screen Mapping", L"Custom screen mapping area applied and saved!");
    });
}

void MainWindow::LoadConfigToUI() {
    const DriverConfig& c = m_driver.GetConfig();

    // Preset selection
    if (c.tablet_max_x == 5040 && c.tablet_max_y == 3780) {
        SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 0, 0);
    } else if (c.tablet_max_x == 10206 && c.tablet_max_y == 7422) {
        SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 1, 0);
    } else if (c.tablet_max_x == 13918 && c.tablet_max_y == 10206) {
        SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 2, 0);
    } else if (c.tablet_max_x == 5104 && c.tablet_max_y == 3712) {
        SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 3, 0);
    } else {
        SendMessageW(m_hComboTabletPreset, CB_SETCURSEL, 4, 0);
    }

    SetWindowTextW(m_hEditMaxX, std::to_wstring(c.tablet_max_x).c_str());
    SetWindowTextW(m_hEditMaxY, std::to_wstring(c.tablet_max_y).c_str());
    SendMessageW(m_hCheckAutoDetect, BM_SETCHECK, c.auto_detect_bounds ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(m_hComboMapping, CB_SETCURSEL, static_cast<WPARAM>(c.mapping_mode), 0);
    SendMessageW(m_hCheckAspect, BM_SETCHECK, c.lock_aspect_ratio ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hCheckInk, BM_SETCHECK, c.use_windows_ink ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hComboCurve, CB_SETCURSEL, static_cast<WPARAM>(c.curve_type), 0);

    int deadzone_pos = static_cast<int>(c.pressure_min_threshold * 100.0);
    SendMessageW(m_hSliderDeadzone, TBM_SETPOS, TRUE, deadzone_pos);

    SendMessageW(m_hCheckSmoothing, BM_SETCHECK, c.enable_smoothing ? BST_CHECKED : BST_UNCHECKED, 0);
    int smooth_pos = static_cast<int>(c.filter_min_cutoff * 10.0);
    SendMessageW(m_hSliderSmoothing, TBM_SETPOS, TRUE, smooth_pos);

    SendMessageW(m_hCheckStartup, BM_SETCHECK, c.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hCheckMinimizeTray, BM_SETCHECK, c.minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);

    m_minimize_to_tray = c.minimize_to_tray;
}

void MainWindow::SaveConfigFromUI() {
    DriverConfig c = m_driver.GetConfig();

    wchar_t bufX[32]{ 0 };
    wchar_t bufY[32]{ 0 };
    GetWindowTextW(m_hEditMaxX, bufX, 32);
    GetWindowTextW(m_hEditMaxY, bufY, 32);

    try {
        c.tablet_max_x = static_cast<uint32_t>(std::stoul(bufX));
        c.tablet_max_y = static_cast<uint32_t>(std::stoul(bufY));
    } catch (...) {}

    if (c.tablet_max_x < 500) c.tablet_max_x = 5040;
    if (c.tablet_max_y < 500) c.tablet_max_y = 3780;

    c.auto_detect_bounds = (SendMessageW(m_hCheckAutoDetect, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.mapping_mode = static_cast<MappingMode>(SendMessageW(m_hComboMapping, CB_GETCURSEL, 0, 0));
    c.lock_aspect_ratio = (SendMessageW(m_hCheckAspect, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.use_windows_ink = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.curve_type = static_cast<PressureCurveType>(SendMessageW(m_hComboCurve, CB_GETCURSEL, 0, 0));

    int deadzone_pos = static_cast<int>(SendMessageW(m_hSliderDeadzone, TBM_GETPOS, 0, 0));
    c.pressure_min_threshold = static_cast<double>(deadzone_pos) / 100.0;

    c.enable_smoothing = (SendMessageW(m_hCheckSmoothing, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int smooth_pos = static_cast<int>(SendMessageW(m_hSliderSmoothing, TBM_GETPOS, 0, 0));
    c.filter_min_cutoff = static_cast<double>(smooth_pos) / 10.0;

    int b1 = static_cast<int>(SendMessageW(m_hComboBarrel1, CB_GETCURSEL, 0, 0));
    c.barrel_1_action = (b1 == 0) ? ButtonAction::RightClick : (b1 == 1) ? ButtonAction::Undo : (b1 == 2) ? ButtonAction::EraserToggle : ButtonAction::Disabled;

    int b2 = static_cast<int>(SendMessageW(m_hComboBarrel2, CB_GETCURSEL, 0, 0));
    c.barrel_2_action = (b2 == 0) ? ButtonAction::EraserToggle : (b2 == 1) ? ButtonAction::Redo : (b2 == 2) ? ButtonAction::MiddleClick : ButtonAction::Disabled;

    c.start_with_windows = (SendMessageW(m_hCheckStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
    c.minimize_to_tray = (SendMessageW(m_hCheckMinimizeTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_minimize_to_tray = c.minimize_to_tray;

    m_driver.SetConfig(c);
    ConfigManager::SaveConfig(c);
}

void MainWindow::StartCalibration() {
    m_calibration_step = CalibrationStep::WaitingForTopLeft;
    SetWindowTextW(m_hBtnCalibrate, L"Touch Top-Left...");
}

void MainWindow::OnStateUpdate(const TabletRawState& raw, const TabletProcessedState& processed) {
    std::lock_guard<std::mutex> lock(m_ui_mutex);
    m_live_raw = raw;
    m_live_processed = processed;

    // 2-point Corner Calibration state machine
    if (processed.is_contact || raw.tip_switch) {
        if (m_calibration_step == CalibrationStep::WaitingForTopLeft) {
            m_calib_min_x = raw.raw_x;
            m_calib_min_y = raw.raw_y;
            m_calibration_step = CalibrationStep::WaitingForBottomRight;
            SetWindowTextW(m_hBtnCalibrate, L"Touch Bottom-Right...");
        } else if (m_calibration_step == CalibrationStep::WaitingForBottomRight) {
            if (raw.raw_x > m_calib_min_x + 300 && raw.raw_y > m_calib_min_y + 300) {
                m_calib_max_x = raw.raw_x;
                m_calib_max_y = raw.raw_y;
                m_calibration_step = CalibrationStep::Completed;
                SetWindowTextW(m_hBtnCalibrate, L"Calibrate Corners");

                SetWindowTextW(m_hEditMaxX, std::to_wstring(m_calib_max_x).c_str());
                SetWindowTextW(m_hEditMaxY, std::to_wstring(m_calib_max_y).c_str());
                SaveConfigFromUI();
            }
        }
    }
}

void MainWindow::OnConnectionUpdate(bool connected, const std::wstring& device_name) {
    std::lock_guard<std::mutex> lock(m_ui_mutex);
    m_is_connected = connected;
    m_device_name = connected ? device_name : L"Disconnected";
    m_trayIcon.UpdateTooltip(L"Wacom CT-0405-U Driver: " + m_device_name);
}

void MainWindow::OnTimer() {
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_HSCROLL: {
            SaveConfigFromUI();
            InvalidateRect(m_hWnd, nullptr, FALSE);
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (wmId == ID_COMBO_TABLET_PRESET && code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(m_hComboTabletPreset, CB_GETCURSEL, 0, 0));
                if (sel == 0) {
                    SetWindowTextW(m_hEditMaxX, L"5040");
                    SetWindowTextW(m_hEditMaxY, L"3780");
                } else if (sel == 1) {
                    SetWindowTextW(m_hEditMaxX, L"10206");
                    SetWindowTextW(m_hEditMaxY, L"7422");
                } else if (sel == 2) {
                    SetWindowTextW(m_hEditMaxX, L"13918");
                    SetWindowTextW(m_hEditMaxY, L"10206");
                } else if (sel == 3) {
                    SetWindowTextW(m_hEditMaxX, L"5104");
                    SetWindowTextW(m_hEditMaxY, L"3712");
                }
                SaveConfigFromUI();
            } else if (code == CBN_SELCHANGE || code == BN_CLICKED) {
                if (wmId == ID_BTN_CALIBRATE) {
                    StartCalibration();
                } else if (wmId == ID_BTN_RESET_AREA) {
                    ResetActiveArea();
                } else if (wmId == ID_BTN_SET_SCREEN_AREA) {
                    OpenScreenAreaOverlay();
                } else if (wmId == ID_BTN_APPLY) {
                    SaveConfigFromUI();
                    m_trayIcon.ShowBalloon(L"Wacom CT-0405-U Driver", L"Settings saved and applied successfully!");
                } else {
                    SaveConfigFromUI();
                }
            } else if (code == EN_KILLFOCUS) {
                SaveConfigFromUI();
            }
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_CLOSE:
            SaveConfigFromUI();
            ShowWindow(m_hWnd, SW_HIDE);
            return 0;

        case WM_SYSCOMMAND: {
            UINT cmd = (wParam & 0xFFF0);
            if (cmd == SC_MINIMIZE || cmd == SC_CLOSE) {
                SaveConfigFromUI();
                ShowWindow(m_hWnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                ShowWindow(m_hWnd, SW_RESTORE);
                SetForegroundWindow(m_hWnd);
            } else if (lParam == WM_RBUTTONUP) {
                bool is_ink = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                m_trayIcon.ShowContextMenu(m_hWnd, m_is_connected, is_ink, [this](int cmd) {
                    if (cmd == ID_TRAY_OPEN) {
                        ShowWindow(m_hWnd, SW_RESTORE);
                        SetForegroundWindow(m_hWnd);
                    } else if (cmd == ID_TRAY_TOGGLE_INK) {
                        bool cur = (SendMessageW(m_hCheckInk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        SendMessageW(m_hCheckInk, BM_SETCHECK, cur ? BST_UNCHECKED : BST_CHECKED, 0);
                        SaveConfigFromUI();
                    } else if (cmd == ID_TRAY_EXIT) {
                        SaveConfigFromUI();
                        m_trayIcon.Remove();
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
            SaveConfigFromUI();
            m_trayIcon.Remove();
            KillTimer(m_hWnd, ID_TIMER_REFRESH);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void MainWindow::OnPaint(HDC hdc) {
    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    int width = rcClient.right - rcClient.left;
    int height = rcClient.bottom - rcClient.top;

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

    Gdiplus::Graphics g(hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Deep Slate Background
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
    Gdiplus::Font titleFont(L"Segoe UI", 11.5f, Gdiplus::FontStyleBold);
    Gdiplus::Font badgeFont(L"Segoe UI", 8.0f, Gdiplus::FontStyleBold);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));

    g.DrawString(L"WACOM CT-0405-U DRIVER", -1, &titleFont, Gdiplus::PointF(16.0f, 15.0f), &titleBrush);

    bool connected;
    std::wstring dev_name;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        connected = m_is_connected || m_driver.IsConnected();
        dev_name = m_device_name;
    }

    // Dynamic Compact Connection Badge Pill
    float badgeW = 200.0f;
    float badgeH = 24.0f;
    float badgeX = static_cast<float>(width) - badgeW - 16.0f;
    float badgeY = 14.0f;

    Gdiplus::GraphicsPath badgePath;
    AddRoundedRectangle(badgePath, badgeX, badgeY, badgeW, badgeH, 12.0f);

    if (connected) {
        Gdiplus::SolidBrush bBg(Gdiplus::Color(255, 19, 45, 35));
        Gdiplus::Pen bBorder(Gdiplus::Color(255, 35, 134, 54), 1.2f);
        Gdiplus::SolidBrush bText(Gdiplus::Color(255, 63, 185, 80));

        g.FillPath(&bBg, &badgePath);
        g.DrawPath(&bBorder, &badgePath);

        std::wstring text = L"● CONNECTED: " + dev_name;
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(text.c_str(), -1, &badgeFont, Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &fmt, &bText);
    } else {
        Gdiplus::SolidBrush bBg(Gdiplus::Color(255, 45, 22, 25));
        Gdiplus::Pen bBorder(Gdiplus::Color(255, 218, 54, 51), 1.2f);
        Gdiplus::SolidBrush bText(Gdiplus::Color(255, 248, 81, 73));

        g.FillPath(&bBg, &badgePath);
        g.DrawPath(&bBorder, &badgePath);

        std::wstring text = L"○ DISCONNECTED (USB)";
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(text.c_str(), -1, &badgeFont, Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &fmt, &bText);
    }

    // Top Header Divider
    Gdiplus::Pen dividerPen(Gdiplus::Color(255, 37, 42, 56), 1.0f);
    g.DrawLine(&dividerPen, 16.0f, 46.0f, static_cast<float>(width - 16), 46.0f);
}

void MainWindow::RenderCards(Gdiplus::Graphics& g, int /*width*/, int /*height*/) {
    Gdiplus::Font cardHeaderFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font labelFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
    Gdiplus::Font helperFont(L"Segoe UI", 7.5f, Gdiplus::FontStyleRegular);

    Gdiplus::SolidBrush headerBrush(Gdiplus::Color(255, 76, 201, 240));   // Cyan Accent
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 201, 209, 217));    // Crisp Light Text
    Gdiplus::SolidBrush cyanBrush(Gdiplus::Color(255, 76, 201, 240));     // Live Cyan Value
    Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 63, 185, 80));     // Green Status
    Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 248, 81, 73));       // Red Status
    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 240, 243, 250));    // Pure Light Text

    Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 27, 31, 42));
    Gdiplus::Pen cardBorder(Gdiplus::Color(255, 46, 54, 72), 1.0f);

    auto draw_card = [&](float x, float y, float w, float h) {
        Gdiplus::GraphicsPath path;
        AddRoundedRectangle(path, x, y, w, h, 6.0f);
        g.FillPath(&cardBg, &path);
        g.DrawPath(&cardBorder, &path);
    };

    float cardX = 16.0f;
    float cardW = 448.0f;

    // --- CARD 1: DRIVER STATUS (Y = 56, H = 82) ---
    draw_card(cardX, 56.0f, cardW, 82.0f);
    g.DrawString(L"DRIVER STATUS & HARDWARE PROFILE", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 66.0f), &headerBrush);

    DriverConfig cfg = m_driver.GetConfig();
    bool connected = m_is_connected || m_driver.IsConnected();

    // Row 1
    g.DrawString(L"Device:", -1, &labelFont, Gdiplus::PointF(cardX + 14.0f, 88.0f), &labelBrush);
    g.DrawString(connected ? (m_device_name + L" (Active)").c_str() : L"Disconnected", -1, &valueFont, Gdiplus::PointF(cardX + 68.0f, 88.0f), connected ? &greenBrush : &redBrush);

    g.DrawString(L"Bounds:", -1, &labelFont, Gdiplus::PointF(cardX + 235.0f, 88.0f), &labelBrush);
    std::wstring bStr = std::to_wstring(cfg.tablet_max_x) + L" x " + std::to_wstring(cfg.tablet_max_y);
    g.DrawString(bStr.c_str(), -1, &valueFont, Gdiplus::PointF(cardX + 295.0f, 88.0f), &whiteBrush);

    // Row 2
    g.DrawString(L"Pointer:", -1, &labelFont, Gdiplus::PointF(cardX + 14.0f, 110.0f), &labelBrush);
    g.DrawString(cfg.use_windows_ink ? L"Native Ink (Pen)" : L"Absolute Mouse", -1, &valueFont, Gdiplus::PointF(cardX + 68.0f, 110.0f), &cyanBrush);

    g.DrawString(L"Target:", -1, &labelFont, Gdiplus::PointF(cardX + 235.0f, 110.0f), &labelBrush);
    std::wstring mapStr = (cfg.mapping_mode == MappingMode::CustomArea) ? L"Custom Area" : ((cfg.mapping_mode == MappingMode::PrimaryMonitor) ? L"Primary Display" : L"Virtual Desktop");
    g.DrawString(mapStr.c_str(), -1, &valueFont, Gdiplus::PointF(cardX + 295.0f, 110.0f), &whiteBrush);


    // --- CARD 2: TABLET SPACE & CALIBRATION (Y = 146, H = 122) ---
    draw_card(cardX, 146.0f, cardW, 122.0f);
    g.DrawString(L"TABLET SPACE & CALIBRATION", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 154.0f), &headerBrush);
    g.DrawString(L"Max X:", -1, &labelFont, Gdiplus::PointF(cardX + 14.0f, 208.0f), &labelBrush);
    g.DrawString(L"Max Y:", -1, &labelFont, Gdiplus::PointF(cardX + 162.0f, 208.0f), &labelBrush);


    // --- CARD 3: SCREEN MAPPING & WINDOWS INK (Y = 276, H = 102) ---
    draw_card(cardX, 276.0f, cardW, 102.0f);
    g.DrawString(L"SCREEN MAPPING & WINDOWS INK", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 284.0f), &headerBrush);


    // --- CARD 4: PRESSURE & SENSITIVITY (Y = 386, H = 104) ---
    draw_card(cardX, 386.0f, cardW, 104.0f);
    g.DrawString(L"PRESSURE & SENSITIVITY", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 394.0f), &headerBrush);

    int deadzone_pos = static_cast<int>(SendMessageW(m_hSliderDeadzone, TBM_GETPOS, 0, 0));
    std::wstring deadzoneStr = std::to_wstring(deadzone_pos) + L"%";
    g.DrawString(L"Deadzone:", -1, &labelFont, Gdiplus::PointF(cardX + 14.0f, 444.0f), &labelBrush);
    g.DrawString(deadzoneStr.c_str(), -1, &valueFont, Gdiplus::PointF(cardX + 140.0f, 444.0f), &cyanBrush);

    // Interactive Curve Graph Box
    RECT curve_graph_rect{ static_cast<LONG>(cardX + 195.0f), 414, static_cast<LONG>(cardX + 434.0f), 478 };
    RenderCurveGraph(g, curve_graph_rect);


    // --- CARD 5: SMOOTHING & JITTER FILTER (Y = 498, H = 82) ---
    draw_card(cardX, 498.0f, cardW, 82.0f);
    g.DrawString(L"SMOOTHING & JITTER FILTER", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 506.0f), &headerBrush);

    int smooth_pos = static_cast<int>(SendMessageW(m_hSliderSmoothing, TBM_GETPOS, 0, 0));
    std::wostringstream ssCutoff;
    ssCutoff << std::fixed << std::setprecision(2) << (static_cast<double>(smooth_pos) / 10.0) << L" Hz";
    g.DrawString(L"Min Cutoff:", -1, &labelFont, Gdiplus::PointF(cardX + 265.0f, 528.0f), &labelBrush);
    g.DrawString(ssCutoff.str().c_str(), -1, &valueFont, Gdiplus::PointF(cardX + 372.0f, 528.0f), &cyanBrush);


    // --- CARD 6: PEN BARREL BUTTON ACTIONS (Y = 588, H = 72) ---
    draw_card(cardX, 588.0f, cardW, 72.0f);
    g.DrawString(L"PEN BARREL BUTTONS", -1, &cardHeaderFont, Gdiplus::PointF(cardX + 14.0f, 596.0f), &headerBrush);
    g.DrawString(L"Barrel 1 (Lower):", -1, &labelFont, Gdiplus::PointF(cardX + 14.0f, 614.0f), &labelBrush);
    g.DrawString(L"Barrel 2 (Upper):", -1, &labelFont, Gdiplus::PointF(cardX + 234.0f, 614.0f), &labelBrush);


    // --- CARD 7: SYSTEM & ACTIONS (Y = 668, H = 78) ---
    draw_card(cardX, 668.0f, cardW, 78.0f);
}

void MainWindow::RenderCurveGraph(Gdiplus::Graphics& g, const RECT& rect) {
    float gx = static_cast<float>(rect.left);
    float gy = static_cast<float>(rect.top);
    float gw = static_cast<float>(rect.right - rect.left);
    float gh = static_cast<float>(rect.bottom - rect.top);

    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path, gx, gy, gw, gh, 4.0f);

    Gdiplus::SolidBrush graphBg(Gdiplus::Color(255, 18, 21, 28));
    Gdiplus::Pen graphBorder(Gdiplus::Color(255, 46, 54, 72), 1.0f);
    g.FillPath(&graphBg, &path);
    g.DrawPath(&graphBorder, &path);

    // Reference Diagonal Line (45 degree linear baseline)
    Gdiplus::Pen refPen(Gdiplus::Color(70, 140, 150, 170), 1.0f);
    refPen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawLine(&refPen, gx + 6.0f, gy + gh - 6.0f, gx + gw - 6.0f, gy + 6.0f);

    int sel = static_cast<int>(SendMessageW(m_hComboCurve, CB_GETCURSEL, 0, 0));
    PressureCurveType curveType = static_cast<PressureCurveType>(sel);

    Gdiplus::Pen curvePen(Gdiplus::Color(255, 6, 214, 160), 2.0f);
    curvePen.SetLineJoin(Gdiplus::LineJoinRound);

    float innerPad = 5.0f;
    float innerW = gw - innerPad * 2.0f;
    float innerH = gh - innerPad * 2.0f;
    float startX = gx + innerPad;
    float startY = gy + gh - innerPad;

    Gdiplus::PointF prevPoint(startX, startY);

    const int steps = 30;
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(steps);
        double val = t;
        switch (curveType) {
            case PressureCurveType::Linear: val = t; break;
            case PressureCurveType::Soft: val = std::pow(t, 0.65); break;
            case PressureCurveType::VerySoft: val = std::pow(t, 0.45); break;
            case PressureCurveType::Firm: val = std::pow(t, 1.55); break;
            case PressureCurveType::Hard: val = std::pow(t, 2.2); break;
            default: val = t; break;
        }

        float px = startX + static_cast<float>(t) * innerW;
        float py = startY - static_cast<float>(val) * innerH;
        Gdiplus::PointF curPoint(px, py);
        g.DrawLine(&curvePen, prevPoint, curPoint);
        prevPoint = curPoint;
    }

    // Live Pressure Marker
    TabletProcessedState processed;
    {
        std::lock_guard<std::mutex> lock(m_ui_mutex);
        processed = m_live_processed;
    }

    if (processed.in_proximity && processed.pressure > 0.0) {
        float markX = startX + static_cast<float>(processed.pressure) * innerW;
        float markY = startY - static_cast<float>(processed.pressure) * innerH;
        Gdiplus::SolidBrush markBrush(Gdiplus::Color(255, 255, 159, 28));
        Gdiplus::Pen markRing(Gdiplus::Color(255, 255, 255, 255), 1.5f);
        g.FillEllipse(&markBrush, markX - 3.5f, markY - 3.5f, 7.0f, 7.0f);
        g.DrawEllipse(&markRing, markX - 3.5f, markY - 3.5f, 7.0f, 7.0f);
    }
}

} // namespace ct0405
