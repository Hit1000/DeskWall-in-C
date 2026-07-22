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

// Global state
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

// Shared state for tray menu
bool g_paused = false;
bool g_muted = true;
MonitorMode g_monitorMode = MonitorMode::Span;
bool g_startWithWindows = true;

// TaskbarCreated message — Explorer restarts broadcast this
static UINT WM_TASKBAR_CREATED = 0;

// Forward declarations
static LRESULT CALLBACK RenderWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK TrayWndProc(HWND, UINT, WPARAM, LPARAM);
static void CreateRenderWindow(HINSTANCE hInstance);
static void StartInjectionRetry();
static void UpdatePauseState();
static void ApplyWallpaper();
static void SetStartupRegistry(bool enable);
static void TriggerRepaint();

// --- Startup registry (HKCU Run key) ---

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

// --- Trigger repaint ---

static void TriggerRepaint() {
    if (g_renderWindow) {
        InvalidateRect(g_renderWindow, nullptr, FALSE);
    }
}

// --- Apply wallpaper based on config ---

static void ApplyWallpaper() {
    if (g_config.wallpaperPath.empty()) return;

    std::wstring ext = PathFindExtensionW(g_config.wallpaperPath.c_str());
    for (auto& c : ext) c = towlower(c);

    bool isVideo = (ext == L".mp4" || ext == L".webm" || ext == L".avi");

    // Shutdown existing renderers
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

// --- Menu handlers (called from tray.cpp) ---

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
}

void OnMenuMuteUnmute() {
    g_config.muted = !g_config.muted;
    g_muted = g_config.muted;
    if (g_videoRenderer) {
        g_videoRenderer->SetMuted(g_config.muted);
    }
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

    if (g_config.pauseOnFullscreenApp && IsFullscreenAppInForeground()) {
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
    } else if (!shouldPause && g_paused && !g_fullscreenPaused && !g_batteryPaused && !g_sessionLocked) {
        g_paused = false;
        if (g_videoRenderer) g_videoRenderer->Resume();
        TriggerRepaint();
    }
}

// --- Render window ---

static void CreateRenderWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = RenderWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DeskWallRender";
    wc.hbrBackground = CreateSolidBrush(0x00000000); // black background
    RegisterClassExW(&wc);

    RECT vr = GetVirtualScreenRect();

    g_renderWindow = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"DeskWallRender", L"DeskWall",
        WS_POPUP,
        vr.left, vr.top, vr.right - vr.left, vr.bottom - vr.top,
        nullptr, nullptr, hInstance, nullptr);
}

static LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        // Render wallpaper on paint — this is the correct render path
        if (!g_paused && g_injected) {
            if (g_config.wallpaperType == WallpaperType::Video && g_videoRenderer) {
                g_videoRenderer->Render();
            } else if (g_config.wallpaperType == WallpaperType::Image && g_imageRenderer.HasImage()) {
                g_imageRenderer.Render();
            }
        }
        // Validate the paint region
        ValidateRect(hwnd, nullptr);
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            UINT w = LOWORD(lParam);
            UINT h = HIWORD(lParam);
            g_imageRenderer.OnResize(w, h);
            if (g_videoRenderer) g_videoRenderer->OnResize(w, h);
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
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            TrayShowMenu(hwnd, g_config);
        }
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
        // For video wallpapers, continuously trigger repaints
        if (!g_paused && g_injected && g_config.wallpaperType == WallpaperType::Video) {
            TriggerRepaint();
        }
        return 0;
    }

    if (msg == WM_POWERBROADCAST) {
        if (wParam == PBT_APMSUSPEND) {
            g_sessionLocked = true;
        } else if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
            g_sessionLocked = false;
        }
        return TRUE;
    }

    if (msg == WM_WTSSESSION_CHANGE) {
        if (wParam == WTS_SESSION_LOCK) {
            g_sessionLocked = true;
        } else if (wParam == WTS_SESSION_UNLOCK) {
            g_sessionLocked = false;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Entry point ---

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\DeskWallMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_hInstance = hInstance;

    if (IsRemoteDesktopSession()) {
        LogMessage(L"Running in Remote Desktop session — rendering disabled");
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    g_config = ConfigLoad();
    g_muted = g_config.muted;
    g_monitorMode = g_config.monitorMode;
    g_startWithWindows = g_config.startWithWindows;

    SetStartupRegistry(g_config.startWithWindows);

    // Create the render window first (WS_POPUP, not yet parented)
    CreateRenderWindow(hInstance);
    g_trayWindow = TrayCreate(hInstance, g_config);
    SetWindowLongPtrW(g_trayWindow, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);
    RegisterSessionNotifications(g_trayWindow);

    // Show render window before injection (needed for raised desktop Z-order)
    ShowWindow(g_renderWindow, SW_SHOWNOACTIVATE);

    // Try injection with retry loop for 24H2
    if (!IsRemoteDesktopSession()) {
        StartInjectionRetry();
    }

    // Load wallpaper if configured
    ApplyWallpaper();

    // Timer for periodic checks (fullscreen, battery) and video frame updates
    SetTimer(g_trayWindow, 1, 33, nullptr); // ~30fps for video repaints

    // Message loop — render is driven by WM_PAINT, triggered by Timer/InvalidateRect
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    KillTimer(g_trayWindow, 1);
    UnregisterSessionNotifications(g_trayWindow);
    g_imageRenderer.Shutdown();
    if (g_videoRenderer) {
        g_videoRenderer->Shutdown();
        g_videoRenderer->Release();
    }
    TrayDestroy(g_trayWindow);
    DestroyWindow(g_renderWindow);
    CoUninitialize();

    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}

// --- Injection retry ---

static void StartInjectionRetry() {
    for (int i = 0; i < 20; i++) {
        if (TryInjectWallpaperWindow(g_renderWindow)) {
            g_injected = true;
            TriggerRepaint();
            return;
        }
        Sleep(500);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    LogMessage(L"Failed to inject into WorkerW after 20 attempts");
}
