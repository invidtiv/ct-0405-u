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
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    DeviceModel model = DeviceModel::Unknown;

    // True only for the PenPartner / CT-0405-U family this driver decodes.
    bool is_supported = false;
};

class DeviceEnumerator {
public:
    static std::vector<DiscoveredDevice> EnumerateAllHidDevices();
    static std::vector<DiscoveredDevice> EnumerateWacomDevices();

    // Finds the best CT-0405-U collection to open. Returns false when no
    // supported tablet is present, even if other Wacom hardware is attached.
    static bool FindFirstSupportedTablet(DiscoveredDevice& out_device);

    // Finds a Wacom tablet this driver recognises but deliberately does not
    // drive, so the user can be told what it is instead of seeing nothing.
    static bool FindRecognisedUnsupportedTablet(DiscoveredDevice& out_device);

    static DeviceModel IdentifyModel(uint16_t vid, uint16_t pid);

    // Human-readable product name for a known VID/PID, including models this
    // driver does not support.
    static std::wstring DescribeProduct(uint16_t vid, uint16_t pid);

    // Higher is better. A tablet exposes several HID collections under one
    // VID/PID (digitizer, mouse, vendor-defined); only some of them carry pen
    // reports. Exposed for diagnostics so the CLI can show the ranking.
    static int ScoreCandidate(const DiscoveredDevice& dev);
};

} // namespace ct0405
