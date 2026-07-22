#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

using json = nlohmann::json;

static std::wstring GetLocalAppData() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return path;
    }
    return L"";
}

std::wstring ConfigDir() {
    return GetLocalAppData() + L"\\DeskWall";
}

std::wstring ConfigPath() {
    return ConfigDir() + L"\\config.json";
}

std::wstring LogPath() {
    return ConfigDir() + L"\\log.txt";
}

static void EnsureDirExists() {
    std::wstring dir = ConfigDir();
    CreateDirectoryW(dir.c_str(), nullptr);
}

void LogMessage(const std::wstring& msg) {
    EnsureDirExists();
    std::wofstream log(LogPath(), std::ios::app);
    if (log.is_open()) {
        // Simple timestamp
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
            << L" " << st.wHour << L":" << st.wMinute << L":" << st.wSecond
            << L"] " << msg << std::endl;
    }
}

// JSON serialization helpers
static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), result.data(), size, nullptr, nullptr);
    return result;
}

static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), result.data(), size);
    return result;
}

Config ConfigLoad() {
    Config cfg;
    std::ifstream file(ConfigPath());
    if (!file.is_open()) return cfg;

    try {
        json j;
        file >> j;

        std::string type = j.value("wallpaperType", "image");
        if (type == "image") cfg.wallpaperType = WallpaperType::Image;
        else if (type == "video") cfg.wallpaperType = WallpaperType::Video;
        else cfg.wallpaperType = WallpaperType::None;

        cfg.wallpaperPath = Utf8ToWide(j.value("wallpaperPath", ""));

        std::string mode = j.value("monitorMode", "span");
        if (mode == "duplicate") cfg.monitorMode = MonitorMode::Duplicate;
        else if (mode == "perMonitor") cfg.monitorMode = MonitorMode::PerMonitor;
        else cfg.monitorMode = MonitorMode::Span;

        if (j.contains("perMonitorPaths") && j["perMonitorPaths"].is_object()) {
            for (auto& [key, val] : j["perMonitorPaths"].items()) {
                cfg.perMonitorPaths[Utf8ToWide(key)] = Utf8ToWide(val.get<std::string>());
            }
        }

        cfg.muted = j.value("muted", true);
        cfg.volume = j.value("volume", 0.5f);
        cfg.fpsCap = j.value("fpsCap", 30);
        cfg.pauseOnBattery = j.value("pauseOnBattery", true);
        cfg.pauseOnFullscreenApp = j.value("pauseOnFullscreenApp", true);
        cfg.startWithWindows = j.value("startWithWindows", true);
    } catch (const std::exception& e) {
        LogMessage(L"Config: Failed to parse config.json: " + Utf8ToWide(e.what()));
    }

    return cfg;
}

void ConfigSave(const Config& cfg) {
    EnsureDirExists();
    json j;

    switch (cfg.wallpaperType) {
        case WallpaperType::Image: j["wallpaperType"] = "image"; break;
        case WallpaperType::Video: j["wallpaperType"] = "video"; break;
        default: j["wallpaperType"] = "none"; break;
    }

    j["wallpaperPath"] = WideToUtf8(cfg.wallpaperPath);

    switch (cfg.monitorMode) {
        case MonitorMode::Span: j["monitorMode"] = "span"; break;
        case MonitorMode::Duplicate: j["monitorMode"] = "duplicate"; break;
        case MonitorMode::PerMonitor: j["monitorMode"] = "perMonitor"; break;
    }

    json paths = json::object();
    for (auto& [k, v] : cfg.perMonitorPaths) {
        paths[WideToUtf8(k)] = WideToUtf8(v);
    }
    j["perMonitorPaths"] = paths;

    j["muted"] = cfg.muted;
    j["volume"] = cfg.volume;
    j["fpsCap"] = cfg.fpsCap;
    j["pauseOnBattery"] = cfg.pauseOnBattery;
    j["pauseOnFullscreenApp"] = cfg.pauseOnFullscreenApp;
    j["startWithWindows"] = cfg.startWithWindows;

    std::ofstream file(ConfigPath());
    if (file.is_open()) {
        file << j.dump(2);
    } else {
        LogMessage(L"Config: Failed to write config.json");
    }
}
