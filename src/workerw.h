#pragma once
#include <windows.h>

struct InjectionResult {
    HWND parent;          // Window to SetParent into
    HWND insertAfter;     // Z-order anchor (for raised desktop), or NULL
    HWND systemWorkerW;   // The actual WorkerW handle (for raised desktop push-behind)
    bool needsLayered;    // True if raised desktop — window needs WS_EX_LAYERED
    bool found;           // True if injection target was found
};

// Find the injection target (WorkerW/Progman) without needing a window yet.
// Send the spawn message and enumerate. Returns found=false if nothing found.
InjectionResult FindInjectionTarget();

// Apply the injection: parent the render window into the found target.
// Returns true on success.
bool ApplyInjection(HWND renderWindow, const InjectionResult& injection);

// One-shot: find target + apply injection. Retries internally on failure.
bool TryInjectWallpaperWindow(HWND renderWindow);

// Re-inject after Explorer restarts (TaskbarCreated message).
// Returns true if injection succeeded, false if retry is needed.
bool ReInjectAfterExplorerRestart(HWND renderWindow);

// Check if running inside a Remote Desktop session.
bool IsRemoteDesktopSession();

// Get the virtual screen rectangle (covers all monitors).
RECT GetVirtualScreenRect();
