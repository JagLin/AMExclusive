#pragma once

#include "ExclusiveSink.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Declared by the Steinberg SDK (iasiodrv.h); kept opaque here so only
// AsioSink.cpp depends on the locally supplied SDK.
struct IASIO;

namespace ammod::audio_v2 {

struct AsioCallbackBridge;

// ASIO output backend.
//
// ASIO drivers are apartment-threaded COM objects: they must be created and
// controlled from one STA thread, and init() expects a window handle. Every
// driver call therefore runs on a private STA worker that owns a message-only
// window and pumps messages; the public methods marshal to it and wait.
// Sample data never passes through that worker: the driver's bufferSwitch
// callback pulls from the source under the same rule as the WASAPI render
// thread (no allocation, no waiting, no locks).
//
// The driver stays loaded across Close()/Open() so a track change only
// rebuilds buffers, and sets the sample rate when it differs. It is released
// after kIdleReleaseMs without a new Open(), when another driver is requested,
// or after the driver asked for a reset.
//
// Unlike WASAPI, ASIO cannot decline a period: the half that is not filled in
// time is still played. When the source cannot provide a full period the
// callback writes zeros (counted as controlledSilenceFrames) and asks the
// worker to stop the driver once the last real half has been played; it never
// repeats or invents audio. The leading half at Start() and the tail after
// end-of-stream are zero-filled for the same reason.
class AsioSink final : public IExclusiveSink {
public:
    static constexpr DWORD kIdleReleaseMs = 5000;
    static constexpr DWORD kCommandTimeoutMs = 10000;

    AsioSink() noexcept = default;
    ~AsioSink() override;

    AsioSink(const AsioSink&) = delete;
    AsioSink& operator=(const AsioSink&) = delete;

    HRESULT Open(const ExclusiveSinkConfig& config) noexcept override;
    HRESULT Start(IIntegerPcmSource& source) noexcept override;
    void Stop() noexcept override;
    void Close() noexcept override;

    ExclusiveSinkState State() const noexcept override {
        return state_.load(std::memory_order_acquire);
    }
    ExclusiveSinkStats Stats() const noexcept override;
    const PcmFormat& Format() const noexcept override { return config_.format; }
    // Written by the worker during Open/Close, which the caller waits for.
    std::uint32_t BufferFrames() const noexcept override { return bufferFrames_; }
    bool WaitingForSource() const noexcept override {
        return waitingForSource_.load(std::memory_order_acquire);
    }
    bool EndOfStreamSubmitted() const noexcept override {
        return endOfStreamSubmitted_.load(std::memory_order_acquire);
    }
    void SetResumeBlocked(bool blocked) noexcept override;

private:
    friend struct AsioCallbackBridge;

    enum class Command : std::uint8_t {
        None,
        Open,
        Start,
        Stop,
        Close,
        Shutdown,
    };

    // Caller side.
    HRESULT EnsureWorker() noexcept;
    HRESULT Dispatch(Command command) noexcept;
    void ShutdownWorker() noexcept;

    // Worker side.
    static DWORD WINAPI WorkerThunk(void* context) noexcept;
    DWORD WorkerMain() noexcept;
    HRESULT RunCommand(Command command) noexcept;
    HRESULT OpenOnWorker() noexcept;
    HRESULT StartOnWorker() noexcept;
    void StopOnWorker() noexcept;
    void CloseOnWorker() noexcept;
    HRESULT LoadDriver() noexcept;
    void ReleaseDriver() noexcept;
    HRESULT ApplySampleRate() noexcept;
    HRESULT NegotiateBuffers() noexcept;
    void HandleCallbackSignal() noexcept;
    void TryResumeAfterSourceWait() noexcept;
    void ZeroAllHalves() noexcept;
    void Diagnostic(const std::wstring& text) noexcept;

    // Real-time side (driver callback thread).
    void RenderHalf(long index) noexcept;
    void ZeroHalf(long index) noexcept;
    void PackHalf(long index) noexcept;
    void RequestWorker() noexcept;

    void RecordError(HRESULT error) noexcept;
    void NotifyLifecycle(ExclusiveSinkLifecycleEvent event, HRESULT result) noexcept;

    ExclusiveSinkConfig config_{};

    // Worker thread and caller handshake.
    std::mutex callerMutex_;
    HANDLE worker_{};
    HANDLE commandEvent_{};
    HANDLE doneEvent_{};
    HANDLE callbackEvent_{};
    Command pendingCommand_{Command::None};
    HRESULT commandResult_{S_OK};
    // Command arguments, written by the caller under callerMutex_ and read by
    // the worker only while that command runs. config_ itself is worker-owned.
    ExclusiveSinkConfig pendingConfig_{};
    IIntegerPcmSource* pendingSource_{};
    // Caller-owned: set once a command timed out (see Dispatch).
    bool workerHung_{};
    HWND window_{};

    // Driver session; touched only by the worker, except that the render
    // callback reads the buffers and sample type while the driver runs.
    IASIO* driver_{};
    std::wstring loadedDriverName_;
    bool reloadBeforeOpen_{};
    bool buffersCreated_{};
    bool driverStarted_{};
    bool outputReadySupported_{};
    long sampleType_{-1};
    std::uint32_t bufferFrames_{};
    void* channelBuffers_[2][2]{};
    std::vector<std::int32_t> canonical_;
    ULONGLONG idleDeadline_{};

    // Shared with the render callback.
    IIntegerPcmSource* source_{};
    std::atomic<bool> rendering_{};
    std::atomic<bool> stopRequested_{};
    std::atomic<bool> faultPending_{};
    std::atomic<bool> resetRequested_{};
    std::atomic<bool> waitingForSource_{};
    std::atomic<bool> resumeBlocked_{};
    std::atomic<bool> endOfStreamSubmitted_{};
    // Callbacks still to run before the driver may be stopped, so the last
    // half holding real samples is played out first. Callback-thread only
    // while the driver runs; reset by the worker while it is stopped.
    std::uint32_t tailCallbacks_{};

    std::atomic<ExclusiveSinkState> state_{ExclusiveSinkState::Closed};
    std::atomic<std::uint64_t> submittedFrames_{};
    std::atomic<std::uint64_t> submittedBuffers_{};
    std::atomic<std::uint64_t> controlledSilenceFrames_{};
    std::atomic<std::uint64_t> sourceWaits_{};
    std::atomic<std::uint64_t> underruns_{};
    std::atomic<LONG> lastError_{S_OK};
};

} // namespace ammod::audio_v2
