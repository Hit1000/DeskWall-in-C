#include "power.h"

// NOTIFY_FOR_THIS_SESSION from wtsapi32.h — define here to avoid linking the lib
#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION 1
#endif

bool IsFullscreenAppInForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // Skip our own windows and the shell
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    if (fgPid == GetCurrentProcessId()) return false;

    // Check if the window has caption/border — real fullscreen apps typically don't
    LONG style = GetWindowLongW(fg, GWL_STYLE);
    if (style & WS_CAPTION) return false;
    if (style & WS_THICKFRAME) return false;

    // Check if window bounds match its monitor's full bounds
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) return false;

    RECT rc;
    if (!GetWindowRect(fg, &rc)) return false;

    // Must cover the entire monitor work area (or full area for exclusive fullscreen)
    return rc.left <= mi.rcMonitor.left && rc.top <= mi.rcMonitor.top &&
           rc.right >= mi.rcMonitor.right && rc.bottom >= mi.rcMonitor.bottom;
}

bool IsOnBattery() {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        // ACLineStatus: 0 = offline, 1 = online, 255 = unknown
        return sps.ACLineStatus == 0;
    }
    return false;
}

void RegisterSessionNotifications(HWND hwnd) {
    // WTSRegisterSessionNotification is in wtsapi32, but we can load it dynamically
    // to avoid an extra link lib. However, it's available since XP so just link it.
    // Actually, it's in wtsapi32.dll — let's use LoadLibrary to be safe.
    typedef BOOL (WINAPI *PFN_WTSRegisterSessionNotification)(HWND, DWORD);
    static auto pfn = reinterpret_cast<PFN_WTSRegisterSessionNotification>(
        GetProcAddress(GetModuleHandleW(L"wtsapi32.dll"), "WTSRegisterSessionNotification"));
    if (pfn) {
        pfn(hwnd, NOTIFY_FOR_THIS_SESSION);
    }
}

void UnregisterSessionNotifications(HWND hwnd) {
    typedef BOOL (WINAPI *PFN_WTSUnRegisterSessionNotification)(HWND);
    static auto pfn = reinterpret_cast<PFN_WTSUnRegisterSessionNotification>(
        GetProcAddress(GetModuleHandleW(L"wtsapi32.dll"), "WTSUnRegisterSessionNotification"));
    if (pfn) {
        pfn(hwnd);
    }
}
