#pragma once
#include <windows.h>
#include <string>
#include <map>

enum class WallpaperType { None, Image, Video };
enum class MonitorMode { Span, Duplicate, PerMonitor };

struct Config {
    WallpaperType wallpaperType = WallpaperType::None;
    std::wstring wallpaperPath;
    MonitorMode monitorMode = MonitorMode::Span;
    std::map<std::wstring, std::wstring> perMonitorPaths; // device name -> path
    bool muted = true;
    float volume = 0.5f;
    int fpsCap = 30;
    bool pauseOnBattery = true;
    bool pauseOnFullscreenApp = true;
    bool startWithWindows = true;
};

// Load config from %LOCALAPPDATA%\DeskWall\config.json
Config ConfigLoad();

// Save config to %LOCALAPPDATA%\DeskWall\config.json
void ConfigSave(const Config& config);

// Get the config directory path.
std::wstring ConfigDir();

// Get the config file path.
std::wstring ConfigPath();

// Get the log file path.
std::wstring LogPath();

// Write a line to the log file.
void LogMessage(const std::wstring& msg);
