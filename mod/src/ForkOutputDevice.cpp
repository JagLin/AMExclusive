#include "ForkOutputDevice.h"
#include "IpcTransport.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace ammod::ui_fork {
namespace {

using namespace winrt;
namespace Xaml = Microsoft::UI::Xaml;
namespace Controls = Microsoft::UI::Xaml::Controls;
namespace Media = Microsoft::UI::Xaml::Media;
namespace Automation = Microsoft::UI::Xaml::Automation;
using namespace ammod::ipc;

constexpr wchar_t kComboName[] = L"AMExclusiveOutputDeviceBox";

OutputDeviceHost g_host{};

// Output-device selection follows the hardware buffer protocol: `requested`
// stays set until the Broker echoes it in a status message (or gives up after
// a few unanswered retries); `pending` is what the pipe thread still has to
// send. Both are guarded by g_outputMutex.
struct OutputSelection {
    OutputBackend backend{OutputBackend::Wasapi};
    std::wstring asioDriver;
    bool operator==(const OutputSelection&) const = default;
};
std::mutex g_outputMutex;
std::optional<OutputSelection> g_requestedOutput;
std::optional<OutputSelection> g_pendingOutput;
ULONGLONG g_outputAckDeadlineTick{};
int g_outputRetries{};
std::atomic<bool> g_outputComboUpdating{};

std::mutex g_comboMutex;
winrt::weak_ref<Controls::ComboBox> g_outputDeviceBox;

std::wstring Localized(std::wstring_view chinese, std::wstring_view english) {
    return g_host.localized ? g_host.localized(chinese, english) : std::wstring(english);
}

void Log(const std::wstring& message) {
    if (g_host.log) g_host.log(message);
}

Message StatusSnapshot() {
    return g_host.statusSnapshot ? g_host.statusSnapshot() : Message{};
}

Xaml::DependencyObject FindByName(const Xaml::DependencyObject& node, std::wstring_view name) {
    if (!node) return nullptr;
    try {
        if (auto element = node.try_as<Xaml::FrameworkElement>(); element && element.Name() == name) {
            return node;
        }
        const int count = Media::VisualTreeHelper::GetChildrenCount(node);
        for (int index = 0; index < count; ++index) {
            if (auto found = FindByName(Media::VisualTreeHelper::GetChild(node, index), name)) {
                return found;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

// ASIO drivers register under HKLM\SOFTWARE\ASIO; the key name is what the
// Agent loads. Read-only, enumerated on the UI thread when the card is built.
std::vector<std::wstring> EnumerateAsioDrivers() {
    std::vector<std::wstring> drivers;
    HKEY root{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) !=
        ERROR_SUCCESS) {
        return drivers;
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[256]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(root, index, name, &length, nullptr, nullptr,
                                             nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS && length != 0 && length < kOutputDeviceChars) {
            drivers.emplace_back(name, length);
        }
    }
    RegCloseKey(root);
    std::sort(drivers.begin(), drivers.end());
    return drivers;
}

Controls::ComboBoxItem MakeOutputItem(const std::wstring& label, const OutputSelection& selection) {
    Controls::ComboBoxItem item;
    item.Content(box_value(hstring{label}));
    item.Tag(box_value(hstring{
        std::wstring(selection.backend == OutputBackend::Asio ? L"asio:" : L"wasapi:") +
        selection.asioDriver}));
    return item;
}

std::optional<OutputSelection> SelectionFromItem(const Windows::Foundation::IInspectable& value) {
    const auto item = value.try_as<Controls::ComboBoxItem>();
    if (!item) return std::nullopt;
    const std::wstring tag = unbox_value_or<hstring>(item.Tag(), L"").c_str();
    if (tag.rfind(L"asio:", 0) == 0) return OutputSelection{OutputBackend::Asio, tag.substr(5)};
    if (tag.rfind(L"wasapi:", 0) == 0) return OutputSelection{};
    return std::nullopt;
}

OutputSelection SelectionFromStatus(const Message& status) {
    wchar_t driver[kOutputDeviceChars]{};
    wcsncpy_s(driver, status.fork.outputDevice, _TRUNCATE);
    if (status.fork.outputBackend == OutputBackend::Asio && driver[0]) {
        return OutputSelection{OutputBackend::Asio, driver};
    }
    return OutputSelection{};
}

// What the card should show: an unconfirmed user choice wins over status.
OutputSelection CurrentOutputSelection() {
    {
        std::lock_guard lock(g_outputMutex);
        if (g_requestedOutput) return *g_requestedOutput;
    }
    return SelectionFromStatus(StatusSnapshot());
}

void PopulateOutputDeviceBox(const Controls::ComboBox& combo) {
    g_outputComboUpdating.store(true);
    combo.Items().Clear();
    combo.Items().Append(MakeOutputItem(
        Localized(L"WASAPI（默认播放设备）", L"WASAPI (default playback device)"),
        OutputSelection{}));
    for (const auto& driver : EnumerateAsioDrivers()) {
        combo.Items().Append(MakeOutputItem(L"ASIO · " + driver,
                                            OutputSelection{OutputBackend::Asio, driver}));
    }
    g_outputComboUpdating.store(false);
}

void ApplyOutputDeviceStatus(const Controls::ComboBox& combo) {
    if (!combo || combo.IsDropDownOpen()) return;
    const auto wanted = CurrentOutputSelection();
    const auto items = combo.Items();
    int match = -1;
    for (std::uint32_t index = 0; index < items.Size(); ++index) {
        const auto selection = SelectionFromItem(items.GetAt(index));
        if (selection && *selection == wanted) {
            match = static_cast<int>(index);
            break;
        }
    }
    g_outputComboUpdating.store(true);
    if (match < 0 && wanted.backend == OutputBackend::Asio) {
        // Show a persisted driver that is no longer registered instead of
        // silently presenting a different device as the active choice.
        items.Append(MakeOutputItem(
            L"ASIO · " + wanted.asioDriver + Localized(L"（未安装）", L" (not installed)"),
            wanted));
        match = static_cast<int>(items.Size()) - 1;
    }
    if (match >= 0 && combo.SelectedIndex() != match) combo.SelectedIndex(match);
    g_outputComboUpdating.store(false);
}

void CommitOutputDevice(const Controls::ComboBox& combo) {
    if (!combo || g_outputComboUpdating.load()) return;
    const auto selection = SelectionFromItem(combo.SelectedItem());
    if (!selection || *selection == CurrentOutputSelection()) return;
    {
        std::lock_guard lock(g_outputMutex);
        g_requestedOutput = *selection;
        g_pendingOutput = *selection;
        g_outputAckDeadlineTick = 0;
        g_outputRetries = 0;
    }
    Log(L"output device selection queued backend=" +
        std::wstring(selection->backend == OutputBackend::Asio ? L"asio" : L"wasapi") +
        L" asioDriver=" + selection->asioDriver);
}

void RememberOutputDeviceBox(const Controls::ComboBox& combo) {
    std::lock_guard lock(g_comboMutex);
    g_outputDeviceBox = make_weak(combo);
}

} // namespace

void InitializeOutputDevice(const OutputDeviceHost& host) noexcept {
    g_host = host;
}

std::optional<std::wstring> OutputErrorText(ErrorCategory category) {
    if (category == fork::kErrorAsioDriverUnavailable) {
        return Localized(L"所选的 ASIO 驱动无法启动",
                         L"The selected ASIO driver could not start");
    }
    if (category == fork::kErrorAsioDriverReset) {
        return Localized(L"ASIO 驱动要求重置，将在下一首歌重新载入",
                         L"The ASIO driver requested a reset; it reloads on the next track");
    }
    return std::nullopt;
}

void AppendOutputDeviceCard(const Controls::StackPanel& target,
                            const Controls::Border& card,
                            const std::vector<Controls::TextBlock>& nativeText) {
    Controls::TextBlock outputTitle;
    outputTitle.Text(Localized(L"输出设备", L"Output device"));
    Controls::TextBlock outputDescription;
    outputDescription.Text(Localized(
        L"选择独占输出使用的设备，从下一首歌开始生效。",
        L"Choose the device for exclusive output; takes effect from the next track."));
    outputDescription.TextWrapping(Xaml::TextWrapping::Wrap);
    if (!nativeText.empty()) outputTitle.Style(nativeText[0].Style());
    if (nativeText.size() > 1) outputDescription.Style(nativeText[1].Style());

    Controls::StackPanel outputLabels;
    outputLabels.VerticalAlignment(Xaml::VerticalAlignment::Center);
    outputLabels.Children().Append(outputTitle);
    outputLabels.Children().Append(outputDescription);

    Controls::ComboBox outputDevice;
    outputDevice.Name(kComboName);
    outputDevice.MinWidth(260.0);
    outputDevice.VerticalAlignment(Xaml::VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(
        outputDevice, Localized(L"独占输出设备", L"Exclusive output device"));
    outputDevice.SelectionChanged([outputDevice](auto&&, auto&&) {
        CommitOutputDevice(outputDevice);
    });
    PopulateOutputDeviceBox(outputDevice);

    Controls::ColumnDefinition outputLabelColumn;
    outputLabelColumn.Width(Xaml::GridLength{1.0, Xaml::GridUnitType::Star});
    Controls::ColumnDefinition outputInputColumn;
    outputInputColumn.Width(Xaml::GridLength{1.0, Xaml::GridUnitType::Auto});
    Controls::Grid outputContent;
    outputContent.ColumnDefinitions().Append(outputLabelColumn);
    outputContent.ColumnDefinitions().Append(outputInputColumn);
    outputContent.Children().Append(outputLabels);
    Controls::Grid::SetColumn(outputDevice, 1);
    outputContent.Children().Append(outputDevice);

    Controls::Border outputCard;
    outputCard.Name(L"AMExclusiveOutputDeviceCard");
    outputCard.Child(outputContent);
    outputCard.Background(card.Background());
    outputCard.BorderBrush(card.BorderBrush());
    outputCard.BorderThickness(card.BorderThickness());
    outputCard.CornerRadius(card.CornerRadius());
    outputCard.Padding(card.Padding());
    outputCard.Margin(card.Margin());
    outputCard.HorizontalAlignment(card.HorizontalAlignment());
    outputCard.VerticalAlignment(card.VerticalAlignment());
    outputCard.MinHeight(card.MinHeight());
    outputCard.MaxWidth(card.MaxWidth());
    target.Children().Append(outputCard);

    RememberOutputDeviceBox(outputDevice);
    ApplyOutputDeviceStatus(outputDevice);
    Log(L"Output device settings card attached to PlaybackSettingsPage");
}

void AttachExistingOutputDeviceCard(const Xaml::DependencyObject& page) {
    if (auto combo = FindByName(page, kComboName).try_as<Controls::ComboBox>()) {
        RememberOutputDeviceBox(combo);
        ApplyOutputDeviceStatus(combo);
    }
}

void RefreshOutputDeviceCard() {
    winrt::weak_ref<Controls::ComboBox> outputDevice;
    {
        std::lock_guard lock(g_comboMutex);
        outputDevice = g_outputDeviceBox;
    }
    if (auto combo = outputDevice.get()) ApplyOutputDeviceStatus(combo);
}

void OnOutputPipeConnected() {
    std::lock_guard lock(g_outputMutex);
    if (g_requestedOutput) {
        g_pendingOutput = g_requestedOutput;
        g_outputAckDeadlineTick = 0;
    }
}

bool SendPendingOutputDevice(const std::function<bool(const Message&)>& write) {
    std::optional<OutputSelection> pendingOutput;
    {
        std::lock_guard lock(g_outputMutex);
        pendingOutput.swap(g_pendingOutput);
    }
    if (!pendingOutput) return true;
    auto message = NewMessage(fork::kSetOutputDevice, Role::Ui);
    message.uiPid = GetCurrentProcessId();
    message.fork.outputBackend = pendingOutput->backend;
    wcsncpy_s(message.fork.outputDevice, pendingOutput->asioDriver.c_str(), _TRUNCATE);
    const bool written = write(message);
    std::lock_guard lock(g_outputMutex);
    if (!written) {
        if (!g_pendingOutput) g_pendingOutput = pendingOutput;
        return false;
    }
    g_outputAckDeadlineTick = GetTickCount64() + 1000;
    return true;
}

void OnOutputPipeIdle() {
    bool outputAbandoned{};
    {
        // The Broker rejects an unregistered driver silently; after a few
        // unanswered retries fall back to showing the persisted status instead
        // of retrying forever.
        std::lock_guard lock(g_outputMutex);
        if (g_requestedOutput && g_outputAckDeadlineTick != 0 &&
            GetTickCount64() >= g_outputAckDeadlineTick) {
            g_outputAckDeadlineTick = 0;
            if (++g_outputRetries > 3) {
                g_requestedOutput.reset();
                outputAbandoned = true;
            } else if (!g_pendingOutput) {
                g_pendingOutput = g_requestedOutput;
            }
        }
    }
    if (outputAbandoned) {
        Log(L"output device selection not confirmed by broker; reverting display");
        if (g_host.scheduleStatusRefresh) g_host.scheduleStatusRefresh();
    }
}

void OnOutputStatusReceived(const Message& status) {
    bool outputConfirmed{};
    {
        std::lock_guard lock(g_outputMutex);
        if (g_requestedOutput && *g_requestedOutput == SelectionFromStatus(status)) {
            g_requestedOutput.reset();
            g_outputAckDeadlineTick = 0;
            g_outputRetries = 0;
            outputConfirmed = true;
        }
    }
    if (outputConfirmed) Log(L"output device selection confirmed by broker");
}

} // namespace ammod::ui_fork
