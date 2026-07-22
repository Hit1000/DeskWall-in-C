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

    return true;
}

void ImageRenderer::Shutdown() {
    if (m_bitmap) { m_bitmap->Release(); m_bitmap = nullptr; }
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
    if (m_wicFactory) { m_wicFactory->Release(); m_wicFactory = nullptr; }
    if (m_factory) { m_factory->Release(); m_factory = nullptr; }
}

bool ImageRenderer::CreateRenderTarget(UINT width, UINT height) {
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }

    // Use explicit size — GetClientRect can return 0x0 before the parent
    // lays out the child window.
    if (width == 0 || height == 0) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
    }

    if (width == 0 || height == 0) {
        LogMessage(L"ImageRenderer: Render target size is 0x0");
        return false;
    }

    D2D1_SIZE_U size = D2D1::SizeU(width, height);

    HRESULT hr = m_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size,
            D2D1_PRESENT_OPTIONS_NONE),
        &m_renderTarget);

    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: CreateHwndRenderTarget failed");
        return false;
    }
    return true;
}

bool ImageRenderer::LoadImageFile(const std::wstring& path) {
    if (!m_wicFactory) return false;

    // Release old bitmap
    if (m_bitmap) {
        m_bitmap->Release();
        m_bitmap = nullptr;
    }

    // Get window size for render target
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UINT rtWidth = rc.right - rc.left;
    UINT rtHeight = rc.bottom - rc.top;

    // Create render target if needed
    if (!m_renderTarget) {
        if (!CreateRenderTarget(rtWidth, rtHeight)) return false;
    }

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
    if (FAILED(hr)) {
        decoder->Release();
        LogMessage(L"ImageRenderer: GetFrame failed");
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = m_wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    }

    // Scale to window size to save memory (like Rust version)
    IWICBitmapScaler* scaler = nullptr;
    if (SUCCEEDED(hr) && rtWidth > 0 && rtHeight > 0) {
        hr = m_wicFactory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(converter, rtWidth, rtHeight,
                WICBitmapInterpolationModeFant);
        }
    }

    if (SUCCEEDED(hr)) {
        // Use scaler if available, otherwise converter
        IWICBitmapSource* source = scaler ? (IWICBitmapSource*)scaler : (IWICBitmapSource*)converter;
        hr = m_renderTarget->CreateBitmapFromWicBitmap(source, nullptr, &m_bitmap);
    }

    if (scaler) scaler->Release();
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
    if (FAILED(hr)) {
        LogMessage(L"ImageRenderer: EndDraw failed");
    }
}

void ImageRenderer::OnResize(UINT width, UINT height) {
    if (m_renderTarget && width > 0 && height > 0) {
        m_renderTarget->Resize(D2D1::SizeU(width, height));
    }
}
