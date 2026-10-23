#pragma once

#include <windows.h>

namespace ammod::audio {

// AMExclusive fork HRESULTs. Upstream numbers its codes sequentially from
// 0x201 in AudioErrors.h; the fork uses 0x280 onwards so the two never collide.

// The configured ASIO driver is missing, failed to load/init, or the build has
// no ASIO SDK. Unlike a format rejection this is an output-configuration fault.
inline constexpr HRESULT kAsioDriverUnavailable =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x280);
// The driver asked for a reset (kAsioResetRequest) while buffers were live.
inline constexpr HRESULT kAsioDriverReset =
    MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x281);

} // namespace ammod::audio
