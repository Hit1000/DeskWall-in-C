#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <string>

class VideoRenderer : public IMFMediaEngineNotify {
public:
    VideoRenderer();
    ~VideoRenderer();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IMFMediaEngineNotify
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override;

    bool Initialize(HWND hwnd);
    void Shutdown();

    bool LoadVideo(const std::wstring& path);
    void Render();

    void SetMuted(bool muted);
    bool IsMuted() const { return m_muted; }
    void SetVolume(float volume);
    float GetVolume() const { return m_volume; }

    void Pause();
    void Resume();
    bool IsPaused() const { return m_paused; }

    bool HasVideo() const { return m_mediaEngine != nullptr; }

    void OnResize(UINT width, UINT height);

private:
    volatile LONG m_refCount = 1;
    HWND m_hwnd = nullptr;
    bool m_muted = true;
    float m_volume = 0.5f;
    bool m_paused = false;
    bool m_mediaEngineReady = false;
    bool m_shutdownCalled = false;

    IMFMediaEngine* m_mediaEngine = nullptr;
};
