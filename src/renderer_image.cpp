#include "renderer_image.h"
#include "config.h"
#include "workerw.h"

ImageRenderer::ImageRenderer() {}
ImageRenderer::~ImageRenderer() { Shutdown(); }

bool ImageRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    // D2D/WIC factories and render target are created lazily in LoadImageFile
    // to avoid allocating GPU resources when only video wallpapers are used.
    return true;
}

void ImageRenderer::Shutdown() {
    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
    if (m_wicFactory) { m_wicFactory->Release(); m_wicFactory = nullptr; }
    if (m_factory) { m_factory->Release(); m_factory = nullptr; }
}

// Ensure D2D/WIC factories and render target exist. Called lazily from
// LoadImageFile and OnDisplayChange so GPU resources are only allocated
// when an image wallpaper is actually in use (not for video-only setups).
bool ImageRenderer::EnsureResources() {
    if (m_renderTarget) return true;

    if (!m_factory) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory);
        if (FAILED(hr)) { LogMessage(L"ImageRenderer: D2D1CreateFactory failed"); return false; }
    }
    if (!m_wicFactory) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_wicFactory));
        if (FAILED(hr)) { LogMessage(L"ImageRenderer: WIC factory creation failed"); return false; }
    }

    RECT vr = GetVirtualScreenRect();
    UINT w = vr.right - vr.left;
    UINT h = vr.bottom - vr.top;

    HRESULT hr = m_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(w, h), D2D1_PRESENT_OPTIONS_NONE),
        &m_renderTarget);
    if (FAILED(hr)) { LogMessage(L"ImageRenderer: CreateHwndRenderTarget failed"); return false; }

    return true;
}

bool ImageRenderer::LoadImageFile(const std::wstring& path) {
    m_imagePath = path;
    if (!EnsureResources()) return false;

    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: CreateDecoderFromFilename failed for: " + path);
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); return false; }

    IWICFormatConverter* converter = nullptr;
    hr = m_wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    }

    // Create bitmap at original resolution — D2D's DrawBitmap handles
    // scaling to render target size on the GPU, no CPU-side scaling needed.
    if (SUCCEEDED(hr)) {
        hr = m_renderTarget->CreateBitmapFromWicBitmap(converter, nullptr, &m_bitmap);
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();

    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: Failed to create D2D bitmap");
        return false;
    }
    return true;
}

void ImageRenderer::Render() {
    if (!m_renderTarget || !m_bitmap) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0x1a1a2e));

    D2D1_SIZE_F rtSize = m_renderTarget->GetSize();
    D2D1_RECT_F destRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);
    m_renderTarget->DrawBitmap(m_bitmap, destRect, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    HRESULT hr = m_renderTarget->EndDraw();
    if (FAILED(hr)) LogMessage(L"ImageRenderer: EndDraw failed");
}

void ImageRenderer::OnResize(UINT width, UINT height) {
    if (m_renderTarget && width > 0 && height > 0)
        m_renderTarget->Resize(D2D1::SizeU(width, height));
}

void ImageRenderer::OnDisplayChange() {
    // Release stale render target (DPI is baked into it).
    // Keep factories — they're DPI-independent and reusable.
    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }

    // Reload image — this recreates the render target via EnsureResources.
    if (!m_imagePath.empty()) {
        LoadImageFile(m_imagePath);
    }
}
