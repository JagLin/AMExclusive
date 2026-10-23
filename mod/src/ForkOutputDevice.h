#pragma once

// AMExclusive fork: the "Output device" card on Apple Music's playback settings
// page and its UI <-> Broker selection protocol. Kept out of UiAdapter.cpp so an
// upstream merge only meets the few calls UiAdapter makes into this module.

#include "IpcProtocol.h"

#include <windows.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ammod::ui_fork {

// Services UiAdapter already owns, handed over once at startup.
struct OutputDeviceHost {
    std::wstring (*localized)(std::wstring_view chinese, std::wstring_view english){};
    void (*log)(const std::wstring& message){};
    ammod::ipc::Message (*statusSnapshot)(){};
    void (*scheduleStatusRefresh)(){};
};

void InitializeOutputDevice(const OutputDeviceHost& host) noexcept;

std::optional<std::wstring> OutputErrorText(ammod::ipc::ErrorCategory category);

// UI thread. The card copies the chrome of the native settings card and the
// text styles of the native row, like the exclusive-mode and buffer cards.
void AppendOutputDeviceCard(const winrt::Microsoft::UI::Xaml::Controls::StackPanel& target,
                            const winrt::Microsoft::UI::Xaml::Controls::Border& templateCard,
                            const std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock>&
                                nativeText);
void AttachExistingOutputDeviceCard(const winrt::Microsoft::UI::Xaml::DependencyObject& page);
void RefreshOutputDeviceCard();

// Broker pipe thread.
void OnOutputPipeConnected();
// Returns false when the write failed and the connection must be dropped.
bool SendPendingOutputDevice(const std::function<bool(const ammod::ipc::Message&)>& write);
void OnOutputPipeIdle();
void OnOutputStatusReceived(const ammod::ipc::Message& status);

} // namespace ammod::ui_fork
