#include "InputInjector.h"
#include <algorithm>

namespace ct0405 {

InputInjector::InputInjector() {
    // Dynamically load user32.dll synthetic pointer functions
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
    }

    if (hUser32) {
        m_pfnCreateDevice = reinterpret_cast<PFN_CreateSyntheticPointerDevice>(
            GetProcAddress(hUser32, "CreateSyntheticPointerDevice"));
        m_pfnInjectInput = reinterpret_cast<PFN_InjectSyntheticPointerInput>(
            GetProcAddress(hUser32, "InjectSyntheticPointerInput"));
        m_pfnDestroyDevice = reinterpret_cast<PFN_DestroySyntheticPointerDevice>(
            GetProcAddress(hUser32, "DestroySyntheticPointerDevice"));
    }
}

InputInjector::~InputInjector() {
    Shutdown();
}

bool InputInjector::Initialize(bool use_windows_ink) {
    m_use_ink = use_windows_ink;
    if (m_use_ink && m_pfnCreateDevice && !m_synthetic_device) {
        // Create synthetic pen device with max 1 pointer and POINTER_FEEDBACK_DEFAULT
        m_synthetic_device = m_pfnCreateDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
        if (m_synthetic_device) {
            m_ink_initialized = true;
            return true;
        }
    }
    return false;
}

void InputInjector::Shutdown() {
    ReleaseAll();
    if (m_synthetic_device && m_pfnDestroyDevice) {
        m_pfnDestroyDevice(m_synthetic_device);
        m_synthetic_device = nullptr;
    }
    m_ink_initialized = false;
}

void InputInjector::UpdateConfig(const DriverConfig& config) {
    m_config = config;
    if (config.use_windows_ink != m_use_ink) {
        Shutdown();
        Initialize(config.use_windows_ink);
    }
}

void InputInjector::Inject(const TabletProcessedState& state) {
    if (!state.in_proximity) {
        ReleaseAll();
        return;
    }

    HandleButtonShortcuts(state);

    if (m_use_ink && m_synthetic_device && m_pfnInjectInput) {
        InjectWindowsInk(state);
    } else {
        InjectMouse(state);
    }

    m_prev_in_proximity = state.in_proximity;
    m_prev_in_contact = state.is_contact;
    m_prev_barrel_1 = state.barrel_button_1;
    m_prev_barrel_2 = state.barrel_button_2;
    m_prev_eraser = state.eraser_active;
    m_last_x = state.screen_x;
    m_last_y = state.screen_y;
}

void InputInjector::InjectWindowsInk(const TabletProcessedState& state) {
    POINTER_TYPE_INFO pointerInfo{};
    pointerInfo.type = PT_PEN;

    POINTER_INFO& pi = pointerInfo.penInfo.pointerInfo;
    pi.pointerType = PT_PEN;
    pi.pointerId = 0;
    pi.ptPixelLocation.x = state.screen_x;
    pi.ptPixelLocation.y = state.screen_y;
    pi.ptPixelLocationRaw = pi.ptPixelLocation;
    pi.historyCount = 1;

    // Compute pointer flags
    POINTER_FLAGS flags = POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE;

    if (state.in_proximity) {
        flags |= POINTER_FLAG_INRANGE;
    }

    if (state.is_contact) {
        flags |= POINTER_FLAG_INCONTACT;
        if (!m_prev_in_contact) {
            flags |= POINTER_FLAG_DOWN;
        } else {
            flags |= POINTER_FLAG_UPDATE;
        }
    } else {
        if (m_prev_in_contact) {
            flags |= POINTER_FLAG_UP;
        } else {
            flags |= POINTER_FLAG_UPDATE;
        }
    }

    pi.pointerFlags = flags;

    // Pen specific info
    PEN_FLAGS penFlags = PEN_FLAG_NONE;
    if (state.barrel_button_1 || (m_config.barrel_1_action == ButtonAction::RightClick && state.barrel_button_1)) {
        penFlags |= PEN_FLAG_BARREL;
    }
    if (state.eraser_active || (m_config.barrel_2_action == ButtonAction::EraserToggle && state.barrel_button_2)) {
        penFlags |= PEN_FLAG_ERASER | PEN_FLAG_INVERTED;
    }

    pointerInfo.penInfo.penFlags = penFlags;
    pointerInfo.penInfo.penMask = PEN_MASK_PRESSURE;
    pointerInfo.penInfo.pressure = std::clamp(state.injection_pressure, 0u, 1024u);
    pointerInfo.penInfo.rotation = 0;
    pointerInfo.penInfo.tiltX = 0;
    pointerInfo.penInfo.tiltY = 0;

    m_pfnInjectInput(m_synthetic_device, &pointerInfo, 1);
}

void InputInjector::InjectMouse(const TabletProcessedState& state) {
    int v_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int v_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int v_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int v_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (v_width <= 0) v_width = 1920;
    if (v_height <= 0) v_height = 1080;

    double norm_x = static_cast<double>(state.screen_x - v_left) / static_cast<double>(v_width);
    double norm_y = static_cast<double>(state.screen_y - v_top) / static_cast<double>(v_height);

    DWORD abs_x = static_cast<DWORD>(std::clamp(norm_x * 65535.0, 0.0, 65535.0));
    DWORD abs_y = static_cast<DWORD>(std::clamp(norm_y * 65535.0, 0.0, 65535.0));

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = abs_x;
    input.mi.dy = abs_y;
    input.mi.mouseData = 0;
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;

    if (state.is_contact && !m_prev_in_contact) {
        input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    } else if (!state.is_contact && m_prev_in_contact) {
        input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
    }

    if (state.barrel_button_1 && !m_prev_barrel_1) {
        input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
    } else if (!state.barrel_button_1 && m_prev_barrel_1) {
        input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void InputInjector::HandleButtonShortcuts(const TabletProcessedState& state) {
    // Custom button shortcuts (Undo: Ctrl+Z, Redo: Ctrl+Y)
    auto trigger_shortcut = [](ButtonAction action, bool pressed, bool prev_pressed) {
        if (pressed && !prev_pressed) {
            if (action == ButtonAction::Undo) {
                INPUT inputs[4]{};
                // Ctrl Down
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_CONTROL;
                // Z Down
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = 'Z';
                // Z Up
                inputs[2].type = INPUT_KEYBOARD;
                inputs[2].ki.wVk = 'Z';
                inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
                // Ctrl Up
                inputs[3].type = INPUT_KEYBOARD;
                inputs[3].ki.wVk = VK_CONTROL;
                inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(4, inputs, sizeof(INPUT));
            } else if (action == ButtonAction::Redo) {
                INPUT inputs[4]{};
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_CONTROL;
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = 'Y';
                inputs[2].type = INPUT_KEYBOARD;
                inputs[2].ki.wVk = 'Y';
                inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
                inputs[3].type = INPUT_KEYBOARD;
                inputs[3].ki.wVk = VK_CONTROL;
                inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(4, inputs, sizeof(INPUT));
            }
        }
    };

    if (m_config.barrel_1_action == ButtonAction::Undo || m_config.barrel_1_action == ButtonAction::Redo) {
        trigger_shortcut(m_config.barrel_1_action, state.barrel_button_1, m_prev_barrel_1);
    }
    if (m_config.barrel_2_action == ButtonAction::Undo || m_config.barrel_2_action == ButtonAction::Redo) {
        trigger_shortcut(m_config.barrel_2_action, state.barrel_button_2, m_prev_barrel_2);
    }
}

void InputInjector::ReleaseAll() {
    if (m_prev_in_proximity || m_prev_in_contact) {
        if (m_use_ink && m_synthetic_device && m_pfnInjectInput) {
            POINTER_TYPE_INFO pointerInfo{};
            pointerInfo.type = PT_PEN;
            POINTER_INFO& pi = pointerInfo.penInfo.pointerInfo;
            pi.pointerType = PT_PEN;
            pi.pointerId = 0;
            pi.ptPixelLocation.x = m_last_x;
            pi.ptPixelLocation.y = m_last_y;
            pi.pointerFlags = POINTER_FLAG_PRIMARY | POINTER_FLAG_UP;
            pointerInfo.penInfo.penMask = PEN_MASK_PRESSURE;
            pointerInfo.penInfo.pressure = 0;
            m_pfnInjectInput(m_synthetic_device, &pointerInfo, 1);
        } else {
            INPUT input{};
            input.type = INPUT_MOUSE;
            if (m_prev_in_contact) {
                input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            }
            if (m_prev_barrel_1) {
                input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
            }
            if (input.mi.dwFlags != 0) {
                SendInput(1, &input, sizeof(INPUT));
            }
        }
    }

    m_prev_in_proximity = false;
    m_prev_in_contact = false;
    m_prev_barrel_1 = false;
    m_prev_barrel_2 = false;
    m_prev_eraser = false;
}

} // namespace ct0405
