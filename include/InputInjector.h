#pragma once

#include "Common.h"
#include <windows.h>
#include <mutex>
#include <cstdint>

namespace ct0405 {

// Function pointer signatures for Windows Synthetic Pointer API
typedef HSYNTHETICPOINTERDEVICE(WINAPI* PFN_CreateSyntheticPointerDevice)(
    POINTER_INPUT_TYPE pointerType,
    ULONG maxCount,
    POINTER_FEEDBACK_MODE mode
);

typedef BOOL(WINAPI* PFN_InjectSyntheticPointerInput)(
    HSYNTHETICPOINTERDEVICE device,
    const POINTER_TYPE_INFO* pointerInfo,
    UINT32 count
);

typedef VOID(WINAPI* PFN_DestroySyntheticPointerDevice)(
    HSYNTHETICPOINTERDEVICE device
);

class InputInjector {
public:
    InputInjector();
    ~InputInjector();

    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // Returns true when the requested mode is ready to inject. Mouse-emulation
    // mode needs no device, so it always succeeds.
    bool Initialize(bool use_windows_ink = true);
    void Shutdown();

    // Inject state into Windows using the supplied configuration snapshot.
    void Inject(const TabletProcessedState& state, const DriverConfig& config);

    // Release all held buttons / contacts on proximity loss or shutdown.
    void ReleaseAll();

    bool IsWindowsInkActive() const;
    bool IsWindowsInkSupported() const { return m_pfnCreateDevice != nullptr; }

private:
    // Which synthetic mouse buttons are currently held down by us.
    enum HeldButton : uint32_t {
        HB_NONE   = 0,
        HB_LEFT   = 1u << 0,
        HB_RIGHT  = 1u << 1,
        HB_MIDDLE = 1u << 2
    };

    void InjectWindowsInkLocked(const TabletProcessedState& state, const DriverConfig& config,
                                bool pen_contact, bool barrel_flag, bool eraser_flag);
    void InjectMouseLocked(const TabletProcessedState& state, bool left_contact);
    void ReleaseAllLocked();

    // Applies an edge-triggered button action. `pressed`/`prev_pressed` describe
    // the physical button; returns the pen flags the action contributes.
    void ApplyButtonAction(ButtonAction action, bool pressed, bool prev_pressed,
                           bool& out_barrel_flag, bool& out_eraser_flag);

    void SetHeldButton(uint32_t bit, bool down);
    void SendKeyChord(WORD modifier, WORD key);

    // InjectSyntheticPointerInput places the pointer relative to the virtual
    // desktop origin rather than in absolute screen coordinates. Converts one
    // to the other.
    static POINT ToInjectionSpace(int32_t screen_x, int32_t screen_y);

    mutable std::mutex m_mutex;   // guards the synthetic device and all prev-state

    HSYNTHETICPOINTERDEVICE m_synthetic_device = nullptr;
    bool m_use_ink = true;

    // Pointer API function pointers (resolved once in the constructor, const thereafter)
    PFN_CreateSyntheticPointerDevice m_pfnCreateDevice = nullptr;
    PFN_InjectSyntheticPointerInput m_pfnInjectInput = nullptr;
    PFN_DestroySyntheticPointerDevice m_pfnDestroyDevice = nullptr;

    // Previous state tracking
    bool m_prev_in_proximity = false;
    bool m_prev_pen_contact = false;
    bool m_prev_barrel_1 = false;
    bool m_prev_barrel_2 = false;
    uint32_t m_held_buttons = HB_NONE;
    int32_t m_last_x = 0;
    int32_t m_last_y = 0;
};

} // namespace ct0405
