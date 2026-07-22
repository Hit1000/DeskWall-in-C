#include "workerw.h"
#include "config.h"

struct EnumWindowsData {
    HWND progmanLike;
    HWND defView;
    HWND workerw;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
    HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (defView) {
        data->progmanLike = hwnd;
        data->defView = defView;
        HWND sibling = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
        if (sibling) {
            data->workerw = sibling;
        } else {
            data->workerw = FindWindowExW(hwnd, nullptr, L"WorkerW", nullptr);
        }
    }
    return TRUE;
}

InjectionResult FindInjectionTarget() {
    InjectionResult result = {};

    HWND progman = FindWindowW(L"Progman", nullptr);

    if (progman) {
        DWORD_PTR sendResult = 0;
        SendMessageTimeoutW(progman, 0x052C, (WPARAM)0xD, (LPARAM)1, SMTO_NORMAL, 1000, &sendResult);
        Sleep(1000);
        SendMessageTimeoutW(progman, 0x052C, (WPARAM)0xD, (LPARAM)1, SMTO_NORMAL, 1000, &sendResult);
    } else {
        LogMessage(L"WorkerW: FindWindowW(Progman) failed; skipping spawn, trying enumeration");
    }

    bool raisedDesktop = false;
    if (progman) {
        LONG exStyle = GetWindowLongW(progman, GWL_EXSTYLE);
        raisedDesktop = (exStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;
        if (raisedDesktop) {
            LogMessage(L"WorkerW: Detected raised desktop (WS_EX_NOREDIRECTIONBITMAP)");
        }
    }

    EnumWindowsData data = {};
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    if (!data.defView) {
        LogMessage(L"WorkerW: No SHELLDLL_DefView found");
        return result;
    }

    if (raisedDesktop) {
        result.parent = data.progmanLike;
        result.insertAfter = data.defView;
        result.needsLayered = true;
        result.systemWorkerW = data.workerw;
        result.found = true;
        LogMessage(L"WorkerW: Raised desktop target found");
        return result;
    }

    if (data.workerw) {
        result.parent = data.workerw;
        result.needsLayered = false;
        result.found = true;
        LogMessage(L"WorkerW: Classic target found");
        return result;
    }

    LogMessage(L"WorkerW: SHELLDLL_DefView found but no WorkerW");
    return result;
}

bool ApplyInjection(HWND renderWindow, const InjectionResult& injection) {
    if (!injection.found || !injection.parent) return false;

    if (injection.needsLayered) {
        // Raised desktop: window should already be WS_CHILD of Progman
        // with WS_EX_LAYERED (created that way). Just set alpha and Z-order.
        SetLayeredWindowAttributes(renderWindow, 0, 255, LWA_ALPHA);

        // Z-order: right after SHELLDLL_DefView
        SetWindowPos(renderWindow, injection.insertAfter,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // Push system WorkerW behind us
        if (injection.systemWorkerW) {
            SetWindowPos(injection.systemWorkerW, renderWindow,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        ShowWindow(renderWindow, SW_SHOWNOACTIVATE);
        InvalidateRect(renderWindow, nullptr, TRUE);
        UpdateWindow(renderWindow);

        LogMessage(L"WorkerW: Raised desktop injection applied");
        return true;
    }

    // Classic: SetParent into WorkerW
    SetParent(renderWindow, injection.parent);
    ShowWindow(renderWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(renderWindow, HWND_BOTTOM,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(renderWindow, nullptr, TRUE);
    UpdateWindow(renderWindow);

    LogMessage(L"WorkerW: Classic injection applied");
    return true;
}

bool TryInjectWallpaperWindow(HWND renderWindow) {
    InjectionResult injection = FindInjectionTarget();
    if (!injection.found) return false;
    return ApplyInjection(renderWindow, injection);
}

void OnExplorerRestarted(HWND renderWindow) {
    LogMessage(L"WorkerW: Explorer restarted, re-injecting...");
    SetParent(renderWindow, nullptr);

    // Restore WS_POPUP style
    LONG style = GetWindowLongW(renderWindow, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_POPUP;
    SetWindowLongW(renderWindow, GWL_STYLE, style);
    LONG exStyle = GetWindowLongW(renderWindow, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_LAYERED);
    SetWindowLongW(renderWindow, GWL_EXSTYLE, exStyle);

    for (int i = 0; i < 20; i++) {
        InjectionResult injection = FindInjectionTarget();
        if (injection.found) {
            // For raised desktop, recreate with correct styles
            if (injection.needsLayered) {
                SetWindowLongW(renderWindow, GWL_STYLE,
                    (GetWindowLongW(renderWindow, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
                SetWindowLongW(renderWindow, GWL_EXSTYLE,
                    GetWindowLongW(renderWindow, GWL_EXSTYLE) | WS_EX_LAYERED);
                SetParent(renderWindow, injection.parent);
            }
            ApplyInjection(renderWindow, injection);
            return;
        }
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
