#include "tray.h"
#include "config.h"
#include "resource.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>

static NOTIFYICONDATAW g_nid = {};
static HMENU g_hMenu = nullptr;

// Forward declare — main.cpp provides these
extern void OnMenuChangeWallpaper(HWND hwnd);
extern void OnMenuPauseResume();
extern void OnMenuMuteUnmute();
extern void OnMenuMonitorMode(MonitorMode mode);
extern void OnMenuStartWithWindows(bool enable);
extern void OnMenuExit();

// Current state for menu checkmarks
extern bool g_paused;
extern bool g_muted;
extern MonitorMode g_monitorMode;
extern bool g_startWithWindows;

HWND TrayCreate(HINSTANCE hInstance, Config& config) {
    // Create a message-only window for tray callbacks
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DeskWallTray";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DeskWallTray", L"DeskWall",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!hwnd) return nullptr;

    // Load icon — try the embedded resource, fall back to a stock icon
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!hIcon) {
        hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(IDI_APPLICATION));
    }

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hIcon;
    wcscpy_s(g_nid.szTip, L"DeskWall");

    Shell_NotifyIconW(NIM_ADD, &g_nid);

    return hwnd;
}

void TrayDestroy(HWND hwnd) {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hMenu) {
        DestroyMenu(g_hMenu);
        g_hMenu = nullptr;
    }
    DestroyWindow(hwnd);
}

void TraySetTooltip(HWND hwnd, const std::wstring& text) {
    g_nid.hWnd = hwnd;
    wcscpy_s(g_nid.szTip, text.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TrayUpdateIcon(HWND hwnd, bool paused) {
    g_nid.hWnd = hwnd;
    // Could swap icon for paused state if desired — for now just update tooltip
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void AppendCheck(HMENU menu, UINT id, const wchar_t* text, bool checked) {
    UINT flags = MF_STRING;
    if (checked) flags |= MF_CHECKED;
    AppendMenuW(menu, flags, id, text);
}

void TrayShowMenu(HWND hwnd, Config& config) {
    if (g_hMenu) DestroyMenu(g_hMenu);
    g_hMenu = CreatePopupMenu();

    AppendMenuW(g_hMenu, MF_STRING, IDM_CHANGE_WALLPAPER, L"Change Wallpaper…");
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);

    AppendCheck(g_hMenu, IDM_PAUSE_RESUME, L"Pause", g_paused);
    AppendCheck(g_hMenu, IDM_MUTE_UNMUTE, L"Mute", g_muted);

    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);

    // Monitor mode submenu
    HMENU subMonitor = CreatePopupMenu();
    AppendCheck(subMonitor, IDM_MONITOR_SPAN, L"Span all monitors", g_monitorMode == MonitorMode::Span);
    AppendCheck(subMonitor, IDM_MONITOR_DUPLICATE, L"Duplicate on all", g_monitorMode == MonitorMode::Duplicate);
    AppendCheck(subMonitor, IDM_MONITOR_PERMONITOR, L"Per-monitor", g_monitorMode == MonitorMode::PerMonitor);
    AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)subMonitor, L"Monitor mode");

    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendCheck(g_hMenu, IDM_START_WITH_WINDOWS, L"Start with Windows", g_startWithWindows);
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hMenu, MF_STRING, IDM_EXIT, L"Exit");

    // Required to dismiss menu properly
    SetForegroundWindow(hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
}
