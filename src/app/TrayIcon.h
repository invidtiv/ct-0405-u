#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

namespace ct0405 {

// Scoped constants rather than macros, so these cannot collide with anything
// else that happens to be included.
constexpr UINT WM_TRAYICON = WM_APP + 101;

constexpr int ID_TRAY_OPEN       = 2001;
constexpr int ID_TRAY_TOGGLE_INK = 2002;
constexpr int ID_TRAY_STATUS     = 2003;
constexpr int ID_TRAY_EXIT       = 2004;

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Initialize(HWND hWnd, UINT uCallbackMsg, HICON hIcon, const std::wstring& tooltip);
    void Remove();

    void UpdateTooltip(const std::wstring& tooltip);
    void UpdateIcon(HICON hIcon);
    void ShowBalloon(const std::wstring& title, const std::wstring& message, DWORD infoFlags = NIIF_INFO);

    void ShowContextMenu(HWND hWnd, bool is_connected, bool is_ink_mode, const std::function<void(int)>& on_command);

private:
    NOTIFYICONDATAW m_nid{};
    bool m_installed = false;
};

} // namespace ct0405
