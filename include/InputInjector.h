#pragma once

#include "Common.h"
#include <windows.h>

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

    bool Initialize(bool use_windows_ink = true);
    void Shutdown();

    void UpdateConfig(const DriverConfig& config);

    // Inject state into Windows
    void Inject(const TabletProcessedState& state);

    // Release all held buttons / contacts on proximity loss
    void ReleaseAll();

    bool IsWindowsInkSupported() const { return m_synthetic_device != nullptr; }

private:
    void InjectWindowsInk(const TabletProcessedState& state);
    void InjectMouse(const TabletProcessedState& state);
    void HandleButtonShortcuts(const TabletProcessedState& state);

    DriverConfig m_config;
    HSYNTHETICPOINTERDEVICE m_synthetic_device = nullptr;
    bool m_ink_initialized = false;
    bool m_use_ink = true;

    // Pointer API function pointers
    PFN_CreateSyntheticPointerDevice m_pfnCreateDevice = nullptr;
    PFN_InjectSyntheticPointerInput m_pfnInjectInput = nullptr;
    PFN_DestroySyntheticPointerDevice m_pfnDestroyDevice = nullptr;

    // Previous state tracking
    bool m_prev_in_proximity = false;
    bool m_prev_in_contact = false;
    bool m_prev_barrel_1 = false;
    bool m_prev_barrel_2 = false;
    bool m_prev_eraser = false;
    int32_t m_last_x = 0;
    int32_t m_last_y = 0;
};

} // namespace ct0405
