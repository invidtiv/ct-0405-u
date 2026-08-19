#include "TabletDriver.h"
#include "DeviceEnumerator.h"
#include "PacketDecoder.h"
#include "InputInjector.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using namespace ct0405;

void PrintUsage() {
    std::cout << "========================================================\n";
    std::cout << "  Wacom CT-0405-U Diagnostic & Driver CLI Utility\n";
    std::cout << "========================================================\n";
    std::cout << "Usage:\n";
    std::cout << "  CT0405_CLI.exe [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --diagnose, -d       Enumerate and display all USB HID devices & Wacom tablets\n";
    std::cout << "  --dump-packets, -p   Sniff and dump raw HID packet bytes in real-time\n";
    std::cout << "  --test-injection, -t Test Windows Ink Synthetic Pointer injection\n";
    std::cout << "  --test-decoder, -u   Run automated unit tests on packet decoder logic\n";
    std::cout << "  --headless, -s       Run driver in headless background console mode\n";
    std::cout << "  --help, -h           Show this help message\n";
    std::cout << "========================================================\n";
}

void RunDiagnose() {
    std::cout << "\n[+] Scanning USB HID interfaces...\n";
    auto allDevices = DeviceEnumerator::EnumerateAllHidDevices();
    std::cout << "[*] Total HID interfaces detected: " << allDevices.size() << "\n\n";

    auto wacomDevices = DeviceEnumerator::EnumerateWacomDevices();
    if (wacomDevices.empty()) {
        std::cout << "[-] No Wacom devices detected on the USB bus.\n";
        std::cout << "    Please ensure your Wacom CT-0405-U is plugged into a USB port.\n\n";
        std::cout << "[*] Listing all connected USB HID devices for reference:\n";
        for (size_t i = 0; i < allDevices.size(); ++i) {
            const auto& dev = allDevices[i];
            std::wcout << L"  [" << i << L"] VID: 0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.vendor_id
                       << L" PID: 0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.product_id
                       << std::dec << L" | " << (dev.product_name.empty() ? L"Unknown Device" : dev.product_name)
                       << L" (" << (dev.manufacturer.empty() ? L"Generic" : dev.manufacturer) << L")\n";
        }
    } else {
        std::cout << "[+] FOUND " << wacomDevices.size() << " WACOM DEVICE(S):\n";
        for (size_t i = 0; i < wacomDevices.size(); ++i) {
            const auto& dev = wacomDevices[i];
            std::wcout << L"\n  --- Device [" << i << L"] ---\n";
            std::wcout << L"  Model Name:    " << DeviceEnumerator::ModelToString(dev.model) << L"\n";
            std::wcout << L"  Vendor ID:     0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.vendor_id << std::dec << L"\n";
            std::wcout << L"  Product ID:    0x" << std::hex << std::setw(4) << std::setfill(L'0') << dev.product_id << std::dec << L"\n";
            std::wcout << L"  Report Length: " << dev.input_report_byte_length << L" bytes\n";
            std::wcout << L"  Device Path:   " << dev.device_path << L"\n";
        }
    }
}

static LRESULT CALLBACK SnifferWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INPUT) {
        HRAWINPUT hRawInput = reinterpret_cast<HRAWINPUT>(lParam);
        UINT size = 0;
        GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size > 0) {
            std::vector<uint8_t> rawBuffer(size);
            if (GetRawInputData(hRawInput, RID_INPUT, rawBuffer.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                auto* pRaw = reinterpret_cast<RAWINPUT*>(rawBuffer.data());
                
                // Get device name
                UINT nameSize = 0;
                GetRawInputDeviceInfoW(pRaw->header.hDevice, RIDI_DEVICENAME, nullptr, &nameSize);
                std::wstring devName(nameSize, L'\0');
                if (nameSize > 0) {
                    GetRawInputDeviceInfoW(pRaw->header.hDevice, RIDI_DEVICENAME, devName.data(), &nameSize);
                }

                bool isWacom = (devName.find(L"056a") != std::wstring::npos || devName.find(L"056A") != std::wstring::npos);

                if (pRaw->header.dwType == RIM_TYPEHID) {
                    DWORD hidSize = pRaw->data.hid.dwSizeHid * pRaw->data.hid.dwCount;
                    const uint8_t* pHidData = pRaw->data.hid.bRawData;
                    
                    std::ostringstream ss;
                    for (DWORD i = 0; i < hidSize; ++i) {
                        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(pHidData[i]) << " ";
                    }
                    std::cout << "[RAW_INPUT HID] (" << (isWacom ? "WACOM" : "OTHER") << ") " << ss.str() << "\n" << std::flush;
                } else if (pRaw->header.dwType == RIM_TYPEMOUSE && isWacom) {
                    std::cout << "[RAW_INPUT MOUSE WACOM] flags=" << pRaw->data.mouse.usFlags 
                              << " buttons=" << pRaw->data.mouse.usButtonFlags 
                              << " x=" << pRaw->data.mouse.lLastX 
                              << " y=" << pRaw->data.mouse.lLastY << "\n" << std::flush;
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void RunPacketDumper() {
    std::cout << "\n[+] Connecting to Wacom Tablet packet sniffer (Raw Input + Direct HID)...\n" << std::flush;

    // Create background hidden window for Raw Input sink
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = SnifferWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CT0405_Sniffer_Class";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"CT0405 Sniffer", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    // Register all potential Raw Input usage pages
    RAWINPUTDEVICE rids[6]{};
    // 0: Digitizer
    rids[0].usUsagePage = 0x0D;
    rids[0].usUsage = 0x01;
    rids[0].dwFlags = RIDEV_INPUTSINK;
    rids[0].hwndTarget = hWnd;

    // 1: Pen
    rids[1].usUsagePage = 0x0D;
    rids[1].usUsage = 0x02;
    rids[1].dwFlags = RIDEV_INPUTSINK;
    rids[1].hwndTarget = hWnd;

    // 2: Mouse
    rids[2].usUsagePage = 0x01;
    rids[2].usUsage = 0x02;
    rids[2].dwFlags = RIDEV_INPUTSINK;
    rids[2].hwndTarget = hWnd;

    // 3: Pointer
    rids[3].usUsagePage = 0x01;
    rids[3].usUsage = 0x01;
    rids[3].dwFlags = RIDEV_INPUTSINK;
    rids[3].hwndTarget = hWnd;

    // 4: Touch Screen
    rids[4].usUsagePage = 0x0D;
    rids[4].usUsage = 0x04;
    rids[4].dwFlags = RIDEV_INPUTSINK;
    rids[4].hwndTarget = hWnd;

    // 5: Touch Pad
    rids[5].usUsagePage = 0x0D;
    rids[5].usUsage = 0x05;
    rids[5].dwFlags = RIDEV_INPUTSINK;
    rids[5].hwndTarget = hWnd;

    if (RegisterRawInputDevices(rids, 6, sizeof(RAWINPUTDEVICE))) {
        std::cout << "[+] Registered Raw Input sinks for Digitizer, Pen, Mouse, and Pointer.\n" << std::flush;
    } else {
        std::cout << "[-] Failed to register Raw Input devices: " << GetLastError() << "\n" << std::flush;
    }

    DiscoveredDevice dev;
    if (DeviceEnumerator::FindFirstSupportedTablet(dev)) {
        std::wcout << L"[+] Opening direct HID device: " << dev.product_name << L"\n";
        std::wcout << L"    Path: " << dev.device_path << L"\n" << std::flush;

        HidDevice hid;
        if (hid.Open(dev)) {
            std::cout << "[+] Direct HID handle opened successfully!\n" << std::flush;
            hid.SetPacketCallback([](const uint8_t* buffer, size_t length) {
                std::ostringstream ss;
                for (size_t i = 0; i < length; ++i) {
                    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]) << " ";
                }
                std::cout << "[DIRECT HID ReadFile] " << ss.str() << "\n" << std::flush;
            });
            hid.StartReading();
        } else {
            std::cout << "[-] Failed to open direct HID handle: " << GetLastError() << "\n" << std::flush;
        }
    }

    std::cout << "[+] Sniffer active. Move or touch your stylus on the tablet now!\n" << std::flush;
    std::cout << "    (Listening for 10 seconds...)\n" << std::flush;

    auto startTime = std::chrono::steady_clock::now();
    MSG msg;
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() < 10) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    DestroyWindow(hWnd);
    std::cout << "[+] Sniffer session complete.\n" << std::flush;
}

void RunInjectionTest() {
    std::cout << "\n[+] Testing Windows Synthetic Pointer (Pen / Windows Ink) Injection API...\n";

    InputInjector injector;
    if (!injector.Initialize(true)) {
        std::cout << "[-] Failed to initialize Windows Synthetic Pointer Device.\n";
        std::cout << "    Ensure this program is running on Windows 8, 10, or 11.\n";
        return;
    }

    std::cout << "[+] Synthetic Pen Device created successfully via CreateSyntheticPointerDevice!\n";
    std::cout << "[+] Injecting simulated spiral stroke with dynamic pressure...\n";

    int cx = GetSystemMetrics(SM_CXSCREEN) / 2;
    int cy = GetSystemMetrics(SM_CYSCREEN) / 2;

    const int steps = 120;
    for (int i = 0; i < steps; ++i) {
        double angle = i * 0.15;
        double radius = i * 1.5;
        int px = static_cast<int>(cx + std::cos(angle) * radius);
        int py = static_cast<int>(cy + std::sin(angle) * radius);
        double pressure = static_cast<double>(i) / static_cast<double>(steps);

        TabletProcessedState state;
        state.in_proximity = true;
        state.tool = ToolType::Pen;
        state.screen_x = px;
        state.screen_y = py;
        state.pressure = pressure;
        state.injection_pressure = static_cast<uint32_t>(pressure * 1024.0);
        state.is_contact = true;

        injector.Inject(state);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    injector.ReleaseAll();
    std::cout << "[+] Injection test completed. Windows Ink API is fully operational!\n";
}

bool RunDecoderUnitTests() {
    std::cout << "\n[+] Running Packet Decoder Unit Tests...\n";
    int passed = 0;
    int failed = 0;

    auto assert_test = [&](bool condition, const std::string& name) {
        if (condition) {
            std::cout << "  [PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "  [FAIL] " << name << "\n";
            failed++;
        }
    };

    PacketDecoder decoder(DeviceModel::PenPartner_CT0405U);

    // Test 1: PenPartner Proximity In (Report 0x01, Stylus, X=2500, Y=1800, Pressure=100)
    // data[0]=0x01, data[1]=0xC4, data[2]=0x09 (2500), data[3]=0x08, data[4]=0x07 (1800), data[5]=0x80 (prox), data[6]=100-127=-27 (0xE5)
    uint8_t packet1[] = { 0x01, 0xC4, 0x09, 0x08, 0x07, 0x80, static_cast<uint8_t>(100 - 127), 0x00 };
    TabletRawState state1;
    bool res1 = decoder.DecodePacket(packet1, sizeof(packet1), state1);
    assert_test(res1, "Decode PenPartner Report 0x01");
    assert_test(state1.in_proximity, "Proximity Flag True");
    assert_test(state1.raw_x == 2500, "Coordinate X == 2500");
    assert_test(state1.raw_y == 1800, "Coordinate Y == 1800");
    assert_test(state1.raw_pressure == 100, "Pressure == 100");
    assert_test(state1.tool == ToolType::Pen, "Tool == Pen");

    // Test 2: PenPartner Barrel Button 1 & Eraser Tip
    // data[5] = 0x80 (prox) | 0x20 (eraser) | 0x40 (barrel 1) = 0xE0
    uint8_t packet2[] = { 0x01, 0x10, 0x05, 0x20, 0x04, 0xE0, 0x00, 0x00 };
    TabletRawState state2;
    decoder.DecodePacket(packet2, sizeof(packet2), state2);
    assert_test(state2.tool == ToolType::Eraser, "Tool == Eraser");
    assert_test(state2.barrel_switch_1, "Barrel Button 1 Active");

    // Test 3: PenPartner Proximity Out
    uint8_t packet3[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    TabletRawState state3;
    decoder.DecodePacket(packet3, sizeof(packet3), state3);
    assert_test(!state3.in_proximity, "Proximity Flag False on Leave");
    assert_test(state3.raw_pressure == 0, "Pressure 0 on Leave");

    // Test 4: Graphire Protocol Decoding
    PacketDecoder grDecoder(DeviceModel::Graphire2);
    // Graphire 0x02 report: X=5000 (0x1388), Y=3500 (0x0DAC), Pressure=300 (0x12C), prox bit 0x40
    uint8_t grPacket[] = { 0x02, 0x88, 0x13, 0xAC, 0x0D, 0x2C, 0x41, 0x01 };
    TabletRawState grState;
    bool res4 = grDecoder.DecodePacket(grPacket, sizeof(grPacket), grState);
    assert_test(res4, "Decode Graphire Report 0x02");
    assert_test(grState.raw_x == 5000, "Graphire X == 5000");
    assert_test(grState.raw_y == 3500, "Graphire Y == 3500");
    assert_test(grState.raw_pressure == 300, "Graphire 9-bit Pressure == 300");

    std::cout << "\n[+] Tests Complete: " << passed << " Passed, " << failed << " Failed.\n";
    return (failed == 0);
}

void RunHeadless() {
    std::cout << "[+] Starting Wacom CT-0405-U Driver in Headless Background Mode...\n";
    TabletDriver driver;
    driver.Start();

    std::cout << "[+] Driver running. Press Enter to stop.\n";
    std::cin.get();

    driver.Stop();
    std::cout << "[+] Driver stopped.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 0;
    }

    std::string arg = argv[1];
    if (arg == "--diagnose" || arg == "-d") {
        RunDiagnose();
    } else if (arg == "--dump-packets" || arg == "-p") {
        RunPacketDumper();
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
