#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <shlobj.h>

namespace ct0405 {

std::wstring ConfigManager::GetDefaultConfigPath() {
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\CT0405_Driver";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\config.json";
    }
    return L"config.json";
}

bool ConfigManager::SetAutoStart(bool enable) {
    HKEY hKey;
    const wchar_t* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* valName = L"WacomCT0405Driver";
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
        RegSetValueExW(hKey, valName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, valName);
    }

    RegCloseKey(hKey);
    return true;
}

bool ConfigManager::IsAutoStartEnabled() {
    HKEY hKey;
    const wchar_t* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* valName = L"WacomCT0405Driver";
    DWORD type = 0;
    DWORD size = 0;
    LONG res = RegQueryValueExW(hKey, valName, nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

std::string ConfigManager::SerializeToJson(const DriverConfig& c) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"tablet_max_x\": " << c.tablet_max_x << ",\n";
    ss << "  \"tablet_max_y\": " << c.tablet_max_y << ",\n";
    ss << "  \"tablet_max_pressure\": " << c.tablet_max_pressure << ",\n";
    ss << "  \"auto_detect_bounds\": " << (c.auto_detect_bounds ? "true" : "false") << ",\n";
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

static bool ExtractFieldBool(const std::string& json, const std::string& key, bool default_val) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t true_pos = json.find("true", colon);
    size_t false_pos = json.find("false", colon);
    size_t comma = json.find_first_of(",}\n", colon);

    if (true_pos != std::string::npos && true_pos < comma) return true;
    if (false_pos != std::string::npos && false_pos < comma) return false;
    return default_val;
}

static double ExtractFieldDouble(const std::string& json, const std::string& key, double default_val) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
    size_t end = json.find_first_of(",}\n", start);
    if (start != std::string::npos && end != std::string::npos) {
        try {
            return std::stod(json.substr(start, end - start));
        } catch (...) {}
    }
    return default_val;
}

static int ExtractFieldInt(const std::string& json, const std::string& key, int default_val) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t start = json.find_first_not_of(" \t\r\n", colon + 1);
    size_t end = json.find_first_of(",}\n", start);
    if (start != std::string::npos && end != std::string::npos) {
        try {
            return std::stoi(json.substr(start, end - start));
        } catch (...) {}
    }
    return default_val;
}

DriverConfig ConfigManager::DeserializeFromJson(const std::string& json) {
    DriverConfig c;
    c.tablet_max_x = static_cast<uint32_t>(ExtractFieldInt(json, "tablet_max_x", static_cast<int>(c.tablet_max_x)));
    c.tablet_max_y = static_cast<uint32_t>(ExtractFieldInt(json, "tablet_max_y", static_cast<int>(c.tablet_max_y)));
    c.tablet_max_pressure = static_cast<uint32_t>(ExtractFieldInt(json, "tablet_max_pressure", static_cast<int>(c.tablet_max_pressure)));
    c.auto_detect_bounds = ExtractFieldBool(json, "auto_detect_bounds", c.auto_detect_bounds);

    c.mapping_mode = static_cast<MappingMode>(ExtractFieldInt(json, "mapping_mode", static_cast<int>(c.mapping_mode)));
    c.target_monitor_index = ExtractFieldInt(json, "target_monitor_index", c.target_monitor_index);
    c.lock_aspect_ratio = ExtractFieldBool(json, "lock_aspect_ratio", c.lock_aspect_ratio);

    c.tablet_area_left = ExtractFieldDouble(json, "tablet_area_left", c.tablet_area_left);
    c.tablet_area_top = ExtractFieldDouble(json, "tablet_area_top", c.tablet_area_top);
    c.tablet_area_right = ExtractFieldDouble(json, "tablet_area_right", c.tablet_area_right);
    c.tablet_area_bottom = ExtractFieldDouble(json, "tablet_area_bottom", c.tablet_area_bottom);

    c.custom_screen_rect.left = ExtractFieldInt(json, "custom_screen_left", c.custom_screen_rect.left);
    c.custom_screen_rect.top = ExtractFieldInt(json, "custom_screen_top", c.custom_screen_rect.top);
    c.custom_screen_rect.right = ExtractFieldInt(json, "custom_screen_right", c.custom_screen_rect.right);
    c.custom_screen_rect.bottom = ExtractFieldInt(json, "custom_screen_bottom", c.custom_screen_rect.bottom);

    c.curve_type = static_cast<PressureCurveType>(ExtractFieldInt(json, "curve_type", static_cast<int>(c.curve_type)));
    c.pressure_min_threshold = ExtractFieldDouble(json, "pressure_min_threshold", c.pressure_min_threshold);
    c.pressure_max_threshold = ExtractFieldDouble(json, "pressure_max_threshold", c.pressure_max_threshold);
    c.custom_bezier_p1 = ExtractFieldDouble(json, "custom_bezier_p1", c.custom_bezier_p1);
    c.custom_bezier_p2 = ExtractFieldDouble(json, "custom_bezier_p2", c.custom_bezier_p2);

    c.enable_smoothing = ExtractFieldBool(json, "enable_smoothing", c.enable_smoothing);
    c.filter_min_cutoff = ExtractFieldDouble(json, "filter_min_cutoff", c.filter_min_cutoff);
    c.filter_beta = ExtractFieldDouble(json, "filter_beta", c.filter_beta);
    c.filter_d_cutoff = ExtractFieldDouble(json, "filter_d_cutoff", c.filter_d_cutoff);

    c.tip_action = static_cast<ButtonAction>(ExtractFieldInt(json, "tip_action", static_cast<int>(c.tip_action)));
    c.barrel_1_action = static_cast<ButtonAction>(ExtractFieldInt(json, "barrel_1_action", static_cast<int>(c.barrel_1_action)));
    c.barrel_2_action = static_cast<ButtonAction>(ExtractFieldInt(json, "barrel_2_action", static_cast<int>(c.barrel_2_action)));

    c.use_windows_ink = ExtractFieldBool(json, "use_windows_ink", c.use_windows_ink);
    c.start_with_windows = ExtractFieldBool(json, "start_with_windows", c.start_with_windows);
    c.minimize_to_tray = ExtractFieldBool(json, "minimize_to_tray", c.minimize_to_tray);
    c.start_minimized = ExtractFieldBool(json, "start_minimized", c.start_minimized);

    return c;
}

DriverConfig ConfigManager::LoadConfig(const std::wstring& file_path) {
    // 1. Try local file path first
    std::ifstream file(file_path);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        return DeserializeFromJson(buffer.str());
    }

    // 2. Try AppData path
    std::wstring appDataPath = GetDefaultConfigPath();
    file.open(appDataPath);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        return DeserializeFromJson(buffer.str());
    }

    // 3. Fallback to default and create file
    DriverConfig def;
    SaveConfig(def, appDataPath);
    return def;
}

bool ConfigManager::SaveConfig(const DriverConfig& config, const std::wstring& file_path) {
    std::string json = SerializeToJson(config);

    // Save to default AppData path
    std::wstring appDataPath = GetDefaultConfigPath();
    std::ofstream appDataFile(appDataPath);
    if (appDataFile.is_open()) {
        appDataFile << json;
        appDataFile.close();
    }

    // Also save to local path if different
    if (file_path != appDataPath) {
        std::ofstream localFile(file_path);
        if (localFile.is_open()) {
            localFile << json;
            localFile.close();
        }
    }

    SetAutoStart(config.start_with_windows);
    return true;
}

} // namespace ct0405
