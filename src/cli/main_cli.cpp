#include "TabletDriver.h"
#include "DeviceEnumerator.h"
#include "PacketDecoder.h"
#include "SignalProcessor.h"
#include "CoordinateMapper.h"
#include "ConfigManager.h"
#include "InputInjector.h"

#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <iostream>
#include <iomanip>
#include <sstream>     // std::wostringstream - previously relied on transitive includes
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>       // std::cos / std::sin - likewise
#include <algorithm>

using namespace ct0405;

namespace {

// Everything goes through the wide stream. Mixing std::cout and std::wcout
// fixes the stream orientation on first use and can silently swallow all
// subsequent output from the other one.
std::wostream& out = std::wcout;

std::wstring ToHex(const uint8_t* data, size_t length) {
    std::wostringstream ss;
    for (size_t i = 0; i < length; ++i) {
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill(L'0')
           << static_cast<int>(data[i]) << L" ";
    }
    return ss.str();
}

std::wstring UsageDescription(uint16_t page, uint16_t usage) {
    if (page == HID_USAGE_PAGE_DIGITIZER) {
        if (usage == HID_USAGE_DIGITIZER) return L"Digitizer";
        if (usage == HID_USAGE_PEN) return L"Pen";
        return L"Digitizer (other)";
    }
    if (page == HID_USAGE_PAGE_GENERIC) {
        if (usage == 0x02) return L"Mouse";
        if (usage == 0x01) return L"Pointer";
        return L"Generic Desktop";
    }
    if (page >= HID_USAGE_PAGE_VENDOR_MIN) return L"Vendor-defined";
    if (page == 0) return L"Unavailable";
    return L"Other";
}

void PrintUsage() {
    out << L"========================================================\n";
    out << L"  Wacom CT-0405-U Diagnostic & Driver CLI Utility\n";
    out << L"========================================================\n";
    out << L"Usage:\n";
    out << L"  CT0405_CLI.exe [options]\n\n";
    out << L"Options:\n";
    out << L"  --diagnose, -d        Enumerate HID interfaces and rank tablet candidates\n";
    out << L"  --dump-packets, -p [s] Sniff raw HID packets (default 10 seconds)\n";
    out << L"  --test-injection, -t  Test Windows Ink Synthetic Pointer injection\n";
    out << L"  --test-decoder, -u    Run the automated regression suite\n";
    out << L"  --trace, -r [s]       Show raw -> normalized -> screen mapping (no injection)\n";
    out << L"  --probe, -b [s]       Narrate the low-level HID read path (diagnostic)\n";
    out << L"  --headless, -s        Run driver in headless background console mode\n";
    out << L"  --help, -h            Show this help message\n";
    out << L"========================================================\n";
}

void RunDiagnose() {
    out << L"\n[+] Scanning USB HID interfaces...\n";
    const auto allDevices = DeviceEnumerator::EnumerateAllHidDevices();
    out << L"[*] Total HID interfaces detected: " << allDevices.size() << L"\n\n";

    const auto wacomDevices = DeviceEnumerator::EnumerateWacomDevices();
    if (wacomDevices.empty()) {
        out << L"[-] No Wacom devices detected on the USB bus.\n";
        out << L"    Please ensure your Wacom CT-0405-U is plugged into a USB port.\n\n";
        out << L"[*] Listing all connected USB HID devices for reference:\n";
        for (size_t i = 0; i < allDevices.size(); ++i) {
            const auto& dev = allDevices[i];
            out << L"  [" << i << L"] VID: 0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.vendor_id
                << L" PID: 0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.product_id
                << std::dec << std::setfill(L' ')
                << L" | " << (dev.product_name.empty() ? L"Unknown Device" : dev.product_name)
                << L" (" << (dev.manufacturer.empty() ? L"Generic" : dev.manufacturer) << L")\n";
        }
        return;
    }

    out << L"[+] FOUND " << wacomDevices.size() << L" WACOM HID COLLECTION(S):\n";
    out << L"    A tablet exposes several collections under one VID/PID; only some\n";
    out << L"    of them carry pen reports. Higher score = better candidate.\n";

    for (size_t i = 0; i < wacomDevices.size(); ++i) {
        const auto& dev = wacomDevices[i];
        out << L"\n  --- Collection [" << i << L"] ---\n";
        out << L"  Model Name:    " << dev.product_name << L"\n";
        out << L"  Supported:     " << (dev.is_supported ? L"yes - decoded by this driver"
                                                         : L"NO - recognised but not driven") << L"\n";
        out << L"  Vendor ID:     0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.vendor_id
            << std::dec << std::setfill(L' ') << L"\n";
        out << L"  Product ID:    0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.product_id
            << std::dec << std::setfill(L' ') << L"\n";
        out << L"  Usage Page:    0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.usage_page
            << std::dec << std::setfill(L' ')
            << L" / 0x" << std::hex << dev.usage << std::dec
            << L"  (" << UsageDescription(dev.usage_page, dev.usage) << L")\n";
        out << L"  Report Length: " << dev.input_report_byte_length << L" bytes\n";
        out << L"  Candidate Score: " << DeviceEnumerator::ScoreCandidate(dev) << L"\n";
        out << L"  Device Path:   " << dev.device_path << L"\n";
    }

    DiscoveredDevice chosen;
    if (DeviceEnumerator::FindFirstSupportedTablet(chosen)) {
        out << L"\n[+] Driver would open: " << chosen.product_name
            << L"  (score " << DeviceEnumerator::ScoreCandidate(chosen) << L")\n";
        out << L"    " << chosen.device_path << L"\n";
        return;
    }

    DiscoveredDevice other;
    if (DeviceEnumerator::FindRecognisedUnsupportedTablet(other)) {
        out << L"\n[-] " << other.product_name << L" is attached, but this driver only\n";
        out << L"    supports the Wacom PenPartner / CT-0405-U (PID 0x0000, 0x0003).\n";
        out << L"    Its report layout differs and is not decoded here.\n";
    } else {
        out << L"\n[-] No supported tablet found.\n";
    }
}

LRESULT CALLBACK SnifferWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg != WM_INPUT) {
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    HRAWINPUT hRawInput = reinterpret_cast<HRAWINPUT>(lParam);
    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return 0;

    std::vector<uint8_t> rawBuffer(size);
    if (GetRawInputData(hRawInput, RID_INPUT, rawBuffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
        return 0;
    }

    auto* pRaw = reinterpret_cast<RAWINPUT*>(rawBuffer.data());

    UINT nameSize = 0;
    GetRawInputDeviceInfoW(pRaw->header.hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
    std::wstring devName;
    if (nameSize > 0) {
        devName.resize(nameSize);
        if (GetRawInputDeviceInfoW(pRaw->header.hDevice, RIDI_DEVICENAME, devName.data(), &nameSize) == static_cast<UINT>(-1)) {
            devName.clear();
        }
        devName.erase(std::find(devName.begin(), devName.end(), L'\0'), devName.end());
    }

    std::wstring lowered = devName;
    for (auto& ch : lowered) ch = static_cast<wchar_t>(towlower(ch));
    const bool isWacom = lowered.find(L"056a") != std::wstring::npos;

    if (pRaw->header.dwType == RIM_TYPEHID) {
        const DWORD hidSize = pRaw->data.hid.dwSizeHid * pRaw->data.hid.dwCount;
        out << L"[RAW_INPUT HID] (" << (isWacom ? L"WACOM" : L"OTHER") << L") "
            << ToHex(pRaw->data.hid.bRawData, hidSize) << L"\n" << std::flush;
    } else if (pRaw->header.dwType == RIM_TYPEMOUSE && isWacom) {
        out << L"[RAW_INPUT MOUSE WACOM] flags=" << pRaw->data.mouse.usFlags
            << L" buttons=" << pRaw->data.mouse.usButtonFlags
            << L" x=" << pRaw->data.mouse.lLastX
            << L" y=" << pRaw->data.mouse.lLastY << L"\n" << std::flush;
    }
    return 0;
}

void RunPacketDumper(int seconds) {
    out << L"\n[+] Connecting to Wacom Tablet packet sniffer (Raw Input + Direct HID)...\n" << std::flush;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = SnifferWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CT0405_Sniffer_Class";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        out << L"[-] Failed to register sniffer window class: " << GetLastError() << L"\n";
        return;
    }

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"CT0405 Sniffer", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hWnd) {
        out << L"[-] Failed to create sniffer window: " << GetLastError() << L"\n";
        return;
    }

    RAWINPUTDEVICE rids[6]{};
    const struct { USHORT page; USHORT usage; } targets[6] = {
        { 0x0D, 0x01 },  // Digitizer
        { 0x0D, 0x02 },  // Pen
        { 0x01, 0x02 },  // Mouse
        { 0x01, 0x01 },  // Pointer
        { 0x0D, 0x04 },  // Touch Screen
        { 0x0D, 0x05 },  // Touch Pad
    };
    for (int i = 0; i < 6; ++i) {
        rids[i].usUsagePage = targets[i].page;
        rids[i].usUsage = targets[i].usage;
        rids[i].dwFlags = RIDEV_INPUTSINK;
        rids[i].hwndTarget = hWnd;
    }

    if (RegisterRawInputDevices(rids, 6, sizeof(RAWINPUTDEVICE))) {
        out << L"[+] Registered Raw Input sinks for Digitizer, Pen, Mouse, and Pointer.\n" << std::flush;
    } else {
        out << L"[-] Failed to register Raw Input devices: " << GetLastError() << L"\n" << std::flush;
    }

    // The HID handle must outlive the capture loop. Previously it was declared
    // inside the `if` block below, so it was destroyed before a single packet
    // could be read and only the Raw Input path ever produced output.
    HidDevice hid;
    DiscoveredDevice dev;
    if (DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        out << L"[+] Opening direct HID device: " << dev.product_name << L"\n";
        out << L"    Path: " << dev.device_path << L"\n" << std::flush;

        if (hid.Open(dev)) {
            out << L"[+] Direct HID handle opened successfully.\n" << std::flush;
            hid.SetPacketCallback([](const uint8_t* buffer, size_t length) {
                out << L"[DIRECT HID ReadFile] " << ToHex(buffer, length) << L"\n" << std::flush;
            });
            hid.StartReading();
        } else {
            out << L"[-] Failed to open direct HID handle: " << GetLastError() << L"\n" << std::flush;
        }
    } else {
        out << L"[-] No supported tablet found for direct HID capture.\n" << std::flush;
    }

    out << L"[+] Sniffer active. Move or touch your stylus on the tablet now.\n" << std::flush;
    out << L"    (Listening for " << seconds << L" seconds...)\n" << std::flush;

    const auto startTime = std::chrono::steady_clock::now();
    MSG msg;
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - startTime).count() < seconds) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    hid.Close();
    DestroyWindow(hWnd);
    out << L"[+] Sniffer session complete.\n";
}

void RunInjectionTest() {
    out << L"\n[+] Testing Windows Synthetic Pointer (Pen / Windows Ink) Injection API...\n";

    InputInjector injector;
    if (!injector.Initialize(true)) {
        out << L"[-] Failed to initialize Windows Synthetic Pointer Device.\n";
        out << L"    Ensure this program is running on Windows 8, 10, or 11.\n";
        return;
    }

    out << L"[+] Synthetic Pen Device created via CreateSyntheticPointerDevice.\n";
    out << L"[+] Injecting simulated spiral stroke with dynamic pressure...\n";

    DriverConfig config;   // defaults: tip = left click, ink enabled

    const int cx = GetSystemMetrics(SM_CXSCREEN) / 2;
    const int cy = GetSystemMetrics(SM_CYSCREEN) / 2;

    constexpr int steps = 120;
    for (int i = 0; i < steps; ++i) {
        const double angle = i * 0.15;
        const double radius = i * 1.5;
        const double pressure = static_cast<double>(i) / static_cast<double>(steps);

        TabletProcessedState state;
        state.in_proximity = true;
        state.tool = ToolType::Pen;
        state.screen_x = static_cast<int32_t>(cx + std::cos(angle) * radius);
        state.screen_y = static_cast<int32_t>(cy + std::sin(angle) * radius);
        state.raw_normalized_pressure = pressure;
        state.pressure = pressure;
        state.injection_pressure = static_cast<uint32_t>(pressure * 1024.0);
        state.is_contact = true;

        injector.Inject(state, config);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    injector.ReleaseAll();
    out << L"[+] Injection test completed. Windows Ink API is operational.\n";
}

// --- Regression suite ------------------------------------------------------

struct TestRunner {
    int passed = 0;
    int failed = 0;

    void check(bool condition, const wchar_t* name) {
        if (condition) {
            out << L"  [PASS] " << name << L"\n";
            ++passed;
        } else {
            out << L"  [FAIL] " << name << L"\n";
            ++failed;
        }
    }

    void section(const wchar_t* title) {
        out << L"\n  -- " << title << L" --\n";
    }
};

bool RunDecoderUnitTests() {
    out << L"\n[+] Running regression suite...\n";
    TestRunner t;

    // ---------------------------------------------------------------- decoder
    t.section(L"PenPartner protocol");
    {
        PacketDecoder decoder;

        // report 0x01, X=2500, Y=1800, pressure=100, in proximity
        uint8_t packet1[] = { 0x01, 0xC4, 0x09, 0x08, 0x07, 0x80,
                              static_cast<uint8_t>(100 - 127), 0x00 };
        TabletRawState s1;
        t.check(decoder.DecodePacket(packet1, sizeof(packet1), s1), L"Decode PenPartner report 0x01");
        t.check(s1.in_proximity, L"Proximity flag true");
        t.check(s1.raw_x == 2500, L"Coordinate X == 2500");
        t.check(s1.raw_y == 1800, L"Coordinate Y == 1800");
        t.check(s1.raw_pressure == 100, L"Pressure == 100");
        t.check(s1.tool == ToolType::Pen, L"Tool == Pen");

        // eraser end + barrel switch
        uint8_t packet2[] = { 0x01, 0x10, 0x05, 0x20, 0x04, 0xE0, 0x00, 0x00 };
        TabletRawState s2;
        decoder.DecodePacket(packet2, sizeof(packet2), s2);
        t.check(s2.tool == ToolType::Eraser, L"Tool == Eraser");
        t.check(s2.barrel_switch_1, L"Barrel button 1 active");

        // proximity out holds the last position instead of jumping to origin
        uint8_t packet3[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        TabletRawState s3;
        decoder.DecodePacket(packet3, sizeof(packet3), s3);
        t.check(!s3.in_proximity, L"Proximity flag false on leave");
        t.check(s3.raw_pressure == 0, L"Pressure 0 on leave");
        t.check(s3.raw_x == 0x0510, L"Position held across proximity loss");

        // report 0x02 (motion while in range)
        uint8_t packet4[] = { 0x02, 0xC4, 0x09, 0x08, 0x07, 0x00,
                              static_cast<uint8_t>(50 - 127), 0x00 };
        TabletRawState s4;
        t.check(decoder.DecodePacket(packet4, sizeof(packet4), s4), L"Decode PenPartner report 0x02");
        t.check(s4.in_proximity && s4.raw_pressure == 50, L"Report 0x02 carries pressure");
    }

    t.section(L"Reports this driver must refuse");
    {
        PacketDecoder decoder;

        uint8_t unknown_id[] = { 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        TabletRawState s1;
        t.check(!decoder.DecodePacket(unknown_id, sizeof(unknown_id), s1),
                L"Unknown report id rejected, not reinterpreted");

        uint8_t too_short[] = { 0x01, 0x02, 0x03, 0x04 };
        TabletRawState s2;
        t.check(!decoder.DecodePacket(too_short, sizeof(too_short), s2), L"Short report rejected");

        // A Graphire-layout report happens to reuse report id 0x02. Decoding it
        // as PenPartner yields X = 34945 on a 5040-wide tablet, so the
        // plausibility guard throws it out instead of flinging the cursor.
        uint8_t graphire_shaped[] = { 0x02, 0x81, 0x88, 0x13, 0xAC, 0x0D, 0x2C, 0x01 };
        TabletRawState s3;
        t.check(!decoder.DecodePacket(graphire_shaped, sizeof(graphire_shaped), s3),
                L"Foreign report layout rejected by the plausibility guard");

        t.check(!decoder.DecodePacket(nullptr, 8, s3), L"Null buffer rejected");
    }

    t.section(L"Observed extents (auto-expand)");
    {
        PacketDecoder decoder;

        // Zero means "nothing seen yet". Seeding these with the nominal
        // capabilities made auto-expand a floor, so calibrating to a SMALLER
        // area could never take effect.
        t.check(decoder.GetObservedMaxX() == 0, L"Fresh decoder has observed X of zero");
        t.check(decoder.GetObservedMaxY() == 0, L"Fresh decoder has observed Y of zero");
        t.check(decoder.GetObservedMaxPressure() == 0, L"Fresh decoder has observed pressure of zero");

        uint8_t packet[] = { 0x01, 0xC4, 0x09, 0x08, 0x07, 0x80,
                             static_cast<uint8_t>(100 - 127), 0x00 };
        TabletRawState st;
        decoder.DecodePacket(packet, sizeof(packet), st);
        t.check(decoder.GetObservedMaxX() == 2500, L"Observed X records what was decoded");
        t.check(decoder.GetObservedMaxY() == 1800, L"Observed Y records what was decoded");
        t.check(decoder.GetObservedMaxPressure() == 100, L"Observed pressure records what was decoded");

        // Declaring capabilities must not pretend the hardware reported them.
        TabletCapabilities big;
        big.max_x = 30000;
        big.max_y = 30000;
        decoder.SetCapabilities(big);
        t.check(decoder.GetObservedMaxX() == 2500, L"SetCapabilities does not inflate observed extents");

        decoder.Reset();
        t.check(decoder.GetObservedMaxX() == 0, L"Reset forgets observed extents");
    }

    t.section(L"Device identification");
    {
        t.check(DeviceEnumerator::IdentifyModel(WACOM_VENDOR_ID, PID_PENPARTNER_CT0405U)
                    == DeviceModel::PenPartner_CT0405U, L"CT-0405-U identified as supported");
        t.check(DeviceEnumerator::IdentifyModel(WACOM_VENDOR_ID, PID_PENPARTNER_2)
                    == DeviceModel::PenPartner_CT0405U, L"PenPartner rev 2 identified as supported");
        t.check(DeviceEnumerator::IdentifyModel(WACOM_VENDOR_ID, PID_GRAPHIRE_3_4X5)
                    == DeviceModel::RecognisedUnsupported, L"Graphire 3 recognised but unsupported");
        t.check(DeviceEnumerator::IdentifyModel(WACOM_VENDOR_ID, PID_VOLITO)
                    == DeviceModel::RecognisedUnsupported, L"Volito recognised but unsupported");
        t.check(DeviceEnumerator::IdentifyModel(0x046D, 0xC52B)
                    == DeviceModel::Unknown, L"Non-Wacom vendor is Unknown");
        t.check(DeviceEnumerator::DescribeProduct(WACOM_VENDOR_ID, PID_GRAPHIRE_3_4X5)
                    == L"Wacom Graphire 3", L"Unsupported models still get a readable name");

        DiscoveredDevice unsupported;
        unsupported.vendor_id = WACOM_VENDOR_ID;
        unsupported.product_id = PID_GRAPHIRE_3_4X5;
        unsupported.model = DeviceModel::RecognisedUnsupported;
        unsupported.is_supported = false;
        unsupported.usage_page = HID_USAGE_PAGE_DIGITIZER;
        unsupported.input_report_byte_length = 8;
        t.check(DeviceEnumerator::ScoreCandidate(unsupported) < 0,
                L"Unsupported tablet is never a candidate to open");
    }

    // -------------------------------------------------------- signal processor
    t.section(L"Pressure deadzone gates contact");
    {
        SignalProcessor proc;
        DriverConfig cfg;
        cfg.enable_smoothing = false;
        cfg.pressure_min_threshold = 0.5;
        cfg.pressure_max_threshold = 1.0;

        TabletCapabilities caps;   // 5040 x 3780, pressure 255

        TabletRawState raw;
        raw.in_proximity = true;
        raw.tool = ToolType::Pen;
        raw.raw_x = 2520;
        raw.raw_y = 1890;
        raw.tip_switch = true;               // hardware switch trips on any touch
        raw.raw_pressure = 64;               // 0.25 normalized - below the deadzone
        raw.timestamp_us = GetCurrentTimestampUs();

        auto light = proc.Process(raw, cfg, caps);
        t.check(!light.is_contact, L"Light touch below deadzone does NOT register contact");
        t.check(light.pressure == 0.0, L"Light touch reports zero pressure");

        proc.Reset();
        raw.raw_pressure = 220;              // ~0.86 normalized - above the deadzone
        raw.timestamp_us = GetCurrentTimestampUs();
        auto firm = proc.Process(raw, cfg, caps);
        t.check(firm.is_contact, L"Firm touch above deadzone registers contact");
        t.check(firm.pressure > 0.0, L"Firm touch reports non-zero pressure");
        t.check(firm.raw_normalized_pressure > 0.8, L"Pre-curve pressure exposed for the UI");
    }

    t.section(L"Non-zero tablet origin");
    {
        SignalProcessor proc;
        DriverConfig cfg;
        cfg.enable_smoothing = false;
        cfg.pressure_min_threshold = 0.0;

        // Measured from a real CT-0405-U: the surface starts around 108,91 and
        // runs to about 5001,3780. Treating the origin as 0,0 shifted every
        // coordinate by the inset.
        TabletCapabilities caps;
        caps.min_x = 108;  caps.max_x = 5001;
        caps.min_y = 91;   caps.max_y = 3780;

        TabletRawState raw;
        raw.in_proximity = true;
        raw.timestamp_us = GetCurrentTimestampUs();

        raw.raw_x = 108; raw.raw_y = 91;
        auto tl = proc.Process(raw, cfg, caps);
        t.check(std::abs(tl.normalized_x) < 1e-9, L"Physical top-left maps to 0.0 in X");
        t.check(std::abs(tl.normalized_y) < 1e-9, L"Physical top-left maps to 0.0 in Y");

        proc.Reset();
        raw.raw_x = 5001; raw.raw_y = 3780;
        raw.timestamp_us = GetCurrentTimestampUs();
        auto br = proc.Process(raw, cfg, caps);
        t.check(std::abs(br.normalized_x - 1.0) < 1e-9, L"Physical bottom-right maps to 1.0 in X");
        t.check(std::abs(br.normalized_y - 1.0) < 1e-9, L"Physical bottom-right maps to 1.0 in Y");

        proc.Reset();
        raw.raw_x = (108 + 5001) / 2; raw.raw_y = (91 + 3780) / 2;
        raw.timestamp_us = GetCurrentTimestampUs();
        auto mid = proc.Process(raw, cfg, caps);
        t.check(std::abs(mid.normalized_x - 0.5) < 1e-3, L"Surface centre maps to 0.5 in X");
        t.check(std::abs(mid.normalized_y - 0.5) < 1e-3, L"Surface centre maps to 0.5 in Y");

        // A minimum at or above the maximum must be ignored, not divide by zero.
        proc.Reset();
        TabletCapabilities broken;
        broken.min_x = 9000; broken.max_x = 5000;
        broken.min_y = 9000; broken.max_y = 5000;
        raw.raw_x = 2500; raw.raw_y = 2500;
        raw.timestamp_us = GetCurrentTimestampUs();
        auto safe = proc.Process(raw, cfg, broken);
        t.check(safe.normalized_x >= 0.0 && safe.normalized_x <= 1.0, L"Inverted bounds produce a sane result");
    }

    t.section(L"Capabilities drive normalization");
    {
        SignalProcessor proc;
        DriverConfig cfg;
        cfg.enable_smoothing = false;
        cfg.pressure_min_threshold = 0.0;

        // A Graphire 3-sized tablet: the far edge must map to 1.0, not clamp
        // early the way it did when the 5040 default always won.
        TabletCapabilities caps;
        caps.max_x = 13918;
        caps.max_y = 10206;
        caps.max_pressure = 511;

        TabletRawState raw;
        raw.in_proximity = true;
        raw.raw_x = 13918;
        raw.raw_y = 10206;
        raw.timestamp_us = GetCurrentTimestampUs();

        auto s = proc.Process(raw, cfg, caps);
        t.check(std::abs(s.normalized_x - 1.0) < 1e-9, L"Far edge X normalizes to 1.0");
        t.check(std::abs(s.normalized_y - 1.0) < 1e-9, L"Far edge Y normalizes to 1.0");

        proc.Reset();
        raw.raw_x = 6959;   // exactly half
        raw.raw_y = 5103;
        raw.timestamp_us = GetCurrentTimestampUs();
        auto mid = proc.Process(raw, cfg, caps);
        t.check(std::abs(mid.normalized_x - 0.5) < 1e-3, L"Mid-tablet X normalizes to 0.5");
    }

    // ------------------------------------------------------ coordinate mapper
    t.section(L"Coordinate mapping round-trip");
    {
        CoordinateMapper mapper;
        DriverConfig cfg;
        cfg.mapping_mode = MappingMode::CustomArea;
        cfg.custom_screen_rect = RECT{ 100, 200, 1380, 1160 };   // 1280x960, 4:3
        cfg.lock_aspect_ratio = false;

        TabletCapabilities caps;   // 4:3

        int32_t sx = 0, sy = 0;
        mapper.MapToScreen(0.5, 0.5, cfg, caps, sx, sy);
        t.check(sx == 740 && sy == 680, L"Centre maps to the centre of the target rect");

        mapper.MapToScreen(0.0, 0.0, cfg, caps, sx, sy);
        t.check(sx == 100 && sy == 200, L"Origin maps to the top-left of the target rect");

        double nx = 0.0, ny = 0.0;
        mapper.ScreenToNormalized(740, 680, cfg, caps, nx, ny);
        t.check(std::abs(nx - 0.5) < 1e-3 && std::abs(ny - 0.5) < 1e-3,
                L"ScreenToNormalized inverts MapToScreen");

        // With the active area cropped, the inverse must still round-trip.
        cfg.tablet_area_left = 0.25;
        cfg.tablet_area_right = 0.75;
        mapper.MapToScreen(0.5, 0.5, cfg, caps, sx, sy);
        mapper.ScreenToNormalized(sx, sy, cfg, caps, nx, ny);
        t.check(std::abs(nx - 0.5) < 1e-2, L"Round-trip holds with a cropped active area");
    }

    // ---------------------------------------------------------- config
    t.section(L"Configuration round-trip");
    {
        DriverConfig original;
        original.barrel_1_action = ButtonAction::Undo;
        original.barrel_2_action = ButtonAction::PanScroll;
        original.tip_action = ButtonAction::MiddleClick;
        original.curve_type = PressureCurveType::CustomBezier;
        original.mapping_mode = MappingMode::SpecificMonitor;
        original.target_monitor_index = 2;
        original.bounds_source = BoundsSource::UserSet;
        original.tablet_max_x = 13918;
        original.tablet_max_y = 10206;
        original.tablet_min_x = 108;
        original.tablet_min_y = 91;
        original.pressure_min_threshold = 0.125;
        original.filter_beta = 0.0075;
        original.minimize_to_tray = false;

        const std::string json = ConfigManager::SerializeToJson(original);
        const DriverConfig loaded = ConfigManager::DeserializeFromJson(json);

        t.check(loaded.barrel_1_action == ButtonAction::Undo, L"Barrel 1 action survives a save/load");
        t.check(loaded.barrel_2_action == ButtonAction::PanScroll, L"Barrel 2 action survives a save/load");
        t.check(loaded.tip_action == ButtonAction::MiddleClick, L"Tip action survives a save/load");
        t.check(loaded.curve_type == PressureCurveType::CustomBezier, L"Curve type survives a save/load");
        t.check(loaded.mapping_mode == MappingMode::SpecificMonitor, L"Mapping mode survives a save/load");
        t.check(loaded.target_monitor_index == 2, L"Monitor index survives a save/load");
        t.check(loaded.bounds_source == BoundsSource::UserSet, L"Bounds source survives a save/load");
        t.check(loaded.tablet_max_x == 13918 && loaded.tablet_max_y == 10206, L"Tablet bounds survive a save/load");
        t.check(loaded.tablet_min_x == 108 && loaded.tablet_min_y == 91, L"Tablet origin survives a save/load");
        t.check(std::abs(loaded.pressure_min_threshold - 0.125) < 1e-9, L"Deadzone survives a save/load");
        t.check(std::abs(loaded.filter_beta - 0.0075) < 1e-9, L"Filter beta survives a save/load");
        t.check(loaded.minimize_to_tray == false, L"Close-to-tray survives a save/load");

        // Out-of-range values in a hand-edited file must not produce a bad enum.
        const std::string corrupt =
            "{ \"curve_type\": 99, \"mapping_mode\": -4, \"barrel_1_action\": 250, "
            "\"tablet_max_x\": 3, \"pressure_min_threshold\": 7.5 }";
        const DriverConfig sane = ConfigManager::DeserializeFromJson(corrupt);
        t.check(static_cast<int>(sane.curve_type) >= 0 &&
                sane.curve_type <= PressureCurveType::CustomBezier, L"Out-of-range curve clamped");
        t.check(static_cast<int>(sane.mapping_mode) >= 0 &&
                sane.mapping_mode <= MappingMode::CustomArea, L"Out-of-range mapping mode clamped");
        t.check(sane.barrel_1_action >= ButtonAction::Default &&
                sane.barrel_1_action <= ButtonAction::Disabled, L"Out-of-range button action clamped");
        t.check(sane.tablet_max_x >= MIN_SANE_TABLET_BOUND, L"Absurd tablet bound rejected");
        t.check(sane.pressure_min_threshold <= 1.0, L"Out-of-range deadzone clamped");
    }

    t.section(L"Pressure curves");
    {
        DriverConfig cfg;
        cfg.curve_type = PressureCurveType::Linear;
        t.check(std::abs(SignalProcessor::ApplyPressureCurve(0.5, cfg) - 0.5) < 1e-9, L"Linear curve is identity");

        cfg.curve_type = PressureCurveType::Soft;
        t.check(SignalProcessor::ApplyPressureCurve(0.5, cfg) > 0.5, L"Soft curve lifts mid-range");

        cfg.curve_type = PressureCurveType::Hard;
        t.check(SignalProcessor::ApplyPressureCurve(0.5, cfg) < 0.5, L"Hard curve lowers mid-range");

        for (auto type : { PressureCurveType::Linear, PressureCurveType::Soft, PressureCurveType::VerySoft,
                           PressureCurveType::Firm, PressureCurveType::Hard, PressureCurveType::CustomBezier }) {
            cfg.curve_type = type;
            const double lo = SignalProcessor::ApplyPressureCurve(0.0, cfg);
            const double hi = SignalProcessor::ApplyPressureCurve(1.0, cfg);
            if (std::abs(lo) > 1e-9 || std::abs(hi - 1.0) > 1e-9) {
                t.check(false, L"Every curve maps 0->0 and 1->1");
                break;
            }
        }
        t.check(true, L"Every curve maps 0->0 and 1->1");
    }

    out << L"\n[+] Tests complete: " << t.passed << L" passed, " << t.failed << L" failed.\n";
    return t.failed == 0;
}

// Read-only mapping trace. Opens the tablet, decodes and maps exactly as the
// driver would, and prints what it sees - but never injects, so the cursor
// stays put while you touch the corners.
void RunTrace(int seconds) {
    const DriverConfig cfg = ConfigManager::LoadConfig();

    out << L"\n=== Configuration in effect ===\n";
    out << L"  Config file:      " << ConfigManager::GetConfigPath() << L"\n";
    out << L"  Portable mode:    " << (ConfigManager::IsPortableMode()
                                       ? L"ON (portable.txt present)" : L"off") << L"\n";
    out << L"  tablet range:     X " << cfg.tablet_min_x << L".." << cfg.tablet_max_x
        << L"   Y " << cfg.tablet_min_y << L".." << cfg.tablet_max_y << L"\n";
    out << L"  bounds_source:    " << static_cast<int>(cfg.bounds_source)
        << (cfg.bounds_source == BoundsSource::UserSet ? L"  (UserSet)"
            : cfg.bounds_source == BoundsSource::Detected ? L"  (Detected)" : L"  (Default)") << L"\n";
    out << L"  auto_detect:      " << (cfg.auto_detect_bounds ? L"ON" : L"off") << L"\n";
    out << L"  mapping_mode:     " << static_cast<int>(cfg.mapping_mode) << L"\n";
    out << L"  lock_aspect:      " << (cfg.lock_aspect_ratio ? L"ON" : L"off") << L"\n";
    out << L"  active area:      " << cfg.tablet_area_left << L"," << cfg.tablet_area_top
        << L" -> " << cfg.tablet_area_right << L"," << cfg.tablet_area_bottom << L"\n";
    out << L"  custom rect:      " << cfg.custom_screen_rect.left << L"," << cfg.custom_screen_rect.top
        << L" -> " << cfg.custom_screen_rect.right << L"," << cfg.custom_screen_rect.bottom << L"\n";

    CoordinateMapper mapper;
    const RECT vd = mapper.GetVirtualDesktopBounds();
    const RECT target = mapper.GetTargetScreenBounds(cfg);
    out << L"  virtual desktop:  " << vd.left << L"," << vd.top << L" -> " << vd.right << L"," << vd.bottom << L"\n";
    out << L"  MAPS ONTO:        " << target.left << L"," << target.top << L" -> "
        << target.right << L"," << target.bottom
        << L"   (" << (target.right - target.left) << L" x " << (target.bottom - target.top) << L")\n";

    PacketDecoder decoder;
    decoder.SetAutoDetectBounds(cfg.auto_detect_bounds);

    // Exactly what SignalProcessor will divide by.
    TabletCapabilities caps = decoder.GetCapabilities();
    if (cfg.bounds_source == BoundsSource::UserSet) {
        if (cfg.tablet_max_x >= MIN_SANE_TABLET_BOUND) caps.max_x = cfg.tablet_max_x;
        if (cfg.tablet_max_y >= MIN_SANE_TABLET_BOUND) caps.max_y = cfg.tablet_max_y;
        if (cfg.tablet_min_x < caps.max_x)             caps.min_x = cfg.tablet_min_x;
        if (cfg.tablet_min_y < caps.max_y)             caps.min_y = cfg.tablet_min_y;
    }
    if (cfg.auto_detect_bounds && cfg.bounds_source != BoundsSource::UserSet) {
        const uint32_t seen_x = decoder.GetObservedMaxX();
        const uint32_t seen_y = decoder.GetObservedMaxY();
        if (seen_x > 0) caps.max_x = std::max(caps.max_x, seen_x);
        if (seen_y > 0) caps.max_y = std::max(caps.max_y, seen_y);
    }
    out << L"  EFFECTIVE BOUNDS: X " << caps.min_x << L".." << caps.max_x
        << L"   Y " << caps.min_y << L".." << caps.max_y;
    if (caps.max_x != cfg.tablet_max_x || caps.max_y != cfg.tablet_max_y) {
        out << L"   <-- differs from tablet_max above";
    }
    out << L"\n\n";

    DiscoveredDevice dev;
    if (!DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        out << L"[-] No supported tablet found.\n";
        return;
    }

    HidDevice hid;
    if (!hid.Open(dev)) {
        out << L"[-] Could not open " << dev.product_name << L": " << GetLastError() << L"\n";
        return;
    }

    SignalProcessor processor;
    DriverConfig trace_cfg = cfg;
    trace_cfg.enable_smoothing = false;   // raw truth, no filter lag

    uint32_t min_x = 0xFFFFFFFF, min_y = 0xFFFFFFFF, max_x = 0, max_y = 0;
    int32_t max_p = 0;
    int taps = 0;
    bool prev_contact = false;
    auto last_print = std::chrono::steady_clock::now();

    hid.SetPacketCallback([&](const uint8_t* buf, size_t len) {
        TabletRawState raw;
        if (!decoder.DecodePacket(buf, len, raw)) {
            out << L"[undecoded] " << ToHex(buf, len) << L"\n" << std::flush;
            return;
        }
        if (!raw.in_proximity) { prev_contact = false; return; }

        min_x = std::min(min_x, raw.raw_x);  max_x = std::max(max_x, raw.raw_x);
        min_y = std::min(min_y, raw.raw_y);  max_y = std::max(max_y, raw.raw_y);
        max_p = std::max(max_p, raw.raw_pressure);

        auto processed = processor.Process(raw, trace_cfg, caps);
        int32_t sx = 0, sy = 0;
        mapper.MapToScreen(processed.normalized_x, processed.normalized_y, trace_cfg, caps, sx, sy);

        const bool is_tap = processed.is_contact && !prev_contact;
        const auto now = std::chrono::steady_clock::now();
        const bool due = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print).count() > 120;
        prev_contact = processed.is_contact;
        if (!is_tap && !due) return;
        last_print = now;

        out << (is_tap ? L">> TAP  " : L"        ")
            << L"raw(" << std::setw(5) << raw.raw_x << L"," << std::setw(5) << raw.raw_y << L")"
            << L"  norm(" << std::fixed << std::setprecision(3) << processed.normalized_x
            << L"," << processed.normalized_y << L")"
            << L"  screen(" << std::setw(5) << sx << L"," << std::setw(5) << sy << L")"
            << L"  p=" << std::setw(3) << raw.raw_pressure
            << L"\n" << std::flush;
        if (is_tap) ++taps;
    });

    hid.StartReading();
    out << L"[+] Tracing for " << seconds << L" seconds - NOTHING IS INJECTED, your cursor will not move.\n";
    out << L"    Sweep the pen right around the edge of the drawing area, into all\n";
    out << L"    four corners. Sliding finds the true extremes; tapping does not.\n\n";

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start).count() < seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    hid.Close();

    out << L"\n=== Observed over " << taps << L" taps ===\n";
    if (max_x == 0 && max_y == 0) {
        out << L"  No pen data seen. Was the pen in range?\n";
        return;
    }
    out << L"  raw X range:      " << min_x << L" .. " << max_x << L"\n";
    out << L"  raw Y range:      " << min_y << L" .. " << max_y << L"\n";
    out << L"  peak pressure:    " << max_p << L"\n";
    out << L"  bounds used:      " << caps.max_x << L" x " << caps.max_y << L"\n";
    const double cover_x = caps.max_x ? (100.0 * max_x / caps.max_x) : 0.0;
    const double cover_y = caps.max_y ? (100.0 * max_y / caps.max_y) : 0.0;
    out << L"  corner reaches:   " << std::fixed << std::setprecision(1)
        << cover_x << L"% of X, " << cover_y << L"% of Y\n";
    if (cover_x > 100.5 || cover_y > 100.5) {
        out << L"  ^ the pen runs PAST the configured bounds, so the edges clamp;\n";
        out << L"    bounds are set SMALLER than the tablet actually reports.\n";
    } else if (cover_x < 97.0 || cover_y < 97.0) {
        out << L"  ^ the far corner does not reach the edge of the mapped area;\n";
        out << L"    bounds are set LARGER than the tablet reached in this sweep.\n";
    }
    out << L"  suggested range:  X " << min_x << L".." << max_x
        << L"   Y " << min_y << L".." << max_y << L"\n";
}

// Low-level read probe. Deliberately bypasses HidDevice and talks to Win32
// directly, narrating every step, so a silent "no data" can be told apart from
// a read path that is actually broken.
void RunProbe(int seconds) {
    DiscoveredDevice dev;
    if (!DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        out << L"[-] No supported tablet found.\n";
        return;
    }

    out << L"\n=== Device ===\n";
    out << L"  " << dev.product_name << L"  VID 0x" << std::hex << dev.vendor_id
        << L" PID 0x" << dev.product_id << std::dec << L"\n";
    out << L"  " << dev.device_path << L"\n";

    out << L"\n=== Opening ===\n";
    HANDLE h = CreateFileW(dev.device_path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        out << L"  GENERIC_READ failed, error " << GetLastError() << L"\n";
        h = CreateFileW(dev.device_path.c_str(), 0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            out << L"  zero-access open ALSO failed, error " << GetLastError() << L"\n";
            return;
        }
        out << L"  opened with NO read access - ReadFile can never deliver data.\n";
    } else {
        out << L"  opened GENERIC_READ | FILE_FLAG_OVERLAPPED  OK\n";
    }

    DWORD report_len = 0;
    PHIDP_PREPARSED_DATA pre = nullptr;
    if (HidD_GetPreparsedData(h, &pre)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS) {
            report_len = caps.InputReportByteLength;
            out << L"  UsagePage 0x" << std::hex << caps.UsagePage
                << L"  Usage 0x" << caps.Usage << std::dec << L"\n";
            out << L"  InputReportByteLength   = " << caps.InputReportByteLength << L"\n";
            out << L"  OutputReportByteLength  = " << caps.OutputReportByteLength << L"\n";
            out << L"  FeatureReportByteLength = " << caps.FeatureReportByteLength << L"\n";
        }
        HidD_FreePreparsedData(pre);
    } else {
        out << L"  HidD_GetPreparsedData failed, error " << GetLastError() << L"\n";
    }

    if (report_len == 0) report_len = 8;

    // HID requires the read buffer to match the input report length exactly on
    // some stacks; over-sizing it is a common cause of silent read failure.
    out << L"\n=== Reading (buffer = exactly " << report_len << L" bytes) ===\n";
    out << L"  Move or tap the pen. Heartbeats print every second either way.\n\n";

    std::vector<uint8_t> buf(report_len, 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    const auto start = std::chrono::steady_clock::now();
    int reads_issued = 0, packets = 0, pend_count = 0;
    bool io_pending = false;

    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start).count() < seconds) {

        if (!io_pending) {
            ResetEvent(ov.hEvent);
            DWORD got = 0;
            ++reads_issued;
            if (ReadFile(h, buf.data(), report_len, &got, &ov)) {
                out << L"  [immediate] " << got << L" bytes: " << ToHex(buf.data(), got) << L"\n" << std::flush;
                ++packets;
                continue;
            }
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                io_pending = true;
                ++pend_count;
            } else {
                out << L"  [ReadFile FAILED] error " << err;
                if (err == ERROR_INVALID_USER_BUFFER || err == 1784) out << L"  (buffer size wrong for this HID report)";
                if (err == ERROR_ACCESS_DENIED) out << L"  (no read access)";
                if (err == ERROR_DEVICE_NOT_CONNECTED) out << L"  (device gone)";
                out << L"\n" << std::flush;
                break;
            }
        }

        const DWORD w = WaitForSingleObject(ov.hEvent, 1000);
        if (w == WAIT_OBJECT_0) {
            DWORD got = 0;
            if (GetOverlappedResult(h, &ov, &got, FALSE)) {
                io_pending = false;
                ++packets;
                out << L"  [packet " << packets << L"] " << got << L" bytes: "
                    << ToHex(buf.data(), got) << L"\n" << std::flush;
            } else {
                const DWORD err = GetLastError();
                out << L"  [GetOverlappedResult FAILED] error " << err << L"\n" << std::flush;
                io_pending = false;
                if (err != ERROR_OPERATION_ABORTED) break;
            }
        } else if (w == WAIT_TIMEOUT) {
            const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            out << L"  ... waiting  (" << secs << L"s, reads issued=" << reads_issued
                << L", pending=" << (io_pending ? L"yes" : L"no")
                << L", packets=" << packets << L")\n" << std::flush;
        } else {
            out << L"  [wait failed] " << GetLastError() << L"\n" << std::flush;
            break;
        }
    }

    CancelIoEx(h, &ov);
    DWORD drained = 0;
    GetOverlappedResult(h, &ov, &drained, TRUE);
    CloseHandle(ov.hEvent);
    CloseHandle(h);

    out << L"\n=== Result ===\n";
    out << L"  reads issued : " << reads_issued << L"\n";
    out << L"  packets read : " << packets << L"\n";
    if (packets == 0) {
        out << L"\n  No packets. If the heartbeats above show the read staying PENDING,\n";
        out << L"  the handle is healthy and the device simply sent nothing - meaning the\n";
        out << L"  pen was not in range. If instead ReadFile failed, the error is printed.\n";
    }
}

void RunHeadless() {
    out << L"[+] Starting Wacom CT-0405-U Driver in headless background mode...\n";
    TabletDriver driver;

    driver.SetConnectionCallback([](bool connected, const std::wstring& name) {
        out << (connected ? L"[+] Connected: " : L"[-] Disconnected: ") << name << L"\n" << std::flush;
    });

    driver.Start();

    out << L"[+] Driver running. Press Enter to stop.\n" << std::flush;
    std::wcin.get();

    driver.Stop();
    out << L"[+] Driver stopped.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 0;
    }

    const std::string arg = argv[1];

    if (arg == "--diagnose" || arg == "-d") {
        RunDiagnose();
    } else if (arg == "--dump-packets" || arg == "-p") {
        int seconds = 10;
        if (argc >= 3) {
            try {
                seconds = std::clamp(std::stoi(argv[2]), 1, 600);
            } catch (...) {}
        }
        RunPacketDumper(seconds);
    } else if (arg == "--trace" || arg == "-r") {
        int seconds = 30;
        if (argc >= 3) {
            try { seconds = std::clamp(std::stoi(argv[2]), 1, 600); } catch (...) {}
        }
        RunTrace(seconds);
    } else if (arg == "--probe" || arg == "-b") {
        int seconds = 20;
        if (argc >= 3) {
            try { seconds = std::clamp(std::stoi(argv[2]), 1, 600); } catch (...) {}
        }
        RunProbe(seconds);
    } else if (arg == "--test-injection" || arg == "-t") {
        RunInjectionTest();
    } else if (arg == "--test-decoder" || arg == "-u") {
        return RunDecoderUnitTests() ? 0 : 1;
    } else if (arg == "--headless" || arg == "-s") {
        RunHeadless();
    } else {
        PrintUsage();
    }

    return 0;
}
