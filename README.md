# Wacom CT-0405-U Custom Windows Driver & Control Panel

A modern, high-performance, standalone Windows driver, background service, and graphical control panel for the **Wacom CT-0405-U** (Wacom PenPartner) USB graphics tablet on **64-bit Windows 10 and Windows 11**.

> **Engineered & Developed by Antigravity powered by Gemini 3.7 Flash** (Google DeepMind).

---

## Background & Motivation

The **Wacom CT-0405-U** (Wacom PenPartner) is an iconic, durable USB digitizer tablet. However, official support ended more than 15 years ago. On modern 64-bit Windows (Windows 10/11):
- Official legacy Wacom drivers fail to install or crash due to 64-bit kernel signature enforcement.
- They lack modern **Windows Ink API** support, preventing pressure sensitivity in modern creative and note-taking apps (Photoshop, Clip Studio Paint, Krita, OneNote, Windows Whiteboard, MS Paint).
- Modern multi-monitor and ultra-wide setups cause severe aspect-ratio distortion (circles drawn on the tablet become squished ovals on 16:9/21:9 monitors).

This project restores the tablet to full working order with sub-millisecond latency, zero external runtime dependencies, native Windows Ink pressure sensitivity, multi-monitor mapping with 1:1 aspect ratio constraint, adaptive 1-Euro jitter smoothing, on-screen mapping overlays, background tray minimization, and an interactive modern control panel.

---

## Supported Hardware

| Model Name | Model Code | USB VID | USB PID | Active Area (X x Y) | Pressure Levels |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Wacom PenPartner USB** | **CT-0405-U** | `0x056A` | `0x0000` / `0x0003` | 5040 x 3780 | 256 levels (8-bit) |

This driver targets **one device family and decodes one report protocol**, verified
against real CT-0405-U hardware.

### Not supported

Graphire (1/2/3/4) and Volito tablets share the same USB vendor ID but use a
**different report layout** that this driver does not decode and that has never been
tested here. They are recognised by product ID purely so the app can tell you what is
plugged in and that it will not be driven — rather than misreading their packets as
PenPartner data and flinging the cursor around. Plug one in and the control panel
names it and reports it as unsupported.

If you want to add support for one, `CT0405_CLI.exe --dump-packets` captures the raw
bytes you would need.

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

### 4. Automatic & User-Definable Tablet Space
- **Automatic detection:** the tablet's coordinate bounds are adopted on connect, so mapping is correct with no manual setup.
- Use the stock CT-0405-U bounds (5040 x 3780), or enter your own `Max X` and `Max Y`. Typing bounds or running the corner calibration marks them as user-set, so automatic detection will not overwrite them. **Reset Bounds** hands control back to detection.
- **Auto-Expand Bounds:** raises the active bounds when the hardware reports coordinates beyond the current ceiling.

### 5. Native Windows Ink (Synthetic Pen Pointer Injection)
- Uses Windows `CreateSyntheticPointerDevice` and `InjectSyntheticPointerInput` API (`POINTER_TYPE_PEN`) for native pressure sensitivity and eraser tool switching in all modern Windows applications.
- Mouse emulation fallback available.

### 6. Adaptive 1-Euro Smoothing Filter
- Eliminates sensor jitter when drawing fine lines or moving slowly while preserving zero lag during fast strokes.

### 7. Customizable Pressure Curves & Deadzones
- Select from Linear, Soft, Very Soft, Firm, Hard, or Custom Bézier curves with a real-time graph preview. The live marker plots the actual operating point: pressure going in on the X axis, curve output on the Y axis.
- The **deadzone** suppresses the click as well as the pressure value, so light resting contact does not register as a stroke.

### 7b. Configurable Pen Tip & Barrel Buttons
- The pen tip and both barrel buttons can each be set to: Default (native pen barrel), Left/Right/Middle Click, Eraser Toggle, Undo (Ctrl+Z), Redo (Ctrl+Y), Pan / Scroll Drag, or Disabled.
- Assignments apply in both Windows Ink and mouse-emulation modes.

### 8. Background Service & System Tray
- Custom high-DPI application icon with neon-cyan tablet and stylus theme.
- **Close to Tray** (checkbox, on by default): clicking `[X]`, pressing `Alt+F4`, or minimizing hides the window to the notification area and keeps the driver running. Turn it **off** and the close button exits the application normally.
- **Exit Application:** right-click the notification tray icon and choose **"Exit Driver"** to cleanly terminate the process.
- **Multi-monitor aware:** the driver re-reads the display layout when monitors are added, removed, or rearranged.

### 9. Automatic Settings Persistence
- All settings, custom resolutions, overlay rectangles, and calibration bounds are saved to `%APPDATA%\CT0405_Driver\config.json`.
- **Portable mode:** place a `config.json` next to the executable and that file is used instead. The location is resolved from the executable's own directory, so it does not change when the app is started by Windows autostart.
- Settings are applied to the running driver immediately and written to disk shortly after you stop adjusting them, so dragging a slider does not thrash the file.
- Writes are atomic (write-then-rename); a failed save is reported rather than silently discarded.

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
# Enumerate HID collections and show how the driver ranks tablet candidates
.\build\bin\Release\CT0405_CLI.exe --diagnose

# Sniff live packet bytes from the tablet (optional duration in seconds)
.\build\bin\Release\CT0405_CLI.exe --dump-packets 15

# Run the automated regression suite
.\build\bin\Release\CT0405_CLI.exe --test-decoder

# Test Windows Ink synthetic pen pointer injection
.\build\bin\Release\CT0405_CLI.exe --test-injection

# Run the driver headless, without the control panel
.\build\bin\Release\CT0405_CLI.exe --headless
```

`--diagnose` prints the HID **usage page**, whether the device is supported, and a
candidate score for every collection. A tablet publishes several collections under one
VID/PID and only some of them carry pen reports, so the driver picks the digitizer
collection by score rather than taking whichever one Windows enumerates first.

### Report protocol

The CT-0405-U sends 7-byte reports. The decoder implements exactly this layout and
refuses anything that does not match, rather than guessing:

| Byte | Meaning |
| :--- | :--- |
| `[0]` | Report id (`0x01` status, `0x02` motion) |
| `[1..2]` | X position, little endian |
| `[3..4]` | Y position, little endian |
| `[5]` | Status: bit 7 in-range, bit 6 barrel, bit 5 eraser, bit 4 barrel 2 |
| `[6]` | Pressure, signed, biased by +127 |

A report that decodes to coordinates far outside the tablet's physical range is
discarded as a misread rather than injected as pointer input.

---

## Credits & Attribution

This driver software and control panel application was designed, architected, and developed by **Antigravity** using **Gemini 3.7 Flash** (Google DeepMind).

---

## License

This project is licensed under the [MIT License](LICENSE).
