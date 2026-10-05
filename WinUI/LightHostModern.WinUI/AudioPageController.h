#pragma once
#include "HostConnection.h"
#include "DialogPresentation.h"
#include "Localization.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <algorithm>
#include <tuple>

namespace lightHostModern::ui
{
// Commands are built from a single adopted snapshot. Read-only enumeration may
// outlive that snapshot; it never changes the generation attached to the edit.
class AudioPageController
{
public:
    using JsonObject = winrt::Windows::Data::Json::JsonObject;
    using JsonValue = winrt::Windows::Data::Json::JsonValue;
    using ComboBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox;

    static JsonObject draft(const std::string& snapshot)
    {
        const auto state = ipc::parseSnapshotObject(snapshot).GetNamedObject(L"audioSelection");
        auto result = JsonObject::Parse(state.GetNamedObject(L"editable").Stringify());
        result.SetNamedValue(L"expectedGeneration", state.GetNamedValue(L"generation"));
        return result;
    }
    static void text(JsonObject const& object, const wchar_t* key, const winrt::hstring& value)
    { object.SetNamedValue(key, JsonValue::CreateStringValue(value)); }
    static winrt::hstring command(JsonObject const& object)
    { return L"select-audio-device:" + object.Stringify(); }
    static void defaultChannels(JsonObject const& object)
    {
        text(object, L"inputMask", L"0"); text(object, L"outputMask", L"0");
        object.SetNamedValue(L"defaultInputChannels", JsonValue::CreateBooleanValue(true));
        object.SetNamedValue(L"defaultOutputChannels", JsonValue::CreateBooleanValue(true));
    }
    static winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> selectionCommand(
        std::shared_ptr<HostConnection> connection, std::string field, std::string value)
    {
        auto request = draft(connection->snapshotJson);
        if (field == "backend")
        {
            const auto optionsWire = winrt::to_string(co_await connection->requestAsync("audio-device-options:" + value));
            const auto options = ipc::parseObject(optionsWire);
            if (!options.GetNamedBoolean(L"available", false)) throw winrt::hresult_invalid_argument(L"Audio backend is unavailable");
            text(request, L"backend", winrt::to_hstring(value));
            text(request, L"input", options.GetNamedString(L"suggestedInput", L""));
            text(request, L"output", options.GetNamedString(L"suggestedOutput", L""));
            defaultChannels(request);
            request.SetNamedValue(L"sampleRate", JsonValue::CreateNumberValue(0));
            request.SetNamedValue(L"bufferSize", JsonValue::CreateNumberValue(0));
        }
        else if (field == "input" || field == "output")
        {
            text(request, winrt::to_hstring(field).c_str(), winrt::to_hstring(value));
            if (request.GetNamedString(L"backend") == L"ASIO")
            { text(request, L"input", winrt::to_hstring(value)); text(request, L"output", winrt::to_hstring(value)); }
            defaultChannels(request);
        }
        else if (field == "sampleRate" || field == "bufferSize")
        {
            const auto parsed = std::stod(value);
            request.SetNamedValue(winrt::to_hstring(field), JsonValue::CreateNumberValue(parsed));
        }
        else throw winrt::hresult_invalid_argument();
        co_return command(request);
    }
    static winrt::hstring channelCommand(const std::string& snapshot, bool input, int first, int last, bool enabled)
    {
        if (first < 0 || last < first || last >= 256) throw winrt::hresult_invalid_argument();
        auto request = draft(snapshot);
        const auto key = input ? L"inputMask" : L"outputMask";
        std::wstring mask(request.GetNamedString(key));
        if (mask.size() > 256) throw winrt::hresult_invalid_argument();
        if (mask.size() < static_cast<size_t>(last + 1)) mask.insert(0, last + 1 - mask.size(), L'0');
        for (int channel = first; channel <= last; ++channel) mask[mask.size() - 1 - channel] = enabled ? L'1' : L'0';
        text(request, key, winrt::hstring(mask));
        request.SetNamedValue(input ? L"defaultInputChannels" : L"defaultOutputChannels", JsonValue::CreateBooleanValue(false));
        return command(request);
    }
    static winrt::hstring deviceName(ComboBox const& box)
    {
        if (const auto item = box.SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem>())
            return winrt::unbox_value<winrt::hstring>(item.Tag());
        return L"";
    }
    static void devices(ComboBox const& box, std::vector<std::string> names, const std::string& selected,
        const winrt::hstring& none, bool preserveMissing = false)
    {
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        if (preserveMissing && !selected.empty() && std::find(names.begin(), names.end(), selected) == names.end()) names.push_back(selected);
        // Avoid rebuilding an unchanged native picker (and preserve its open state).
        bool same = box.Items().Size() == names.size() + 1;
        if (same)
            for (uint32_t i = 0; i < box.Items().Size(); ++i)
            {
                const auto item = box.Items().GetAt(i).try_as<ComboBoxItem>();
                if (!item || winrt::unbox_value<winrt::hstring>(item.Tag()) != winrt::to_hstring(i ? names[i - 1] : "")) { same = false; break; }
            }
        int selectedIndex = 0;
        if (!same) box.Items().Clear();
        for (size_t i = 0; i <= names.size(); ++i)
        {
            const auto identity = i ? winrt::to_hstring(names[i - 1]) : winrt::hstring{};
            if (!same)
            {
                ComboBoxItem item; item.Tag(winrt::box_value(identity));
                item.Content(winrt::box_value(i ? identity : none)); box.Items().Append(item);
            }
            if (winrt::to_string(identity) == selected) selectedIndex = static_cast<int>(i);
        }
        box.Items().GetAt(0).as<ComboBoxItem>().Content(winrt::box_value(none));
        if (box.SelectedIndex() != selectedIndex) box.SelectedIndex(selectedIndex);
    }
};

struct PreferredAudioDraft
{
    uint64_t revision = 0;
    bool closed = false;
};

inline winrt::fire_and_forget loadPreferredAudioOptions(std::shared_ptr<HostConnection> connection,
    std::shared_ptr<PreferredAudioDraft> draft, winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::ContentDialog> weakDialog,
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::ComboBox> weakInput,
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::ComboBox> weakOutput,
    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Controls::TextBlock> weakStatus,
    std::string backend, std::string input, std::string output, bool preserveMissing,
    winrt::hstring none, winrt::hstring loading, winrt::hstring failed)
{
    using namespace winrt;
    const auto revision = ++draft->revision;
    if (auto dialog = weakDialog.get()) dialog.IsPrimaryButtonEnabled(false);
    if (auto status = weakStatus.get()) status.Text(loading);
    try
    {
        const auto wire = to_string(co_await connection->requestAsync("audio-device-options:" + backend));
        if (draft->closed || revision != draft->revision) co_return;
        if (ipc::extractString(wire, "status") != "ok") throw hresult_error(E_FAIL);
        auto inputBox = weakInput.get(); auto outputBox = weakOutput.get();
        if (!inputBox || !outputBox) co_return;
        if (!preserveMissing) { input = ipc::extractString(wire, "suggestedInput"); output = ipc::extractString(wire, "suggestedOutput"); }
        const bool separate = ipc::extractBool(wire, "separateInputsAndOutputs", true);
        AudioPageController::devices(inputBox, ipc::extractStringArray(wire, "inputs"), separate ? input : output, none, preserveMissing);
        AudioPageController::devices(outputBox, ipc::extractStringArray(wire, "outputs"), output, none, preserveMissing);
        inputBox.Tag(box_value(!separate));
        outputBox.Visibility(separate ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        if (auto dialog = weakDialog.get()) dialog.IsPrimaryButtonEnabled(true);
        if (auto status = weakStatus.get()) status.Text(L"");
    }
    catch (...)
    {
        if (!draft->closed && revision == draft->revision)
            if (auto status = weakStatus.get()) status.Text(failed);
    }
}

inline winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> showPreferredAudioDialog(
    winrt::Microsoft::UI::Xaml::FrameworkElement owner, ::LightHostModernWinUI::LocalizationCatalog& catalog,
    std::shared_ptr<HostConnection> connection)
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    const auto snapshot = connection->snapshotJson;
    const auto generation = ipc::parseSnapshotObject(snapshot).GetNamedObject(L"audioSelection").GetNamedValue(L"generation");
    auto names = ipc::extractStringArray(snapshot, "backendNames");
    auto selectedBackend = ipc::extractString(snapshot, "audioPersistenceCustomBackend");
    if (selectedBackend.empty() && !names.empty()) selectedBackend = names.front();
    if (!selectedBackend.empty() && std::find(names.begin(), names.end(), selectedBackend) == names.end()) names.push_back(selectedBackend);
    ContentDialog dialog; dialog.XamlRoot(owner.XamlRoot()); dialog.RequestedTheme(owner.ActualTheme());
    dialog.Title(box_value(catalog.text("settings.persistence.preferred", L"Preferred device")));
    dialog.PrimaryButtonText(catalog.text("common.save", L"Save")); dialog.CloseButtonText(catalog.text("common.cancel", L"Cancel"));
    dialog.DefaultButton(ContentDialogButton::Primary); dialog.IsPrimaryButtonEnabled(false);
    StackPanel body; body.Spacing(16); body.MinWidth((std::min)(400.0, (std::max)(200.0, owner.XamlRoot().Size().Width - 100.0)));
    TextBlock hint; hint.Text(catalog.text("settings.persistence.preferredDescription", L"Select the backend and device LightHostModern should retry."));
    hint.TextWrapping(TextWrapping::Wrap); body.Children().Append(hint);
    ComboBox backend, input, output;
    for (const auto& row : {std::tuple{backend, L"RecoveryAudioBackend", "settings.persistence.backend", L"Audio backend"},
                           {input, L"RecoveryInputDevice", "settings.persistence.inputDevice", L"Input device"},
                           {output, L"RecoveryOutputDevice", "settings.persistence.outputDevice", L"Output device"}})
    {
        const auto box = std::get<0>(row); box.HorizontalAlignment(HorizontalAlignment::Stretch);
        box.Header(box_value(catalog.text(std::get<2>(row), std::get<3>(row))));
        Automation::AutomationProperties::SetAutomationId(box, std::get<1>(row));
        Automation::AutomationProperties::SetName(box, catalog.text(std::get<2>(row), std::get<3>(row)));
        body.Children().Append(box);
    }
    TextBlock status; status.TextWrapping(TextWrapping::Wrap);
    Automation::AutomationProperties::SetLiveSetting(status, Automation::Peers::AutomationLiveSetting::Polite);
    body.Children().Append(status); dialog.Content(body);
    int index = -1;
    for (const auto& name : names) { backend.Items().Append(box_value(to_hstring(name))); if (name == selectedBackend) index = backend.Items().Size() - 1; }
    backend.SelectedIndex(index);
    auto state = std::make_shared<PreferredAudioDraft>();
    const auto none = catalog.text("common.none", L"None");
    const auto loading = catalog.text("audio.loadingDevices", L"Loading devices…");
    const auto failed = catalog.text("audio.deviceListFailed", L"Could not read the device list. Select the backend again to retry.");
    const auto load = [connection, state, weakDialog = make_weak(dialog), weakInput = make_weak(input), weakOutput = make_weak(output),
        weakStatus = make_weak(status), none, loading, failed](std::string type, std::string in, std::string out, bool preserve) {
        loadPreferredAudioOptions(connection, state, weakDialog, weakInput, weakOutput, weakStatus, std::move(type), std::move(in), std::move(out), preserve, none, loading, failed);
    };
    const auto changed = backend.SelectionChanged([load](const winrt::Windows::Foundation::IInspectable& sender, const SelectionChangedEventArgs&) {
        const auto box = sender.as<ComboBox>();
        if (box.SelectedIndex() >= 0) load(to_string(unbox_value<hstring>(box.SelectedItem())), {}, {}, false);
    });
    if (index >= 0) load(selectedBackend, ipc::extractString(snapshot, "audioPersistenceCustomInputDevice"), ipc::extractString(snapshot, "audioPersistenceCustomOutputDevice"), true);
    ContentDialogResult result = ContentDialogResult::None;
    try { result = co_await lightHostModern::ui::showAppDialog(dialog); }
    catch (...) { state->closed = true; backend.SelectionChanged(changed); throw; }
    state->closed = true; backend.SelectionChanged(changed);
    if (result != ContentDialogResult::Primary || backend.SelectedIndex() < 0) co_return L"";
    ipc::JsonObject request;
    request.SetNamedValue(L"expectedGeneration", generation);
    AudioPageController::text(request, L"backend", unbox_value<hstring>(backend.SelectedItem()));
    AudioPageController::text(request, L"input", AudioPageController::deviceName(input));
    const bool coupled = input.Tag() && unbox_value<bool>(input.Tag());
    AudioPageController::text(request, L"output", AudioPageController::deviceName(coupled ? input : output));
    co_return L"set-preferred-audio-device:" + request.Stringify();
}
}
