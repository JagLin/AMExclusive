#pragma once

#include "PcmTypes.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ammod::audio_v2 {

// A decoder-side queue can be temporarily empty while Apple waits for a
// lossless network segment. This is a recoverable wait, not a format or
// endpoint failure. The sink stops its exclusive client without acquiring
// another endpoint buffer, keeps native output suppressed, and resumes the
// same generation only after a complete P0 buffer is available.
inline constexpr HRESULT kAudioSourceWouldBlock = static_cast<HRESULT>(0x88770001L);

class IIntegerPcmSource {
public:
    virtual ~IIntegerPcmSource() = default;

    // A sink must not acquire an endpoint buffer unless the source can fill
    // the complete period, or has reached an explicit end-of-stream tail.
    // This keeps a network wait from becoming fabricated output samples.
    virtual bool CanProvide(std::uint32_t capacityFrames) const noexcept {
        return capacityFrames != 0;
    }

    // Approximate number of source frames that can be consumed immediately.
    // This is diagnostic only; the sink never drives the Apple graph from the
    // endpoint render thread.
    virtual std::size_t AvailableFrames() const noexcept { return SIZE_MAX; }

    // The sink always asks for a complete endpoint buffer. A source may return
    // fewer frames only when endOfStream is true. It may return
    // kAudioSourceWouldBlock when Apple is waiting for the next lossless
    // segment; the sink keeps the generation alive in that case.
    virtual HRESULT Fill(std::int32_t* destination,
                         std::uint32_t capacityFrames,
                         std::uint32_t& writtenFrames,
                         bool& endOfStream) noexcept = 0;

};

// Lifecycle notifications are optional and diagnostic-only.  They are invoked
// on the thread that performs the corresponding sink operation and carry the
// HRESULT observed at the operation boundary.  The production hook uses them
// to build a QPC ownership timeline; tests leave the callback unset.
enum class ExclusiveSinkLifecycleEvent : std::uint8_t {
    OpenEnter,
    OpenExit,
    InitializeEnter,
    InitializeExit,
    StartEnter,
    StartExit,
    StopEnter,
    StopExit,
    CloseEnter,
    CloseExit,
};

using ExclusiveSinkLifecycleCallback = void (*)(
    void* context, ExclusiveSinkLifecycleEvent event, HRESULT result) noexcept;

// Free-form, diagnostic-only text (negotiated driver format, buffer size...).
// Never invoked from a real-time render callback.
using ExclusiveSinkDiagnosticCallback = void (*)(void* context, const wchar_t* text) noexcept;

enum class ExclusiveSinkBackend : std::uint8_t {
    Wasapi,
    Asio,
};

struct ExclusiveSinkConfig final {
    PcmFormat format{};
    std::array<PcmFormat, kMaxFormatCandidates> formatCandidates{};
    std::uint32_t formatCandidateCount{};
    // Backend-specific device identity: the MMDevice endpoint id for WASAPI,
    // the driver name under HKLM\SOFTWARE\ASIO for ASIO.
    std::wstring endpointId;
    std::uint32_t periodFrames{};
    // WASAPI only: retries after AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED.
    std::uint32_t alignmentRetries{2};
    // Non-owning producer notification. The coordinator owns this handle and
    // keeps it valid until the sink has stopped and closed.
    HANDLE sourceReadyEvent{};
    void* lifecycleContext{};
    ExclusiveSinkLifecycleCallback onLifecycle{};
    ExclusiveSinkDiagnosticCallback onDiagnostic{};
};

struct ExclusiveSinkStats final {
    std::uint64_t submittedFrames{};
    std::uint64_t submittedBuffers{};
    std::uint64_t controlledSilenceFrames{};
    std::uint64_t sourceWaits{};
    std::uint64_t underruns{};
    std::uint64_t getBufferFailures{};
    std::uint64_t releaseBufferFailures{};
    HRESULT lastError{S_OK};
};

enum class ExclusiveSinkState : std::uint8_t {
    Closed,
    Open,
    Running,
    Faulted,
};

// The coordinator's only view of a physical output backend. Every method runs
// on the coordinator/runtime worker thread except WaitingForSource(), which the
// P0 producer reads; implementations must keep that one a plain atomic load.
// A backend owns its own render thread and must uphold the same contract as
// the WASAPI sink: never fabricate samples, stop instead of padding on
// kAudioSourceWouldBlock, and submit a short period only at end-of-stream.
class IExclusiveSink {
public:
    virtual ~IExclusiveSink() = default;

    virtual HRESULT Open(const ExclusiveSinkConfig& config) noexcept = 0;
    virtual HRESULT Start(IIntegerPcmSource& source) noexcept = 0;
    virtual void Stop() noexcept = 0;
    virtual void Close() noexcept = 0;

    virtual ExclusiveSinkState State() const noexcept = 0;
    virtual ExclusiveSinkStats Stats() const noexcept = 0;
    // The format actually negotiated by Open(); valid until Close().
    virtual const PcmFormat& Format() const noexcept = 0;
    virtual bool WaitingForSource() const noexcept = 0;
    virtual bool EndOfStreamSubmitted() const noexcept = 0;
    // The coordinator blocks the render thread from consuming a producer
    // notification while it pauses/resumes Apple's graph.  This closes the
    // small race where the source becomes ready between the worker's state
    // check and its graph-control call.
    virtual void SetResumeBlocked(bool blocked) noexcept = 0;
};

} // namespace ammod::audio_v2
