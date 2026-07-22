#pragma once
#include <windows.h>

// Attempt to parent our render window into Explorer's WorkerW layer.
// Returns true on success. Caller should retry on failure.
bool TryInjectWallpaperWindow(HWND renderWindow);

// Re-inject after Explorer restarts (TaskbarCreated message).
void OnExplorerRestarted(HWND renderWindow);

// Check if running inside a Remote Desktop session.
bool IsRemoteDesktopSession();

// Get the virtual screen rectangle (covers all monitors).
RECT GetVirtualScreenRect();
