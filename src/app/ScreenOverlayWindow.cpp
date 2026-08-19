#include "ScreenOverlayWindow.h"
#include <windowsx.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace ct0405 {

constexpr int HANDLE_RADIUS = 7;
constexpr int HANDLE_TOUCH = 14;

// Helper to format aspect ratios nicely (e.g. 4:3, 16:9, etc.)
static std::wstring FormatAspectRatio(double w, double h) {
    if (h <= 0.001 || w <= 0.001) return L"N/A";
    double ratio = w / h;
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(2) << ratio << L":1";

    if (std::abs(ratio - (4.0 / 3.0)) < 0.025) {
        ss << L" (4:3)";
    } else if (std::abs(ratio - (16.0 / 9.0)) < 0.025) {
        ss << L" (16:9)";
    } else if (std::abs(ratio - (16.0 / 10.0)) < 0.025) {
        ss << L" (16:10)";
    } else if (std::abs(ratio - (21.0 / 9.0)) < 0.035) {
        ss << L" (21:9)";
    } else if (std::abs(ratio - (3.0 / 2.0)) < 0.025) {
        ss << L" (3:2)";
    } else if (std::abs(ratio - 1.0) < 0.02) {
        ss << L" (1:1)";
    } else if (std::abs(ratio - (5.0 / 4.0)) < 0.025) {
        ss << L" (5:4)";
    }
    return ss.str();
}

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

ScreenOverlayWindow::ScreenOverlayWindow() {}

ScreenOverlayWindow::~ScreenOverlayWindow() {
    Close();
}

bool ScreenOverlayWindow::Show(HINSTANCE hInstance, HWND hParent, const RECT& initial_rect, double tablet_aspect, uint32_t tablet_w, uint32_t tablet_h, ApplyCallback on_apply) {
    m_hParent = hParent;
    m_on_apply = on_apply;
    m_tablet_aspect = (tablet_aspect > 0.1) ? tablet_aspect : (4.0 / 3.0);
    m_tablet_w = (tablet_w > 100) ? tablet_w : 5040;
    m_tablet_h = (tablet_h > 100) ? tablet_h : 3780;
    m_lock_aspect = true;

    const wchar_t CLASS_NAME[] = L"CT0405_ScreenOverlay_Class";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 16, 20));
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    m_virtual_rect = { vx, vy, vx + vw, vy + vh };

    int init_w = initial_rect.right - initial_rect.left;
    int init_h = initial_rect.bottom - initial_rect.top;
    if (init_w > 100 && init_h > 100) {
        m_selection_rect = initial_rect;
    } else {
        int w = std::min(vw, 1280);
        int h = static_cast<int>(w / m_tablet_aspect);
        if (h > vh - 100) {
            h = vh - 150;
            w = static_cast<int>(h * m_tablet_aspect);
        }
        int cx = vx + (vw - w) / 2;
        int cy = vy + (vh - h) / 2;
        m_selection_rect = { cx, cy, cx + w, cy + h };
    }

    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        CLASS_NAME,
        L"CT0405 Screen Mapping Overlay",
        WS_POPUP,
        vx, vy, vw, vh,
        hParent,
        nullptr,
        hInstance,
        this
    );

    if (!m_hWnd) return false;

    // Set 90% opacity
    SetLayeredWindowAttributes(m_hWnd, 0, 230, LWA_ALPHA);

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);

    return true;
}

void ScreenOverlayWindow::Close() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

LRESULT CALLBACK ScreenOverlayWindow::OverlayProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ScreenOverlayWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<ScreenOverlayWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<ScreenOverlayWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

OverlayHitTest ScreenOverlayWindow::PerformHitTest(int x, int y) {
    POINT pt{ x, y };

    if (PtInRect(&m_btn_apply_rect, pt)) return OverlayHitTest::BtnApply;
    if (PtInRect(&m_btn_cancel_rect, pt)) return OverlayHitTest::BtnCancel;
    if (PtInRect(&m_btn_ratio_rect, pt)) return OverlayHitTest::BtnMatchRatio;
    if (PtInRect(&m_btn_primary_rect, pt)) return OverlayHitTest::BtnPrimary;

    int l = m_selection_rect.left;
    int r = m_selection_rect.right;
    int t = m_selection_rect.top;
    int b = m_selection_rect.bottom;
    int mx = (l + r) / 2;
    int my = (t + b) / 2;

    auto in_point = [&](int px, int py) {
        return (std::abs(x - px) <= HANDLE_TOUCH && std::abs(y - py) <= HANDLE_TOUCH);
    };

    if (in_point(l, t)) return OverlayHitTest::TopLeft;
    if (in_point(r, t)) return OverlayHitTest::TopRight;
    if (in_point(l, b)) return OverlayHitTest::BottomLeft;
    if (in_point(r, b)) return OverlayHitTest::BottomRight;

    if (in_point(mx, t)) return OverlayHitTest::Top;
    if (in_point(mx, b)) return OverlayHitTest::Bottom;
    if (in_point(l, my)) return OverlayHitTest::Left;
    if (in_point(r, my)) return OverlayHitTest::Right;

    if (PtInRect(&m_selection_rect, pt)) {
        return OverlayHitTest::Inside;
    }

    return OverlayHitTest::None;
}

void ScreenOverlayWindow::UpdateCursor(OverlayHitTest hit) {
    HCURSOR hCur = LoadCursor(nullptr, IDC_ARROW);
    switch (hit) {
        case OverlayHitTest::Inside:
            hCur = LoadCursor(nullptr, IDC_SIZEALL);
            break;
        case OverlayHitTest::TopLeft:
        case OverlayHitTest::BottomRight:
            hCur = LoadCursor(nullptr, IDC_SIZENWSE);
            break;
        case OverlayHitTest::TopRight:
        case OverlayHitTest::BottomLeft:
            hCur = LoadCursor(nullptr, IDC_SIZENESW);
            break;
        case OverlayHitTest::Top:
        case OverlayHitTest::Bottom:
            hCur = LoadCursor(nullptr, IDC_SIZENS);
            break;
        case OverlayHitTest::Left:
        case OverlayHitTest::Right:
            hCur = LoadCursor(nullptr, IDC_SIZEWE);
            break;
        case OverlayHitTest::BtnApply:
        case OverlayHitTest::BtnCancel:
        case OverlayHitTest::BtnMatchRatio:
        case OverlayHitTest::BtnPrimary:
            hCur = LoadCursor(nullptr, IDC_HAND);
            break;
        default:
            hCur = LoadCursor(nullptr, IDC_ARROW);
            break;
    }
    SetCursor(hCur);
}

void ScreenOverlayWindow::ConstrainToAspectRatio(RECT& rc, OverlayHitTest hit) {
    if (!m_lock_aspect || m_tablet_aspect <= 0.01) return;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 80) w = 80;
    if (h < 60) h = 60;

    switch (hit) {
        case OverlayHitTest::BottomRight:
            h = static_cast<int>(w / m_tablet_aspect);
            rc.bottom = rc.top + h;
            break;
        case OverlayHitTest::TopRight:
            h = static_cast<int>(w / m_tablet_aspect);
            rc.top = rc.bottom - h;
            break;
        case OverlayHitTest::BottomLeft:
            h = static_cast<int>(w / m_tablet_aspect);
            rc.bottom = rc.top + h;
            break;
        case OverlayHitTest::TopLeft:
            h = static_cast<int>(w / m_tablet_aspect);
            rc.top = rc.bottom - h;
            break;
        case OverlayHitTest::Right:
        case OverlayHitTest::Left:
            h = static_cast<int>(w / m_tablet_aspect);
            rc.bottom = rc.top + h;
            break;
        case OverlayHitTest::Bottom:
        case OverlayHitTest::Top:
            w = static_cast<int>(h * m_tablet_aspect);
            rc.right = rc.left + w;
            break;
        default:
            break;
    }
}

LRESULT ScreenOverlayWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_SETCURSOR:
            return TRUE;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (!m_is_dragging) {
                OverlayHitTest hit = PerformHitTest(x, y);
                UpdateCursor(hit);
            } else {
                int dx = x - m_drag_start_mouse.x;
                int dy = y - m_drag_start_mouse.y;

                RECT newRect = m_drag_start_rect;

                if (m_active_hit == OverlayHitTest::Inside) {
                    newRect.left += dx;
                    newRect.right += dx;
                    newRect.top += dy;
                    newRect.bottom += dy;
                } else {
                    if (m_active_hit == OverlayHitTest::Left || m_active_hit == OverlayHitTest::TopLeft || m_active_hit == OverlayHitTest::BottomLeft) {
                        newRect.left += dx;
                    }
                    if (m_active_hit == OverlayHitTest::Right || m_active_hit == OverlayHitTest::TopRight || m_active_hit == OverlayHitTest::BottomRight) {
                        newRect.right += dx;
                    }
                    if (m_active_hit == OverlayHitTest::Top || m_active_hit == OverlayHitTest::TopLeft || m_active_hit == OverlayHitTest::TopRight) {
                        newRect.top += dy;
                    }
                    if (m_active_hit == OverlayHitTest::Bottom || m_active_hit == OverlayHitTest::BottomLeft || m_active_hit == OverlayHitTest::BottomRight) {
                        newRect.bottom += dy;
                    }

                    // Enforce minimum size
                    if (newRect.right - newRect.left < 80) newRect.right = newRect.left + 80;
                    if (newRect.bottom - newRect.top < 60) newRect.bottom = newRect.top + 60;

                    // Moving/resizing with SHIFT held allows FREEFORM mode (does NOT follow tablet aspect ratio)
                    bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (!shiftHeld && m_lock_aspect) {
                        ConstrainToAspectRatio(newRect, m_active_hit);
                    }
                }

                m_selection_rect = newRect;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            OverlayHitTest hit = PerformHitTest(x, y);
            if (hit == OverlayHitTest::BtnApply) {
                if (m_on_apply) m_on_apply(m_selection_rect);
                Close();
                return 0;
            } else if (hit == OverlayHitTest::BtnCancel) {
                Close();
                return 0;
            } else if (hit == OverlayHitTest::BtnMatchRatio) {
                int w = m_selection_rect.right - m_selection_rect.left;
                int h = static_cast<int>(w / m_tablet_aspect);
                m_selection_rect.bottom = m_selection_rect.top + h;
                m_lock_aspect = true;
                InvalidateRect(m_hWnd, nullptr, FALSE);
                return 0;
            } else if (hit == OverlayHitTest::BtnPrimary) {
                RECT primary{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
                int w = primary.right - primary.left;
                int h = static_cast<int>(w / m_tablet_aspect);
                if (h > primary.bottom) {
                    h = primary.bottom;
                    w = static_cast<int>(h * m_tablet_aspect);
                }
                int cx = (primary.right - w) / 2;
                int cy = (primary.bottom - h) / 2;
                m_selection_rect = { cx, cy, cx + w, cy + h };
                m_lock_aspect = true;
                InvalidateRect(m_hWnd, nullptr, FALSE);
                return 0;
            }

            if (hit != OverlayHitTest::None) {
                SetCapture(m_hWnd);
                m_is_dragging = true;
                m_active_hit = hit;
                m_drag_start_mouse = { x, y };
                m_drag_start_rect = m_selection_rect;
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (m_is_dragging) {
                ReleaseCapture();
                m_is_dragging = false;
                m_active_hit = OverlayHitTest::None;
            }
            return 0;

        case WM_LBUTTONDBLCLK: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (PtInRect(&m_selection_rect, { x, y })) {
                if (m_on_apply) m_on_apply(m_selection_rect);
                Close();
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                if (m_on_apply) m_on_apply(m_selection_rect);
                Close();
            } else if (wParam == VK_ESCAPE) {
                Close();
            } else if (wParam == 'M' || wParam == 'm') {
                // Quick shortcut to match tablet aspect ratio
                int w = m_selection_rect.right - m_selection_rect.left;
                int h = static_cast<int>(w / m_tablet_aspect);
                m_selection_rect.bottom = m_selection_rect.top + h;
                m_lock_aspect = true;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (wParam == 'P' || wParam == 'p') {
                // Quick shortcut to fit primary monitor
                RECT primary{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
                int w = primary.right - primary.left;
                int h = static_cast<int>(w / m_tablet_aspect);
                if (h > primary.bottom) {
                    h = primary.bottom;
                    w = static_cast<int>(h * m_tablet_aspect);
                }
                int cx = (primary.right - w) / 2;
                int cy = (primary.bottom - h) / 2;
                m_selection_rect = { cx, cy, cx + w, cy + h };
                m_lock_aspect = true;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (wParam == VK_LEFT) {
                int shift = (GetKeyState(VK_CONTROL) & 0x8000) ? 20 : 2;
                m_selection_rect.left -= shift;
                m_selection_rect.right -= shift;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (wParam == VK_RIGHT) {
                int shift = (GetKeyState(VK_CONTROL) & 0x8000) ? 20 : 2;
                m_selection_rect.left += shift;
                m_selection_rect.right += shift;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (wParam == VK_UP) {
                int shift = (GetKeyState(VK_CONTROL) & 0x8000) ? 20 : 2;
                m_selection_rect.top -= shift;
                m_selection_rect.bottom -= shift;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            } else if (wParam == VK_DOWN) {
                int shift = (GetKeyState(VK_CONTROL) & 0x8000) ? 20 : 2;
                m_selection_rect.top += shift;
                m_selection_rect.bottom += shift;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_KEYUP:
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hWnd, &ps);
            OnPaint(hdc);
            EndPaint(m_hWnd, &ps);
            return 0;
        }
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void ScreenOverlayWindow::OnPaint(HDC hdc) {
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

    // Dimmed Dark Scrim
    Gdiplus::SolidBrush scrimBrush(Gdiplus::Color(190, 10, 12, 18));
    g.FillRectangle(&scrimBrush, 0, 0, width, height);

    // Punch out / clear selection area
    float sx = static_cast<float>(m_selection_rect.left);
    float sy = static_cast<float>(m_selection_rect.top);
    float sw = static_cast<float>(m_selection_rect.right - m_selection_rect.left);
    float sh = static_cast<float>(m_selection_rect.bottom - m_selection_rect.top);

    double current_aspect = (sh > 0.0) ? (static_cast<double>(sw) / static_cast<double>(sh)) : 1.0;
    bool is_matched = std::abs(current_aspect - m_tablet_aspect) < 0.03;
    bool shift_held = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Interior tint color
    Gdiplus::Color themeCol = is_matched ? Gdiplus::Color(255, 76, 201, 240) : (shift_held ? Gdiplus::Color(255, 255, 170, 0) : Gdiplus::Color(255, 255, 107, 129));
    Gdiplus::SolidBrush fillBrush(Gdiplus::Color(25, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()));
    g.FillRectangle(&fillBrush, sx, sy, sw, sh);

    // Outer glow & main border
    Gdiplus::Pen glowPen(Gdiplus::Color(90, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()), 4.0f);
    g.DrawRectangle(&glowPen, sx - 1.0f, sy - 1.0f, sw + 2.0f, sh + 2.0f);

    Gdiplus::Pen borderPen(themeCol, 2.0f);
    g.DrawRectangle(&borderPen, sx, sy, sw, sh);

    // Inner Grid / Crosshair
    Gdiplus::Pen gridPen(Gdiplus::Color(50, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()), 1.0f);
    g.DrawLine(&gridPen, sx + sw / 2.0f, sy, sx + sw / 2.0f, sy + sh);
    g.DrawLine(&gridPen, sx, sy + sh / 2.0f, sx + sw, sy + sh / 2.0f);

    // Handles
    auto draw_handle = [&](float x, float y) {
        Gdiplus::SolidBrush hBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Pen hPen(themeCol, 2.0f);
        g.FillEllipse(&hBrush, x - HANDLE_RADIUS, y - HANDLE_RADIUS, HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f);
        g.DrawEllipse(&hPen, x - HANDLE_RADIUS, y - HANDLE_RADIUS, HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f);
    };

    draw_handle(sx, sy);
    draw_handle(sx + sw, sy);
    draw_handle(sx, sy + sh);
    draw_handle(sx + sw, sy + sh);
    draw_handle(sx + sw / 2.0f, sy);
    draw_handle(sx + sw / 2.0f, sy + sh);
    draw_handle(sx, sy + sh / 2.0f);
    draw_handle(sx + sw, sy + sh / 2.0f);

    // Top Floating Toolbar Banner
    float tbW = 860.0f;
    float tbH = 74.0f;
    float tbX = (static_cast<float>(width) - tbW) / 2.0f;
    float tbY = 24.0f;

    Gdiplus::GraphicsPath tbPath;
    AddRoundedRectangle(tbPath, tbX, tbY, tbW, tbH, 8.0f);

    Gdiplus::SolidBrush tbBg(Gdiplus::Color(245, 20, 23, 31));
    Gdiplus::Pen tbBorder(Gdiplus::Color(255, 50, 60, 80), 1.2f);
    g.FillPath(&tbBg, &tbPath);
    g.DrawPath(&tbBorder, &tbPath);

    Gdiplus::Font titleFont(L"Segoe UI", 10.5f, Gdiplus::FontStyleBold);
    Gdiplus::Font subFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleRegular);
    Gdiplus::Font btnFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);

    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 180, 190, 210));
    Gdiplus::SolidBrush cyanBrush(Gdiplus::Color(255, 76, 201, 240));
    Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 6, 214, 160));
    Gdiplus::SolidBrush amberBrush(Gdiplus::Color(255, 255, 170, 0));

    g.DrawString(L"INTERACTIVE TABLET SCREEN MAPPING OVERLAY", -1, &titleFont, Gdiplus::PointF(tbX + 18.0f, tbY + 10.0f), &textBrush);

    // Format Overlay Aspect Ratio vs Calibrated Tablet Aspect Ratio
    std::wstring overlayRatioStr = FormatAspectRatio(sw, sh);
    std::wstring tabletRatioStr = FormatAspectRatio(m_tablet_w, m_tablet_h);

    std::wostringstream ssInfo;
    ssInfo << L"Overlay: " << static_cast<int>(sw) << L"x" << static_cast<int>(sh) << L" (" << overlayRatioStr << L")  |  "
           << L"Calibrated Tablet: " << m_tablet_w << L"x" << m_tablet_h << L" (" << tabletRatioStr << L")  |  "
           << (is_matched ? L"[✓ 1:1 Matched]" : (shift_held ? L"[⚠️ Freeform (Shift Active)]" : L"[≠ Freeform Ratio]"));

    g.DrawString(ssInfo.str().c_str(), -1, &subFont, Gdiplus::PointF(tbX + 18.0f, tbY + 36.0f), is_matched ? &greenBrush : (shift_held ? &amberBrush : &cyanBrush));

    // Helper tip line
    Gdiplus::Font tipFont(L"Segoe UI", 7.5f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 140, 150, 170));
    g.DrawString(L"Tip: Hold SHIFT while dragging handles to resize freely without locking aspect ratio.", -1, &tipFont, Gdiplus::PointF(tbX + 18.0f, tbY + 54.0f), &tipBrush);

    // Toolbar action buttons
    auto draw_btn = [&](float bx, float by, float bw, float bh, const wchar_t* text, Gdiplus::Color bgColor, Gdiplus::Color textColor, Gdiplus::Color borderCol, RECT& out_rect) {
        Gdiplus::GraphicsPath bPath;
        AddRoundedRectangle(bPath, bx, by, bw, bh, 4.0f);

        Gdiplus::SolidBrush bBrush(bgColor);
        Gdiplus::Pen bPen(borderCol, 1.0f);
        g.FillPath(&bBrush, &bPath);
        g.DrawPath(&bPen, &bPath);

        Gdiplus::SolidBrush tBrush(textColor);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(text, -1, &btnFont, Gdiplus::RectF(bx, by, bw, bh), &sf, &tBrush);

        out_rect = { static_cast<int>(bx), static_cast<int>(by), static_cast<int>(bx + bw), static_cast<int>(by + bh) };
    };

    float btnY = tbY + 18.0f;
    draw_btn(tbX + tbW - 365.0f, btnY, 115.0f, 32.0f, L"Match Ratio [M]", Gdiplus::Color(255, 35, 40, 55), Gdiplus::Color(255, 76, 201, 240), Gdiplus::Color(255, 76, 201, 240), m_btn_ratio_rect);
    draw_btn(tbX + tbW - 240.0f, btnY, 115.0f, 32.0f, L"Apply [Enter]", Gdiplus::Color(255, 31, 111, 235), Gdiplus::Color(255, 255, 255, 255), Gdiplus::Color(255, 88, 166, 255), m_btn_apply_rect);
    draw_btn(tbX + tbW - 115.0f, btnY, 100.0f, 32.0f, L"Cancel [Esc]", Gdiplus::Color(255, 35, 40, 52), Gdiplus::Color(255, 220, 225, 235), Gdiplus::Color(255, 70, 78, 98), m_btn_cancel_rect);

    // Selection Size & Ratio Floating Badge
    float badgeW = 370.0f;
    float badgeH = 30.0f;
    float badgeX = sx + (sw - badgeW) / 2.0f;
    float badgeY = sy + sh + 10.0f;
    if (badgeY + badgeH > static_cast<float>(height) - 10.0f) badgeY = sy - badgeH - 10.0f;

    Gdiplus::GraphicsPath badgePath;
    AddRoundedRectangle(badgePath, badgeX, badgeY, badgeW, badgeH, 15.0f);

    Gdiplus::SolidBrush badgeBg(Gdiplus::Color(235, 18, 21, 28));
    Gdiplus::Pen badgeBorder(themeCol, 1.0f);
    g.FillPath(&badgeBg, &badgePath);
    g.DrawPath(&badgeBorder, &badgePath);

    std::wostringstream ssBadge;
    ssBadge << static_cast<int>(sw) << L"x" << static_cast<int>(sh) << L"  |  "
            << L"Overlay: " << overlayRatioStr << L"  |  Tablet: " << tabletRatioStr;
    Gdiplus::StringFormat sfBadge;
    sfBadge.SetAlignment(Gdiplus::StringAlignmentCenter);
    sfBadge.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush badgeTxt(themeCol);
    g.DrawString(ssBadge.str().c_str(), -1, &subFont, Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &sfBadge, &badgeTxt);

    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

} // namespace ct0405
