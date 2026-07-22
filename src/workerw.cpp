#include "workerw.h"
#include "config.h"

static InjectionResult g_injection = {};
static bool g_injected = false;

const InjectionResult* GetInjectionState() {
    return g_injected ? &g_injection : nullptr;
}

struct EnumWindowsData {
    HWND progmanLike;  // Window hosting SHELLDLL_DefView
    HWND defView;      // The SHELLDLL_DefView itself
    HWND workerw;      // The WorkerW sibling (or child)
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
    HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView) {
        data->progmanLike = hwnd;
        data->defView = defView;

        // Classic shell: WorkerW is a top-level sibling of the DefView host.
        HWND sibling = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
        if (sibling) {
            data->workerw = sibling;
        } else {
            // Nested/raised-desktop: WorkerW is a direct child of the DefView host.
            data->workerw = FindWindowExW(hwnd, nullptr, L"WorkerW", nullptr);
        }
    }
    return TRUE;
}

bool TryInjectWallpaperWindow(HWND renderWindow) {
    HWND progman = FindWindowW(L"Progman", nullptr);

    if (progman) {
        // Send the undocumented message to spawn a WorkerW behind icons.
        // Use wParam=0xD, lParam=1 (Lively's variant — more reliable on newer builds).
        // Use SendMessageTimeoutW so we never hang if Explorer doesn't respond.
        // Send twice with a pause for Windows 11 24H2 compatibility.
        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, 0x052C, (WPARAM)0xD, (LPARAM)1, SMTO_NORMAL, 1000, &result);
        Sleep(1000);
        SendMessageTimeoutW(progman, 0x052C, (WPARAM)0xD, (LPARAM)1, SMTO_NORMAL, 1000, &result);
    } else {
        LogMessage(L"WorkerW: FindWindowW(Progman) failed; still trying enumeration");
    }

    // Check for "raised desktop" — Progman with WS_EX_NOREDIRECTIONBITMAP
    bool raisedDesktop = false;
    if (progman) {
        LONG exStyle = GetWindowLongW(progman, GWL_EXSTYLE);
        raisedDesktop = (exStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;
        if (raisedDesktop) {
            LogMessage(L"WorkerW: Detected raised desktop (WS_EX_NOREDIRECTIONBITMAP)");
        }
    }

    // Enumerate to find the DefView host and WorkerW.
    EnumWindowsData data{};
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    if (raisedDesktop) {
        // Microsoft's "raised desktop" architecture: Progman has no GDI surface;
        // SHELLDLL_DefView is a WS_EX_LAYERED child of Progman; a WorkerW child
        // renders the wallpaper beneath it. Attach directly to Progman,
        // Z-ordered between DefView and WorkerW.
        if (!data.defView) {
            LogMessage(L"WorkerW: Raised desktop but no SHELLDLL_DefView found");
            return false;
        }

        g_injection.parent = data.progmanLike;
        g_injection.insertAfter = data.defView;
        g_injection.needsLayered = true;
        g_injection.systemWorkerW = data.workerw ? data.workerw : nullptr;
        g_injected = true;

        // Create as WS_CHILD of Progman with WS_EX_LAYERED
        SetWindowLongW(renderWindow, GWL_STYLE, GetWindowLongW(renderWindow, GWL_STYLE) | WS_CHILD);
        SetWindowLongW(renderWindow, GWL_EXSTYLE,
            GetWindowLongW(renderWindow, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        SetParent(renderWindow, data.progmanLike);
        SetLayeredWindowAttributes(renderWindow, 0, 255, LWA_ALPHA);

        // Size to cover the full virtual desktop
        RECT vr = GetVirtualScreenRect();
        SetWindowPos(renderWindow, nullptr,
            vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Z-order: sit right after SHELLDLL_DefView
        SetWindowPos(renderWindow, data.defView,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // Push the system WorkerW behind us
        if (data.workerw) {
            SetWindowPos(data.workerw, renderWindow,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        ShowWindow(renderWindow, SW_SHOWNOACTIVATE);
        InvalidateRect(renderWindow, nullptr, TRUE);
        UpdateWindow(renderWindow);

        LogMessage(L"WorkerW: Raised desktop injection successful");
        return true;
    }

    // Classic shell path
    if (!data.workerw) {
        LogMessage(L"WorkerW: Could not find WorkerW window");
        return false;
    }

    g_injection.parent = data.workerw;
    g_injection.insertAfter = nullptr;
    g_injection.needsLayered = false;
    g_injection.systemWorkerW = nullptr;
    g_injected = true;

    SetParent(renderWindow, data.workerw);

    // Size and position to cover the full virtual desktop
    RECT vr = GetVirtualScreenRect();
    SetWindowPos(renderWindow, nullptr,
        vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(renderWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(renderWindow, HWND_BOTTOM,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(renderWindow, nullptr, TRUE);
    UpdateWindow(renderWindow);

    LogMessage(L"WorkerW: Classic injection successful");
    return true;
}

void OnExplorerRestarted(HWND renderWindow) {
    LogMessage(L"WorkerW: Explorer restarted, re-injecting...");
    // Unparent first
    SetParent(renderWindow, nullptr);
    g_injected = false;
    g_injection = {};

    // Restore window styles
    LONG style = GetWindowLongW(renderWindow, GWL_STYLE);
    style &= ~WS_CHILD;
    SetWindowLongW(renderWindow, GWL_STYLE, style);
    LONG exStyle = GetWindowLongW(renderWindow, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_LAYERED);
    SetWindowLongW(renderWindow, GWL_EXSTYLE, exStyle);

    // Retry loop for 24H2 compatibility
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
