#include "ScreenOverlayWindow.h"
#include <windowsx.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace ct0405 {

constexpr int HANDLE_RADIUS = 7;
constexpr int HANDLE_TOUCH = 14;
constexpr int MIN_SELECTION_W = 80;
constexpr int MIN_SELECTION_H = 60;

static const wchar_t OVERLAY_CLASS_NAME[] = L"CT0405_ScreenOverlay_Class";

// Registered once per process. The old code called CreateSolidBrush on every
// Show(), but RegisterClassExW only succeeds the first time - so every
// subsequent open leaked a GDI brush that nothing owned.
static bool EnsureOverlayClassRegistered(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &ScreenOverlayWindow::OverlayProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 16, 20));
    wc.lpszClassName = OVERLAY_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        // The brush is owned by the class once registration succeeds; on
        // failure it is ours to free.
        DeleteObject(wc.hbrBackground);
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    registered = true;
    return true;
}

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

RECT ScreenOverlayWindow::ClientToScreenRect(const RECT& r) const {
    return RECT{
        r.left + m_virtual_rect.left,
        r.top + m_virtual_rect.top,
        r.right + m_virtual_rect.left,
        r.bottom + m_virtual_rect.top
    };
}

RECT ScreenOverlayWindow::ScreenToClientRect(const RECT& r) const {
    return RECT{
        r.left - m_virtual_rect.left,
        r.top - m_virtual_rect.top,
        r.right - m_virtual_rect.left,
        r.bottom - m_virtual_rect.top
    };
}

void ScreenOverlayWindow::ClampToCanvas(RECT& rc) const {
    const int canvas_w = m_virtual_rect.right - m_virtual_rect.left;
    const int canvas_h = m_virtual_rect.bottom - m_virtual_rect.top;

    int w = std::max(MIN_SELECTION_W, static_cast<int>(rc.right - rc.left));
    int h = std::max(MIN_SELECTION_H, static_cast<int>(rc.bottom - rc.top));
    w = std::min(w, canvas_w);
    h = std::min(h, canvas_h);

    rc.left = std::clamp<LONG>(rc.left, 0, canvas_w - w);
    rc.top = std::clamp<LONG>(rc.top, 0, canvas_h - h);
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
}

bool ScreenOverlayWindow::Show(HINSTANCE hInstance, HWND hParent, const RECT& initial_rect_screen,
                               double tablet_aspect, uint32_t tablet_w, uint32_t tablet_h,
                               bool lock_aspect, ApplyCallback on_apply) {
    if (m_hWnd) {
        SetForegroundWindow(m_hWnd);
        return true;
    }

    m_hParent = hParent;
    m_on_apply = std::move(on_apply);
    m_tablet_aspect = (tablet_aspect > 0.1) ? tablet_aspect : (4.0 / 3.0);
    m_tablet_w = (tablet_w > 100) ? tablet_w : 5040;
    m_tablet_h = (tablet_h > 100) ? tablet_h : 3780;
    m_lock_aspect = lock_aspect;

    if (!EnsureOverlayClassRegistered(hInstance)) return false;

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    m_virtual_rect = { vx, vy, vx + vw, vy + vh };

    const int init_w = initial_rect_screen.right - initial_rect_screen.left;
    const int init_h = initial_rect_screen.bottom - initial_rect_screen.top;

    if (init_w > 100 && init_h > 100) {
        // Convert the caller's screen rectangle into our client space once.
        m_selection_rect = ScreenToClientRect(initial_rect_screen);
    } else {
        int w = std::min(vw, 1280);
        int h = static_cast<int>(w / m_tablet_aspect);
        if (h > vh - 100) {
            h = std::max(MIN_SELECTION_H, vh - 150);
            w = static_cast<int>(h * m_tablet_aspect);
        }
        const int cx = (vw - w) / 2;   // client space: origin is 0,0
        const int cy = (vh - h) / 2;
        m_selection_rect = { cx, cy, cx + w, cy + h };
    }
    ClampToCanvas(m_selection_rect);

    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        OVERLAY_CLASS_NAME,
        L"CT0405 Screen Mapping Overlay",
        WS_POPUP,
        vx, vy, vw, vh,
        hParent,
        nullptr,
        hInstance,
        this
    );

    if (!m_hWnd) return false;

    SetLayeredWindowAttributes(m_hWnd, 0, 230, LWA_ALPHA);

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);

    return true;
}

void ScreenOverlayWindow::Close() {
    if (m_hWnd) {
        HWND hWnd = m_hWnd;
        m_hWnd = nullptr;              // clear first: DestroyWindow re-enters this proc
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        DestroyWindow(hWnd);
    }
    if (m_is_dragging) {
        ReleaseCapture();
        m_is_dragging = false;
        m_active_hit = OverlayHitTest::None;
    }
}

void ScreenOverlayWindow::Commit() {
    // Hand the caller a screen rectangle, converting back exactly once.
    if (m_on_apply) {
        RECT selection = m_selection_rect;
        ClampToCanvas(selection);
        m_on_apply(ClientToScreenRect(selection));
    }
    Close();
}

LRESULT CALLBACK ScreenOverlayWindow::OverlayProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ScreenOverlayWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<ScreenOverlayWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        if (pThis) pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<ScreenOverlayWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (uMsg == WM_NCDESTROY) {
        // Stop any late message from resurrecting a dangling pointer.
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
    }

    if (pThis && pThis->m_hWnd) {
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

    const int l = m_selection_rect.left;
    const int r = m_selection_rect.right;
    const int t = m_selection_rect.top;
    const int b = m_selection_rect.bottom;
    const int mx = (l + r) / 2;
    const int my = (t + b) / 2;

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
    LPCWSTR cursor = IDC_ARROW;
    switch (hit) {
        case OverlayHitTest::Inside:      cursor = IDC_SIZEALL; break;
        case OverlayHitTest::TopLeft:
        case OverlayHitTest::BottomRight: cursor = IDC_SIZENWSE; break;
        case OverlayHitTest::TopRight:
        case OverlayHitTest::BottomLeft:  cursor = IDC_SIZENESW; break;
        case OverlayHitTest::Top:
        case OverlayHitTest::Bottom:      cursor = IDC_SIZENS; break;
        case OverlayHitTest::Left:
        case OverlayHitTest::Right:       cursor = IDC_SIZEWE; break;
        case OverlayHitTest::BtnApply:
        case OverlayHitTest::BtnCancel:
        case OverlayHitTest::BtnMatchRatio:
        case OverlayHitTest::BtnPrimary:  cursor = IDC_HAND; break;
        default:                          cursor = IDC_ARROW; break;
    }
    SetCursor(LoadCursor(nullptr, cursor));
}

void ScreenOverlayWindow::ConstrainToAspectRatio(RECT& rc, OverlayHitTest hit) const {
    if (!m_lock_aspect || m_tablet_aspect <= 0.01) return;

    const int w = std::max(MIN_SELECTION_W, static_cast<int>(rc.right - rc.left));
    const int h = std::max(MIN_SELECTION_H, static_cast<int>(rc.bottom - rc.top));

    // Each case keeps the edge the user is NOT dragging pinned in place, so the
    // grabbed corner stays under the cursor instead of sliding away.
    switch (hit) {
        case OverlayHitTest::BottomRight:
            rc.right = rc.left + w;
            rc.bottom = rc.top + static_cast<int>(w / m_tablet_aspect);
            break;
        case OverlayHitTest::TopRight:
            rc.right = rc.left + w;
            rc.top = rc.bottom - static_cast<int>(w / m_tablet_aspect);
            break;
        case OverlayHitTest::BottomLeft:
            rc.left = rc.right - w;
            rc.bottom = rc.top + static_cast<int>(w / m_tablet_aspect);
            break;
        case OverlayHitTest::TopLeft:
            rc.left = rc.right - w;
            rc.top = rc.bottom - static_cast<int>(w / m_tablet_aspect);
            break;
        case OverlayHitTest::Right:
        case OverlayHitTest::Left:
            rc.bottom = rc.top + static_cast<int>(w / m_tablet_aspect);
            break;
        case OverlayHitTest::Bottom:
        case OverlayHitTest::Top:
            rc.right = rc.left + static_cast<int>(h * m_tablet_aspect);
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
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (!m_is_dragging) {
                UpdateCursor(PerformHitTest(x, y));
            } else {
                const int dx = x - m_drag_start_mouse.x;
                const int dy = y - m_drag_start_mouse.y;

                RECT newRect = m_drag_start_rect;

                if (m_active_hit == OverlayHitTest::Inside) {
                    OffsetRect(&newRect, dx, dy);
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

                    if (newRect.right - newRect.left < MIN_SELECTION_W) newRect.right = newRect.left + MIN_SELECTION_W;
                    if (newRect.bottom - newRect.top < MIN_SELECTION_H) newRect.bottom = newRect.top + MIN_SELECTION_H;

                    // Holding SHIFT resizes freely, ignoring the tablet aspect.
                    const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (!shiftHeld) {
                        ConstrainToAspectRatio(newRect, m_active_hit);
                    }
                }

                ClampToCanvas(newRect);
                m_selection_rect = newRect;
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            const OverlayHitTest hit = PerformHitTest(x, y);
            if (hit == OverlayHitTest::BtnApply) {
                Commit();
                return 0;
            }
            if (hit == OverlayHitTest::BtnCancel) {
                Close();
                return 0;
            }
            if (hit == OverlayHitTest::BtnMatchRatio) {
                const int w = m_selection_rect.right - m_selection_rect.left;
                m_selection_rect.bottom = m_selection_rect.top + static_cast<int>(w / m_tablet_aspect);
                m_lock_aspect = true;
                ClampToCanvas(m_selection_rect);
                InvalidateRect(m_hWnd, nullptr, FALSE);
                return 0;
            }
            if (hit == OverlayHitTest::BtnPrimary) {
                // Primary monitor, expressed in client space.
                RECT primary_screen{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
                RECT primary = ScreenToClientRect(primary_screen);
                int w = primary.right - primary.left;
                int h = static_cast<int>(w / m_tablet_aspect);
                if (h > primary.bottom - primary.top) {
                    h = primary.bottom - primary.top;
                    w = static_cast<int>(h * m_tablet_aspect);
                }
                const int cx = primary.left + ((primary.right - primary.left) - w) / 2;
                const int cy = primary.top + ((primary.bottom - primary.top) - h) / 2;
                m_selection_rect = { cx, cy, cx + w, cy + h };
                m_lock_aspect = true;
                ClampToCanvas(m_selection_rect);
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

        case WM_CAPTURECHANGED:
            m_is_dragging = false;
            m_active_hit = OverlayHitTest::None;
            return 0;

        case WM_LBUTTONDBLCLK: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&m_selection_rect, pt)) {
                Commit();
            }
            return 0;
        }

        case WM_KEYDOWN: {
            const int step = (GetKeyState(VK_CONTROL) & 0x8000) ? 20 : 2;
            switch (wParam) {
                case VK_RETURN:
                case VK_SPACE:
                    Commit();
                    return 0;
                case VK_ESCAPE:
                    Close();
                    return 0;
                case 'M': {
                    const int w = m_selection_rect.right - m_selection_rect.left;
                    m_selection_rect.bottom = m_selection_rect.top + static_cast<int>(w / m_tablet_aspect);
                    m_lock_aspect = true;
                    break;
                }
                case 'P': {
                    RECT primary_screen{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
                    RECT primary = ScreenToClientRect(primary_screen);
                    int w = primary.right - primary.left;
                    int h = static_cast<int>(w / m_tablet_aspect);
                    if (h > primary.bottom - primary.top) {
                        h = primary.bottom - primary.top;
                        w = static_cast<int>(h * m_tablet_aspect);
                    }
                    const int cx = primary.left + ((primary.right - primary.left) - w) / 2;
                    const int cy = primary.top + ((primary.bottom - primary.top) - h) / 2;
                    m_selection_rect = { cx, cy, cx + w, cy + h };
                    m_lock_aspect = true;
                    break;
                }
                case VK_LEFT:  OffsetRect(&m_selection_rect, -step, 0); break;
                case VK_RIGHT: OffsetRect(&m_selection_rect,  step, 0); break;
                case VK_UP:    OffsetRect(&m_selection_rect, 0, -step); break;
                case VK_DOWN:  OffsetRect(&m_selection_rect, 0,  step); break;
                default:
                    return 0;
            }
            ClampToCanvas(m_selection_rect);
            InvalidateRect(m_hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_KEYUP:
            // Only the Shift indicator depends on key-up state.
            if (wParam == VK_SHIFT) {
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hWnd, &ps);
            OnPaint(hdc);
            EndPaint(m_hWnd, &ps);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void ScreenOverlayWindow::OnPaint(HDC hdc) {
    RECT rcClient;
    GetClientRect(m_hWnd, &rcClient);
    const int width = rcClient.right - rcClient.left;
    const int height = rcClient.bottom - rcClient.top;
    if (width <= 0 || height <= 0) return;

    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) return;
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    if (!hbmMem) { DeleteDC(hdcMem); return; }
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

    Gdiplus::Graphics g(hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Dimmed Dark Scrim
    Gdiplus::SolidBrush scrimBrush(Gdiplus::Color(190, 10, 12, 18));
    g.FillRectangle(&scrimBrush, 0, 0, width, height);

    const float sx = static_cast<float>(m_selection_rect.left);
    const float sy = static_cast<float>(m_selection_rect.top);
    const float sw = static_cast<float>(m_selection_rect.right - m_selection_rect.left);
    const float sh = static_cast<float>(m_selection_rect.bottom - m_selection_rect.top);

    const double current_aspect = (sh > 0.0f) ? (static_cast<double>(sw) / static_cast<double>(sh)) : 1.0;
    const bool is_matched = std::abs(current_aspect - m_tablet_aspect) < 0.03;
    const bool shift_held = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    Gdiplus::Color themeCol = is_matched ? Gdiplus::Color(255, 76, 201, 240)
                                         : (shift_held ? Gdiplus::Color(255, 255, 170, 0)
                                                       : Gdiplus::Color(255, 255, 107, 129));
    Gdiplus::SolidBrush fillBrush(Gdiplus::Color(25, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()));
    g.FillRectangle(&fillBrush, sx, sy, sw, sh);

    Gdiplus::Pen glowPen(Gdiplus::Color(90, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()), 4.0f);
    g.DrawRectangle(&glowPen, sx - 1.0f, sy - 1.0f, sw + 2.0f, sh + 2.0f);

    Gdiplus::Pen borderPen(themeCol, 2.0f);
    g.DrawRectangle(&borderPen, sx, sy, sw, sh);

    Gdiplus::Pen gridPen(Gdiplus::Color(50, themeCol.GetR(), themeCol.GetG(), themeCol.GetB()), 1.0f);
    g.DrawLine(&gridPen, sx + sw / 2.0f, sy, sx + sw / 2.0f, sy + sh);
    g.DrawLine(&gridPen, sx, sy + sh / 2.0f, sx + sw, sy + sh / 2.0f);

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
    const float tbW = 860.0f;
    const float tbH = 74.0f;
    const float tbX = (static_cast<float>(width) - tbW) / 2.0f;
    const float tbY = 24.0f;

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
    Gdiplus::SolidBrush cyanBrush(Gdiplus::Color(255, 76, 201, 240));
    Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 6, 214, 160));
    Gdiplus::SolidBrush amberBrush(Gdiplus::Color(255, 255, 170, 0));

    g.DrawString(L"INTERACTIVE TABLET SCREEN MAPPING OVERLAY", -1, &titleFont,
                 Gdiplus::PointF(tbX + 18.0f, tbY + 10.0f), &textBrush);

    const std::wstring overlayRatioStr = FormatAspectRatio(sw, sh);
    const std::wstring tabletRatioStr = FormatAspectRatio(static_cast<double>(m_tablet_w),
                                                          static_cast<double>(m_tablet_h));

    // Plain glyphs only: the emoji variation selector this previously used
    // renders as tofu in GDI+ with Segoe UI.
    std::wostringstream ssInfo;
    ssInfo << L"Overlay: " << static_cast<int>(sw) << L"x" << static_cast<int>(sh) << L" (" << overlayRatioStr << L")  |  "
           << L"Calibrated Tablet: " << m_tablet_w << L"x" << m_tablet_h << L" (" << tabletRatioStr << L")  |  "
           << (is_matched ? L"[matched 1:1]" : (shift_held ? L"[freeform - shift held]" : L"[freeform ratio]"));

    g.DrawString(ssInfo.str().c_str(), -1, &subFont, Gdiplus::PointF(tbX + 18.0f, tbY + 36.0f),
                 is_matched ? &greenBrush : (shift_held ? &amberBrush : &cyanBrush));

    Gdiplus::Font tipFont(L"Segoe UI", 7.5f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 140, 150, 170));
    g.DrawString(L"Tip: Hold SHIFT while dragging handles to resize freely without locking aspect ratio.",
                 -1, &tipFont, Gdiplus::PointF(tbX + 18.0f, tbY + 54.0f), &tipBrush);

    auto draw_btn = [&](float bx, float by, float bw, float bh, const wchar_t* text,
                        Gdiplus::Color bgColor, Gdiplus::Color textColor, Gdiplus::Color borderCol, RECT& out_rect) {
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

        out_rect = { static_cast<LONG>(bx), static_cast<LONG>(by),
                     static_cast<LONG>(bx + bw), static_cast<LONG>(by + bh) };
    };

    const float btnY = tbY + 18.0f;
    draw_btn(tbX + tbW - 365.0f, btnY, 115.0f, 32.0f, L"Match Ratio [M]",
             Gdiplus::Color(255, 35, 40, 55), Gdiplus::Color(255, 76, 201, 240),
             Gdiplus::Color(255, 76, 201, 240), m_btn_ratio_rect);
    draw_btn(tbX + tbW - 240.0f, btnY, 115.0f, 32.0f, L"Apply [Enter]",
             Gdiplus::Color(255, 31, 111, 235), Gdiplus::Color(255, 255, 255, 255),
             Gdiplus::Color(255, 88, 166, 255), m_btn_apply_rect);
    draw_btn(tbX + tbW - 115.0f, btnY, 100.0f, 32.0f, L"Cancel [Esc]",
             Gdiplus::Color(255, 35, 40, 52), Gdiplus::Color(255, 220, 225, 235),
             Gdiplus::Color(255, 70, 78, 98), m_btn_cancel_rect);

    // Selection Size & Ratio Floating Badge
    const float badgeW = 370.0f;
    const float badgeH = 30.0f;
    float badgeX = sx + (sw - badgeW) / 2.0f;
    float badgeY = sy + sh + 10.0f;
    if (badgeY + badgeH > static_cast<float>(height) - 10.0f) badgeY = sy - badgeH - 10.0f;
    badgeX = std::clamp(badgeX, 8.0f, static_cast<float>(width) - badgeW - 8.0f);

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
    g.DrawString(ssBadge.str().c_str(), -1, &subFont,
                 Gdiplus::RectF(badgeX, badgeY, badgeW, badgeH), &sfBadge, &badgeTxt);

    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

} // namespace ct0405
