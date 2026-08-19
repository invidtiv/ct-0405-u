#pragma once

#include "Common.h"
#include <string>

namespace ct0405 {

class ConfigManager {
public:
    static DriverConfig LoadConfig(const std::wstring& file_path = L"config.json");
    static bool SaveConfig(const DriverConfig& config, const std::wstring& file_path = L"config.json");

    static std::wstring GetDefaultConfigPath();
    static bool SetAutoStart(bool enable);
    static bool IsAutoStartEnabled();

private:
    static std::string SerializeToJson(const DriverConfig& config);
    static DriverConfig DeserializeFromJson(const std::string& json_str);
};

} // namespace ct0405
