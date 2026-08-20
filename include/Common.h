#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>

namespace ct0405 {

// Known Wacom USB Vendor ID
constexpr uint16_t WACOM_VENDOR_ID = 0x056A;

// The tablets this driver actually drives. Both are PenPartner units speaking
// the same report protocol, verified against hardware.
constexpr uint16_t PID_PENPARTNER_CT0405U = 0x0000; // CT-0405-U USB PenPartner
constexpr uint16_t PID_PENPARTNER_2       = 0x0003; // PenPartner revision

// Other Wacom tablets of the same era. This driver does NOT decode them - their
// report layouts are different and unverified. They are listed only so a user
// who plugs one in gets told what it is and that it is not supported, instead of
// having it silently misinterpreted as a PenPartner.
constexpr uint16_t PID_GRAPHIRE_1         = 0x0010;
constexpr uint16_t PID_GRAPHIRE_2_ET0405U = 0x0011;
constexpr uint16_t PID_GRAPHIRE_3_4X5     = 0x0013;
constexpr uint16_t PID_GRAPHIRE_3_6X8     = 0x0014;
constexpr uint16_t PID_GRAPHIRE_4_4X5     = 0x0015;
constexpr uint16_t PID_GRAPHIRE_4_6X8     = 0x0016;
constexpr uint16_t PID_VOLITO             = 0x0060;
constexpr uint16_t PID_VOLITO_2           = 0x0061;

// Hardware constants for the CT-0405-U / PenPartner.
constexpr uint32_t CT0405U_MAX_X          = 5040;
constexpr uint32_t CT0405U_MAX_Y          = 3780;
constexpr uint32_t CT0405U_MAX_PRESSURE   = 255;
constexpr uint32_t CT0405U_RESOLUTION_LPI = 1016;

// HID usage pages / usages used to pick the digitizer collection out of the
// several collections a single tablet exposes under one VID/PID.
constexpr uint16_t HID_USAGE_PAGE_GENERIC    = 0x01;
constexpr uint16_t HID_USAGE_PAGE_DIGITIZER  = 0x0D;
constexpr uint16_t HID_USAGE_PAGE_VENDOR_MIN = 0xFF00;

constexpr uint16_t HID_USAGE_DIGITIZER = 0x01;
constexpr uint16_t HID_USAGE_PEN       = 0x02;
constexpr uint16_t HID_USAGE_MOUSE     = 0x02;

enum class DeviceModel {
    Unknown,                // Not a Wacom, or a Wacom we cannot name
    PenPartner_CT0405U,     // The supported device
    RecognisedUnsupported   // A Wacom we can name but deliberately do not drive
};

enum class ToolType {
    None,
    Pen,       // Stylus tip
    Eraser,    // Stylus rubber/eraser end
    Mouse      // Cordless tablet mouse
};

struct TabletCapabilities {
    uint32_t max_x = CT0405U_MAX_X;
    uint32_t max_y = CT0405U_MAX_Y;
    uint32_t max_pressure = CT0405U_MAX_PRESSURE;
    uint32_t resolution_lpi = CT0405U_RESOLUTION_LPI;
    bool has_eraser = true;
    bool has_barrel_switch = true;
    bool has_second_barrel_switch = false;   // CT-0405-U has one barrel switch
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

    // Normalized pressure 0.0 - 1.0 straight off the hardware, before the
    // deadzone remap and response curve. Kept so the UI can plot the operating
    // point against the curve (curve input on X, curve output on Y).
    double raw_normalized_pressure = 0.0;

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

// NOTE: these values are persisted to config.json as plain integers.
// Never reorder or remove an entry - append only, or old configs silently
// change meaning.
enum class ButtonAction {
    Default,          // Native Pen Action (barrel flag / no synthetic click)
    LeftClick,
    RightClick,
    MiddleClick,
    EraserToggle,
    Undo,             // Ctrl + Z
    Redo,             // Ctrl + Y
    PanScroll,        // Middle-button drag (pan in most canvas apps)
    Disabled
};

// Where the tablet coordinate bounds in DriverConfig came from. Without this
// the driver cannot distinguish "nobody has configured bounds yet" from "the
// user deliberately chose 5040x3780", so a detected device could never safely
// override the defaults.
enum class BoundsSource {
    Default,   // Untouched built-in defaults - safe to overwrite on detection
    Detected,  // Adopted from the connected device - safe to re-detect
    UserSet    // Typed, calibrated or preset-picked by the user - never override
};

// Bounds below this are almost certainly a typo rather than a real tablet.
constexpr uint32_t MIN_SANE_TABLET_BOUND = 500;

struct DriverConfig {
    // Custom Tablet Space Bounds
    uint32_t tablet_max_x = CT0405U_MAX_X;
    uint32_t tablet_max_y = CT0405U_MAX_Y;
    uint32_t tablet_max_pressure = CT0405U_MAX_PRESSURE;
    bool auto_detect_bounds = true;
    BoundsSource bounds_source = BoundsSource::Default;

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

// Configuration crosses the UI thread -> HID reader thread boundary. It is
// published as an immutable snapshot rather than mutated in place, so a reader
// always sees one internally consistent set of values.
using ConfigPtr = std::shared_ptr<const DriverConfig>;

// Callback types for events
using StateCallback = std::function<void(const TabletRawState& raw, const TabletProcessedState& processed)>;
using ConnectionCallback = std::function<void(bool connected, const std::wstring& device_name)>;

inline uint64_t GetCurrentTimestampUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Clamps a persisted integer back into a valid enumerator. Hand-edited or
// version-skewed config files must not produce out-of-range enums.
template <typename E>
inline E ClampEnum(int value, E max_inclusive, E fallback) {
    if (value < 0 || value > static_cast<int>(max_inclusive)) return fallback;
    return static_cast<E>(value);
}

} // namespace ct0405
