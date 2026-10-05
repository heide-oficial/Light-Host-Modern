#pragma once
#include "PluginItem.h"
#include "PluginRows.h"
#include "VisualColorDialog.h"
#include "Localization.h"
#include <unordered_set>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace lightHostModern::ui
{
class PluginPageController
{
public:
    using Item = winrt::LightHostModernWinUI::PluginItem;
    winrt::Windows::Foundation::Collections::IObservableVector<Item> items = winrt::single_threaded_observable_vector<Item>();
    std::vector<PluginRowData> source;
    bool running = false, allowChanges = true;
    explicit PluginPageController(bool isRunning) : running(isRunning) {}

    void adopt(std::vector<PluginRowData> rows) { source = std::move(rows); }

    void render(const std::wstring& query, int sort, bool compact,
        ::LightHostModernWinUI::LocalizationCatalog& localization, const winrt::Microsoft::UI::Xaml::Controls::ListView& view, bool grouped = false, bool globalBypassed = false)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        std::unordered_set<std::string> alive;
        for (const auto& row : source)
        {
            const auto id = running ? row.instanceId : row.knownId;
            alive.insert(id);
            auto found = models.find(id);
            if (found == models.end()) found = models.emplace(id, winrt::make<winrt::LightHostModernWinUI::implementation::PluginItem>()).first;
            auto item = found->second;
            item.Id(to_hstring(id)); item.KnownId(to_hstring(row.knownId)); item.Name(to_hstring(row.name)); item.CardTint(cardTint(running?to_hstring(row.cardColor):hstring{}));
            item.Manufacturer(row.manufacturer.empty() ? localization.text("plugins.unknownManufacturer", L"Unknown manufacturer") : to_hstring(row.manufacturer));
            item.Format(to_hstring(row.format)); item.Status(localization.translatedSource(to_hstring(row.status)));
            const bool failed = row.status == "Error" || row.status == "Unavailable" || row.status == "In chain (unavailable)";
            const bool bypassed = (running&&globalBypassed) || row.bypassed || row.status == "Running (bypassed)";
            const auto state = failed ? L"Critical" : bypassed ? L"Caution" : row.status == "Available" ? L"Available" : L"Success";
            item.StateGlyph(failed ? L"\xEA39" : bypassed ? L"\xE769" : row.status == "Available" ? L"\xE710" : L"\xE768");
            const auto resources = Application::Current().Resources();
            item.StatusBadgeStyle(resources.Lookup(box_value(hstring(L"Plugin") + state + L"BadgeStyle")).as<Style>());
            item.StatusIconStyle(resources.Lookup(box_value(hstring(L"Plugin") + state + L"IconStyle")).as<Style>());
            item.Position(running ? to_hstring(row.originalIndex + 1) : L""); item.OriginalIndex(row.originalIndex);
            if(running&&globalBypassed&&!failed)item.Status(localization.translatedSource(L"Bypassed"));
            item.Running(running); item.Bypassed(bypassed); item.CanReorder(allowChanges && running && sort == 0 && query.empty());
            item.RowHeight(compact ? 56.0 : 64.0);
            item.Metadata(item.Manufacturer() + L" \u00b7 " + item.Format());
            item.PositionVisibility(running ? Visibility::Visible : Visibility::Collapsed);
            item.AccessibleName(item.Position() + L" " + item.Name() + L", " + item.Status());
            item.ActionName(localization.format("plugins.actionsFor", L"Actions for {0}", {std::wstring(item.Position() + L" " + item.Name())}));
            if (auto container = view.ContainerFromItem(item).try_as<FrameworkElement>()){
                container.Opacity(running&&bypassed?.65:1);
                Automation::AutomationProperties::SetName(container, item.AccessibleName());}
        }
        for (auto it = models.begin(); it != models.end();)
            if (!alive.contains(it->first)) it = models.erase(it); else ++it;
        auto ordered = filterAndSortPluginRows(source, query, sort, running);
        if (grouped && !running) std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            return foldPluginText(a.manufacturer) < foldPluginText(b.manufacturer);
        });
        std::vector<Item> desired;
        desired.reserve(ordered.size());
        const bool showGroups = grouped && !running;
        const bool groupingChanged = showGroups != wasGrouped;
        wasGrouped = showGroups;
        std::unordered_set<std::string> aliveHeaders;
        std::string previousManufacturer;
        for (size_t index = 0; index < ordered.size(); ++index)
        {
            const auto& row = ordered[index];
            auto item = models.at(running ? row.instanceId : row.knownId);
            const auto manufacturer = foldPluginText(row.manufacturer);
            if (showGroups && (index == 0 || manufacturer != previousManufacturer))
            {
                aliveHeaders.insert(manufacturer);
                auto found = headers.find(manufacturer);
                if (found == headers.end()) found = headers.emplace(manufacturer, winrt::make<winrt::LightHostModernWinUI::implementation::PluginItem>()).first;
                auto header = found->second;
                header.Id(L"manufacturer-" + to_hstring(manufacturer)); header.IsGroupHeader(true);
                header.GroupHeading(item.Manufacturer()); header.AccessibleName(item.Manufacturer());
                header.GroupVisibility(Visibility::Visible); header.PluginVisibility(Visibility::Collapsed);
                desired.push_back(header);
            }
            item.GroupVisibility(Visibility::Collapsed);
            item.BranchWidth(GridLengthHelper::FromPixels(showGroups ? 32.0 : 0.0));
            item.BranchVisibility(showGroups ? Visibility::Visible : Visibility::Collapsed);
            const bool hasNext = index + 1 < ordered.size() && foldPluginText(ordered[index + 1].manufacturer) == manufacturer;
            item.TailVisibility(showGroups && hasNext ? Visibility::Visible : Visibility::Collapsed);
            desired.push_back(item); previousManufacturer = manufacturer;
        }
        for (auto it = headers.begin(); it != headers.end();)
            if (!aliveHeaders.contains(it->first)) it = headers.erase(it); else ++it;
        const auto selected = view.SelectedItem().try_as<Item>();
        const auto oldSelectionIndex = view.SelectedIndex();
        Item anchor{nullptr};
        float anchorY = 0;
        if (auto panel = view.ItemsPanelRoot().try_as<ItemsStackPanel>())
            if (panel.FirstVisibleIndex() >= 0 && static_cast<uint32_t>(panel.FirstVisibleIndex()) < items.Size())
            {
                anchor = items.GetAt(panel.FirstVisibleIndex());
                if (auto container = view.ContainerFromIndex(panel.FirstVisibleIndex()).try_as<FrameworkElement>())
                    anchorY = container.TransformToVisual(view).TransformPoint({0, 0}).Y;
            }
        bool changed = false;
        for (uint32_t index = 0; index < desired.size(); ++index)
        {
            if (index < items.Size() && items.GetAt(index) == desired[index]) continue;
            uint32_t existing = 0;
            if (items.IndexOf(desired[index], existing)) items.RemoveAt(existing);
            items.InsertAt(index, desired[index]); changed = true;
        }
        while (items.Size() > desired.size()) { items.RemoveAtEnd(); changed = true; }
        if (selected && view.SelectionMode() == ListViewSelectionMode::Single)
        {
            uint32_t index = 0;
            if (items.IndexOf(selected, index)) view.SelectedItem(selected);
            else if (!items.Size()) view.SelectedIndex(-1);
            else view.SelectedIndex((std::min)(oldSelectionIndex, static_cast<int32_t>(items.Size() - 1)));
        }
        view.CanDragItems(allowChanges && running && sort == 0 && query.empty());
        view.AllowDrop(view.CanDragItems());
        if (groupingChanged && items.Size()) view.ScrollIntoView(items.GetAt(0), ScrollIntoViewAlignment::Leading);
        if (changed && anchor && !groupingChanged)
        {
            uint32_t index = 0;
            if (items.IndexOf(anchor, index))
            {
                auto weakView = winrt::make_weak(view);
                view.DispatcherQueue().TryEnqueue([weakView, anchor, anchorY] {
                    if (auto list = weakView.get())
                    {
                        list.ScrollIntoView(anchor, ScrollIntoViewAlignment::Leading);
                        list.UpdateLayout();
                        if (auto container = list.ContainerFromItem(anchor).try_as<FrameworkElement>())
                        {
                            const auto newY = container.TransformToVisual(list).TransformPoint({0, 0}).Y;
                            if (auto scroll = findScroll(list)) scroll.ChangeView(nullptr, scroll.VerticalOffset() + newY - anchorY, nullptr, true);
                        }
                    }
                });
            }
        }
    }
private:
    static winrt::Microsoft::UI::Xaml::Controls::ScrollViewer findScroll(winrt::Microsoft::UI::Xaml::DependencyObject const& root)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        if (auto scroll = root.try_as<Controls::ScrollViewer>()) return scroll;
        for (int index = 0; index < Media::VisualTreeHelper::GetChildrenCount(root); ++index)
            if (auto scroll = findScroll(Media::VisualTreeHelper::GetChild(root, index))) return scroll;
        return nullptr;
    }
    std::unordered_map<std::string, Item> models, headers;
    bool wasGrouped = false;
};
}
