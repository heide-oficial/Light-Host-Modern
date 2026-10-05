#pragma once
#include "Localization.h"
#include "DialogPresentation.h"
#include "PluginRows.h"
#include "DisplayNameDialog.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace lightHostModern::ui
{
inline bool validInstanceName(std::wstring_view name)
{
    size_t begin = 0, end = name.size();
    while (begin < end && iswspace(name[begin])) ++begin;
    while (end > begin && iswspace(name[end - 1])) --end;
    size_t count = 0;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const auto c = name[i];
        if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029) return false;
        if (i >= begin && i < end) ++count;
        if (c >= 0xd800 && c <= 0xdbff)
        { if (++i >= name.size() || name[i] < 0xdc00 || name[i] > 0xdfff) return false; }
        else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    return count <= 128;
}

// Dialog presentation returns an action. Only the owning controller sends it.
inline winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> showPluginDialog(
    winrt::Microsoft::UI::Xaml::FrameworkElement owner, ::LightHostModernWinUI::LocalizationCatalog& catalog,
    std::string action, std::string id, std::string details, std::vector<PluginRowData> instances, bool running = true)
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    ContentDialog dialog;
    dialog.XamlRoot(owner.XamlRoot()); dialog.RequestedTheme(owner.ActualTheme());
    dialog.CloseButtonText(catalog.text("common.cancel", L"Cancel"));
    StackPanel body; body.Spacing(12); body.MinWidth(300);
    TextBox name; ListView destinations; std::vector<std::string> ids;
    const auto label = [&](const char* key, const wchar_t* fallback) { return std::wstring(catalog.text(key, fallback)); };
    if (action == "rename")
    {
        dialog.Title(box_value(catalog.text(running ? "plugins.rename" : "plugins.renameInstalled", running ? L"Rename instance" : L"Rename plugin")));
        dialog.PrimaryButtonText(catalog.text("common.save", L"Save"));
        dialog.SecondaryButtonText(catalog.text("plugins.restoreName", L"Restore original name"));
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(760.0));
        body.MinWidth(560);
        name.Header(box_value(catalog.text("plugins.customName", L"Custom name")));
        name.Text(to_hstring(ipc::extractString(details, "customName")));
        name.PlaceholderText(to_hstring(ipc::extractString(details, "name")));
        name.MaxLength(256); name.AcceptsReturn(false);
        Automation::AutomationProperties::SetAutomationId(name, L"InstanceName");
        Automation::AutomationProperties::SetName(name, catalog.text("plugins.customName", L"Custom name"));
        TextBlock hint; hint.Text(catalog.text("plugins.nameHint", L"Up to 128 characters. Leave empty to restore the original name.")); hint.TextWrapping(TextWrapping::Wrap);
        body.Children().Append(name); body.Children().Append(hint);
        name.TextChanged([weak = make_weak(dialog)](winrt::Windows::Foundation::IInspectable const& sender, TextChangedEventArgs const&) {
            if (auto current = weak.get()) current.IsPrimaryButtonEnabled(validInstanceName(sender.as<TextBox>().Text()));
        });
        focusDialogTextBox(dialog,owner,name);
    }
    else if (action == "swap")
    {
        dialog.Title(box_value(catalog.text("plugins.swap", L"Swap position")));
        dialog.PrimaryButtonText(catalog.text("plugins.swap", L"Swap position")); dialog.IsPrimaryButtonEnabled(false);
        TextBlock hint; hint.Text(catalog.text("plugins.swapHint", L"Choose another instance in the actual processing order.")); hint.TextWrapping(TextWrapping::Wrap); body.Children().Append(hint);
        destinations.Height((std::max)(120.0, (std::min)(360.0, owner.XamlRoot().Size().Height - 300.0)));
        Automation::AutomationProperties::SetAutomationId(destinations, L"SwapInstanceList");
        Automation::AutomationProperties::SetName(destinations, catalog.text("plugins.swapTarget", L"Instance to swap with"));
        for (const auto& row : instances) if (row.instanceId != id)
        {
            ids.push_back(row.instanceId);
            destinations.Items().Append(box_value(to_hstring(std::to_string(row.originalIndex + 1) + ". " + row.name)));
        }
        destinations.Height((std::min)(destinations.Height(), (std::max)(120.0, ids.size() * 44.0)));
        destinations.SelectionChanged([weak = make_weak(dialog)](winrt::Windows::Foundation::IInspectable const& sender, const auto&) {
            if (auto current = weak.get()) current.IsPrimaryButtonEnabled(sender.as<ListView>().SelectedIndex() >= 0);
        });
        body.Children().Append(destinations);
    }
    else
    {
        dialog.Title(box_value(catalog.text("plugins.details", L"Plugin details")));
        dialog.CloseButtonText(catalog.text("common.close", L"Close"));
        dialog.Opened([](const ContentDialog& sender, const ContentDialogOpenedEventArgs&) {
            // Keep the native dialog template and keyboard behavior. Its single
            // Close button normally occupies only the last command column.
            const auto stretch = [](const auto& self, DependencyObject root) -> bool {
                if (auto button = root.try_as<Button>(); button && button.Name() == L"CloseButton") {
                    if (auto parent = Media::VisualTreeHelper::GetParent(button).try_as<Grid>()) {
                        Grid::SetColumn(button, 0);
                        Grid::SetColumnSpan(button, (std::max)(1, static_cast<int>(parent.ColumnDefinitions().Size())));
                        button.HorizontalAlignment(HorizontalAlignment::Stretch);
                    }
                    return true;
                }
                for (int i = 0; i < Media::VisualTreeHelper::GetChildrenCount(root); ++i)
                    if (self(self, Media::VisualTreeHelper::GetChild(root, i))) return true;
                return false;
            };
            stretch(stretch, sender);
        });
        std::wstring text;
        for (const auto& field : {std::pair{"name", "originalName"}, {"manufacturer", "manufacturer"}, {"format", "format"},
             {"version", "version"}, {"identity", "identity"}, {"knownId", "knownId"}, {"instanceId", "instanceId"}, {"path", "path"}})
        {
            const auto value = ipc::extractString(details, field.first);
            if (value.empty() && std::string(field.first) == "instanceId") continue;
            text += std::wstring(catalog.text(std::string("details.") + field.second, to_hstring(field.second).c_str()))
                + L": " + (value.empty() ? label("common.unavailable", L"Unavailable") : std::wstring(to_hstring(value))) + L"\n";
        }
        for (const auto* field : {"availability", "loading", "declaredMetadata", "verifiedMetadata"})
        {
            const auto value = ipc::extractString(details, field);
            if (value.empty()) continue;
            text += std::wstring(catalog.text(std::string("details.") + field, to_hstring(field).c_str())) + L": "
                + std::wstring(catalog.text(std::string("details.value.") + value, to_hstring(value).c_str())) + L"\n";
        }
        const auto error = ipc::extractString(details, "error");
        if (!error.empty()) text += label("details.loadError", L"Load error") + L": "
            + std::wstring(catalog.text("plugins.error." + error, to_hstring(error).c_str())) + L"\n";
        text += L"\n" + label("details.buses", L"Buses") + L"\n";
        const auto buses = ipc::extractArray(details, "buses");
        if (buses.Size() == 0) text += label("common.unavailable", L"Unavailable");
        for (const auto& value : buses)
        {
            const auto bus = to_string(value.Stringify());
            const auto direction = ipc::extractString(bus, "direction");
            text += std::wstring(catalog.text("details.value." + direction, to_hstring(direction).c_str())) + L" · "
                + std::wstring(to_hstring(ipc::extractString(bus, "name"))) + L" · "
                + std::to_wstring(static_cast<int>(ipc::extractNumber(bus, "channels"))) + L" " + label("details.channels", L"channels") + L" · "
                + label(ipc::extractBool(bus, "main") ? "details.main" : "details.auxiliary", ipc::extractBool(bus, "main") ? L"Main" : L"Auxiliary") + L" · "
                + label(ipc::extractBool(bus, "enabled") ? "details.enabled" : "details.disabled", ipc::extractBool(bus, "enabled") ? L"Enabled" : L"Disabled") + L"\n"
                + label("details.defaultLayout", L"Default layout") + L": " + std::wstring(to_hstring(ipc::extractString(bus, "layout")))
                + L" (" + std::to_wstring(static_cast<int>(ipc::extractNumber(bus, "defaultChannels"))) + L")\n";
        }
        TextBlock content; content.Text(text); content.TextWrapping(TextWrapping::Wrap); content.IsTextSelectionEnabled(true);
        Automation::AutomationProperties::SetAutomationId(content, L"PluginDetailsText");
        ScrollViewer scroll; scroll.MaxHeight((std::max)(140.0, (std::min)(460.0, owner.XamlRoot().Size().Height - 260.0))); scroll.Content(content);
        body.Children().Append(scroll);
    }
    dialog.Content(body);
    const auto result = co_await lightHostModern::ui::showAppDialog(dialog);
    if (action == "rename" && result == ContentDialogResult::Secondary)
        co_return to_hstring(std::string(running ? "rename-plugin:" : "rename-known-plugin:") + id + ":");
    if (result != ContentDialogResult::Primary) co_return L"";
    if (action == "rename") co_return to_hstring(std::string(running ? "rename-plugin:" : "rename-known-plugin:") + id + ":" + to_string(name.Text()));
    if (action == "swap" && destinations.SelectedIndex() >= 0)
        co_return to_hstring("swap-plugin-with:" + id + ":" + ids.at(static_cast<size_t>(destinations.SelectedIndex())));
    co_return L"";
}
}
