#include "MainWindow.h"
#include "TabletDriver.h"
#include "ConfigManager.h"
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR pCmdLine, int nCmdShow) {
    // Single instance: hand focus to the running copy rather than starting a
    // second driver that would fight it for the device.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"WacomCT0405Driver_SingleInstanceMutex");
    const DWORD mutexError = GetLastError();

    if (hMutex && mutexError == ERROR_ALREADY_EXISTS) {
        HWND hWndExisting = FindWindowW(L"CT0405_ControlPanel_Class", nullptr);
        if (hWndExisting) {
            ShowWindow(hWndExisting, SW_SHOW);
            ShowWindow(hWndExisting, SW_RESTORE);
            SetForegroundWindow(hWndExisting);
        }
        CloseHandle(hMutex);
        return 0;
    }

    bool start_minimized = false;
    if (pCmdLine) {
        const std::wstring cmdLine = pCmdLine;
        if (cmdLine.find(L"--minimized") != std::wstring::npos ||
            cmdLine.find(L"-m") != std::wstring::npos) {
            start_minimized = true;
        }
    }

    // The saved preference applies too, not just the command line.
    if (!start_minimized) {
        start_minimized = ct0405::ConfigManager::LoadConfig().start_minimized;
    }

    int exit_code = 0;
    {
        // Scoped so the window is destroyed before the driver it references.
        ct0405::TabletDriver driver;
        driver.Start();

        ct0405::MainWindow window(hInstance, driver);
        if (!window.Create(nCmdShow, start_minimized)) {
            driver.Stop();
            if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
            return 1;
        }

        exit_code = window.RunMessageLoop();
        driver.Stop();
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return exit_code;
}
