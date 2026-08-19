#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <functional>
#include <string>

namespace ct0405 {

enum class OverlayHitTest {
    None,
    Inside,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Top,
    Bottom,
    Left,
    Right,
    BtnApply,
    BtnCancel,
    BtnMatchRatio,
    BtnPrimary
};

class ScreenOverlayWindow {
public:
    using ApplyCallback = std::function<void(const RECT& selected_rect)>;

    ScreenOverlayWindow();
    ~ScreenOverlayWindow();

    bool Show(HINSTANCE hInstance, HWND hParent, const RECT& initial_rect, double tablet_aspect, uint32_t tablet_w, uint32_t tablet_h, ApplyCallback on_apply);
    void Close();

private:
    static LRESULT CALLBACK OverlayProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hdc);
    OverlayHitTest PerformHitTest(int x, int y);
    void UpdateCursor(OverlayHitTest hit);
    void ConstrainToAspectRatio(RECT& rc, OverlayHitTest hit);

    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;
    ApplyCallback m_on_apply;

    RECT m_virtual_rect{ 0, 0, 1920, 1080 };
    RECT m_selection_rect{ 200, 200, 1200, 950 };
    double m_tablet_aspect = 4.0 / 3.0;
    uint32_t m_tablet_w = 5040;
    uint32_t m_tablet_h = 3780;
    bool m_lock_aspect = true;

    // Mouse Interaction
    bool m_is_dragging = false;
    OverlayHitTest m_active_hit = OverlayHitTest::None;
    POINT m_drag_start_mouse{ 0, 0 };
    RECT m_drag_start_rect{ 0, 0, 0, 0 };

    // Button rects in screen coords
    RECT m_btn_apply_rect{ 0, 0, 0, 0 };
    RECT m_btn_cancel_rect{ 0, 0, 0, 0 };
    RECT m_btn_ratio_rect{ 0, 0, 0, 0 };
    RECT m_btn_primary_rect{ 0, 0, 0, 0 };
};

} // namespace ct0405
