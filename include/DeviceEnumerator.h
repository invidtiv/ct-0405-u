#pragma once

#include "Common.h"
#include <string>
#include <vector>

namespace ct0405 {

struct DiscoveredDevice {
    std::wstring device_path;
    std::wstring product_name;
    std::wstring manufacturer;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    uint16_t version_number = 0;
    uint16_t input_report_byte_length = 0;
    DeviceModel model = DeviceModel::Unknown;
    bool is_supported = false;
};

class DeviceEnumerator {
public:
    static std::vector<DiscoveredDevice> EnumerateAllHidDevices();
    static std::vector<DiscoveredDevice> EnumerateWacomDevices();
    static bool FindFirstSupportedTablet(DiscoveredDevice& out_device);

    static DeviceModel IdentifyModel(uint16_t vid, uint16_t pid);
    static std::wstring ModelToString(DeviceModel model);
};

} // namespace ct0405
