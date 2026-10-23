#pragma once

// AMExclusive fork additions to the IPC protocol, kept out of IpcProtocol.h so
// an upstream merge only meets a few anchor lines there. Fork message types and
// error categories use values from 0x100, clear of upstream's sequential
// numbering, and the fork fields sit at the end of Message.

#include "ForkAudioErrors.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace ammod::ipc {

enum class MessageType : std::uint16_t;
enum class ErrorCategory : std::uint16_t;

inline constexpr std::size_t kOutputDeviceChars = 64;

enum class OutputBackend : std::uint8_t {
    Wasapi = 0,
    Asio = 1,
};

#pragma pack(push, 1)
struct ForkMessageFields {
    // Broker-owned output selection. outputDevice is the ASIO driver key under
    // HKLM\SOFTWARE\ASIO; it is kept while WASAPI is selected so switching back
    // restores the last driver.
    OutputBackend outputBackend{OutputBackend::Wasapi};
    wchar_t outputDevice[kOutputDeviceChars]{};
};
#pragma pack(pop)

namespace fork {

// UI output-device selection. Like the hardware buffer it is persisted by the
// Broker and applies from the next generation; the current song keeps its
// sink. Carried in Message::fork.
inline constexpr auto kSetOutputDevice = static_cast<MessageType>(0x100);

inline constexpr auto kErrorAsioDriverUnavailable = static_cast<ErrorCategory>(0x100);
inline constexpr auto kErrorAsioDriverReset = static_cast<ErrorCategory>(0x101);

inline bool CategorizeAudioError(HRESULT hr, ErrorCategory& category) noexcept {
    switch (hr) {
    case ammod::audio::kAsioDriverUnavailable:
        category = kErrorAsioDriverUnavailable;
        return true;
    case ammod::audio::kAsioDriverReset:
        category = kErrorAsioDriverReset;
        return true;
    default:
        return false;
    }
}

} // namespace fork
} // namespace ammod::ipc
