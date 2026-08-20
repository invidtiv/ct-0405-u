#include "TrayIcon.h"

namespace ct0405 {

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Initialize(HWND hWnd, UINT uCallbackMsg, HICON hIcon, const std::wstring& tooltip) {
    Remove();

    m_nid = NOTIFYICONDATAW{};
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hWnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = uCallbackMsg;
    m_nid.hIcon = hIcon ? hIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);

    m_installed = Shell_NotifyIconW(NIM_ADD, &m_nid) == TRUE;
    return m_installed;
}

void TrayIcon::Remove() {
    if (m_installed) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_installed = false;
    }
}

void TrayIcon::UpdateTooltip(const std::wstring& tooltip) {
    if (!m_installed) return;
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::UpdateIcon(HICON hIcon) {
    if (!m_installed || !hIcon) return;
    m_nid.hIcon = hIcon;
    m_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& message, DWORD infoFlags) {
    if (!m_installed) return;
    m_nid.uFlags = NIF_INFO;
    wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
    m_nid.dwInfoFlags = infoFlags;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowContextMenu(HWND hWnd, bool is_connected, bool is_ink_mode,
                               const std::function<void(int)>& on_command) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    const std::wstring statusText = is_connected ? L"Status: Connected" : L"Status: Disconnected";
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, ID_TRAY_STATUS, statusText.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | MF_DEFAULT, ID_TRAY_OPEN, L"Open Control Panel");

    UINT inkFlags = MF_STRING;
    if (is_ink_mode) inkFlags |= MF_CHECKED;
    AppendMenuW(hMenu, inkFlags, ID_TRAY_TOGGLE_INK, L"Windows Ink Mode");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Driver");

    SetForegroundWindow(hWnd);
    const int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTBUTTON,
                                   pt.x, pt.y, 0, hWnd, nullptr);

    // Documented workaround: without a message posted to the owner after
    // TrackPopupMenu, the menu does not dismiss when the user clicks elsewhere.
    PostMessageW(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);

    if (cmd && on_command) {
        on_command(cmd);
    }
}

} // namespace ct0405
