# Wacom CT-0405-U Custom Windows Driver & Control Panel

A modern, high-performance, standalone Windows driver, background service, and graphical control panel for the **Wacom CT-0405-U** (Wacom PenPartner) and legacy Graphire series USB graphics tablets on **64-bit Windows 10 and Windows 11**.

> **Engineered & Developed by Antigravity powered by Gemini 3.7 Flash** (Google DeepMind).

---

## Background & Motivation

The **Wacom CT-0405-U** (and its siblings like the Graphire ET-0405-U) are iconic, durable USB digitizer tablets. However, official support ended more than 15 years ago. On modern 64-bit Windows (Windows 10/11):
- Official legacy Wacom drivers fail to install or crash due to 64-bit kernel signature enforcement.
- They lack modern **Windows Ink API** support, preventing pressure sensitivity in modern creative and note-taking apps (Photoshop, Clip Studio Paint, Krita, OneNote, Windows Whiteboard, MS Paint).
- Modern multi-monitor and ultra-wide setups cause severe aspect-ratio distortion (circles drawn on the tablet become squished ovals on 16:9/21:9 monitors).

This project restores the tablet to full working order with sub-millisecond latency, zero external runtime dependencies, native Windows Ink pressure sensitivity, multi-monitor mapping with 1:1 aspect ratio constraint, adaptive 1-Euro jitter smoothing, on-screen mapping overlays, background tray minimization, and an interactive modern control panel.

---

## Supported Hardware

| Model Name | Model Code | USB VID | USB PID | Active Area (X x Y) | Pressure Levels |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Wacom PenPartner USB** | **CT-0405-U** | `0x056A` | `0x0000` / `0x0003` | 5040 x 3780 | 256 levels (8-bit) |
| **Wacom Graphire 1** | **CT-0405** | `0x056A` | `0x0010` | 10206 x 7422 | 512 levels (9-bit) |
| **Wacom Graphire 2** | **ET-0405-U** | `0x056A` | `0x0011` | 10206 x 7422 | 512 levels (9-bit) |
| **Wacom Graphire 3 / 4** | CTE-430 / CTE-440 | `0x056A` | `0x0013` / `0x0015` | 13918 x 10206 | 512 levels (9-bit) |
| **Wacom Volito 1 / 2** | FT-0405-U | `0x056A` | `0x0060` / `0x0061` | 5104 x 3712 | 512 levels (9-bit) |

---

## Key Features

### 1. Compact 1-Column Modern Control Panel
- **Streamlined Vertical Layout:** Clean, compact 1-column configuration dashboard with zero text collisions or dark font contrast issues.
- **Hardware Profile Card:** Shows live device state, active bounds, pointer mode, and screen mapping target.
- **Real-Time Interactive Pressure Curve:** Live visual graph preview with customizable curve equations, live contact pressure marker, and deadzone slider with numeric percentage readout.
- **Live Smoothing Cutoff Readout:** Real-time filter frequency tuning in Hertz (`1.20 Hz`).
- **Native Immersive Dark Theme:** High-contrast Segoe UI typography with custom-drawn dark controls, glowing connection pill badge, and Windows 10/11 dark title bar.

### 2. Interactive On-Screen Mapping Overlay
- Click **"Set Area Overlay..."** to display a translucent, full-screen interactive overlay across your monitors.
- **Aspect Ratio Lock:** The selection rectangle automatically locks to the exact physical proportion of your tablet (e.g. 4:3), ensuring drawings and handwriting are never stretched or distorted.
- **Target Any App Window:** Drag and resize the rectangle over any specific monitor, canvas, or application window (Photoshop, Krita, OneNote, etc.).
- **Quick Controls:** Press `Enter` or double-click to apply, `Esc` to cancel, or use arrow keys to nudge.

### 3. 2-Point Physical Corner Calibration
- Click **"Calibrate Corners"** to lock the hardware boundaries of your physical tablet surface.
- Touch the top-left corner, then the bottom-right corner of the tablet surface with the pen tip.
- The driver captures the exact hardware minimum and maximum coordinates.

### 4. User-Definable Tablet Space & Resolution
- Choose from standard presets (PenPartner 5040x3780, Graphire 10206x7422, Volito, etc.) or enter custom `Max X` and `Max Y` dimensions.
- **Auto-Expand Bounds:** Automatically expands coordinates if higher hardware values are detected.

### 5. Native Windows Ink (Synthetic Pen Pointer Injection)
- Uses Windows `CreateSyntheticPointerDevice` and `InjectSyntheticPointerInput` API (`POINTER_TYPE_PEN`) for native pressure sensitivity and eraser tool switching in all modern Windows applications.
- Mouse emulation fallback available.

### 6. Adaptive 1-Euro Smoothing Filter
- Eliminates sensor jitter when drawing fine lines or moving slowly while preserving zero lag during fast strokes.

### 7. Customizable Pressure Curves & Deadzones
- Select from Linear, Soft, Very Soft, Firm, Hard, or Custom Bézier curves with real-time visual graph preview and adjustable initial deadzone thresholds.

### 8. Background Service & System Tray
- Custom high-DPI application icon with neon-cyan tablet and stylus theme.
- **Close-to-Tray:** Clicking `[X]`, pressing `Alt+F4`, or clicking minimize hides the window to the notification area (system tray), keeping the driver actively running in the background.
- **Exit Application:** Right-click the notification tray icon and choose **"Exit Driver"** to cleanly terminate the process.

### 9. Automatic Settings Persistence
- All settings, custom resolutions, overlay rectangles, and calibration bounds are automatically saved to `%APPDATA%\CT0405_Driver\config.json`.
- Settings persist across application launches, restarts, and system reboots.

---

## Project Structure

```
ct-0405-u/
├── CMakeLists.txt              # CMake build configuration
├── build.bat                   # 1-Click build script for Visual Studio
├── config.default.json         # Default configuration settings
├── LICENSE                     # MIT License
├── include/
│   ├── Common.h                # Data structures, enums, driver config
│   ├── ConfigManager.h         # JSON config serialization & registry startup
│   ├── CoordinateMapper.h      # Display mapping, aspect ratio constraint
│   ├── DeviceEnumerator.h      # USB HID device enumeration & VID/PID detection
│   ├── HidDevice.h             # Overlapped Direct HID hardware reader
│   ├── InputInjector.h         # Windows Synthetic Pointer (Pen) injector
│   ├── PacketDecoder.h         # Multi-format Wacom protocol decoder
│   ├── SignalProcessor.h       # 1-Euro filter & pressure curve transformation
│   └── TabletDriver.h          # Core orchestrator and event pipeline
├── src/
│   ├── app/                    # GUI Control Panel Application
│   │   ├── main_gui.cpp        # WinMain entry point
│   │   ├── MainWindow.h/.cpp   # Control panel UI, layout, custom controls
│   │   ├── ScreenOverlayWindow.h/.cpp # Interactive screen mapping overlay
│   │   └── TrayIcon.h/.cpp     # System tray icon and context menu
│   ├── cli/                    # CLI Diagnostic & Utility Tool
│   │   └── main_cli.cpp        # CLI entry point, packet sniffer, unit tests
│   ├── ConfigManager.cpp
│   ├── CoordinateMapper.cpp
│   ├── DeviceEnumerator.cpp
│   ├── HidDevice.cpp
│   ├── InputInjector.cpp
│   ├── PacketDecoder.cpp
│   ├── SignalProcessor.cpp
│   └── TabletDriver.cpp
└── resources/
    ├── app.manifest            # PerMonitorV2 DPI awareness manifest
    ├── app.rc                  # Windows version information & resources
    └── icon.ico                # Custom multi-resolution application icon
```

---

## Building from Source

### Prerequisites
- **Windows 10 or Windows 11 (64-bit)**
- **Visual Studio 2022 Community** (with "Desktop development with C++" workload)
- **CMake 3.20 or newer**

### Build with CMake
```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The compiled binaries will be output to:
- `build/bin/Release/CT0405_ControlPanel.exe` (GUI Application & Driver)
- `build/bin/Release/CT0405_CLI.exe` (CLI Diagnostic Utility)

---

## Running the Application

### Graphical Control Panel
Launch the control panel:
```cmd
.\build\bin\Release\CT0405_ControlPanel.exe
```

### CLI Diagnostics & Packet Sniffing
```cmd
# Run hardware detection
.\build\bin\Release\CT0405_CLI.exe --diagnose

# Sniff and inspect live packet bytes from the tablet
.\build\bin\Release\CT0405_CLI.exe --dump-packets

# Run automated decoder unit tests
.\build\bin\Release\CT0405_CLI.exe --test-decoder

# Test Windows Ink synthetic pen pointer injection
.\build\bin\Release\CT0405_CLI.exe --test-injection
```

---

## Credits & Attribution

This driver software and control panel application was designed, architected, and developed by **Antigravity** using **Gemini 3.7 Flash** (Google DeepMind).

---

## License

This project is licensed under the [MIT License](LICENSE).
