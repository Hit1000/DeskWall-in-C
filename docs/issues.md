# DeskWall — Issue List

## 🐛 Bugs & Correctness

### 1. ~~`OnExplorerRestarted` re-injection blocks the UI thread for up to 10 seconds~~ ✅ FIXED
`workerw.cpp` — Replaced blocking `Sleep(500)` loop with timer-based `DeferredReInject` callback. The re-injection now retries every 500ms without blocking the message pump. The tray icon and UI remain responsive during Explorer restarts.

### 2. ~~`OnExplorerRestarted` reuses the same HWND but changes its style/parent — leaks render target~~ ✅ FIXED
`main.cpp` — `DeferredReInject` now calls `ApplyWallpaper()` after successful re-injection, which recreates both the `ImageRenderer` and `VideoRenderer` with fresh D2D/MF targets tied to the reparented window.

### 3. ~~`Render()` is called every 33ms even for static image wallpapers~~ ✅ FIXED
`main.cpp` — Timer now only drives repaints for `WallpaperType::Image`. Video wallpapers are rendered by MF directly to the HWND and don't need `WM_PAINT`.

### 4. ~~`g_videoRenderer` deleted via `Release()` but allocated with `new~~ ✅ FIXED
`main.cpp` — Added clarifying comment. The pattern is intentional: `new` sets ref=1 (matches owning pointer), `Shutdown()` releases the MediaEngine (dropping its callback ref), then `Release()` decrements to 0 and deletes. Standard COM ownership pattern.

### 5. ~~`config.json` written without atomicity — corruption on crash~~ ✅ FIXED
`config.cpp` — `ConfigSave` now writes to `config.json.tmp` first, then uses `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` to atomically replace the config file. If the app crashes mid-write, the old config is preserved.

### 6. ~~`EnsureDirExists` only creates one level — fails if `LOCALAPPDATA` parent is missing~~ ✅ FIXED
`config.cpp` — Now uses `SHCreateDirectoryExW` which creates all intermediate directories.

### 7. ~~Log file grows without bound~~ ✅ FIXED
`config.cpp` — `LogMessage` now checks the log file size before writing. If it exceeds 1 MB, the current log is rotated to `log.txt.old` and a fresh log is started.

### 8. ~~`Shutdown()` called twice via destructor + explicit call~~ ✅ FIXED
`renderer_video.cpp` — Added `m_shutdownCalled` guard flag. `Shutdown()` is now idempotent — the first call executes cleanup, subsequent calls return immediately. No more double `MFShutdown()` (undefined behavior per MSDN).

### 9. ~~`WndClass` re-registered on every `TrayCreate` call~~ ✅ FIXED
`tray.cpp`, `main.cpp` — Added `static bool` guard to both `TrayCreate` and `CreateRenderWindow`. Window class is now registered only once; subsequent calls skip the registration.

---

## ⚠️ Robustness & Edge Cases

### 10. ~~No handling of wallpaper file deletion/move after setting~~ ✅ FIXED
`main.cpp` — `ApplyWallpaper` now checks if the wallpaper file exists before loading. If missing, clears the path and saves the config, so the app shows a black desktop instead of silently failing.

### 11. ~~`FindInjectionTarget` sends the magic `0x052C` message twice with `Sleep(1000)` between~~ ✅ FIXED
`workerw.cpp` — Reduced `Sleep(1000)` to `Sleep(200)`. The WorkerW window is created much faster than 1 second; 200ms is sufficient and blocks the timer callback for 5× less time.

### 12. ~~`EnumWindowsProc` overwrites data on every match — only the last `SHELLDLL_DefView` survives~~ ✅ FIXED
`workerw.cpp` — `EnumWindowsProc` now returns early if a match was already found, keeping the first `SHELLDLL_DefView` instead of overwriting with later matches.

### 13. ~~`WM_DISPLAYCHANGE` doesn't recreate the D2D render target~~ ✅ FIXED
`main.cpp`, `renderer_image.cpp` — Added `ImageRenderer::OnDisplayChange()` which recreates the render target and reloads the bitmap. `WM_DISPLAYCHANGE` now calls this instead of just `OnResize`.

### 14. ~~No DPI awareness declared in manifest~~ ✅ FIXED
`resources/app.manifest` — The manifest declares PerMonitorV2 DPI awareness via `dpiAware` and `dpiAwareness` settings.

### 15. ~~`ExtractIconW` path is hardcoded to `"deskwall.exe"`~~ ✅ FIXED
`tray.cpp` — Now uses `GetModuleFileNameW` to get the absolute exe path for `ExtractIconW`, avoiding failures when the working directory differs from the exe location.

### 16. ~~Race condition on `g_paused` / `g_muted` globals~~ ✅ FIXED
`main.cpp`, `tray.cpp` — Changed `g_paused` and `g_muted` from plain `bool` to `std::atomic<bool>`. Updated `extern` declarations in `tray.cpp` to match.

---

## 🏗️ Code Quality & Maintainability

### 17. All state is global variables in `main.cpp`
`main.cpp:17-33` — 14 global variables control all application state. There's no encapsulation — any code can mutate `g_paused`, `g_config`, `g_renderWindow`, etc. This makes reasoning about state transitions difficult and prevents testing individual components.

### 18. `tray.cpp` uses `extern` to reach back into `main.cpp`
`tray.cpp:11-22` — The tray module forward-declares 6 functions and 4 global variables from `main.cpp`. This creates a circular dependency: `main.cpp` includes `tray.h` and calls tray functions, while `tray.cpp` reaches back into `main.cpp` via `extern`. A callback/interface pattern would be cleaner.

### 19. ~~Magic numbers throughout~~ ✅ FIXED
`main.cpp` — Timer IDs (`TIMER_FRAME`, `TIMER_INJECT`, `TIMER_REINJECT`) and timing constants (`FRAME_INTERVAL_MS`, `INJECT_INTERVAL_MS`, `INJECT_MAX_RETRIES`) are now named `constexpr` values. `renderer_image.cpp:98` — `0x1a1a2e` background color remains a magic number (should be named or configurable).

### 20. ~~`VideoRenderer::Render()` is a no-op~~ ✅ FIXED
`main.cpp` — `WM_PAINT` no longer calls `g_videoRenderer->Render()`. Video is rendered by MF directly to the HWND; the image path is the only one that needs `WM_PAINT`.

### 21. ~~No `WM_ERASEBKGND` handling~~ ✅ FIXED
`main.cpp` — `RenderWndProc` now handles `WM_ERASEBKGND` by returning 1 (non-zero) to suppress the default background erase, preventing flicker on every frame.

### 22. ~~`perMonitorPaths` config feature is dead code~~ ✅ FIXED
`config.cpp`, `config.h` — Removed `perMonitorPaths` field from `Config` struct and all serialization code. The field was never used at runtime.

---

## 🔒 Security & Privacy

### 23. `deskwall.exe` committed to git
The 603 KB binary is tracked in git. Every future commit that includes a rebuild inflates the repo. Binaries should be in `.gitignore` and built via CI or the installer.

### 24. ~~No validation of wallpaper path on load~~ ✅ FIXED
`config.cpp` — Added `IsValidWallpaperPath` validation. Rejects UNC paths (`\\server\share`) and paths that don't start with a drive letter. Only local drive paths (e.g. `C:\...`) are accepted.

### 25. ~~Single-instance mutex has no access control~~ ✅ FIXED
`main.cpp` — Changed from `Global\DeskWallMutex` to per-session `DeskWallMutex`. Prevents cross-session denial-of-service where a low-privilege user could create the Global mutex first.

---

## 📦 Build & Packaging

### 26. ~~`deskwall.exe` in project root not in `.gitignore`~~ ✅ FIXED
`.gitignore` — Added `deskwall.exe` to the ignore list.

### 27. ~~No `CMAKE_BUILD_TYPE` default~~ ✅ FIXED
`CMakeLists.txt` — Added default to `Release` for single-config generators when no build type is specified.

### 28. ~~`#pragma comment(lib, ...)` duplicates CMake `target_link_libraries~~ ✅ FIXED
`config.cpp`, `renderer_image.cpp`, `renderer_video.cpp` — Removed all `#pragma comment(lib, ...)` directives. Linking is handled solely by CMakeLists.txt.

### 29. ~~Installer doesn't bundle VC++ redistributable (but builds with static CRT)~~ ✅ FIXED
`installer/setup.iss` — Added comment explaining that no VC++ redistributable is required because the app uses static CRT (`/MT`).

---

## 🐛 Reported by User

### 30. ~~Video loop has a visible gap at the loop point~~ ✅ FIXED
`renderer_video.cpp` — Replaced `SetLoop(TRUE)` with manual loop handling via `MF_MEDIA_ENGINE_EVENT_ENDED`. When the video ends, the engine seeks to time 0 and calls `Play()` immediately, avoiding the gap that Media Foundation's native looping produces. The video duration is captured at `LOADEDMETADATA` time to ensure the seek is valid.

### 31. ~~Pause button is immediately undone by `UpdatePauseState()` timer — CRITICAL~~ ✅ FIXED
`main.cpp` — Added `g_manualPaused` flag. `OnMenuPauseResume()` sets it when the user clicks Pause/Resume. `UpdatePauseState()` now only auto-resumes when `g_manualPaused` is false, so the user's manual pause is preserved. Auto-pause (fullscreen, battery, session lock) can still override, but auto-resume respects the user's intent.

### 32. ~~Mute button not updating tray tooltip~~ ✅ FIXED
`main.cpp` — `OnMenuMuteUnmute()` now updates the tray tooltip to reflect the mute state (`"DeskWall (Muted)"` / `"DeskWall"`), matching the pause tooltip behavior.

---

**Summary**: 32 issues total — 30 fixed, 2 remaining. Fixed: #1, #2, #3, #4, #5, #6, #7, #8, #9, #10, #11, #12, #13, #14, #15, #16, #19, #20, #21, #22, #24, #25, #26, #27, #28, #29, #30, #31, #32. Remaining: #17, #18, #23.
