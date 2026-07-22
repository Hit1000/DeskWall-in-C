cmake_minimum_required(VERSION 3.20)
project(DeskWall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT for zero VC++ Redist dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Fetch nlohmann_json — only third-party dependency
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(deskwall WIN32
    src/main.cpp
    src/workerw.cpp
    src/renderer_image.cpp
    src/renderer_video.cpp
    src/tray.cpp
    src/config.cpp
    src/power.cpp
    resources/app.rc
)

target_include_directories(deskwall PRIVATE src resources)

target_link_libraries(deskwall PRIVATE
    nlohmann_json::nlohmann_json
    # Windows essentials
    user32 gdi32 shell32 ole32 oleaut32
    # Direct2D + WIC (image rendering)
    d2d1 windowscodecs
    # Media Foundation (video rendering — HWND playback, no D3D11 needed)
    mf mfplat mfuuid
    # Other
    shlwapi uuid
)

# /W4 for good warnings, /O1 for release size
target_compile_options(deskwall PRIVATE /W4 /utf-8)
target_compile_options(deskwall PRIVATE $<$<CONFIG:Release>:/O1>)

# Disable linker-generated manifest (we embed our own via app.rc)
target_link_options(deskwall PRIVATE /MANIFEST:NO)
