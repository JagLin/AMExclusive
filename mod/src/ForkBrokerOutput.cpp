#include "ForkBrokerOutput.h"

#include <windows.h>

#include <iterator>

namespace ammod::broker_fork {
namespace {

using namespace ammod::ipc;

struct OutputSetting {
    OutputBackend backend{OutputBackend::Wasapi};
    std::wstring asioDriver;
};

// output=asio without a driver name cannot be honoured, so it reads as WASAPI.
OutputSetting ReadOutputSetting(const std::filesystem::path& ini) {
    wchar_t output[16]{};
    GetPrivateProfileStringW(L"mod", L"output", L"wasapi", output,
        static_cast<DWORD>(std::size(output)), ini.c_str());
    wchar_t driver[kOutputDeviceChars]{};
    GetPrivateProfileStringW(L"mod", L"asio_driver", L"", driver,
        static_cast<DWORD>(std::size(driver)), ini.c_str());
    OutputSetting setting{};
    setting.asioDriver = driver;
    if (_wcsicmp(output, L"asio") == 0 && !setting.asioDriver.empty()) {
        setting.backend = OutputBackend::Asio;
    }
    return setting;
}

bool IsRegisteredAsioDriver(const std::wstring& name) {
    if (name.empty() || name.size() >= kOutputDeviceChars) return false;
    const std::wstring key = L"SOFTWARE\\ASIO\\" + name;
    return RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"CLSID", RRF_RT_REG_SZ,
                        nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// Selecting WASAPI leaves asio_driver untouched so the last ASIO choice is
// remembered for the UI and for switching back.
bool WriteOutputSetting(const std::filesystem::path& ini, OutputBackend backend,
                        const std::wstring& asioDriver) {
    const bool asio = backend == OutputBackend::Asio;
    if (!WritePrivateProfileStringW(L"mod", L"output", asio ? L"asio" : L"wasapi",
                                    ini.c_str())) {
        return false;
    }
    if (asio && !WritePrivateProfileStringW(L"mod", L"asio_driver", asioDriver.c_str(),
                                            ini.c_str())) {
        return false;
    }
    (void)WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
    const auto saved = ReadOutputSetting(ini);
    return saved.backend == backend && (!asio || saved.asioDriver == asioDriver);
}

void ApplyOutputSetting(Message& status, const OutputSetting& setting) {
    status.fork.outputBackend = setting.backend;
    wcsncpy_s(status.fork.outputDevice, setting.asioDriver.c_str(), _TRUNCATE);
}

} // namespace

void LoadOutputSetting(Message& status, const std::filesystem::path& ini) {
    ApplyOutputSetting(status, ReadOutputSetting(ini));
}

std::wstring DescribeOutputSetting(const Message& status) {
    wchar_t driver[kOutputDeviceChars]{};
    wcsncpy_s(driver, status.fork.outputDevice, _TRUNCATE);
    return std::wstring(L"output=") +
        (status.fork.outputBackend == OutputBackend::Asio ? L"asio" : L"wasapi") +
        L" asioDriver=" + driver;
}

bool IsOutputDeviceMessage(const Message& incoming) noexcept {
    return incoming.type == fork::kSetOutputDevice &&
           (incoming.role == Role::Ui || incoming.role == Role::Controller);
}

void HandleOutputDeviceMessage(const Message& incoming, Message& status,
                               const std::filesystem::path& ini, LogFn log) {
    wchar_t driver[kOutputDeviceChars]{};
    wcsncpy_s(driver, incoming.fork.outputDevice, _TRUNCATE);
    wchar_t current[kOutputDeviceChars]{};
    wcsncpy_s(current, status.fork.outputDevice, _TRUNCATE);
    OutputSetting requested{};
    requested.backend = incoming.fork.outputBackend == OutputBackend::Asio
        ? OutputBackend::Asio : OutputBackend::Wasapi;
    requested.asioDriver = requested.backend == OutputBackend::Asio
        ? std::wstring(driver) : std::wstring(current);
    if (requested.backend == OutputBackend::Asio &&
        !IsRegisteredAsioDriver(requested.asioDriver)) {
        log(L"output device rejected: ASIO driver not registered name=" +
            requested.asioDriver);
    } else if (WriteOutputSetting(ini, requested.backend, requested.asioDriver)) {
        status.generation++;
        ApplyOutputSetting(status, requested);
        status.error = ErrorCategory::None;
        status.hresult = S_OK;
        log(L"output device persisted backend=" +
            std::wstring(requested.backend == OutputBackend::Asio ? L"asio" : L"wasapi") +
            L" asioDriver=" + requested.asioDriver);
    } else {
        log(L"output device persistence failed error=" + std::to_wstring(GetLastError()));
    }
}

const wchar_t* OutputErrorReason(ErrorCategory category, bool chinese) noexcept {
    if (category == fork::kErrorAsioDriverUnavailable) {
        return chinese ? L"所选的 ASIO 驱动无法启动，请确认驱动已安装且未被其他程序占用。"
                       : L"The selected ASIO driver could not start. Check that it is "
                         L"installed and not in use by another app.";
    }
    if (category == fork::kErrorAsioDriverReset) {
        return chinese ? L"ASIO 驱动要求重置，将在下一首歌重新载入。"
                       : L"The ASIO driver requested a reset; it reloads on the next track.";
    }
    return nullptr;
}

} // namespace ammod::broker_fork
