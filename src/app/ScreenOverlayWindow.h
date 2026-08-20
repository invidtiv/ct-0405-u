#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <functional>
#include <string>
#include <cstdint>

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

// Full-virtual-desktop overlay for picking the screen rectangle the tablet maps
// onto.
//
// Coordinate spaces: the caller works in SCREEN coordinates (that is what gets
// stored in the config and handed to the mapper). Internally everything -
// painting, hit testing, mouse input - is in CLIENT coordinates. The two differ
// by the virtual-desktop origin, which is non-zero whenever a monitor sits
// above or to the left of the primary. Conversion happens exactly twice: on
// entry in Show(), and on exit before the apply callback.
class ScreenOverlayWindow {
public:
    using ApplyCallback = std::function<void(const RECT& selected_rect_screen)>;

    ScreenOverlayWindow();
    ~ScreenOverlayWindow();

    ScreenOverlayWindow(const ScreenOverlayWindow&) = delete;
    ScreenOverlayWindow& operator=(const ScreenOverlayWindow&) = delete;

    bool Show(HINSTANCE hInstance, HWND hParent, const RECT& initial_rect_screen,
              double tablet_aspect, uint32_t tablet_w, uint32_t tablet_h,
              bool lock_aspect, ApplyCallback on_apply);
    void Close();

    bool IsOpen() const { return m_hWnd != nullptr; }

    // Window procedure for the registered class. Public only because the class
    // registration helper needs to name it.
    static LRESULT CALLBACK OverlayProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hdc);
    OverlayHitTest PerformHitTest(int x, int y);
    void UpdateCursor(OverlayHitTest hit);
    void ConstrainToAspectRatio(RECT& rc, OverlayHitTest hit) const;
    void ClampToCanvas(RECT& rc) const;
    void Commit();

    RECT ClientToScreenRect(const RECT& client_rect) const;
    RECT ScreenToClientRect(const RECT& screen_rect) const;

    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;
    ApplyCallback m_on_apply;

    RECT m_virtual_rect{ 0, 0, 1920, 1080 };          // screen coords
    RECT m_selection_rect{ 200, 200, 1200, 950 };     // CLIENT coords
    double m_tablet_aspect = 4.0 / 3.0;
    uint32_t m_tablet_w = 5040;
    uint32_t m_tablet_h = 3780;
    bool m_lock_aspect = true;

    // Mouse Interaction
    bool m_is_dragging = false;
    OverlayHitTest m_active_hit = OverlayHitTest::None;
    POINT m_drag_start_mouse{ 0, 0 };
    RECT m_drag_start_rect{ 0, 0, 0, 0 };

    // Button rects, in CLIENT coords, refreshed each paint
    RECT m_btn_apply_rect{ 0, 0, 0, 0 };
    RECT m_btn_cancel_rect{ 0, 0, 0, 0 };
    RECT m_btn_ratio_rect{ 0, 0, 0, 0 };
    RECT m_btn_primary_rect{ 0, 0, 0, 0 };
};

} // namespace ct0405
