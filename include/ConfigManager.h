#pragma once

#include "Common.h"
#include <string>

namespace ct0405 {

class ConfigManager {
public:
    // Loads from the single resolved config path (see GetConfigPath).
    // Returns defaults when no config exists yet.
    static DriverConfig LoadConfig();
    static DriverConfig LoadConfigFrom(const std::wstring& file_path);

    // Returns false when the settings could not actually be written. The old
    // signature always returned true, so a failed save looked like a success
    // and the settings vanished at next launch.
    static bool SaveConfig(const DriverConfig& config);
    static bool SaveConfigTo(const DriverConfig& config, const std::wstring& file_path);

    // The one file the driver reads and writes.
    //
    // Portable mode requires an explicit marker file (portable.txt) beside the
    // executable - NOT merely a config.json, because releases before v1.1.1
    // wrote one there as a side effect of every save. Keying off its presence
    // silently promoted that leftover above the user's real settings.
    // Otherwise: %APPDATA%, resolved from the executable's own directory rather
    // than the working directory, which differs on an autostart launch.
    static std::wstring GetConfigPath();
    static std::wstring GetAppDataConfigPath();
    static std::wstring GetPortableMarkerPath();
    static bool IsPortableMode();

    // Path of a stray config.json left beside the executable by an older
    // release, if one is present and portable mode is not enabled.
    static std::wstring FindLegacyStrayConfig();

    // Registry autostart. Deliberately separate from SaveConfig: persisting
    // preferences should not have the side effect of rewriting a Run key.
    static bool SetAutoStart(bool enable);
    static bool IsAutoStartEnabled();

    static std::string SerializeToJson(const DriverConfig& config);
    static DriverConfig DeserializeFromJson(const std::string& json_str);
};

} // namespace ct0405
