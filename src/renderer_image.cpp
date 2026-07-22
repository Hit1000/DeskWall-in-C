#include "renderer_image.h"
#include "config.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

ImageRenderer::ImageRenderer() {}

ImageRenderer::~ImageRenderer() { Shutdown(); }

bool ImageRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory);
    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: D2D1CreateFactory failed");
        return false;
    }

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&m_wicFactory));
    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: WIC factory creation failed");
        return false;
    }

    return CreateRenderTarget();
}

void ImageRenderer::Shutdown() {
    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
    if (m_wicFactory) { m_wicFactory->Release(); m_wicFactory = nullptr; }
    if (m_factory) { m_factory->Release(); m_factory = nullptr; }
}

bool ImageRenderer::CreateRenderTarget() {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    HRESULT hr = m_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size,
            D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &m_renderTarget);

    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: CreateHwndRenderTarget failed");
        return false;
    }
    return true;
}

bool ImageRenderer::LoadImageFile(const std::wstring& path) {
    if (!m_wicFactory || !m_renderTarget) return false;

    // Release old bitmap
    if (m_bitmap) {
        m_bitmap->Release();
        m_bitmap = nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: CreateDecoderFromFilename failed for: " + path);
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        decoder->Release();
        LogMessage(L"ImageRenderer: GetFrame failed");
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = m_wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
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

void ImageRenderer::DrawImage() {
    if (!m_renderTarget || !m_bitmap) return;

    D2D1_SIZE_F rtSize = m_renderTarget->GetSize();
    D2D1_SIZE_F bmpSize = m_bitmap->GetSize();

    // Stretch to fill while maintaining aspect ratio (cover mode)
    float scaleX = rtSize.width / bmpSize.width;
    float scaleY = rtSize.height / bmpSize.height;
    float scale = (scaleX > scaleY) ? scaleX : scaleY;

    float drawW = bmpSize.width * scale;
    float drawH = bmpSize.height * scale;
    float offsetX = (rtSize.width - drawW) / 2.0f;
    float offsetY = (rtSize.height - drawH) / 2.0f;

    D2D1_RECT_F destRect = D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH);

    m_renderTarget->DrawBitmap(m_bitmap, destRect, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void ImageRenderer::Render() {
    if (!m_renderTarget) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0x1a1a2e)); // dark background fallback
    DrawImage();
    m_renderTarget->EndDraw();
}

void ImageRenderer::OnResize(UINT width, UINT height) {
    if (m_renderTarget && width > 0 && height > 0) {
        m_renderTarget->Resize(D2D1::SizeU(width, height));
    }
}
