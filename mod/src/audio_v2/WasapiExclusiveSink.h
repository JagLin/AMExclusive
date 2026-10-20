#pragma once

#include "ExclusiveSink.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ammod::audio_v2 {

class WasapiExclusiveSink final : public IExclusiveSink {
public:
    WasapiExclusiveSink() noexcept = default;
    ~WasapiExclusiveSink() override;

    WasapiExclusiveSink(const WasapiExclusiveSink&) = delete;
    WasapiExclusiveSink& operator=(const WasapiExclusiveSink&) = delete;

    HRESULT Open(const ExclusiveSinkConfig& config) noexcept override;
    HRESULT Start(IIntegerPcmSource& source) noexcept override;
    void Stop() noexcept override;
    void Close() noexcept override;

    ExclusiveSinkState State() const noexcept override {
        return state_.load(std::memory_order_acquire);
    }
    ExclusiveSinkStats Stats() const noexcept override;
    const PcmFormat& Format() const noexcept override { return config_.format; }
    bool WaitingForSource() const noexcept override {
        return waitingForSource_.load(std::memory_order_acquire);
    }
    bool EndOfStreamSubmitted() const noexcept override {
        return endOfStreamSubmitted_.load(std::memory_order_acquire);
    }
    void SetResumeBlocked(bool blocked) noexcept override;

private:
    static DWORD WINAPI RenderThreadThunk(void* context) noexcept;
    DWORD RenderThreadMain() noexcept;

    HRESULT ActivateClient() noexcept;
    HRESULT InitializeClient() noexcept;
    HRESULT RenderOneBuffer() noexcept;
    void RecordError(HRESULT error) noexcept;
    void NotifyLifecycle(ExclusiveSinkLifecycleEvent event, HRESULT result) noexcept;
    void ReleaseInterfaces() noexcept;

    ExclusiveSinkConfig config_{};
    WAVEFORMATEXTENSIBLE waveFormat_{};
    std::vector<std::int32_t> canonicalBuffer_;

    IMMDeviceEnumerator* enumerator_{};
    IMMDevice* device_{};
    IAudioClient* client_{};
    IAudioRenderClient* render_{};

    HANDLE shutdownEvent_{};
    HANDLE audioEvent_{};
    HANDLE renderThread_{};
    std::atomic<bool> stopRequested_{};
    std::atomic<bool> waitingForSource_{};
    std::atomic<bool> resumeBlocked_{};
    std::atomic<bool> endOfStreamSubmitted_{};
    IIntegerPcmSource* source_{};
    std::uint32_t bufferFrames_{};
    bool comInitialized_{};

    std::atomic<ExclusiveSinkState> state_{ExclusiveSinkState::Closed};
    std::atomic<std::uint64_t> submittedFrames_{};
    std::atomic<std::uint64_t> submittedBuffers_{};
    std::atomic<std::uint64_t> controlledSilenceFrames_{};
    std::atomic<std::uint64_t> sourceWaits_{};
    std::atomic<std::uint64_t> underruns_{};
    std::atomic<std::uint64_t> getBufferFailures_{};
    std::atomic<std::uint64_t> releaseBufferFailures_{};
    std::atomic<LONG> lastError_{S_OK};
};

} // namespace ammod::audio_v2
