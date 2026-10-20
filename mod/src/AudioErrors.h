#pragma once

#include <windows.h>

namespace ammod::audio {

// Stable, project-owned HRESULTs used across the audio core, Agent IPC and UI.
inline constexpr HRESULT kUnsupportedLocalInt32 =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);
inline constexpr HRESULT kBitPerfectFormatUnavailable =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x202);
// The configured ASIO driver is missing, failed to load/init, or the build has
// no ASIO SDK. Unlike a format rejection this is an output-configuration fault.
inline constexpr HRESULT kAsioDriverUnavailable =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x203);
// The driver asked for a reset (kAsioResetRequest) while buffers were live.
inline constexpr HRESULT kAsioDriverReset =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x204);

constexpr bool IsPlaybackRejection(HRESULT error) noexcept {
    return error == kUnsupportedLocalInt32 || error == kBitPerfectFormatUnavailable;
}

} // namespace ammod::audio
