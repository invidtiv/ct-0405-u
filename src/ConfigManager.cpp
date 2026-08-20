#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <shlobj.h>

namespace ct0405 {

namespace {

const wchar_t* const AUTOSTART_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const AUTOSTART_VALUE  = L"WacomCT0405Driver";

std::wstring GetExecutableDirectory() {
    wchar_t exePath[MAX_PATH]{ 0 };
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";

    std::wstring path(exePath, len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash);
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Finds "key" as a top-level field and returns the offset of its value.
size_t FindValueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        const size_t colon = json.find_first_not_of(" \t\r\n", pos + needle.size());
        // Require the very next non-space character to be ':' so a key name
        // appearing inside some other field's value cannot match.
        if (colon != std::string::npos && json[colon] == ':') {
            const size_t value = json.find_first_not_of(" \t\r\n", colon + 1);
            return value;
        }
        pos += needle.size();
    }
    return std::string::npos;
}

std::string ExtractRawValue(const std::string& json, const std::string& key) {
    const size_t start = FindValueStart(json, key);
    if (start == std::string::npos) return "";
    const size_t end = json.find_first_of(",}\r\n", start);
    if (end == std::string::npos) return json.substr(start);
    return json.substr(start, end - start);
}

bool ExtractFieldBool(const std::string& json, const std::string& key, bool default_val) {
    std::string raw = ExtractRawValue(json, key);
    if (raw.empty()) return default_val;
    if (raw.find("true") != std::string::npos) return true;
    if (raw.find("false") != std::string::npos) return false;
    return default_val;
}

double ExtractFieldDouble(const std::string& json, const std::string& key, double default_val) {
    const std::string raw = ExtractRawValue(json, key);
    if (raw.empty()) return default_val;
    try {
        return std::stod(raw);
    } catch (...) {
        return default_val;
    }
}

int ExtractFieldInt(const std::string& json, const std::string& key, int default_val) {
    const std::string raw = ExtractRawValue(json, key);
    if (raw.empty()) return default_val;
    try {
        return std::stoi(raw);
    } catch (...) {
        return default_val;
    }
}

uint32_t ClampBound(int value, uint32_t fallback) {
    if (value < static_cast<int>(MIN_SANE_TABLET_BOUND)) return fallback;
    return static_cast<uint32_t>(value);
}

} // namespace

std::wstring ConfigManager::GetAppDataConfigPath() {
    wchar_t appData[MAX_PATH]{ 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\CT0405_Driver";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\config.json";
    }
    return L"config.json";
}

std::wstring ConfigManager::GetConfigPath() {
    const std::wstring exeDir = GetExecutableDirectory();
    if (!exeDir.empty()) {
        const std::wstring portable = exeDir + L"\\config.json";
        if (FileExists(portable)) {
            return portable;
        }
    }
    return GetAppDataConfigPath();
}

bool ConfigManager::SetAutoStart(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_SUBKEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enable) {
        wchar_t exePath[MAX_PATH]{ 0 };
        const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hKey);
            return false;
        }
        const std::wstring cmd = L"\"" + std::wstring(exePath, len) + L"\" --minimized";
        result = RegSetValueExW(hKey, AUTOSTART_VALUE, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(cmd.c_str()),
                                static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(hKey, AUTOSTART_VALUE);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;   // already absent is success
        }
    }

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool ConfigManager::IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_SUBKEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LONG res = RegQueryValueExW(hKey, AUTOSTART_VALUE, nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

std::string ConfigManager::SerializeToJson(const DriverConfig& c) {
    std::ostringstream ss;
    // Enough significant digits that every double round-trips unchanged.
    ss << std::setprecision(12);
    ss << "{\n";
    ss << "  \"tablet_max_x\": " << c.tablet_max_x << ",\n";
    ss << "  \"tablet_max_y\": " << c.tablet_max_y << ",\n";
    ss << "  \"tablet_max_pressure\": " << c.tablet_max_pressure << ",\n";
    ss << "  \"auto_detect_bounds\": " << (c.auto_detect_bounds ? "true" : "false") << ",\n";
    ss << "  \"bounds_source\": " << static_cast<int>(c.bounds_source) << ",\n";
    ss << "  \"mapping_mode\": " << static_cast<int>(c.mapping_mode) << ",\n";
    ss << "  \"target_monitor_index\": " << c.target_monitor_index << ",\n";
    ss << "  \"lock_aspect_ratio\": " << (c.lock_aspect_ratio ? "true" : "false") << ",\n";
    ss << "  \"tablet_area_left\": " << c.tablet_area_left << ",\n";
    ss << "  \"tablet_area_top\": " << c.tablet_area_top << ",\n";
    ss << "  \"tablet_area_right\": " << c.tablet_area_right << ",\n";
    ss << "  \"tablet_area_bottom\": " << c.tablet_area_bottom << ",\n";
    ss << "  \"custom_screen_left\": " << c.custom_screen_rect.left << ",\n";
    ss << "  \"custom_screen_top\": " << c.custom_screen_rect.top << ",\n";
    ss << "  \"custom_screen_right\": " << c.custom_screen_rect.right << ",\n";
    ss << "  \"custom_screen_bottom\": " << c.custom_screen_rect.bottom << ",\n";
    ss << "  \"curve_type\": " << static_cast<int>(c.curve_type) << ",\n";
    ss << "  \"pressure_min_threshold\": " << c.pressure_min_threshold << ",\n";
    ss << "  \"pressure_max_threshold\": " << c.pressure_max_threshold << ",\n";
    ss << "  \"custom_bezier_p1\": " << c.custom_bezier_p1 << ",\n";
    ss << "  \"custom_bezier_p2\": " << c.custom_bezier_p2 << ",\n";
    ss << "  \"enable_smoothing\": " << (c.enable_smoothing ? "true" : "false") << ",\n";
    ss << "  \"filter_min_cutoff\": " << c.filter_min_cutoff << ",\n";
    ss << "  \"filter_beta\": " << c.filter_beta << ",\n";
    ss << "  \"filter_d_cutoff\": " << c.filter_d_cutoff << ",\n";
    ss << "  \"tip_action\": " << static_cast<int>(c.tip_action) << ",\n";
    ss << "  \"barrel_1_action\": " << static_cast<int>(c.barrel_1_action) << ",\n";
    ss << "  \"barrel_2_action\": " << static_cast<int>(c.barrel_2_action) << ",\n";
    ss << "  \"use_windows_ink\": " << (c.use_windows_ink ? "true" : "false") << ",\n";
    ss << "  \"start_with_windows\": " << (c.start_with_windows ? "true" : "false") << ",\n";
    ss << "  \"minimize_to_tray\": " << (c.minimize_to_tray ? "true" : "false") << ",\n";
    ss << "  \"start_minimized\": " << (c.start_minimized ? "true" : "false") << "\n";
    ss << "}\n";
    return ss.str();
}

DriverConfig ConfigManager::DeserializeFromJson(const std::string& json) {
    DriverConfig c;

    c.tablet_max_x = ClampBound(ExtractFieldInt(json, "tablet_max_x", static_cast<int>(c.tablet_max_x)), c.tablet_max_x);
    c.tablet_max_y = ClampBound(ExtractFieldInt(json, "tablet_max_y", static_cast<int>(c.tablet_max_y)), c.tablet_max_y);

    const int pressure = ExtractFieldInt(json, "tablet_max_pressure", static_cast<int>(c.tablet_max_pressure));
    c.tablet_max_pressure = (pressure > 0 && pressure <= 8192) ? static_cast<uint32_t>(pressure) : c.tablet_max_pressure;

    c.auto_detect_bounds = ExtractFieldBool(json, "auto_detect_bounds", c.auto_detect_bounds);

    // Every enum is range-checked on the way in. A hand-edited or
    // version-skewed file must not be able to produce an out-of-range value.
    c.bounds_source = ClampEnum(ExtractFieldInt(json, "bounds_source", static_cast<int>(c.bounds_source)),
                                BoundsSource::UserSet, BoundsSource::Default);
    c.mapping_mode = ClampEnum(ExtractFieldInt(json, "mapping_mode", static_cast<int>(c.mapping_mode)),
                               MappingMode::CustomArea, MappingMode::PrimaryMonitor);
    c.curve_type = ClampEnum(ExtractFieldInt(json, "curve_type", static_cast<int>(c.curve_type)),
                             PressureCurveType::CustomBezier, PressureCurveType::Linear);
    c.tip_action = ClampEnum(ExtractFieldInt(json, "tip_action", static_cast<int>(c.tip_action)),
                             ButtonAction::Disabled, ButtonAction::LeftClick);
    c.barrel_1_action = ClampEnum(ExtractFieldInt(json, "barrel_1_action", static_cast<int>(c.barrel_1_action)),
                                  ButtonAction::Disabled, ButtonAction::RightClick);
    c.barrel_2_action = ClampEnum(ExtractFieldInt(json, "barrel_2_action", static_cast<int>(c.barrel_2_action)),
                                  ButtonAction::Disabled, ButtonAction::EraserToggle);

    c.target_monitor_index = std::max(0, ExtractFieldInt(json, "target_monitor_index", c.target_monitor_index));
    c.lock_aspect_ratio = ExtractFieldBool(json, "lock_aspect_ratio", c.lock_aspect_ratio);

    c.tablet_area_left = std::clamp(ExtractFieldDouble(json, "tablet_area_left", c.tablet_area_left), 0.0, 1.0);
    c.tablet_area_top = std::clamp(ExtractFieldDouble(json, "tablet_area_top", c.tablet_area_top), 0.0, 1.0);
    c.tablet_area_right = std::clamp(ExtractFieldDouble(json, "tablet_area_right", c.tablet_area_right), 0.0, 1.0);
    c.tablet_area_bottom = std::clamp(ExtractFieldDouble(json, "tablet_area_bottom", c.tablet_area_bottom), 0.0, 1.0);

    // An inverted or degenerate active area would divide the mapping by zero.
    if (c.tablet_area_right <= c.tablet_area_left) { c.tablet_area_left = 0.0; c.tablet_area_right = 1.0; }
    if (c.tablet_area_bottom <= c.tablet_area_top) { c.tablet_area_top = 0.0; c.tablet_area_bottom = 1.0; }

    c.custom_screen_rect.left = ExtractFieldInt(json, "custom_screen_left", c.custom_screen_rect.left);
    c.custom_screen_rect.top = ExtractFieldInt(json, "custom_screen_top", c.custom_screen_rect.top);
    c.custom_screen_rect.right = ExtractFieldInt(json, "custom_screen_right", c.custom_screen_rect.right);
    c.custom_screen_rect.bottom = ExtractFieldInt(json, "custom_screen_bottom", c.custom_screen_rect.bottom);
    if (c.custom_screen_rect.right <= c.custom_screen_rect.left ||
        c.custom_screen_rect.bottom <= c.custom_screen_rect.top) {
        c.custom_screen_rect = RECT{ 0, 0, 1920, 1080 };
    }

    c.pressure_min_threshold = std::clamp(ExtractFieldDouble(json, "pressure_min_threshold", c.pressure_min_threshold), 0.0, 0.95);
    c.pressure_max_threshold = std::clamp(ExtractFieldDouble(json, "pressure_max_threshold", c.pressure_max_threshold), 0.05, 1.0);
    if (c.pressure_max_threshold <= c.pressure_min_threshold) {
        c.pressure_min_threshold = 0.0;
        c.pressure_max_threshold = 1.0;
    }

    c.custom_bezier_p1 = std::clamp(ExtractFieldDouble(json, "custom_bezier_p1", c.custom_bezier_p1), 0.0, 1.0);
    c.custom_bezier_p2 = std::clamp(ExtractFieldDouble(json, "custom_bezier_p2", c.custom_bezier_p2), 0.0, 1.0);

    c.enable_smoothing = ExtractFieldBool(json, "enable_smoothing", c.enable_smoothing);
    c.filter_min_cutoff = std::clamp(ExtractFieldDouble(json, "filter_min_cutoff", c.filter_min_cutoff), 0.1, 20.0);
    c.filter_beta = std::clamp(ExtractFieldDouble(json, "filter_beta", c.filter_beta), 0.0, 1.0);
    c.filter_d_cutoff = std::clamp(ExtractFieldDouble(json, "filter_d_cutoff", c.filter_d_cutoff), 0.1, 20.0);

    c.use_windows_ink = ExtractFieldBool(json, "use_windows_ink", c.use_windows_ink);
    c.start_with_windows = ExtractFieldBool(json, "start_with_windows", c.start_with_windows);
    c.minimize_to_tray = ExtractFieldBool(json, "minimize_to_tray", c.minimize_to_tray);
    c.start_minimized = ExtractFieldBool(json, "start_minimized", c.start_minimized);

    return c;
}

DriverConfig ConfigManager::LoadConfigFrom(const std::wstring& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return DriverConfig{};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return DeserializeFromJson(buffer.str());
}

DriverConfig ConfigManager::LoadConfig() {
    const std::wstring path = GetConfigPath();

    if (FileExists(path)) {
        return LoadConfigFrom(path);
    }

    // First run: write the defaults so the file exists and is discoverable.
    DriverConfig defaults;
    SaveConfigTo(defaults, path);
    return defaults;
}

bool ConfigManager::SaveConfigTo(const DriverConfig& config, const std::wstring& file_path) {
    const std::string json = SerializeToJson(config);

    // Write to a sibling temp file and swap it in, so an interrupted write
    // cannot leave a truncated config behind.
    const std::wstring temp_path = file_path + L".tmp";
    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.flush();
        if (!out.good()) return false;
    }

    if (!MoveFileExW(temp_path.c_str(), file_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path.c_str());
        return false;
    }
    return true;
}

bool ConfigManager::SaveConfig(const DriverConfig& config) {
    return SaveConfigTo(config, GetConfigPath());
}

} // namespace ct0405
