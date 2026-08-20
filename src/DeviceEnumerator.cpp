#include "DeviceEnumerator.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cwctype>

namespace ct0405 {

DeviceModel DeviceEnumerator::IdentifyModel(uint16_t vid, uint16_t pid) {
    if (vid != WACOM_VENDOR_ID) {
        return DeviceModel::Unknown;
    }

    switch (pid) {
        case PID_PENPARTNER_CT0405U:
        case PID_PENPARTNER_2:
            return DeviceModel::PenPartner_CT0405U;

        // Recognised, but this driver does not decode their report layouts.
        case PID_GRAPHIRE_1:
        case PID_GRAPHIRE_2_ET0405U:
        case PID_GRAPHIRE_3_4X5:
        case PID_GRAPHIRE_3_6X8:
        case PID_GRAPHIRE_4_4X5:
        case PID_GRAPHIRE_4_6X8:
        case PID_VOLITO:
        case PID_VOLITO_2:
            return DeviceModel::RecognisedUnsupported;

        default:
            return DeviceModel::Unknown;
    }
}

std::wstring DeviceEnumerator::DescribeProduct(uint16_t vid, uint16_t pid) {
    if (vid != WACOM_VENDOR_ID) return L"Unknown Device";

    switch (pid) {
        case PID_PENPARTNER_CT0405U:  return L"Wacom PenPartner (CT-0405-U)";
        case PID_PENPARTNER_2:        return L"Wacom PenPartner";
        case PID_GRAPHIRE_1:          return L"Wacom Graphire 1";
        case PID_GRAPHIRE_2_ET0405U:  return L"Wacom Graphire 2 (ET-0405-U)";
        case PID_GRAPHIRE_3_4X5:
        case PID_GRAPHIRE_3_6X8:      return L"Wacom Graphire 3";
        case PID_GRAPHIRE_4_4X5:
        case PID_GRAPHIRE_4_6X8:      return L"Wacom Graphire 4";
        case PID_VOLITO:              return L"Wacom Volito";
        case PID_VOLITO_2:            return L"Wacom Volito 2";
        default:                      return L"Wacom Tablet";
    }
}

int DeviceEnumerator::ScoreCandidate(const DiscoveredDevice& dev) {
    // Only the device family this driver actually decodes is a candidate.
    if (!dev.is_supported) return -1;

    int score = 0;

    // 1. The collection matters far more than the model. These tablets deliver
    //    pen data either on the digitizer page or on a vendor-defined page;
    //    the generic-desktop mouse collection is the classic wrong choice that
    //    opens cleanly and then never produces a packet.
    if (dev.usage_page == HID_USAGE_PAGE_DIGITIZER) {
        score += 100;
        if (dev.usage == HID_USAGE_DIGITIZER || dev.usage == HID_USAGE_PEN) score += 20;
    } else if (dev.usage_page >= HID_USAGE_PAGE_VENDOR_MIN) {
        score += 80;   // vendor-defined: how the older PenPartner protocol arrives
    } else if (dev.usage_page == HID_USAGE_PAGE_GENERIC && dev.usage == HID_USAGE_MOUSE) {
        score += 5;    // last resort - almost certainly the wrong collection
    } else if (dev.usage_page == 0) {
        score += 40;   // capabilities unavailable (handle refused); still plausible
    } else {
        score += 20;
    }

    // 2. A report long enough to carry x, y and pressure.
    if (dev.input_report_byte_length >= 7) score += 25;
    else if (dev.input_report_byte_length >= 5) score += 10;

    return score;
}

static bool ExtractVidPid(const std::wstring& path, uint16_t& out_vid, uint16_t& out_pid) {
    std::wstring lower = path;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

    size_t vidPos = lower.find(L"vid_");
    size_t pidPos = lower.find(L"pid_");

    if (vidPos != std::wstring::npos && pidPos != std::wstring::npos &&
        vidPos + 8 <= lower.size() && pidPos + 8 <= lower.size()) {
        try {
            out_vid = static_cast<uint16_t>(std::stoul(lower.substr(vidPos + 4, 4), nullptr, 16));
            out_pid = static_cast<uint16_t>(std::stoul(lower.substr(pidPos + 4, 4), nullptr, 16));
            return true;
        } catch (...) {}
    }
    return false;
}

std::vector<DiscoveredDevice> DeviceEnumerator::EnumerateAllHidDevices() {
    std::vector<DiscoveredDevice> result;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);
        if (reqSize == 0) continue;

        std::vector<uint8_t> detailBuffer(reqSize);
        auto* pDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, pDetail, reqSize, nullptr, nullptr)) {
            continue;
        }

        std::wstring devicePath = pDetail->DevicePath;

        uint16_t pathVid = 0;
        uint16_t pathPid = 0;
        bool hasPathVidPid = ExtractVidPid(devicePath, pathVid, pathPid);

        DiscoveredDevice dev;
        dev.device_path = devicePath;
        dev.vendor_id = pathVid;
        dev.product_id = pathPid;
        dev.model = IdentifyModel(pathVid, pathPid);
        dev.is_supported = (dev.model == DeviceModel::PenPartner_CT0405U);
        dev.product_name = DescribeProduct(pathVid, pathPid);
        dev.input_report_byte_length = 0;

        // Only probe devices that might plausibly be ours. Opening every HID
        // device on the machine - which the watcher thread did twice a second
        // while disconnected - can upset unrelated peripherals.
        const bool worth_probing = !hasPathVidPid || pathVid == WACOM_VENDOR_ID;

        bool opened = false;
        if (worth_probing) {
            HANDLE hDev = CreateFileW(
                devicePath.c_str(),
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (hDev != INVALID_HANDLE_VALUE) {
                opened = true;
                HIDD_ATTRIBUTES attrs{};
                attrs.Size = sizeof(HIDD_ATTRIBUTES);

                if (HidD_GetAttributes(hDev, &attrs)) {
                    dev.vendor_id = attrs.VendorID;
                    dev.product_id = attrs.ProductID;
                    dev.version_number = attrs.VersionNumber;
                    dev.model = IdentifyModel(dev.vendor_id, dev.product_id);
                    dev.is_supported = (dev.model == DeviceModel::PenPartner_CT0405U);

                    wchar_t productBuf[256]{ 0 };
                    if (HidD_GetProductString(hDev, productBuf, sizeof(productBuf))) {
                        productBuf[255] = L'\0';
                        if (productBuf[0] != L'\0') dev.product_name = productBuf;
                    }

                    wchar_t vendorBuf[256]{ 0 };
                    if (HidD_GetManufacturerString(hDev, vendorBuf, sizeof(vendorBuf))) {
                        vendorBuf[255] = L'\0';
                        dev.manufacturer = vendorBuf;
                    }

                    PHIDP_PREPARSED_DATA pPreparsed = nullptr;
                    if (HidD_GetPreparsedData(hDev, &pPreparsed)) {
                        HIDP_CAPS caps{};
                        if (HidP_GetCaps(pPreparsed, &caps) == HIDP_STATUS_SUCCESS) {
                            dev.input_report_byte_length = caps.InputReportByteLength;
                            dev.usage_page = caps.UsagePage;
                            dev.usage = caps.Usage;
                        }
                        HidD_FreePreparsedData(pPreparsed);
                    }
                }
                CloseHandle(hDev);
            }
        }

        if (dev.product_name.empty() || dev.product_name == L"Unknown Device") {
            dev.product_name = DescribeProduct(dev.vendor_id, dev.product_id);
        }

        if (hasPathVidPid || opened) {
            result.push_back(dev);
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

std::vector<DiscoveredDevice> DeviceEnumerator::EnumerateWacomDevices() {
    std::vector<DiscoveredDevice> all = EnumerateAllHidDevices();
    std::vector<DiscoveredDevice> wacomDevices;

    for (const auto& dev : all) {
        if (dev.vendor_id == WACOM_VENDOR_ID) {
            wacomDevices.push_back(dev);
        }
    }
    return wacomDevices;
}

bool DeviceEnumerator::FindFirstSupportedTablet(DiscoveredDevice& out_device) {
    const auto wacomDevices = EnumerateWacomDevices();

    // Pick the best-scoring collection of a supported tablet. Taking the first
    // Wacom interface frequently landed on the mouse collection: it opens fine
    // and then never delivers a pen report.
    const DiscoveredDevice* best = nullptr;
    int best_score = 0;
    for (const auto& dev : wacomDevices) {
        if (!dev.is_supported) continue;
        const int score = ScoreCandidate(dev);
        if (!best || score > best_score) {
            best = &dev;
            best_score = score;
        }
    }

    if (!best) return false;
    out_device = *best;
    return true;
}

bool DeviceEnumerator::FindRecognisedUnsupportedTablet(DiscoveredDevice& out_device) {
    for (const auto& dev : EnumerateWacomDevices()) {
        if (dev.model == DeviceModel::RecognisedUnsupported) {
            out_device = dev;
            return true;
        }
    }
    return false;
}

} // namespace ct0405
