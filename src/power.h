#pragma once
#include <windows.h>

// Check if the foreground window is a fullscreen app.
bool IsFullscreenAppInForeground();

// Check if running on battery power.
bool IsOnBattery();

// Register for session notifications (lock/unlock).
void RegisterSessionNotifications(HWND hwnd);

// Unregister session notifications.
void UnregisterSessionNotifications(HWND hwnd);
