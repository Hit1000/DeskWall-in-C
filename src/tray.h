#pragma once
#include <windows.h>
#include <string>

struct Config;

// Menu IDs — shared between tray.cpp and main.cpp's tray window proc
enum TrayMenuId {
    IDM_CHANGE_WALLPAPER = 1001,
    IDM_PAUSE_RESUME,
    IDM_MUTE_UNMUTE,
    IDM_MONITOR_SPAN,
    IDM_MONITOR_DUPLICATE,
    IDM_MONITOR_PERMONITOR,
    IDM_START_WITH_WINDOWS,
    IDM_EXIT,
};

// Create the system tray icon and message-only window.
HWND TrayCreate(HINSTANCE hInstance, Config& config);

// Destroy the tray icon.
void TrayDestroy(HWND hwnd);

// Show the right-click context menu.
void TrayShowMenu(HWND hwnd, Config& config);

// Update the tray tooltip text.
void TraySetTooltip(HWND hwnd, const std::wstring& text);

// Update tray icon (e.g., for pause state).
void TrayUpdateIcon(HWND hwnd, bool paused);
