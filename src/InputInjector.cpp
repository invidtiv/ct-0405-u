#include "InputInjector.h"
#include <algorithm>

namespace ct0405 {

namespace {

// Resolves what the tip should do for a given configuration.
struct TipBehaviour {
    bool pen_contact = true;    // drive the synthetic pen's contact state
    uint32_t mouse_bit = 0;     // synthetic mouse button to hold instead
};

TipBehaviour ResolveTipBehaviour(ButtonAction action) {
    switch (action) {
        case ButtonAction::Default:
        case ButtonAction::LeftClick:
            return { true, 0 };
        case ButtonAction::RightClick:
            return { false, 1u << 1 };
        case ButtonAction::MiddleClick:
        case ButtonAction::PanScroll:
            return { false, 1u << 2 };
        case ButtonAction::Disabled:
            return { false, 0 };
        default:
            return { true, 0 };
    }
}

} // namespace

POINT InputInjector::ToInjectionSpace(int32_t screen_x, int32_t screen_y) {
    // Measured on hardware: with a virtual desktop origin of (0,-541), a
    // pointer injected at screen y=540 landed at y=-1 - offset by exactly the
    // origin. On a single-monitor desktop the origin is (0,0), so absolute and
    // virtual-relative coordinates coincide and the bug is invisible; it only
    // appears once a display sits above or to the left of the primary.
    POINT pt{
        screen_x - GetSystemMetrics(SM_XVIRTUALSCREEN),
        screen_y - GetSystemMetrics(SM_YVIRTUALSCREEN)
    };
    return pt;
}

InputInjector::InputInjector() {
    // Dynamically load user32.dll synthetic pointer functions
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
    }

    if (hUser32) {
        m_pfnCreateDevice = reinterpret_cast<PFN_CreateSyntheticPointerDevice>(
            reinterpret_cast<void*>(GetProcAddress(hUser32, "CreateSyntheticPointerDevice")));
        m_pfnInjectInput = reinterpret_cast<PFN_InjectSyntheticPointerInput>(
            reinterpret_cast<void*>(GetProcAddress(hUser32, "InjectSyntheticPointerInput")));
        m_pfnDestroyDevice = reinterpret_cast<PFN_DestroySyntheticPointerDevice>(
            reinterpret_cast<void*>(GetProcAddress(hUser32, "DestroySyntheticPointerDevice")));
    }
}

InputInjector::~InputInjector() {
    Shutdown();
}

bool InputInjector::IsWindowsInkActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_use_ink && m_synthetic_device != nullptr;
}

bool InputInjector::Initialize(bool use_windows_ink) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_use_ink != use_windows_ink) {
        // Mode is changing: drop anything currently held before switching.
        ReleaseAllLocked();
    }
    m_use_ink = use_windows_ink;

    if (!use_windows_ink) {
        // Mouse emulation needs no device. Reporting failure here (as the old
        // code did) made a perfectly working mode look broken to callers.
        if (m_synthetic_device && m_pfnDestroyDevice) {
            m_pfnDestroyDevice(m_synthetic_device);
            m_synthetic_device = nullptr;
        }
        return true;
    }

    if (m_synthetic_device) {
        return true;
    }

    if (!m_pfnCreateDevice) {
        return false;
    }

    m_synthetic_device = m_pfnCreateDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
    return m_synthetic_device != nullptr;
}

void InputInjector::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseAllLocked();

    // Destroying the device under the same lock Inject() takes is the whole
    // point: previously a config change on the UI thread could free this handle
    // between the HID thread's null check and its call into user32.
    if (m_synthetic_device && m_pfnDestroyDevice) {
        m_pfnDestroyDevice(m_synthetic_device);
        m_synthetic_device = nullptr;
    }
}

void InputInjector::SetHeldButton(uint32_t bit, bool down) {
    const bool currently_held = (m_held_buttons & bit) != 0;
    if (currently_held == down) return;

    DWORD flag = 0;
    switch (bit) {
        case HB_LEFT:   flag = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
        case HB_RIGHT:  flag = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
        case HB_MIDDLE: flag = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        default: return;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    SendInput(1, &input, sizeof(INPUT));

    if (down) m_held_buttons |= bit;
    else      m_held_buttons &= ~bit;
}

void InputInjector::SendKeyChord(WORD modifier, WORD key) {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = modifier;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = key;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = key;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = modifier;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void InputInjector::ApplyButtonAction(ButtonAction action, bool pressed, bool prev_pressed,
                                      bool& out_barrel_flag, bool& out_eraser_flag) {
    const bool rising = pressed && !prev_pressed;

    switch (action) {
        case ButtonAction::Disabled:
            break;

        case ButtonAction::Default:
            // Native pen behaviour: report the barrel switch to the ink stack.
            if (pressed) out_barrel_flag = true;
            break;

        case ButtonAction::EraserToggle:
            if (pressed) out_eraser_flag = true;
            break;

        case ButtonAction::LeftClick:
            SetHeldButton(HB_LEFT, pressed);
            break;

        case ButtonAction::RightClick:
            SetHeldButton(HB_RIGHT, pressed);
            break;

        case ButtonAction::MiddleClick:
        case ButtonAction::PanScroll:
            // PanScroll is a held middle button - the drag-to-pan gesture most
            // canvas applications implement.
            SetHeldButton(HB_MIDDLE, pressed);
            break;

        case ButtonAction::Undo:
            if (rising) SendKeyChord(VK_CONTROL, 'Z');
            break;

        case ButtonAction::Redo:
            if (rising) SendKeyChord(VK_CONTROL, 'Y');
            break;
    }
}

void InputInjector::Inject(const TabletProcessedState& state, const DriverConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!state.in_proximity) {
        ReleaseAllLocked();
        return;
    }

    // Resolve what each physical input should do under this configuration.
    const TipBehaviour tip = ResolveTipBehaviour(config.tip_action);

    bool barrel_flag = false;
    bool eraser_flag = state.eraser_active;   // physical eraser end of the stylus

    ApplyButtonAction(config.barrel_1_action, state.barrel_button_1, m_prev_barrel_1,
                      barrel_flag, eraser_flag);
    ApplyButtonAction(config.barrel_2_action, state.barrel_button_2, m_prev_barrel_2,
                      barrel_flag, eraser_flag);

    // A tip action other than left-click drives a synthetic mouse button
    // instead of the pen's contact state.
    if (tip.mouse_bit != 0) {
        SetHeldButton(tip.mouse_bit, state.is_contact);
    }

    const bool pen_contact = tip.pen_contact && state.is_contact;

    if (m_use_ink && m_synthetic_device && m_pfnInjectInput) {
        InjectWindowsInkLocked(state, config, pen_contact, barrel_flag, eraser_flag);
    } else {
        InjectMouseLocked(state, tip.pen_contact && state.is_contact);
    }

    m_prev_in_proximity = state.in_proximity;
    m_prev_pen_contact = pen_contact;
    m_prev_barrel_1 = state.barrel_button_1;
    m_prev_barrel_2 = state.barrel_button_2;
    m_last_x = state.screen_x;
    m_last_y = state.screen_y;
}

void InputInjector::InjectWindowsInkLocked(const TabletProcessedState& state, const DriverConfig& /*config*/,
                                           bool pen_contact, bool barrel_flag, bool eraser_flag) {
    POINTER_TYPE_INFO pointerInfo{};
    pointerInfo.type = PT_PEN;

    POINTER_INFO& pi = pointerInfo.penInfo.pointerInfo;
    pi.pointerType = PT_PEN;
    pi.pointerId = 0;
    pi.ptPixelLocation = ToInjectionSpace(state.screen_x, state.screen_y);
    pi.ptPixelLocationRaw = pi.ptPixelLocation;
    pi.historyCount = 1;

    POINTER_FLAGS flags = POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_INRANGE;

    if (pen_contact) {
        flags |= POINTER_FLAG_INCONTACT;
        flags |= m_prev_pen_contact ? POINTER_FLAG_UPDATE : POINTER_FLAG_DOWN;
    } else {
        flags |= m_prev_pen_contact ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE;
    }

    pi.pointerFlags = flags;

    PEN_FLAGS penFlags = PEN_FLAG_NONE;
    if (barrel_flag) penFlags |= PEN_FLAG_BARREL;
    if (eraser_flag) penFlags |= PEN_FLAG_ERASER | PEN_FLAG_INVERTED;

    pointerInfo.penInfo.penFlags = penFlags;
    pointerInfo.penInfo.penMask = PEN_MASK_PRESSURE;
    pointerInfo.penInfo.pressure = std::clamp(state.injection_pressure, 0u, 1024u);
    pointerInfo.penInfo.rotation = 0;
    pointerInfo.penInfo.tiltX = 0;
    pointerInfo.penInfo.tiltY = 0;

    m_pfnInjectInput(m_synthetic_device, &pointerInfo, 1);
}

void InputInjector::InjectMouseLocked(const TabletProcessedState& state, bool left_contact) {
    const int v_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int v_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int v_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int v_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (v_width <= 0) v_width = 1920;
    if (v_height <= 0) v_height = 1080;

    // MOUSEEVENTF_ABSOLUTE maps 0..65535 across (extent - 1). Dividing by the
    // full extent left the last row and column unreachable.
    const double span_x = static_cast<double>(std::max(1, v_width - 1));
    const double span_y = static_cast<double>(std::max(1, v_height - 1));

    const double norm_x = static_cast<double>(state.screen_x - v_left) / span_x;
    const double norm_y = static_cast<double>(state.screen_y - v_top) / span_y;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(std::clamp(norm_x * 65535.0, 0.0, 65535.0));
    input.mi.dy = static_cast<LONG>(std::clamp(norm_y * 65535.0, 0.0, 65535.0));
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));

    // Buttons go through the same held-button bookkeeping as barrel actions, so
    // ReleaseAll always knows exactly what is down. The old code emitted a
    // right-button event for barrel 1 unconditionally, ignoring the configured
    // action entirely.
    SetHeldButton(HB_LEFT, left_contact);
}

void InputInjector::ReleaseAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ReleaseAllLocked();
}

void InputInjector::ReleaseAllLocked() {
    // Lift the synthetic pen if it was in contact or in range.
    if ((m_prev_in_proximity || m_prev_pen_contact) &&
        m_use_ink && m_synthetic_device && m_pfnInjectInput) {
        POINTER_TYPE_INFO pointerInfo{};
        pointerInfo.type = PT_PEN;
        POINTER_INFO& pi = pointerInfo.penInfo.pointerInfo;
        pi.pointerType = PT_PEN;
        pi.pointerId = 0;
        pi.ptPixelLocation = ToInjectionSpace(m_last_x, m_last_y);
        pi.ptPixelLocationRaw = pi.ptPixelLocation;
        pi.historyCount = 1;
        pi.pointerFlags = POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE |
                          (m_prev_pen_contact ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE);
        pointerInfo.penInfo.penMask = PEN_MASK_PRESSURE;
        pointerInfo.penInfo.pressure = 0;
        m_pfnInjectInput(m_synthetic_device, &pointerInfo, 1);
    }

    // Release exactly the buttons we are holding - no more, no less.
    SetHeldButton(HB_LEFT, false);
    SetHeldButton(HB_RIGHT, false);
    SetHeldButton(HB_MIDDLE, false);

    m_prev_in_proximity = false;
    m_prev_pen_contact = false;
    m_prev_barrel_1 = false;
    m_prev_barrel_2 = false;
}

} // namespace ct0405
