#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>

#include "workerw.h"
#include "renderer_image.h"
#include "renderer_video.h"
#include "tray.h"
#include "config.h"
#include "power.h"
#include "resource.h"

static HINSTANCE g_hInstance = nullptr;
static HWND g_renderWindow = nullptr;
static HWND g_trayWindow = nullptr;
static Config g_config;
static ImageRenderer g_imageRenderer;
static VideoRenderer* g_videoRenderer = nullptr;
static bool g_injected = false;
static bool g_sessionLocked = false;
static bool g_fullscreenPaused = false;
static bool g_batteryPaused = false;
static int g_injectRetryCount = 0;

bool g_paused = false;
bool g_muted = true;
MonitorMode g_monitorMode = MonitorMode::Span;
bool g_startWithWindows = true;

static UINT WM_TASKBAR_CREATED = 0;

static LRESULT CALLBACK RenderWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK TrayWndProc(HWND, UINT, WPARAM, LPARAM);
static void UpdatePauseState();
static void ApplyWallpaper();
static void SetStartupRegistry(bool enable);
static void TriggerRepaint();
static void DeferredInject(HWND hwnd, UINT msg, UINT_PTR id, DWORD dwTime);

// Detect when the desktop itself (Program Manager / Explorer desktop window) is
// in the foreground. The desktop window typically has WS_CAPTION set, but
// IsFullscreenAppInForeground() checks for the ABSENCE of WS_CAPTION — so a
// desktop without WS_CAPTION (some Windows 11 configurations) would be
// misidentified as a fullscreen app, causing the wallpaper to freeze.
static bool IsDesktopForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // Exclude our own windows
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    if (fgPid == GetCurrentProcessId()) return false;

    wchar_t className[64] = {};
    GetClassNameW(fg, className, 64);

    // Progman is the classic desktop shell window
    if (wcscmp(className, L"Progman") == 0) return true;

    // WorkerW is the hidden window behind the desktop icons
    if (wcscmp(className, L"WorkerW") == 0) return true;

    // Some setups use a "DesktopWindowClass" or similar
    if (wcsstr(className, L"Desktop") != nullptr) return true;

    return false;
}

// --- Startup registry ---

static void SetStartupRegistry(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            wchar_t quoted[MAX_PATH + 2];
            StringCchPrintfW(quoted, MAX_PATH + 2, L"\"%s\"", path);
            RegSetValueExW(hKey, L"DeskWall", 0, REG_SZ,
                (BYTE*)quoted, (DWORD)(wcslen(quoted) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"DeskWall");
        }
        RegCloseKey(hKey);
    }
}

// --- File picker ---

static std::wstring ShowFilePicker() {
    IFileOpenDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) return {};

    COMDLG_FILTERSPEC filters[] = {
        { L"All Supported", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.mp4;*.webm;*.avi" },
        { L"Images", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif" },
        { L"Videos", L"*.mp4;*.webm;*.avi" },
    };
    pfd->SetFileTypes(3, filters);
    pfd->SetFileTypeIndex(1);

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) { pfd->Release(); return {}; }

    IShellItem* item = nullptr;
    hr = pfd->GetResult(&item);
    if (FAILED(hr)) { pfd->Release(); return {}; }

    PWSTR filePath = nullptr;
    item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
    std::wstring result;
    if (filePath) { result = filePath; CoTaskMemFree(filePath); }

    item->Release();
    pfd->Release();
    return result;
}

static void TriggerRepaint() {
    if (g_renderWindow) {
        // Use RedrawWindow with RDW_INVALIDATE | RDW_NOERASE for reliable
        // paint delivery even when the desktop window is in the foreground.
        // Plain InvalidateRect can be deferred by Windows when the window
        // is behind the active desktop shell window.
        RedrawWindow(g_renderWindow, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
    }
}

// --- Apply wallpaper ---

static void ApplyWallpaper() {
    if (g_config.wallpaperPath.empty()) return;

    std::wstring ext = PathFindExtensionW(g_config.wallpaperPath.c_str());
    for (auto& c : ext) c = towlower(c);
    bool isVideo = (ext == L".mp4" || ext == L".webm" || ext == L".avi");

    g_imageRenderer.Shutdown();
    if (g_videoRenderer) {
        g_videoRenderer->Shutdown();
        g_videoRenderer->Release();
        g_videoRenderer = nullptr;
    }

    if (isVideo) {
        g_config.wallpaperType = WallpaperType::Video;
        g_videoRenderer = new VideoRenderer();
        if (g_videoRenderer->Initialize(g_renderWindow)) {
            g_videoRenderer->SetMuted(g_config.muted);
            g_videoRenderer->SetVolume(g_config.volume);
            g_videoRenderer->LoadVideo(g_config.wallpaperPath);
        }
    } else {
        g_config.wallpaperType = WallpaperType::Image;
        if (g_imageRenderer.Initialize(g_renderWindow)) {
            g_imageRenderer.LoadImageFile(g_config.wallpaperPath);
        }
    }

    ConfigSave(g_config);
    TriggerRepaint();
}

// --- Menu handlers ---

void OnMenuChangeWallpaper(HWND) {
    std::wstring path = ShowFilePicker();
    if (!path.empty()) {
        g_config.wallpaperPath = path;
        ApplyWallpaper();
    }
}

void OnMenuPauseResume() {
    g_paused = !g_paused;
    if (g_paused) {
        if (g_videoRenderer) g_videoRenderer->Pause();
    } else {
        if (g_videoRenderer) g_videoRenderer->Resume();
        TriggerRepaint();
    }
    TrayUpdateIcon(g_trayWindow, g_paused);
}

void OnMenuMuteUnmute() {
    g_config.muted = !g_config.muted;
    g_muted = g_config.muted;
    if (g_videoRenderer) g_videoRenderer->SetMuted(g_config.muted);
    ConfigSave(g_config);
}

void OnMenuMonitorMode(MonitorMode mode) {
    g_config.monitorMode = mode;
    g_monitorMode = mode;
    RECT vr = GetVirtualScreenRect();
    SetWindowPos(g_renderWindow, nullptr,
        vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    ConfigSave(g_config);
}

void OnMenuStartWithWindows(bool enable) {
    g_config.startWithWindows = enable;
    g_startWithWindows = enable;
    SetStartupRegistry(enable);
    ConfigSave(g_config);
}

void OnMenuExit() {
    PostQuitMessage(0);
}

// --- Pause logic ---

static void UpdatePauseState() {
    bool shouldPause = false;

    // Don't treat the desktop itself as a fullscreen app — the wallpaper
    // should keep running when the user is looking at the desktop.
    if (g_config.pauseOnFullscreenApp && IsFullscreenAppInForeground() && !IsDesktopForeground()) {
        shouldPause = true;
        g_fullscreenPaused = true;
    } else {
        g_fullscreenPaused = false;
    }

    if (g_config.pauseOnBattery && IsOnBattery()) {
        shouldPause = true;
        g_batteryPaused = true;
    } else {
        g_batteryPaused = false;
    }

    if (g_sessionLocked) shouldPause = true;

    if (shouldPause && !g_paused) {
        g_paused = true;
        if (g_videoRenderer) g_videoRenderer->Pause();
        TrayUpdateIcon(g_trayWindow, true);
    } else if (!shouldPause && g_paused && !g_fullscreenPaused && !g_batteryPaused && !g_sessionLocked) {
        g_paused = false;
        if (g_videoRenderer) g_videoRenderer->Resume();
        TriggerRepaint();
        TrayUpdateIcon(g_trayWindow, false);
    }
}

// --- Render window ---

static HWND CreateRenderWindow(HINSTANCE hInstance, const InjectionResult* injection) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = RenderWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DeskWallRender";
    wc.hbrBackground = CreateSolidBrush(0x00000000);
    RegisterClassExW(&wc);

    RECT vr = GetVirtualScreenRect();

    // Determine styles based on injection target
    DWORD style = WS_POPUP;
    DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    HWND parent = nullptr;

    if (injection && injection->found && injection->needsLayered) {
        // Raised desktop: create as WS_CHILD of Progman with WS_EX_LAYERED
        style = WS_CHILD;
        exStyle |= WS_EX_LAYERED;
        parent = injection->parent;
    }

    HWND hwnd = CreateWindowExW(
        exStyle, L"DeskWallRender", L"DeskWall",
        style,
        vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
        parent, nullptr, hInstance, nullptr);

    return hwnd;
}

static LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (!g_paused && g_injected) {
            if (g_config.wallpaperType == WallpaperType::Video && g_videoRenderer) {
                g_videoRenderer->Render();
            } else if (g_config.wallpaperType == WallpaperType::Image && g_imageRenderer.HasImage()) {
                g_imageRenderer.Render();
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_imageRenderer.OnResize(LOWORD(lParam), HIWORD(lParam));
            if (g_videoRenderer) g_videoRenderer->OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_DISPLAYCHANGE: {
        RECT vr = GetVirtualScreenRect();
        SetWindowPos(hwnd, nullptr,
            vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Deferred injection (non-blocking startup) ---

static void DeferredInject(HWND hwnd, UINT, UINT_PTR, DWORD) {
    InjectionResult injection = FindInjectionTarget();

    if (!injection.found) {
        g_injectRetryCount++;
        if (g_injectRetryCount >= 20) {
            // Give up after 10 seconds — app runs without desktop injection
            KillTimer(hwnd, 2);
            LogMessage(L"Main: Injection target not found after 10s, giving up");
        }
        return;
    }

    // Found the injection target — stop the retry timer
    KillTimer(hwnd, 2);

    // Create the render window with styles matched to the injection target
    g_renderWindow = CreateRenderWindow(g_hInstance, &injection);

    // Apply injection (parent into WorkerW/Progman)
    g_injected = ApplyInjection(g_renderWindow, injection);
    if (g_injected) {
        LogMessage(L"Main: Injection applied successfully");
    } else {
        LogMessage(L"Main: ApplyInjection failed");
    }

    // Load wallpaper now that the render window exists
    ApplyWallpaper();
}

// --- Tray window ---

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TASKBAR_CREATED) {
        OnExplorerRestarted(g_renderWindow);
        TrayDestroy(g_trayWindow);
        g_trayWindow = TrayCreate(g_hInstance, g_config);
        SetWindowLongPtrW(g_trayWindow, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);
        return 0;
    }
    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            TrayShowMenu(hwnd, g_config);
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDM_CHANGE_WALLPAPER: OnMenuChangeWallpaper(hwnd); break;
        case IDM_PAUSE_RESUME: OnMenuPauseResume(); break;
        case IDM_MUTE_UNMUTE: OnMenuMuteUnmute(); break;
        case IDM_MONITOR_SPAN: OnMenuMonitorMode(MonitorMode::Span); break;
        case IDM_MONITOR_DUPLICATE: OnMenuMonitorMode(MonitorMode::Duplicate); break;
        case IDM_MONITOR_PERMONITOR: OnMenuMonitorMode(MonitorMode::PerMonitor); break;
        case IDM_START_WITH_WINDOWS: OnMenuStartWithWindows(!g_startWithWindows); break;
        case IDM_EXIT: OnMenuExit(); break;
        }
        return 0;
    }
    if (msg == WM_TIMER) {
        UpdatePauseState();
        // Drive repaints for BOTH video and image wallpapers so the
        // desktop wallpaper never freezes regardless of focus state.
        if (!g_paused && g_injected)
            TriggerRepaint();
        return 0;
    }
    if (msg == WM_POWERBROADCAST) {
        if (wParam == PBT_APMSUSPEND) g_sessionLocked = true;
        else if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) g_sessionLocked = false;
        return TRUE;
    }
    if (msg == WM_WTSSESSION_CHANGE) {
        if (wParam == WTS_SESSION_LOCK) g_sessionLocked = true;
        else if (wParam == WTS_SESSION_UNLOCK) g_sessionLocked = false;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Entry point ---

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\DeskWallMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    g_hInstance = hInstance;

    if (IsRemoteDesktopSession())
        LogMessage(L"Running in Remote Desktop session — rendering disabled");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    g_config = ConfigLoad();
    g_muted = g_config.muted;
    g_monitorMode = g_config.monitorMode;
    g_startWithWindows = g_config.startWithWindows;
    SetStartupRegistry(g_config.startWithWindows);

    // Step 1: Create tray IMMEDIATELY — no blocking waits.
    // The tray icon appears instantly on startup.
    g_trayWindow = TrayCreate(hInstance, g_config);
    SetWindowLongPtrW(g_trayWindow, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);
    RegisterSessionNotifications(g_trayWindow);

    // Timer for pause checks + frame updates (drives both video and image wallpapers)
    SetTimer(g_trayWindow, 1, 33, nullptr);

    // Step 2: Defer injection to a timer callback. This avoids blocking the
    // main thread with Sleep() loops while Explorer's desktop is still
    // initializing during Windows startup.
    if (!IsRemoteDesktopSession()) {
        g_injectRetryCount = 0;
        SetTimer(g_trayWindow, 2, 500, DeferredInject);
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(g_trayWindow, 1);
    KillTimer(g_trayWindow, 2); // Stop deferred injection timer if still running
    UnregisterSessionNotifications(g_trayWindow);
    g_imageRenderer.Shutdown();
    if (g_videoRenderer) { g_videoRenderer->Shutdown(); g_videoRenderer->Release(); }
    TrayDestroy(g_trayWindow);
    if (g_renderWindow) DestroyWindow(g_renderWindow);
    CoUninitialize();
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}
