#include "MainWindow.h"
#include "TabletDriver.h"
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR pCmdLine, int nCmdShow) {
    // Prevent duplicate instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"WacomCT0405Driver_SingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hWndExisting = FindWindowW(L"CT0405_ControlPanel_Class", nullptr);
        if (hWndExisting) {
            ShowWindow(hWndExisting, SW_RESTORE);
            SetForegroundWindow(hWndExisting);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    bool start_minimized = false;
    if (pCmdLine) {
        std::wstring cmdLine = pCmdLine;
        if (cmdLine.find(L"--minimized") != std::wstring::npos || cmdLine.find(L"-m") != std::wstring::npos) {
            start_minimized = true;
        }
    }

    ct0405::TabletDriver driver;
    driver.Start();

    ct0405::MainWindow window(hInstance, driver);
    if (!window.Create(nCmdShow, start_minimized)) {
        driver.Stop();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    int exit_code = window.RunMessageLoop();

    driver.Stop();
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return exit_code;
}
