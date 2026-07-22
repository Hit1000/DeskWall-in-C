#include "renderer_video.h"
#include "config.h"
#include <shlwapi.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

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
    if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
        LogMessage(L"VideoRenderer: Media Engine error");
    }
    return S_OK;
}

bool VideoRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    MFStartup(MF_VERSION);

    IMFMediaEngineClassFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: MediaEngine factory creation failed");
        return false;
    }

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 2);

    // Callback for lifecycle events
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, this);

    // HWND playback mode: Media Foundation presents frames directly to this window.
    // This is the key attribute — without it, MF has nowhere to render.
    attrs->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, (UINT64)(ULONG_PTR)m_hwnd);

    // MF_MEDIA_ENGINE_REAL_TIME_MODE is a creation flag (0x2)
    hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attrs, &m_mediaEngine);

    if (attrs) attrs->Release();
    if (factory) factory->Release();

    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: MediaEngine instance creation failed");
        return false;
    }

    return true;
}

bool VideoRenderer::LoadVideo(const std::wstring& path) {
    if (!m_mediaEngine) return false;
    m_mediaEngineReady = false;

    // Convert filesystem path to file:// URL — MFMediaEngine::SetSource requires a URL
    wchar_t url[2084]; // INTERNET_MAX_URL_LENGTH
    DWORD urlLen = 2084;
    HRESULT hr = UrlCreateFromPathW(path.c_str(), url, &urlLen, 0);
    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: UrlCreateFromPathW failed for: " + path);
        return false;
    }

    BSTR bstr = SysAllocString(url);
    hr = m_mediaEngine->SetSource(bstr);
    SysFreeString(bstr);

    if (FAILED(hr)) {
        LogMessage(L"VideoRenderer: SetSource failed for: " + path);
        return false;
    }

    return true;
}

void VideoRenderer::Render() {
    // In HWND playback mode, MF presents frames to the window itself.
    // Nothing to draw here — the engine handles it.
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

void VideoRenderer::OnResize(UINT, UINT) {
    // HWND playback mode handles resize automatically
}

void VideoRenderer::Shutdown() {
    if (m_mediaEngine) {
        m_mediaEngine->Shutdown();
        m_mediaEngine->Release();
        m_mediaEngine = nullptr;
    }
    MFShutdown();
}
