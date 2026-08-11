#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shlobj.h>
#include <shlwapi.h>

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
    // SHCreateDirectoryExW creates all intermediate directories,
    // unlike CreateDirectoryW which only creates the final one.
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
}

void LogMessage(const std::wstring& msg) {
    EnsureDirExists();
    std::wstring logPath = LogPath();

    // Rotate log if it exceeds 1 MB
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER size;
        size.HighPart = fad.nFileSizeHigh;
        size.LowPart = fad.nFileSizeLow;
        if (size.QuadPart > 1024 * 1024) {
            std::wstring oldPath = logPath + L".old";
            DeleteFileW(oldPath.c_str());
            MoveFileW(logPath.c_str(), oldPath.c_str());
        }
    }

    std::wofstream log(logPath, std::ios::app);
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

// Validate that a wallpaper path is safe to use — reject UNC paths
// (\\server\share) and paths that don't start with a drive letter.
// This prevents the app from accessing arbitrary network locations
// if the config file is tampered with.
static bool IsValidWallpaperPath(const std::wstring& path) {
    if (path.empty()) return false;
    // Reject UNC paths
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return false;
    // Must start with a drive letter (e.g. C:\)
    if (path.size() >= 3 && iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\') return true;
    return false;
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

        std::wstring path = Utf8ToWide(j.value("wallpaperPath", ""));
        if (IsValidWallpaperPath(path)) {
            cfg.wallpaperPath = path;
        } else if (!path.empty()) {
            LogMessage(L"Config: Rejected wallpaper path (not a local drive path)");
        }

        std::string mode = j.value("monitorMode", "span");
        if (mode == "duplicate") cfg.monitorMode = MonitorMode::Duplicate;
        else if (mode == "perMonitor") cfg.monitorMode = MonitorMode::PerMonitor;
        else cfg.monitorMode = MonitorMode::Span;

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

    j["muted"] = cfg.muted;
    j["volume"] = cfg.volume;
    j["fpsCap"] = cfg.fpsCap;
    j["pauseOnBattery"] = cfg.pauseOnBattery;
    j["pauseOnFullscreenApp"] = cfg.pauseOnFullscreenApp;
    j["startWithWindows"] = cfg.startWithWindows;

    // Atomic write: write to a temp file first, then rename.
    // This prevents corruption if the app crashes or loses power mid-write.
    std::wstring configPath = ConfigPath();
    std::wstring tempPath = configPath + L".tmp";
    std::ofstream file(tempPath);
    if (file.is_open()) {
        file << j.dump(2);
        file.close();
        // ReplaceConfigFile uses MoveFileEx with MOVEFILE_REPLACE_EXISTING
        // which is atomic on NTFS
        MoveFileExW(tempPath.c_str(), configPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    } else {
        LogMessage(L"Config: Failed to write config.json");
        DeleteFileW(tempPath.c_str());
    }
}
