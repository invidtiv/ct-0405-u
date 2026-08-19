#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <functional>

namespace ct0405 {

// Known Wacom USB Vendor ID and Product IDs
constexpr uint16_t WACOM_VENDOR_ID = 0x056A;

// Supported Product IDs
constexpr uint16_t PID_PENPARTNER_CT0405U = 0x0000; // CT-0405-U USB PenPartner
constexpr uint16_t PID_PENPARTNER_2       = 0x0003; // PenPartner revision
constexpr uint16_t PID_GRAPHIRE_1         = 0x0010; // Graphire 1 (4x5)
constexpr uint16_t PID_GRAPHIRE_2_ET0405U = 0x0011; // Graphire 2 (4x5)
constexpr uint16_t PID_GRAPHIRE_3_4X5     = 0x0013; // Graphire 3 (4x5)
constexpr uint16_t PID_GRAPHIRE_3_6X8     = 0x0014; // Graphire 3 (6x8)
constexpr uint16_t PID_GRAPHIRE_4_4X5     = 0x0015; // Graphire 4 (4x5)
constexpr uint16_t PID_GRAPHIRE_4_6X8     = 0x0016; // Graphire 4 (6x8)
constexpr uint16_t PID_VOLITO             = 0x0060; // Volito
constexpr uint16_t PID_VOLITO_2           = 0x0061; // Volito 2

enum class DeviceModel {
    Unknown,
    PenPartner_CT0405U,
    Graphire1,
    Graphire2,
    Graphire3,
    Graphire4,
    Volito,
    Volito2,
    GenericWacom
};

enum class ToolType {
    None,
    Pen,       // Stylus tip
    Eraser,    // Stylus rubber/eraser end
    Mouse      // Cordless tablet mouse
};

struct TabletCapabilities {
    uint32_t max_x = 5040;
    uint32_t max_y = 3780;
    uint32_t max_pressure = 255;
    uint32_t resolution_lpi = 1016;
    bool has_eraser = true;
    bool has_barrel_switch = true;
    bool has_second_barrel_switch = true;
};

// Raw tablet state parsed from a packet
struct TabletRawState {
    bool in_proximity = false;
    ToolType tool = ToolType::None;
    uint32_t raw_x = 0;
    uint32_t raw_y = 0;
    int32_t raw_pressure = 0;
    bool tip_switch = false;
    bool barrel_switch_1 = false;
    bool barrel_switch_2 = false;
    bool eraser_switch = false;
    uint64_t timestamp_us = 0;
    std::string raw_hex = "";
};

// Processed state ready for injection & UI visualization
struct TabletProcessedState {
    bool in_proximity = false;
    ToolType tool = ToolType::None;
    
    // Normalized 0.0 - 1.0 coordinates in tablet space
    double normalized_x = 0.0;
    double normalized_y = 0.0;
    
    // Screen coordinates
    int32_t screen_x = 0;
    int32_t screen_y = 0;
    
    // Normalized pressure 0.0 - 1.0 (after curve & deadzone)
    double pressure = 0.0;
    
    // Scaled pressure for injection (e.g. 0 - 1024 / 4096)
    uint32_t injection_pressure = 0;
    
    bool is_contact = false;
    bool barrel_button_1 = false;
    bool barrel_button_2 = false;
    bool eraser_active = false;
    
    uint64_t timestamp_us = 0;
};

struct MonitorInfo {
    int index = 0;
    std::wstring name;
    std::wstring device_id;
    RECT rect{ 0, 0, 0, 0 };
    bool is_primary = false;
};

enum class MappingMode {
    AllMonitors,      // Virtual Desktop
    PrimaryMonitor,   // Main monitor only
    SpecificMonitor,  // Specific monitor by index
    CustomArea        // Custom screen rectangle
};

enum class PressureCurveType {
    Linear,
    Soft,
    VerySoft,
    Firm,
    Hard,
    CustomBezier
};

enum class ButtonAction {
    Default,          // Native Pen Action
    LeftClick,
    RightClick,
    MiddleClick,
    EraserToggle,
    Undo,             // Ctrl + Z
    Redo,             // Ctrl + Y
    PanScroll,        // Spacebar / Scroll
    Disabled
};

struct DriverConfig {
    // Custom Tablet Space Bounds
    uint32_t tablet_max_x = 5040;
    uint32_t tablet_max_y = 3780;
    uint32_t tablet_max_pressure = 255;
    bool auto_detect_bounds = true;

    // Tablet Active Area (normalized 0.0 - 1.0)
    double tablet_area_left = 0.0;
    double tablet_area_top = 0.0;
    double tablet_area_right = 1.0;
    double tablet_area_bottom = 1.0;

    // Screen Mapping settings
    MappingMode mapping_mode = MappingMode::PrimaryMonitor;
    int target_monitor_index = 0;
    RECT custom_screen_rect{ 0, 0, 1920, 1080 };
    bool lock_aspect_ratio = true;
    
    // Pressure Settings
    PressureCurveType curve_type = PressureCurveType::Linear;
    double pressure_min_threshold = 0.02; // Deadzone threshold
    double pressure_max_threshold = 0.98; // Max clamp threshold
    double custom_bezier_p1 = 0.25;
    double custom_bezier_p2 = 0.75;
    
    // Smoothing / Filter Settings (1-Euro Filter)
    bool enable_smoothing = true;
    double filter_min_cutoff = 1.2;      // Hz - min cutoff frequency
    double filter_beta = 0.005;          // Speed coefficient
    double filter_d_cutoff = 1.0;        // Hz - cutoff for derivative
    
    // Button Actions
    ButtonAction tip_action = ButtonAction::LeftClick;
    ButtonAction barrel_1_action = ButtonAction::RightClick;
    ButtonAction barrel_2_action = ButtonAction::EraserToggle;
    
    // Injection Mode
    bool use_windows_ink = true;         // True: Windows Synthetic Pen Pointer, False: Mouse Emulation
    
    // Application Settings
    bool start_with_windows = false;
    bool minimize_to_tray = true;
    bool start_minimized = false;
    
    // Raw Debugging
    bool log_raw_packets = false;
};

// Callback types for events
using StateCallback = std::function<void(const TabletRawState& raw, const TabletProcessedState& processed)>;
using ConnectionCallback = std::function<void(bool connected, const std::wstring& device_name)>;

inline uint64_t GetCurrentTimestampUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace ct0405
