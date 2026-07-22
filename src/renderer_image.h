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

    // Load an image file. Returns true on success.
    bool LoadImageFile(const std::wstring& path);

    // Render the current image to fill the window.
    void Render();

    // Resize render target when window size changes.
    void OnResize(UINT width, UINT height);

    bool HasImage() const { return m_bitmap != nullptr; }

private:
    HWND m_hwnd = nullptr;
    ID2D1Factory* m_factory = nullptr;
    ID2D1HwndRenderTarget* m_renderTarget = nullptr;
    ID2D1Bitmap* m_bitmap = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;

    bool CreateRenderTarget();
    void DrawImage();
};
