#pragma once
#include "HostTransport.h"
#include "DialogPresentation.h"
#include "Localization.h"
#include <set>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHostModern::ui
{
class ScanFailureDialog : public std::enable_shared_from_this<ScanFailureDialog>
{
    using Catalog = ::LightHostModernWinUI::LocalizationCatalog;
    using ListView = winrt::Microsoft::UI::Xaml::Controls::ListView;
    using TextBlock = winrt::Microsoft::UI::Xaml::Controls::TextBlock;
    using ContentDialog = winrt::Microsoft::UI::Xaml::Controls::ContentDialog;
public:
    static winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> show(
        winrt::Microsoft::UI::Xaml::FrameworkElement owner, Catalog& localization,
        std::shared_ptr<ipc::ClientState> transport, std::wstring pipe, std::string scanId, uint64_t revision)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        auto state = std::make_shared<ScanFailureDialog>();
        state->catalog = &localization; state->transport = std::move(transport); state->pipe = std::move(pipe);
        state->scanId = std::move(scanId); state->revision = revision;
        state->dialog.XamlRoot(owner.XamlRoot()); state->dialog.RequestedTheme(owner.ActualTheme());
        state->dialog.Title(box_value(localization.text("scan.failures", L"Scan failures")));
        state->dialog.PrimaryButtonText(localization.text("scan.retrySelected", L"Retry selected"));
        state->dialog.CloseButtonText(localization.text("common.close", L"Close"));
        state->dialog.IsPrimaryButtonEnabled(false);
        const auto width = (std::clamp)(owner.XamlRoot().Size().Width - 48.0, 320.0, 640.0);
        state->dialog.Resources().Insert(box_value(L"ContentDialogMinWidth"), box_value(width));
        state->dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(width));
        Automation::AutomationProperties::SetAutomationId(state->dialog, L"ScanFailuresDialog");
        StackPanel content; content.Spacing(12);
        state->label.TextWrapping(TextWrapping::Wrap);
        Automation::AutomationProperties::SetAutomationId(state->label, L"ScanFailureSummary");
        content.Children().Append(state->label);
        state->list.SelectionMode(ListViewSelectionMode::Multiple);
        state->list.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        state->list.MaxHeight((std::clamp)(owner.XamlRoot().Size().Height - 240.0, 120.0, 360.0));
        state->list.ItemContainerStyle(Application::Current().Resources().Lookup(box_value(L"ScanFailureItemStyle")).as<Style>());
        ScrollViewer::SetHorizontalScrollMode(state->list, ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(state->list, ScrollBarVisibility::Disabled);
        Automation::AutomationProperties::SetAutomationId(state->list, L"ScanFailureList");
        Automation::AutomationProperties::SetName(state->list, localization.text("scan.selectFailures", L"Select failures to retry"));
        content.Children().Append(state->list); state->dialog.Content(content);
        const std::weak_ptr<ScanFailureDialog> weak = state;
        state->list.ContainerContentChanging([weak](const auto&, ContainerContentChangingEventArgs const& args) {
            if (args.InRecycleQueue()) return;
            if (auto current = weak.lock(); current && !current->loading && !current->closed && !current->stale
                && current->rows.size() < current->total && args.ItemIndex() + 10 >= current->rows.size())
                current->loadMore();
        });
        state->list.SelectionChanged([weak](const auto&, const auto&) {
            if (auto current = weak.lock(); current && !current->closed)
                current->dialog.IsPrimaryButtonEnabled(!current->stale && current->list.SelectedItems().Size() > 0);
        });
        co_await state->load();
        const auto result = co_await lightHostModern::ui::showAppDialog(state->dialog); state->closed = true;
        if (result != ContentDialogResult::Primary || state->stale) co_return L"";
        ipc::JsonArray ids;
        for (const auto& item : state->list.SelectedItems()) {
            uint32_t index = 0;
            if (state->list.Items().IndexOf(item, index) && index < state->rows.size())
                ids.Append(ipc::JsonValue::CreateStringValue(to_hstring(state->rows[index])));
        }
        if (!ids.Size()) co_return L"";
        ipc::JsonObject options;
        options.SetNamedValue(L"scanId", ipc::JsonValue::CreateStringValue(to_hstring(state->scanId)));
        options.SetNamedValue(L"revision", ipc::JsonValue::CreateNumberValue(static_cast<double>(state->revision)));
        options.SetNamedValue(L"ids", ids);
        co_return options.Stringify();
    }
private:
    winrt::fire_and_forget loadMore() { auto lifetime = shared_from_this(); co_await load(); }
    winrt::Windows::Foundation::IAsyncAction load()
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        if (loading || closed || stale) co_return;
        auto lifetime = shared_from_this(); loading = true;
        try {
            ipc::JsonObject options;
            options.SetNamedValue(L"scanId", ipc::JsonValue::CreateStringValue(to_hstring(scanId)));
            options.SetNamedValue(L"revision", ipc::JsonValue::CreateNumberValue(static_cast<double>(revision)));
            options.SetNamedValue(L"offset", ipc::JsonValue::CreateNumberValue(static_cast<double>(rows.size())));
            options.SetNamedValue(L"limit", ipc::JsonValue::CreateNumberValue(100));
            const auto response = to_string(co_await ipc::requestAsync(transport, pipe, "plugin-scan-failures:" + to_string(options.Stringify())));
            if (closed) co_return;
            const auto items = ipc::extractArray(response, "failures");
            const auto nextTotal = ipc::extractNumber(response, "total", -1);
            if (ipc::extractString(response, "status") != "ok" || ipc::extractString(response, "scanId") != scanId
                || ipc::extractNumber(response, "revision", -1) != static_cast<double>(revision)
                || nextTotal < 0 || nextTotal > 1000000 || std::floor(nextTotal) != nextTotal
                || (!rows.empty() && nextTotal != static_cast<double>(total))
                || items.Size() > 100 || rows.size() + items.Size() > nextTotal
                || (rows.size() < nextTotal && items.Size() == 0))
                throw std::runtime_error("Stale failure collection");
            total = static_cast<size_t>(nextTotal);
            const auto resources = Application::Current().Resources();
            for (const auto& item : items) {
                const auto json = to_string(item.Stringify());
                const auto id = ipc::extractString(json, "id"), path = ipc::extractString(json, "path");
                if (id.empty() || !identities.insert(id).second) throw std::runtime_error("Duplicate failure ID");
                const auto reason = ipc::extractString(json, "reason");
                const auto translated = catalog->text("scan.error." + reason.substr(0,reason.find(':')), to_hstring(reason).c_str());
                StackPanel row; row.Spacing(4); row.HorizontalAlignment(HorizontalAlignment::Stretch);
                TextBlock pathText; pathText.Text(to_hstring(path)); pathText.TextWrapping(TextWrapping::Wrap);
                pathText.Style(resources.Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                Automation::AutomationProperties::SetAutomationId(pathText, L"ScanFailurePath" + to_hstring(rows.size()));
                row.Children().Append(pathText);
                Grid errorRow; errorRow.ColumnSpacing(8);
                ColumnDefinition iconColumn; iconColumn.Width(GridLengthHelper::Auto()); errorRow.ColumnDefinitions().Append(iconColumn);
                ColumnDefinition messageColumn; messageColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star)); errorRow.ColumnDefinitions().Append(messageColumn);
                FontIcon icon; icon.Glyph(L"\xE7BA"); icon.FontSize(14); icon.VerticalAlignment(VerticalAlignment::Top); icon.Margin({0,4,0,0});
                icon.Style(resources.Lookup(box_value(L"PluginCautionIconStyle")).as<Style>()); errorRow.Children().Append(icon);
                TextBlock errorText; errorText.Text(translated); errorText.TextWrapping(TextWrapping::Wrap);
                Automation::AutomationProperties::SetAutomationId(errorText, L"ScanFailureReason" + to_hstring(rows.size()));
                Grid::SetColumn(errorText, 1); errorRow.Children().Append(errorText); row.Children().Append(errorRow);
                TextBlock metadata; metadata.Style(resources.Lookup(box_value(L"SecondaryCaptionStyle")).as<Style>());
                metadata.IsTextSelectionEnabled(true);
                metadata.Text(to_hstring(ipc::extractString(json, "format")) + L" \u00b7 " + catalog->text("scan.attempt", L"Attempt")
                    + L" " + to_hstring(static_cast<int>(ipc::extractNumber(json, "attempt")))+L" · "+to_hstring(reason)+L" · "+to_hstring(ipc::extractString(json,"kind")));
                row.Children().Append(metadata);
                Automation::AutomationProperties::SetName(row, to_hstring(path) + L". " + translated + L". " + metadata.Text());
                rows.push_back(id); list.Items().Append(row);
            }
            label.Text(catalog->format("scan.failureSummary", L"{0} failures. Select the items you want to retry.", {std::to_wstring(total)}));
        } catch (...) {
            if (!closed) {
                stale = true; dialog.IsPrimaryButtonEnabled(false);
                label.Text(catalog->text("scan.changed", L"The scan changed or disconnected. Close this dialog and open it again."));
            }
        }
        loading = false;
    }
    Catalog* catalog = nullptr;
    std::shared_ptr<ipc::ClientState> transport;
    std::wstring pipe;
    std::string scanId;
    uint64_t revision = 0;
    size_t total = 0;
    bool loading = false, closed = false, stale = false;
    std::set<std::string> identities;
    std::vector<std::string> rows;
    ContentDialog dialog;
    ListView list;
    TextBlock label;
};
}
