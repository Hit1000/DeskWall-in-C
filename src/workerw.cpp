#include "workerw.h"
#include "config.h"
#include <vector>

static HWND g_workerw = nullptr;

struct EnumWindowsData {
    HWND workerw;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
    HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView) {
        // Found the parent with SHELLDLL_DefView — its next Z-order sibling is the WorkerW we want.
        data->workerw = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
    }
    return TRUE; // continue enumerating
}

bool TryInjectWallpaperWindow(HWND renderWindow) {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) {
        LogMessage(L"WorkerW: Progman window not found");
        return false;
    }

    // Send the undocumented message to spawn a WorkerW behind icons.
    // Use SendMessageTimeoutW so we never hang if Explorer doesn't respond.
    // Send twice with a pause for Windows 11 24H2 compatibility.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    Sleep(1000);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);

    // Enumerate to find the WorkerW that sits behind the icons.
    EnumWindowsData data{};
    data.workerw = nullptr;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    if (!data.workerw) {
        LogMessage(L"WorkerW: Could not find WorkerW window");
        return false;
    }

    g_workerw = data.workerw;

    // Parent our render window into the WorkerW layer.
    SetParent(renderWindow, g_workerw);

    // Size to cover the full virtual desktop.
    RECT vr = GetVirtualScreenRect();
    SetWindowPos(renderWindow, nullptr,
        vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    LogMessage(L"WorkerW: Injection successful");
    return true;
}

void OnExplorerRestarted(HWND renderWindow) {
    LogMessage(L"WorkerW: Explorer restarted, re-injecting...");
    // Unparent first in case the old WorkerW is gone.
    SetParent(renderWindow, nullptr);
    g_workerw = nullptr;

    // Retry loop for 24H2 compatibility.
    for (int i = 0; i < 20; i++) {
        if (TryInjectWallpaperWindow(renderWindow)) return;
        Sleep(500);
    }
    LogMessage(L"WorkerW: Re-injection failed after Explorer restart");
}

bool IsRemoteDesktopSession() {
    return GetSystemMetrics(SM_REMOTESESSION) != 0;
}

RECT GetVirtualScreenRect() {
    RECT rc;
    rc.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rc.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rc.right = rc.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rc.bottom = rc.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rc;
}
