cmake_minimum_required(VERSION 3.20)
project(CT0405_Driver VERSION 1.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Unicode
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:preprocessor)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include_directories(include)

# Core Driver Library
set(CORE_SOURCES
    src/PacketDecoder.cpp
    src/SignalProcessor.cpp
    src/CoordinateMapper.cpp
    src/InputInjector.cpp
    src/DeviceEnumerator.cpp
    src/HidDevice.cpp
    src/ConfigManager.cpp
    src/TabletDriver.cpp
)

set(CORE_HEADERS
    include/Common.h
    include/PacketDecoder.h
    include/SignalProcessor.h
    include/CoordinateMapper.h
    include/InputInjector.h
    include/DeviceEnumerator.h
    include/HidDevice.h
    include/ConfigManager.h
    include/TabletDriver.h
)

add_library(ct0405_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_link_libraries(ct0405_core PUBLIC
    setupapi
    hid
    user32
    gdi32
    shell32
    advapi32
)

# Control Panel GUI Executable (WIN32 GUI Application)
set(GUI_SOURCES
    src/app/main_gui.cpp
    src/app/MainWindow.cpp
    src/app/TrayIcon.cpp
    src/app/ScreenOverlayWindow.cpp
    resources/app.rc
)

set(GUI_HEADERS
    src/app/MainWindow.h
    src/app/TrayIcon.h
    src/app/ScreenOverlayWindow.h
)

add_executable(CT0405_ControlPanel WIN32 ${GUI_SOURCES} ${GUI_HEADERS})
target_link_libraries(CT0405_ControlPanel PRIVATE
    ct0405_core
    gdiplus
    comctl32
    user32
    gdi32
    shell32
    advapi32
    dwmapi
    uxtheme
)

# Embed Windows Manifest for PerMonitorV2 DPI awareness & Visual Styles
if(MSVC)
    target_sources(CT0405_ControlPanel PRIVATE resources/app.manifest)
    set_target_properties(CT0405_ControlPanel PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:\"${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest\""
    )
endif()

# CLI Diagnostic & Utility Executable (Console Application)
set(CLI_SOURCES
    src/cli/main_cli.cpp
)

add_executable(CT0405_CLI ${CLI_SOURCES})
target_link_libraries(CT0405_CLI PRIVATE
    ct0405_core
    user32
    shell32
    advapi32
)
