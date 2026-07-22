#include "renderer_video.h"
#include "config.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

VideoRenderer::VideoRenderer() {}

VideoRenderer::~VideoRenderer() { Shutdown(); }

// IUnknown
HRESULT STDMETHODCALLTYPE VideoRenderer::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
        *ppvObject = static_cast<IMFMediaEngineNotify*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VideoRenderer::AddRef() { return InterlockedIncrement(&m_refCount); }

ULONG STDMETHODCALLTYPE VideoRenderer::Release() {
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

// IMFMediaEngineNotify
HRESULT STDMETHODCALLTYPE VideoRenderer::EventNotify(DWORD event, DWORD_PTR, DWORD) {
    if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) {
        m_mediaEngineReady = true;
        if (m_mediaEngine) {
            m_mediaEngine->SetLoop(TRUE);
            m_mediaEngine->SetMuted(m_muted ? TRUE : FALSE);
            m_mediaEngine->SetVolume(m_volume);
            m_mediaEngine->Play();
        }
    }
    return S_OK;
}

bool VideoRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, featureLevels, 1, D3D11_SDK_VERSION,
        &m_d3dDevice, &featureLevel, &m_d3dContext);
    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: D3D11 device creation failed");
        return false;
    }

    if (!CreateSwapChain()) return false;

    // Initialize Media Foundation
    MFStartup(MF_VERSION);

    // Create Media Engine
    IMFMediaEngineClassFactory* factory = nullptr;
    hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_ALL,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: MediaEngine factory creation failed");
        return false;
    }

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 4);
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, this);
    attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);

    // MF_MEDIA_ENGINE_REAL_TIME_MODE is a creation flag (0x2), not an attribute
    DWORD createFlags = MF_MEDIA_ENGINE_REAL_TIME_MODE;
    hr = factory->CreateInstance(createFlags, attrs, &m_mediaEngine);
    if (attrs) attrs->Release();
    if (factory) factory->Release();

    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: MediaEngine instance creation failed");
        return false;
    }

    return true;
}

bool VideoRenderer::CreateSwapChain() {
    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = m_d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return false;

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) return false;

    IDXGIFactory2* factory = nullptr;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    adapter->Release();
    if (FAILED(hr)) return false;

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = rc.right - rc.left;
    desc.Height = rc.bottom - rc.top;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;

    hr = factory->CreateSwapChainForHwnd(m_d3dDevice, m_hwnd, &desc, nullptr, nullptr, &m_swapChain);
    factory->Release();

    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: Swap chain creation failed");
        return false;
    }

    UpdateRenderTargetView();
    return true;
}

void VideoRenderer::UpdateRenderTargetView() {
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }

    ID3D11Texture2D* backBuffer = nullptr;
    if (m_swapChain && SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        m_d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
        backBuffer->Release();
    }
}

bool VideoRenderer::LoadVideo(const std::wstring& path) {
    if (!m_mediaEngine) return false;

    m_mediaEngineReady = false;

    BSTR bstr = SysAllocString(path.c_str());
    HRESULT hr = m_mediaEngine->SetSource(bstr);
    SysFreeString(bstr);

    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: SetSource failed for: " + path);
        return false;
    }

    return true;
}

void VideoRenderer::Render() {
    if (!m_mediaEngine || !m_mediaEngineReady || !m_swapChain) return;

    // Transfer the media engine's frame to the swap chain back buffer
    LONGLONG pts = 0;
    if (m_mediaEngine->OnVideoStreamTick(&pts) == S_OK) {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            // Transfer frame via MediaEngine's TransferVideoFrame
            // Create a DXGI surface wrapper for the back buffer
            IDXGISurface* surface = nullptr;
            if (SUCCEEDED(backBuffer->QueryInterface(IID_PPV_ARGS(&surface)))) {
                MFVideoNormalizedRect srcRect = { 0.0f, 0.0f, 1.0f, 1.0f };
                RECT dstRect;
                GetClientRect(m_hwnd, &dstRect);
                m_mediaEngine->TransferVideoFrame(surface, &srcRect, &dstRect, nullptr);
                surface->Release();
            }
            backBuffer->Release();
        }
    }

    m_swapChain->Present(1, 0);
}

void VideoRenderer::SetMuted(bool muted) {
    m_muted = muted;
    if (m_mediaEngine) m_mediaEngine->SetMuted(muted ? TRUE : FALSE);
}

void VideoRenderer::SetVolume(float volume) {
    m_volume = volume;
    if (m_mediaEngine) m_mediaEngine->SetVolume(volume);
}

void VideoRenderer::Pause() {
    m_paused = true;
    if (m_mediaEngine) m_mediaEngine->Pause();
}

void VideoRenderer::Resume() {
    m_paused = false;
    if (m_mediaEngine) m_mediaEngine->Play();
}

void VideoRenderer::OnResize(UINT width, UINT height) {
    if (m_swapChain && width > 0 && height > 0) {
        if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
        m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        UpdateRenderTargetView();
    }
}

void VideoRenderer::Shutdown() {
    if (m_mediaEngine) {
        m_mediaEngine->Shutdown();
        m_mediaEngine->Release();
        m_mediaEngine = nullptr;
    }
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
    if (m_d3dDevice) { m_d3dDevice->Release(); m_d3dDevice = nullptr; }
    MFShutdown();
}
