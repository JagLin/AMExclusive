#include "AsioSink.h"
#include "NativeRenderGateProxy.h"
#include "../AudioErrors.h"

#include <objbase.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>

// The SDK include directory is only added when a local copy exists (see
// CMakeLists.txt). Without it the backend compiles as permanently unavailable.
#if __has_include(<iasiodrv.h>)
#define AMMOD_HAS_ASIO_SDK 1
#pragma warning(push, 0)
#include <asiosys.h>
#include <asio.h>
#include <iasiodrv.h>
#pragma warning(pop)
#else
#define AMMOD_HAS_ASIO_SDK 0
#endif

namespace ammod::audio_v2 {

#if AMMOD_HAS_ASIO_SDK

namespace {

// ASIO callbacks carry no context pointer, so the sink that owns the created
// buffers is published here. At most one sink may hold driver buffers.
std::atomic<AsioSink*> g_activeAsioSink{};

constexpr wchar_t kHostWindowClass[] = L"AMExclusiveAsioHost";

struct SampleLayout final {
    std::uint16_t bits{};
    std::uint16_t containerBits{};
    std::uint32_t bytes{};
};

// Only little-endian integer layouts that can carry the canonical
// left-justified int32 without rounding are accepted. Float and odd 18/20-bit
// alignments are rejected rather than converted.
bool DescribeSampleType(long type, SampleLayout& layout) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: layout = {16, 16, 2}; return true;
    case ASIOSTInt24LSB: layout = {24, 24, 3}; return true;
    case ASIOSTInt32LSB: layout = {32, 32, 4}; return true;
    case ASIOSTInt32LSB24: layout = {24, 32, 4}; return true;
    case ASIOSTInt32LSB16: layout = {16, 32, 4}; return true;
    default: return false;
    }
}

std::uint32_t BytesPerSample(long type) noexcept {
    SampleLayout layout{};
    return DescribeSampleType(type, layout) ? layout.bytes : 0u;
}

// Chooses the legal driver buffer size closest to the requested period. ASIO
// encodes the legal set as min/max/granularity, with -1 meaning powers of two.
long PickBufferSize(long minimum, long maximum, long preferred, long granularity,
                    std::uint32_t requested) noexcept {
    if (minimum <= 0 || maximum < minimum) return preferred;
    const long target = requested ? static_cast<long>(requested) : preferred;
    if (granularity == 0 || minimum == maximum) return preferred > 0 ? preferred : minimum;
    long best = preferred > 0 ? preferred : minimum;
    long bestDistance = LONG_MAX;
    const auto consider = [&](long candidate) noexcept {
        const long distance = candidate > target ? candidate - target : target - candidate;
        if (distance < bestDistance || (distance == bestDistance && candidate > best)) {
            best = candidate;
            bestDistance = distance;
        }
    };
    if (granularity < 0) {
        for (long candidate = 1; candidate <= maximum && candidate > 0; candidate <<= 1) {
            if (candidate >= minimum) consider(candidate);
        }
    } else {
        for (long candidate = minimum; candidate <= maximum; candidate += granularity) {
            consider(candidate);
        }
    }
    return best;
}

std::wstring Widen(const char* text) {
    if (!text || !*text) return {};
    wchar_t buffer[256]{};
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, buffer,
                                           static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer) : std::wstring();
}

std::wstring HexText(HRESULT value) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned>(value));
    return buffer;
}

LRESULT CALLBACK HostWindowProc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) noexcept {
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

struct AsioCallbackBridge final {
    static void BufferSwitch(long index, ASIOBool) noexcept {
        if (auto* sink = g_activeAsioSink.load(std::memory_order_acquire)) {
            sink->RenderHalf(index);
        }
    }

    static ASIOTime* BufferSwitchTimeInfo(ASIOTime* params, long index,
                                          ASIOBool directProcess) noexcept {
        BufferSwitch(index, directProcess);
        return params;
    }

    static void SampleRateDidChange(ASIOSampleRate rate) noexcept {
        auto* sink = g_activeAsioSink.load(std::memory_order_acquire);
        if (!sink) return;
        // Some drivers echo the rate the worker just set. Only a rate that no
        // longer matches the negotiated stream invalidates the generation.
        if (rate == static_cast<ASIOSampleRate>(sink->config_.format.sampleRate)) return;
        sink->resetRequested_.store(true, std::memory_order_release);
        sink->RequestWorker();
    }

    static long AsioMessage(long selector, long value, void*, double*) noexcept {
        auto* sink = g_activeAsioSink.load(std::memory_order_acquire);
        switch (selector) {
        case kAsioSelectorSupported:
            return value == kAsioEngineVersion || value == kAsioResetRequest ||
                   value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
                   value == kAsioOverload ? 1L : 0L;
        case kAsioEngineVersion:
            return 2L;
        case kAsioResetRequest:
            if (sink) {
                sink->resetRequested_.store(true, std::memory_order_release);
                sink->RequestWorker();
            }
            return 1L;
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
            // Sample content is unaffected; there is no sequencer to resync.
            return 1L;
        case kAsioOverload:
            if (sink) sink->underruns_.fetch_add(1, std::memory_order_relaxed);
            return 1L;
        default:
            // kAsioSupportsTimeInfo is declined so drivers keep calling the
            // plain bufferSwitch entry point.
            return 0L;
        }
    }
};

namespace {

ASIOCallbacks g_asioCallbacks{
    &AsioCallbackBridge::BufferSwitch,
    &AsioCallbackBridge::SampleRateDidChange,
    &AsioCallbackBridge::AsioMessage,
    &AsioCallbackBridge::BufferSwitchTimeInfo,
};

} // namespace

AsioSink::~AsioSink() {
    Close();
    ShutdownWorker();
}

HRESULT AsioSink::Open(const ExclusiveSinkConfig& config) noexcept {
    Close();
    submittedFrames_.store(0, std::memory_order_release);
    submittedBuffers_.store(0, std::memory_order_release);
    controlledSilenceFrames_.store(0, std::memory_order_release);
    sourceWaits_.store(0, std::memory_order_release);
    underruns_.store(0, std::memory_order_release);
    lastError_.store(S_OK, std::memory_order_release);
    waitingForSource_.store(false, std::memory_order_release);
    resumeBlocked_.store(false, std::memory_order_release);
    endOfStreamSubmitted_.store(false, std::memory_order_release);

    HRESULT hr = EnsureWorker();
    if (SUCCEEDED(hr)) {
        std::lock_guard lock(callerMutex_);
        pendingConfig_ = config;
        hr = Dispatch(Command::Open);
    }
    if (FAILED(hr)) RecordError(hr);
    return hr;
}

HRESULT AsioSink::Start(IIntegerPcmSource& source) noexcept {
    if (state_.load(std::memory_order_acquire) != ExclusiveSinkState::Open) return E_UNEXPECTED;
    std::lock_guard lock(callerMutex_);
    pendingSource_ = &source;
    return Dispatch(Command::Start);
}

void AsioSink::Stop() noexcept {
    const auto current = state_.load(std::memory_order_acquire);
    if (current != ExclusiveSinkState::Running && current != ExclusiveSinkState::Faulted) return;
    std::lock_guard lock(callerMutex_);
    (void)Dispatch(Command::Stop);
}

void AsioSink::Close() noexcept {
    if (state_.load(std::memory_order_acquire) == ExclusiveSinkState::Closed) return;
    std::lock_guard lock(callerMutex_);
    (void)Dispatch(Command::Close);
}

void AsioSink::SetResumeBlocked(bool blocked) noexcept {
    resumeBlocked_.store(blocked, std::memory_order_release);
    if (!blocked && callbackEvent_) SetEvent(callbackEvent_);
}

HRESULT AsioSink::EnsureWorker() noexcept {
    if (worker_) return S_OK;
    commandEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    doneEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    callbackEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr = commandEvent_ && doneEvent_ && callbackEvent_
        ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) {
        worker_ = CreateThread(nullptr, 0, &AsioSink::WorkerThunk, this, 0, nullptr);
        if (!worker_) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(hr)) {
        // The worker reports its STA/window setup through the normal handshake.
        HANDLE handles[2] = {doneEvent_, worker_};
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        hr = wait == WAIT_OBJECT_0 ? commandResult_ : E_ABORT;
    }
    if (FAILED(hr)) ShutdownWorker();
    return hr;
}

// Callers hold callerMutex_. Besides the completion event this also watches
// the worker handle: at process exit the loader has already terminated the
// worker, and a destructor-driven Close() must not wait forever.
//
// The wait is also bounded: the caller is the runtime worker holding its
// control lock, so a driver that never returns would otherwise freeze Apple
// Music's playback. After a timeout the stuck worker is abandoned and this
// sink fails every later command until the process restarts.
HRESULT AsioSink::Dispatch(Command command) noexcept {
    if (!worker_) return E_UNEXPECTED;
    if (workerHung_) return ammod::audio::kAsioDriverUnavailable;
    pendingCommand_ = command;
    SetEvent(commandEvent_);
    HANDLE handles[2] = {doneEvent_, worker_};
    const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, kCommandTimeoutMs);
    if (wait == WAIT_OBJECT_0) return commandResult_;
    if (wait == WAIT_TIMEOUT) {
        workerHung_ = true;
        rendering_.store(false, std::memory_order_release);
        RecordError(ammod::audio::kAsioDriverUnavailable);
        state_.store(ExclusiveSinkState::Faulted, std::memory_order_release);
        if (pendingConfig_.onDiagnostic) {
            pendingConfig_.onDiagnostic(
                pendingConfig_.lifecycleContext,
                L"asio driver did not return within the command timeout; ASIO output "
                L"disabled until Apple Music restarts");
        }
        return ammod::audio::kAsioDriverUnavailable;
    }
    return E_ABORT;
}

void AsioSink::ShutdownWorker() noexcept {
    if (worker_) {
        {
            std::lock_guard lock(callerMutex_);
            pendingCommand_ = Command::Shutdown;
            SetEvent(commandEvent_);
        }
        // Bounded: under the loader lock a live worker could not finish its
        // own thread detach, and a terminated one is already signaled.
        WaitForSingleObject(worker_, 2000);
        CloseHandle(worker_);
        worker_ = nullptr;
    }
    for (HANDLE* handle : {&commandEvent_, &doneEvent_, &callbackEvent_}) {
        if (*handle) {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}

DWORD WINAPI AsioSink::WorkerThunk(void* context) noexcept {
    return static_cast<AsioSink*>(context)->WorkerMain();
}

DWORD AsioSink::WorkerMain() noexcept {
    // Some ASIO drivers are WASAPI-exclusive wrappers (GUAC32's Combo384
    // driver activates an IAudioClient on this thread from start()). Wrapping
    // that client as Apple's native client re-enters the runtime, whose
    // control lock is held by the caller waiting on this very command: a
    // deadlock. Everything the driver activates from here is ours, not Apple's.
    ScopedNativeProxyBypass bypass;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT setup = SUCCEEDED(apartment) ? S_OK : apartment;
    HMODULE module{};
    if (SUCCEEDED(setup) &&
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&HostWindowProc), &module)) {
        setup = HRESULT_FROM_WIN32(GetLastError());
    }
    if (SUCCEEDED(setup)) {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &HostWindowProc;
        windowClass.hInstance = module;
        windowClass.lpszClassName = kHostWindowClass;
        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            setup = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (SUCCEEDED(setup)) {
        // init(sysHandle) wants an HWND; a message-only window is enough since
        // the driver control panel is never opened from here.
        window_ = CreateWindowExW(0, kHostWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                  nullptr, module, nullptr);
        if (!window_) setup = HRESULT_FROM_WIN32(GetLastError());
    }
    commandResult_ = setup;
    SetEvent(doneEvent_);

    while (SUCCEEDED(setup)) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const bool sourceWait = waitingForSource_.load(std::memory_order_acquire) &&
                                buffersCreated_ && !driverStarted_;
        HANDLE handles[3] = {commandEvent_, callbackEvent_, config_.sourceReadyEvent};
        const DWORD count = sourceWait && config_.sourceReadyEvent ? 3u : 2u;
        DWORD timeout = INFINITE;
        if (sourceWait) {
            timeout = 50;
        } else if (driver_ && !buffersCreated_ && idleDeadline_ != 0) {
            const ULONGLONG now = GetTickCount64();
            timeout = now >= idleDeadline_ ? 0u : static_cast<DWORD>(idleDeadline_ - now);
        }

        const DWORD wait = MsgWaitForMultipleObjects(count, handles, FALSE, timeout, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            const Command command = pendingCommand_;
            commandResult_ = RunCommand(command);
            SetEvent(doneEvent_);
            if (command == Command::Shutdown) break;
            continue;
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            HandleCallbackSignal();
            if (sourceWait || waitingForSource_.load(std::memory_order_acquire)) {
                TryResumeAfterSourceWait();
            }
            continue;
        }
        if (wait == WAIT_OBJECT_0 + count) continue; // window messages
        if ((count == 3 && wait == WAIT_OBJECT_0 + 2) || wait == WAIT_TIMEOUT) {
            if (sourceWait) {
                TryResumeAfterSourceWait();
            } else if (driver_ && !buffersCreated_ && idleDeadline_ != 0 &&
                       GetTickCount64() >= idleDeadline_) {
                Diagnostic(L"asio driver idle for " + std::to_wstring(kIdleReleaseMs) +
                           L" ms; releasing");
                ReleaseDriver();
            }
            continue;
        }
        if (wait == WAIT_FAILED) {
            RecordError(HRESULT_FROM_WIN32(GetLastError()));
            Sleep(10);
        }
    }

    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    return 0;
}

HRESULT AsioSink::RunCommand(Command command) noexcept {
    switch (command) {
    case Command::Open: return OpenOnWorker();
    case Command::Start: return StartOnWorker();
    case Command::Stop: StopOnWorker(); return S_OK;
    case Command::Close: CloseOnWorker(); return S_OK;
    case Command::Shutdown:
        CloseOnWorker();
        ReleaseDriver();
        return S_OK;
    case Command::None: break;
    }
    return E_UNEXPECTED;
}

HRESULT AsioSink::OpenOnWorker() noexcept {
    config_ = pendingConfig_;
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::OpenEnter, S_OK);
    const auto finish = [this](HRESULT result) noexcept {
        if (FAILED(result)) {
            RecordError(result);
            CloseOnWorker();
            // A rate/bit-depth rejection says nothing about driver health, so
            // the session may serve the next track; anything else drops it.
            if (result == ammod::audio::kBitPerfectFormatUnavailable) {
                if (driver_) idleDeadline_ = GetTickCount64() + kIdleReleaseMs;
            } else {
                ReleaseDriver();
            }
        }
        NotifyLifecycle(ExclusiveSinkLifecycleEvent::OpenExit, result);
        return result;
    };
    if (!IsValidFormat(config_.format)) return finish(E_INVALIDARG);
    if (config_.endpointId.empty()) {
        Diagnostic(L"asio open rejected: no asio_driver configured");
        return finish(ammod::audio::kAsioDriverUnavailable);
    }

    if (driver_ && (reloadBeforeOpen_ || loadedDriverName_ != config_.endpointId)) {
        ReleaseDriver();
    }
    reloadBeforeOpen_ = false;
    idleDeadline_ = 0;

    HRESULT hr = driver_ ? S_OK : LoadDriver();
    if (FAILED(hr)) return finish(hr);
    hr = ApplySampleRate();
    if (FAILED(hr) && hr != ammod::audio::kBitPerfectFormatUnavailable) {
        Diagnostic(L"asio sample-rate change failed; reloading driver once");
        ReleaseDriver();
        hr = LoadDriver();
        if (SUCCEEDED(hr)) hr = ApplySampleRate();
    }
    if (FAILED(hr)) return finish(hr);

    NotifyLifecycle(ExclusiveSinkLifecycleEvent::InitializeEnter, S_OK);
    hr = NegotiateBuffers();
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::InitializeExit, hr);
    if (FAILED(hr)) return finish(hr);

    state_.store(ExclusiveSinkState::Open, std::memory_order_release);
    return finish(S_OK);
}

HRESULT AsioSink::LoadDriver() noexcept {
    const std::wstring key = L"SOFTWARE\\ASIO\\" + config_.endpointId;
    wchar_t clsidText[64]{};
    DWORD bytes = sizeof(clsidText);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"CLSID",
                                        RRF_RT_REG_SZ, nullptr, clsidText, &bytes);
    CLSID clsid{};
    if (status != ERROR_SUCCESS || FAILED(CLSIDFromString(clsidText, &clsid))) {
        Diagnostic(L"asio driver not registered name=" + config_.endpointId +
                   L" status=" + std::to_wstring(status));
        return ammod::audio::kAsioDriverUnavailable;
    }

    // ASIO drivers are created with their CLSID doubling as the interface id.
    void* instance = nullptr;
    const HRESULT created = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid,
                                             &instance);
    if (FAILED(created) || !instance) {
        Diagnostic(L"asio driver CoCreateInstance failed name=" + config_.endpointId +
                   L" clsid=" + clsidText + L" hr=" + HexText(created));
        return ammod::audio::kAsioDriverUnavailable;
    }
    driver_ = static_cast<IASIO*>(instance);
    if (!driver_->init(window_)) {
        char message[128]{};
        driver_->getErrorMessage(message);
        Diagnostic(L"asio driver init failed name=" + config_.endpointId +
                   L" message=" + Widen(message));
        driver_->Release();
        driver_ = nullptr;
        return ammod::audio::kAsioDriverUnavailable;
    }
    char name[64]{};
    driver_->getDriverName(name);
    loadedDriverName_ = config_.endpointId;
    Diagnostic(L"asio driver loaded name=" + config_.endpointId + L" reported=" + Widen(name) +
               L" version=" + std::to_wstring(driver_->getDriverVersion()));
    return S_OK;
}

void AsioSink::ReleaseDriver() noexcept {
    if (!driver_) return;
    if (driverStarted_) {
        rendering_.store(false, std::memory_order_release);
        driver_->stop();
        driverStarted_ = false;
    }
    if (buffersCreated_) {
        driver_->disposeBuffers();
        buffersCreated_ = false;
        AsioSink* expected = this;
        g_activeAsioSink.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    driver_->Release();
    driver_ = nullptr;
    loadedDriverName_.clear();
    idleDeadline_ = 0;
    Diagnostic(L"asio driver released");
}

HRESULT AsioSink::ApplySampleRate() noexcept {
    const auto wanted = static_cast<ASIOSampleRate>(config_.format.sampleRate);
    ASIOSampleRate current = 0;
    if (driver_->getSampleRate(&current) == ASE_OK && current == wanted) return S_OK;
    if (driver_->canSampleRate(wanted) != ASE_OK) {
        Diagnostic(L"asio driver cannot play rate=" +
                   std::to_wstring(config_.format.sampleRate));
        return ammod::audio::kBitPerfectFormatUnavailable;
    }
    if (driver_->setSampleRate(wanted) != ASE_OK ||
        driver_->getSampleRate(&current) != ASE_OK || current != wanted) {
        return ammod::audio::kAsioDriverUnavailable;
    }
    Diagnostic(L"asio sample rate set rate=" + std::to_wstring(config_.format.sampleRate));
    return S_OK;
}

HRESULT AsioSink::NegotiateBuffers() noexcept {
    long inputs = 0;
    long outputs = 0;
    if (driver_->getChannels(&inputs, &outputs) != ASE_OK || outputs < 2) {
        Diagnostic(L"asio driver has fewer than two outputs");
        return ammod::audio::kAsioDriverUnavailable;
    }
    long minimum = 0;
    long maximum = 0;
    long preferred = 0;
    long granularity = 0;
    if (driver_->getBufferSize(&minimum, &maximum, &preferred, &granularity) != ASE_OK) {
        return ammod::audio::kAsioDriverUnavailable;
    }
    const long frames = PickBufferSize(minimum, maximum, preferred, granularity,
                                       config_.periodFrames);
    if (frames <= 0) return ammod::audio::kAsioDriverUnavailable;

    ASIOChannelInfo channels[2]{};
    for (long channel = 0; channel < 2; ++channel) {
        channels[channel].channel = channel;
        channels[channel].isInput = ASIOFalse;
        if (driver_->getChannelInfo(&channels[channel]) != ASE_OK) {
            return ammod::audio::kAsioDriverUnavailable;
        }
    }
    SampleLayout layout{};
    const std::uint16_t sourceBits = EffectiveSourceValidBits(config_.format);
    if (channels[0].type != channels[1].type ||
        !DescribeSampleType(channels[0].type, layout) || sourceBits > layout.bits) {
        Diagnostic(L"asio sample type rejected type=" + std::to_wstring(channels[0].type) +
                   L"/" + std::to_wstring(channels[1].type) +
                   L" sourceBits=" + std::to_wstring(sourceBits));
        return ammod::audio::kBitPerfectFormatUnavailable;
    }

    try {
        canonical_.assign(static_cast<std::size_t>(frames) * kSupportedChannels, 0);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }

    AsioSink* expected = nullptr;
    if (!g_activeAsioSink.compare_exchange_strong(expected, this, std::memory_order_acq_rel)) {
        return E_UNEXPECTED;
    }
    ASIOBufferInfo buffers[2]{};
    for (long channel = 0; channel < 2; ++channel) {
        buffers[channel].isInput = ASIOFalse;
        buffers[channel].channelNum = channel;
    }
    // Publish the layout before createBuffers: some drivers fire a first
    // bufferSwitch from inside it, which must find a consistent sink.
    sampleType_ = channels[0].type;
    bufferFrames_ = static_cast<std::uint32_t>(frames);
    const ASIOError created = driver_->createBuffers(buffers, 2, frames, &g_asioCallbacks);
    if (created != ASE_OK) {
        expected = this;
        g_activeAsioSink.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        sampleType_ = -1;
        bufferFrames_ = 0;
        Diagnostic(L"asio createBuffers failed error=" + std::to_wstring(created) +
                   L" frames=" + std::to_wstring(frames));
        return ammod::audio::kAsioDriverUnavailable;
    }
    for (long channel = 0; channel < 2; ++channel) {
        channelBuffers_[channel][0] = buffers[channel].buffers[0];
        channelBuffers_[channel][1] = buffers[channel].buffers[1];
    }
    buffersCreated_ = true;
    outputReadySupported_ = driver_->outputReady() == ASE_OK;
    ZeroAllHalves();

    long inputLatency = 0;
    long outputLatency = 0;
    (void)driver_->getLatencies(&inputLatency, &outputLatency);
    config_.format.validBits = layout.bits;
    config_.format.containerBits = layout.containerBits;
    config_.format.sourceValidBits = sourceBits;
    Diagnostic(L"asio buffers ready rate=" + std::to_wstring(config_.format.sampleRate) +
               L" sampleType=" + std::to_wstring(sampleType_) +
               L" bits=" + std::to_wstring(layout.bits) +
               L" container=" + std::to_wstring(layout.containerBits) +
               L" sourceBits=" + std::to_wstring(sourceBits) +
               L" frames=" + std::to_wstring(frames) +
               L" requested=" + std::to_wstring(config_.periodFrames) +
               L" range=" + std::to_wstring(minimum) + L".." + std::to_wstring(maximum) +
               L" preferred=" + std::to_wstring(preferred) +
               L" granularity=" + std::to_wstring(granularity) +
               L" outputLatency=" + std::to_wstring(outputLatency) +
               L" outputReady=" + std::to_wstring(outputReadySupported_ ? 1 : 0) +
               L" channel0=" + Widen(channels[0].name) +
               L" channel1=" + Widen(channels[1].name));
    return S_OK;
}

HRESULT AsioSink::StartOnWorker() noexcept {
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::StartEnter, S_OK);
    const auto finish = [this](HRESULT result) noexcept {
        NotifyLifecycle(ExclusiveSinkLifecycleEvent::StartExit, result);
        return result;
    };
    if (state_.load(std::memory_order_acquire) != ExclusiveSinkState::Open ||
        !buffersCreated_ || driverStarted_ || !pendingSource_) {
        return finish(E_UNEXPECTED);
    }
    source_ = pendingSource_;
    pendingSource_ = nullptr;
    ZeroAllHalves();
    tailCallbacks_ = 0;
    stopRequested_.store(false, std::memory_order_release);
    faultPending_.store(false, std::memory_order_release);
    waitingForSource_.store(false, std::memory_order_release);
    endOfStreamSubmitted_.store(false, std::memory_order_release);
    rendering_.store(true, std::memory_order_release);
    if (driver_->start() != ASE_OK) {
        rendering_.store(false, std::memory_order_release);
        source_ = nullptr;
        RecordError(ammod::audio::kAsioDriverUnavailable);
        state_.store(ExclusiveSinkState::Faulted, std::memory_order_release);
        return finish(ammod::audio::kAsioDriverUnavailable);
    }
    driverStarted_ = true;
    // The half the driver plays first was zeroed above.
    controlledSilenceFrames_.fetch_add(bufferFrames_, std::memory_order_relaxed);
    state_.store(ExclusiveSinkState::Running, std::memory_order_release);
    return finish(S_OK);
}

void AsioSink::StopOnWorker() noexcept {
    const auto current = state_.load(std::memory_order_acquire);
    if (current != ExclusiveSinkState::Running && current != ExclusiveSinkState::Faulted &&
        !driverStarted_) {
        return;
    }
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::StopEnter, S_OK);
    rendering_.store(false, std::memory_order_release);
    HRESULT result = S_OK;
    if (driverStarted_) {
        if (driver_->stop() != ASE_OK) result = ammod::audio::kAsioDriverUnavailable;
        driverStarted_ = false;
    }
    source_ = nullptr;
    tailCallbacks_ = 0;
    stopRequested_.store(false, std::memory_order_release);
    waitingForSource_.store(false, std::memory_order_release);
    resumeBlocked_.store(false, std::memory_order_release);
    if (current != ExclusiveSinkState::Faulted && buffersCreated_) {
        state_.store(ExclusiveSinkState::Open, std::memory_order_release);
    }
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::StopExit, result);
}

void AsioSink::CloseOnWorker() noexcept {
    if (state_.load(std::memory_order_acquire) == ExclusiveSinkState::Closed &&
        !buffersCreated_ && !driverStarted_) {
        return;
    }
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::CloseEnter, S_OK);
    StopOnWorker();
    if (buffersCreated_) {
        driver_->disposeBuffers();
        buffersCreated_ = false;
    }
    AsioSink* expected = this;
    g_activeAsioSink.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    for (auto& channel : channelBuffers_) channel[0] = channel[1] = nullptr;
    sampleType_ = -1;
    bufferFrames_ = 0;
    outputReadySupported_ = false;
    state_.store(ExclusiveSinkState::Closed, std::memory_order_release);
    if (driver_) {
        if (reloadBeforeOpen_) ReleaseDriver();
        else idleDeadline_ = GetTickCount64() + kIdleReleaseMs;
    }
    NotifyLifecycle(ExclusiveSinkLifecycleEvent::CloseExit,
                    static_cast<HRESULT>(lastError_.load(std::memory_order_acquire)));
}

void AsioSink::HandleCallbackSignal() noexcept {
    if (resetRequested_.exchange(false, std::memory_order_acq_rel)) {
        reloadBeforeOpen_ = true;
        if (buffersCreated_) {
            Diagnostic(L"asio driver requested reset; generation faulted, driver reloads "
                       L"before the next open");
            rendering_.store(false, std::memory_order_release);
            if (driverStarted_) {
                driver_->stop();
                driverStarted_ = false;
            }
            RecordError(ammod::audio::kAsioDriverReset);
            state_.store(ExclusiveSinkState::Faulted, std::memory_order_release);
        }
    }
    const bool fault = faultPending_.exchange(false, std::memory_order_acq_rel);
    const bool stop = stopRequested_.exchange(false, std::memory_order_acq_rel);
    if ((fault || stop) && driverStarted_) {
        // A source wait keeps waitingForSource_ set: the loop then waits for
        // the producer notification and restarts this same generation.
        driver_->stop();
        driverStarted_ = false;
    }
}

void AsioSink::TryResumeAfterSourceWait() noexcept {
    if (!waitingForSource_.load(std::memory_order_acquire) || driverStarted_ ||
        !buffersCreated_ || !source_ || resumeBlocked_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) != ExclusiveSinkState::Running) {
        return;
    }
    // Restart only with two periods queued: the driver takes a full period at
    // every callback, so resuming on exactly one would starve again on the
    // very next half and turn one wait into a stutter of short silences.
    if (!source_->CanProvide(bufferFrames_ * 2u)) return;
    ZeroAllHalves();
    tailCallbacks_ = 0;
    stopRequested_.store(false, std::memory_order_release);
    waitingForSource_.store(false, std::memory_order_release);
    rendering_.store(true, std::memory_order_release);
    if (driver_->start() != ASE_OK) {
        rendering_.store(false, std::memory_order_release);
        RecordError(ammod::audio::kAsioDriverUnavailable);
        state_.store(ExclusiveSinkState::Faulted, std::memory_order_release);
        return;
    }
    driverStarted_ = true;
    controlledSilenceFrames_.fetch_add(bufferFrames_, std::memory_order_relaxed);
}

void AsioSink::ZeroAllHalves() noexcept {
    if (!buffersCreated_) return;
    ZeroHalf(0);
    ZeroHalf(1);
}

void AsioSink::RenderHalf(long index) noexcept {
    if (index != 0 && index != 1) return;
    const std::uint32_t frames = bufferFrames_;
    if (!rendering_.load(std::memory_order_acquire)) {
        ZeroHalf(index);
        // Stop only once the last half holding real samples has been played:
        // per the ASIO contract the other half is on air during this call.
        if (tailCallbacks_ != 0) {
            controlledSilenceFrames_.fetch_add(frames, std::memory_order_relaxed);
            if (--tailCallbacks_ == 0) {
                stopRequested_.store(true, std::memory_order_release);
                RequestWorker();
            }
        }
        return;
    }

    IIntegerPcmSource* source = source_;
    if (!source->CanProvide(frames)) {
        // Same lost-wakeup protocol as the WASAPI sink: publish the wait
        // before the second check so a racing producer either is observed
        // here or sees the flag and signals sourceReadyEvent.
        waitingForSource_.store(true, std::memory_order_release);
        if (!source->CanProvide(frames)) {
            ZeroHalf(index);
            sourceWaits_.fetch_add(1, std::memory_order_relaxed);
            controlledSilenceFrames_.fetch_add(frames, std::memory_order_relaxed);
            rendering_.store(false, std::memory_order_release);
            tailCallbacks_ = 1;
            return;
        }
        waitingForSource_.store(false, std::memory_order_release);
    }

    std::uint32_t written = 0;
    bool endOfStream = false;
    const HRESULT fill = source->Fill(canonical_.data(), frames, written, endOfStream);
    // CanProvide() was checked above and the queue is single-consumer, so a
    // short non-terminal fill is a broken source contract, not a network wait.
    if (FAILED(fill) || written > frames || (written != frames && !endOfStream)) {
        ZeroHalf(index);
        underruns_.fetch_add(1, std::memory_order_relaxed);
        RecordError(FAILED(fill) ? fill : E_FAIL);
        rendering_.store(false, std::memory_order_release);
        state_.store(ExclusiveSinkState::Faulted, std::memory_order_release);
        faultPending_.store(true, std::memory_order_release);
        RequestWorker();
        return;
    }
    if (written < frames) {
        std::fill(canonical_.begin() + static_cast<std::ptrdiff_t>(written) * kSupportedChannels,
                  canonical_.end(), 0);
        controlledSilenceFrames_.fetch_add(frames - written, std::memory_order_relaxed);
    }
    PackHalf(index);
    submittedFrames_.fetch_add(frames, std::memory_order_relaxed);
    submittedBuffers_.fetch_add(1, std::memory_order_relaxed);
    if (endOfStream) {
        endOfStreamSubmitted_.store(true, std::memory_order_release);
        rendering_.store(false, std::memory_order_release);
        // This half plays after the one on air now; stop after both are done.
        tailCallbacks_ = 2;
    }
    if (outputReadySupported_) driver_->outputReady();
}

void AsioSink::ZeroHalf(long index) noexcept {
    const std::size_t bytes = static_cast<std::size_t>(bufferFrames_) * BytesPerSample(sampleType_);
    for (auto& channel : channelBuffers_) {
        if (channel[index]) std::memset(channel[index], 0, bytes);
    }
}

// Canonical samples are left-justified int32. Every accepted layout keeps at
// least the source's valid bits, so these shifts only drop bits that are
// already zero: the stored integer values are unchanged.
void AsioSink::PackHalf(long index) noexcept {
    const std::uint32_t frames = bufferFrames_;
    const std::int32_t* canonical = canonical_.data();
    for (std::uint32_t channel = 0; channel < kSupportedChannels; ++channel) {
        void* destination = channelBuffers_[channel][index];
        switch (sampleType_) {
        case ASIOSTInt32LSB: {
            auto* out = static_cast<std::int32_t*>(destination);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                out[frame] = canonical[frame * kSupportedChannels + channel];
            }
            break;
        }
        case ASIOSTInt32LSB24:
        case ASIOSTInt32LSB16: {
            const int shift = sampleType_ == ASIOSTInt32LSB24 ? 8 : 16;
            auto* out = static_cast<std::int32_t*>(destination);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                out[frame] = canonical[frame * kSupportedChannels + channel] >> shift;
            }
            break;
        }
        case ASIOSTInt24LSB: {
            auto* out = static_cast<BYTE*>(destination);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto value = static_cast<std::uint32_t>(
                    canonical[frame * kSupportedChannels + channel]) >> 8u;
                out[frame * 3u] = static_cast<BYTE>(value);
                out[frame * 3u + 1u] = static_cast<BYTE>(value >> 8u);
                out[frame * 3u + 2u] = static_cast<BYTE>(value >> 16u);
            }
            break;
        }
        case ASIOSTInt16LSB: {
            auto* out = static_cast<std::int16_t*>(destination);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                out[frame] = static_cast<std::int16_t>(
                    canonical[frame * kSupportedChannels + channel] >> 16);
            }
            break;
        }
        default:
            break;
        }
    }
}

void AsioSink::RequestWorker() noexcept {
    if (callbackEvent_) SetEvent(callbackEvent_);
}

void AsioSink::Diagnostic(const std::wstring& text) noexcept {
    if (config_.onDiagnostic) config_.onDiagnostic(config_.lifecycleContext, text.c_str());
}

#else // !AMMOD_HAS_ASIO_SDK

AsioSink::~AsioSink() = default;

HRESULT AsioSink::Open(const ExclusiveSinkConfig& config) noexcept {
    if (config.onDiagnostic) {
        config.onDiagnostic(config.lifecycleContext,
                            L"asio open rejected: built without the ASIO SDK");
    }
    RecordError(ammod::audio::kAsioDriverUnavailable);
    return ammod::audio::kAsioDriverUnavailable;
}

HRESULT AsioSink::Start(IIntegerPcmSource&) noexcept { return E_UNEXPECTED; }
void AsioSink::Stop() noexcept {}
void AsioSink::Close() noexcept {}
void AsioSink::SetResumeBlocked(bool blocked) noexcept {
    resumeBlocked_.store(blocked, std::memory_order_release);
}

#endif // AMMOD_HAS_ASIO_SDK

void AsioSink::RecordError(HRESULT error) noexcept {
    if (FAILED(error)) lastError_.store(error, std::memory_order_release);
}

void AsioSink::NotifyLifecycle(ExclusiveSinkLifecycleEvent event, HRESULT result) noexcept {
    if (config_.onLifecycle) config_.onLifecycle(config_.lifecycleContext, event, result);
}

ExclusiveSinkStats AsioSink::Stats() const noexcept {
    return ExclusiveSinkStats{
        submittedFrames_.load(std::memory_order_acquire),
        submittedBuffers_.load(std::memory_order_acquire),
        controlledSilenceFrames_.load(std::memory_order_acquire),
        sourceWaits_.load(std::memory_order_acquire),
        underruns_.load(std::memory_order_acquire),
        0,
        0,
        static_cast<HRESULT>(lastError_.load(std::memory_order_acquire)),
    };
}

} // namespace ammod::audio_v2
