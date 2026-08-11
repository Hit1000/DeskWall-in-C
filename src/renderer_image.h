#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>

class ImageRenderer {
public:
    ImageRenderer();
    ~ImageRenderer();

    bool Initialize(HWND hwnd);
    void Shutdown();

    bool LoadImageFile(const std::wstring& path);
    void Render();

    void OnResize(UINT width, UINT height);
    // Recreate the render target after a display change (resolution/DPI).
    // Also reloads the bitmap since the old render target is discarded.
    void OnDisplayChange();
    bool HasImage() const { return m_bitmap != nullptr; }

private:
    bool EnsureResources();

    HWND m_hwnd = nullptr;
    ID2D1Factory* m_factory = nullptr;
    ID2D1HwndRenderTarget* m_renderTarget = nullptr;
    ID2D1Bitmap* m_bitmap = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;
    std::wstring m_imagePath; // kept for reload after display change
};
