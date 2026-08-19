#include "DeviceEnumerator.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>
#include <string>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace ct0405 {

DeviceModel DeviceEnumerator::IdentifyModel(uint16_t vid, uint16_t pid) {
    if (vid != WACOM_VENDOR_ID) {
        return DeviceModel::Unknown;
    }

    switch (pid) {
        case PID_PENPARTNER_CT0405U:
        case PID_PENPARTNER_2:
            return DeviceModel::PenPartner_CT0405U;
        case PID_GRAPHIRE_1:
            return DeviceModel::Graphire1;
        case PID_GRAPHIRE_2_ET0405U:
            return DeviceModel::Graphire2;
        case PID_GRAPHIRE_3_4X5:
        case PID_GRAPHIRE_3_6X8:
            return DeviceModel::Graphire3;
        case PID_GRAPHIRE_4_4X5:
        case PID_GRAPHIRE_4_6X8:
            return DeviceModel::Graphire4;
        case PID_VOLITO:
            return DeviceModel::Volito;
        case PID_VOLITO_2:
            return DeviceModel::Volito2;
        default:
            return DeviceModel::GenericWacom;
    }
}

std::wstring DeviceEnumerator::ModelToString(DeviceModel model) {
    switch (model) {
        case DeviceModel::PenPartner_CT0405U:
            return L"Wacom PenPartner (CT-0405-U)";
        case DeviceModel::Graphire1:
            return L"Wacom Graphire 1";
        case DeviceModel::Graphire2:
            return L"Wacom Graphire 2 (ET-0405-U)";
        case DeviceModel::Graphire3:
            return L"Wacom Graphire 3";
        case DeviceModel::Graphire4:
            return L"Wacom Graphire 4";
        case DeviceModel::Volito:
        case DeviceModel::Volito2:
            return L"Wacom Volito";
        case DeviceModel::GenericWacom:
            return L"Generic Wacom Tablet";
        default:
            return L"Unknown Device";
    }
}

static bool ExtractVidPid(const std::wstring& path, uint16_t& out_vid, uint16_t& out_pid) {
    std::wstring lower = path;
    for (auto& c : lower) c = towlower(c);

    size_t vidPos = lower.find(L"vid_");
    size_t pidPos = lower.find(L"pid_");

    if (vidPos != std::wstring::npos && pidPos != std::wstring::npos) {
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
        dev.is_supported = (pathVid == WACOM_VENDOR_ID);
        dev.product_name = ModelToString(dev.model);
        dev.input_report_byte_length = 7;

        // Try reading attributes via handle if available
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
            HIDD_ATTRIBUTES attrs{};
            attrs.Size = sizeof(HIDD_ATTRIBUTES);

            if (HidD_GetAttributes(hDev, &attrs)) {
                dev.vendor_id = attrs.VendorID;
                dev.product_id = attrs.ProductID;
                dev.version_number = attrs.VersionNumber;
                dev.model = IdentifyModel(dev.vendor_id, dev.product_id);
                dev.is_supported = (dev.vendor_id == WACOM_VENDOR_ID);

                wchar_t stringBuf[256]{ 0 };
                if (HidD_GetProductString(hDev, stringBuf, sizeof(stringBuf))) {
                    if (wcslen(stringBuf) > 0) dev.product_name = stringBuf;
                }
                if (HidD_GetManufacturerString(hDev, stringBuf, sizeof(stringBuf))) {
                    dev.manufacturer = stringBuf;
                }

                PHIDP_PREPARSED_DATA pPreparsed = nullptr;
                if (HidD_GetPreparsedData(hDev, &pPreparsed)) {
                    HIDP_CAPS caps{};
                    if (HidP_GetCaps(pPreparsed, &caps) == HIDP_STATUS_SUCCESS) {
                        dev.input_report_byte_length = caps.InputReportByteLength;
                    }
                    HidD_FreePreparsedData(pPreparsed);
                }
            }
            CloseHandle(hDev);
        }

        if (dev.product_name.empty() || dev.product_name == L"Unknown Device") {
            dev.product_name = ModelToString(dev.model);
        }

        if (hasPathVidPid || hDev != INVALID_HANDLE_VALUE) {
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
    auto wacomDevices = EnumerateWacomDevices();
    if (wacomDevices.empty()) {
        return false;
    }

    for (const auto& dev : wacomDevices) {
        if (dev.model == DeviceModel::PenPartner_CT0405U || dev.model == DeviceModel::Graphire1 || dev.model == DeviceModel::Graphire2) {
            out_device = dev;
            return true;
        }
    }

    out_device = wacomDevices[0];
    return true;
}

} // namespace ct0405
