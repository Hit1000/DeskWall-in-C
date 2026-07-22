#pragma once
#include <windows.h>

struct InjectionResult {
    HWND parent;          // Window to SetParent into
    HWND insertAfter;     // Z-order anchor (for raised desktop), or NULL
    HWND systemWorkerW;   // The actual WorkerW handle (for raised desktop push-behind)
    bool needsLayered;    // True if raised desktop — window needs WS_EX_LAYERED
};

// Attempt to parent our render window into Explorer's WorkerW layer.
// Returns true on success. Caller should retry on failure.
bool TryInjectWallpaperWindow(HWND renderWindow);

// Re-inject after Explorer restarts (TaskbarCreated message).
void OnExplorerRestarted(HWND renderWindow);

// Check if running inside a Remote Desktop session.
bool IsRemoteDesktopSession();

// Get the virtual screen rectangle (covers all monitors).
RECT GetVirtualScreenRect();

// Get the current injection state.
const InjectionResult* GetInjectionState();
