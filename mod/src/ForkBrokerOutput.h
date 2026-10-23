#pragma once

// AMExclusive fork: the Broker-owned output selection (WASAPI or an ASIO
// driver). Persisted as output/asio_driver in the [mod] section of the ini and
// published to every peer in Message::fork. Kept out of Broker.cpp so an
// upstream merge only meets the few calls Broker makes into this module.

#include "IpcProtocol.h"

#include <filesystem>
#include <string>

namespace ammod::broker_fork {

using LogFn = void (*)(const std::wstring& text);

// Startup: copy the persisted selection into the Broker's status.
void LoadOutputSetting(ammod::ipc::Message& status, const std::filesystem::path& ini);
std::wstring DescribeOutputSetting(const ammod::ipc::Message& status);

bool IsOutputDeviceMessage(const ammod::ipc::Message& incoming) noexcept;
// Validates, persists and publishes a UI/Controller selection; a rejected or
// unpersisted request leaves the status untouched.
void HandleOutputDeviceMessage(const ammod::ipc::Message& incoming,
                               ammod::ipc::Message& status,
                               const std::filesystem::path& ini, LogFn log);

const wchar_t* OutputErrorReason(ammod::ipc::ErrorCategory category, bool chinese) noexcept;

} // namespace ammod::broker_fork
