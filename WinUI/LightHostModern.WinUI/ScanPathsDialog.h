#pragma once
#include "HoverHelp.h"
#include "Localization.h"
#include "VisualPreferences.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace lightHostModern::ui
{
class ScanPathsDialog : public std::enable_shared_from_this<ScanPathsDialog>
{
public:
    using Save = std::function<bool(const std::vector<std::string>&)>;
    static std::shared_ptr<ScanPathsDialog> createPane(winrt::Microsoft::UI::Xaml::FrameworkElement owner,
        ::LightHostModernWinUI::LocalizationCatalog& catalog, std::vector<std::string> paths,
        std::function<std::wstring()> browse, Save save)
    {
        auto state = std::make_shared<ScanPathsDialog>();
        state->paths = std::move(paths); state->persist = std::move(save); state->catalog = &catalog;
        state->create(owner, std::move(browse));
        return state;
    }
    winrt::Microsoft::UI::Xaml::FrameworkElement content() const { return scroller; }
    void localize()
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        hint.Text(catalog->text("dialogs.scanPaths.description", L"Choose folders to search for new or updated plugins. Folder changes are saved automatically."));
        addTitle.Text(catalog->text("dialogs.scanPaths.add", L"Add new path"));
        savedTitle.Text(catalog->text("dialogs.scanPaths.savedPaths", L"Saved paths"));
        editor.PlaceholderText(catalog->text("dialogs.scanPaths.placeholder", L"Enter the path here"));
        Automation::AutomationProperties::SetName(editor, catalog->text("dialogs.scanPaths.add", L"Add new path"));
        const auto nameButton = [](Button const& button, hstring const& name) {
            Automation::AutomationProperties::SetName(button, name);
            lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(name));
        };
        nameButton(picker, catalog->text("dialogs.scanPaths.browse", L"Browse folder"));
        nameButton(saveButton, catalog->text("dialogs.scanPaths.savePath", L"Save path"));
        for (const auto& child : entries.Children())
            if (auto card = child.try_as<Border>()) {
                const auto row = card.Child().as<Grid>();
                nameButton(row.Children().GetAt(1).as<Button>(), catalog->text("plugins.openFolder", L"Open folder"));
                nameButton(row.Children().GetAt(2).as<Button>(), catalog->text("dialogs.scanPaths.remove", L"Remove path"));
            } else if (auto empty = child.try_as<TextBlock>())
                empty.Text(catalog->text("dialogs.scanPaths.empty", L"No folders added yet."));
    }
private:
    using Button = winrt::Microsoft::UI::Xaml::Controls::Button;
    Button iconButton(const wchar_t* glyph, const winrt::hstring& id, const winrt::hstring& name)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        Button button; button.Width(36); button.MinWidth(36); button.Height(36); button.Padding({0,0,0,0});
        button.VerticalAlignment(VerticalAlignment::Center);
        FontIcon icon; icon.Glyph(glyph); icon.FontSize(18); button.Content(icon);
        Automation::AutomationProperties::SetAutomationId(button, id);
        Automation::AutomationProperties::SetName(button, name);
        lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(name));
        return button;
    }
    static std::wstring trim(std::wstring value)
    {
        const auto first = value.find_first_not_of(L" \t\r\n\"");
        if (first == std::wstring::npos) return {};
        return value.substr(first, value.find_last_not_of(L" \t\r\n\"") - first + 1);
    }
    static std::wstring comparisonPath(const std::wstring& value)
    {
        auto normalized = std::filesystem::path(value).lexically_normal().make_preferred().wstring();
        while (normalized.size() > 3 && (normalized.back() == L'\\' || normalized.back() == L'/')) normalized.pop_back();
        return normalized;
    }
    void error(const char* key, const wchar_t* fallback)
    {
        message.Text(catalog->text(key, fallback));
        message.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }
    void create(winrt::Microsoft::UI::Xaml::FrameworkElement owner, std::function<std::wstring()> browse)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        const auto weak = weak_from_this();
        body.Spacing(12);
        body.HorizontalAlignment(HorizontalAlignment::Stretch);
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Text(catalog->text("dialogs.scanPaths.description", L"Choose folders to search for new or updated plugins. Folder changes are saved automatically."));
        Automation::AutomationProperties::SetAutomationId(hint, L"ScanPathsHint");
        body.Children().Append(hint);
        addTitle.Text(catalog->text("dialogs.scanPaths.add", L"Add new path"));
        addTitle.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        body.Children().Append(addTitle);
        Border addCard; addCard.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());
        addCard.Padding({12,8,12,8}); addCard.MinHeight(52);
        StackPanel addContent; addContent.Spacing(12);
        Automation::AutomationProperties::SetAutomationId(addCard, L"AddScanPathCard");
        Grid input; input.ColumnSpacing(8);
        for (const auto width : {GridLengthHelper::FromValueAndType(1, GridUnitType::Star), GridLengthHelper::Auto(), GridLengthHelper::Auto()})
        { ColumnDefinition column; column.Width(width); input.ColumnDefinitions().Append(column); }
        editor.PlaceholderText(catalog->text("dialogs.scanPaths.placeholder", L"Enter the path here"));
        Automation::AutomationProperties::SetAutomationId(editor, L"NewScanPath");
        Automation::AutomationProperties::SetName(editor, catalog->text("dialogs.scanPaths.add", L"Add new path"));
        input.Children().Append(editor);
        picker = iconButton(L"\xE8B7", L"BrowseNewScanPath", catalog->text("dialogs.scanPaths.browse", L"Browse folder"));
        picker.Click([weak, browse = std::move(browse)](const auto&, const auto&) {
            if (auto state = weak.lock())
            {
                const auto selected = browse();
                if (!selected.empty()) state->editor.Text(selected);
                state->editor.Focus(FocusState::Programmatic);
            }
        });
        Grid::SetColumn(picker, 1); input.Children().Append(picker);
        saveButton = iconButton(L"\xE74E", L"SaveNewScanPath", catalog->text("dialogs.scanPaths.savePath", L"Save path"));
        saveButton.IsEnabled(false);
        saveButton.Click([weak](const auto&, const auto&) { if (auto state = weak.lock()) state->add(); });
        editor.TextChanged([weak](const auto&, const auto&) {
            if (auto state = weak.lock()) {
                state->saveButton.IsEnabled(!trim(std::wstring(state->editor.Text())).empty());
                state->message.Visibility(Visibility::Collapsed);
            }
        });
        editor.KeyDown([weak](const auto&, const Input::KeyRoutedEventArgs& args) {
            if (args.Key() == Windows::System::VirtualKey::Enter) {
                args.Handled(true); if (auto state = weak.lock()) state->add();
            }
        });
        Grid::SetColumn(saveButton, 2); input.Children().Append(saveButton); addContent.Children().Append(input);
        message.TextWrapping(TextWrapping::Wrap); message.Visibility(Visibility::Collapsed);
        Automation::AutomationProperties::SetAutomationId(message, L"ScanPathMessage");
        Automation::AutomationProperties::SetLiveSetting(message, Automation::Peers::AutomationLiveSetting::Polite);
        addContent.Children().Append(message);
        addCard.Child(addContent); body.Children().Append(addCard);
        savedTitle.Text(catalog->text("dialogs.scanPaths.savedPaths", L"Saved paths"));
        savedTitle.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        body.Children().Append(savedTitle);
        entries.Spacing(8); body.Children().Append(entries);
        scroller.HorizontalScrollMode(ScrollMode::Disabled);
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroller.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroller.MaxHeight((std::max)(160.0, (std::min)(520.0, owner.XamlRoot().Size().Height - 220.0)));
        Automation::AutomationProperties::SetAutomationId(scroller, L"ScanPathsScroll");
        scroller.Content(body);
        render();
    }
    void add()
    {
        using namespace winrt;
        const auto value = trim(std::wstring(editor.Text()));
        if (value.empty()) return;
        if (value.find_first_of(L"\r\n") != std::wstring::npos || !std::filesystem::path(value).is_absolute())
        { error("dialogs.scanPaths.absolute", L"Enter a full folder path, such as C:\\Audio\\Plugins."); return; }
        const auto candidate = comparisonPath(value);
        for (const auto& path : paths) {
            const auto existing = comparisonPath(std::wstring(to_hstring(path)));
            if (CompareStringOrdinal(candidate.c_str(), -1, existing.c_str(), -1, TRUE) == CSTR_EQUAL)
            { error("dialogs.scanPaths.duplicate", L"This folder is already in the list."); return; }
        }
        auto next = paths; next.push_back(to_string(value));
        if (!persist(next)) { error("dialogs.scanPaths.saveFailed", L"Could not save the scan paths. Try again."); return; }
        paths = std::move(next); editor.Text(L""); render(); editor.Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }
    void render()
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        entries.Children().Clear();
        const auto weak = weak_from_this();
        for (size_t index = 0; index < paths.size(); ++index)
        {
            const auto path = paths[index];
            Border card; card.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());
            card.Padding({12,8,12,8}); card.MinHeight(52);
            Grid row; row.ColumnSpacing(8);
            for (const auto width : {GridLengthHelper::FromValueAndType(1, GridUnitType::Star), GridLengthHelper::Auto(), GridLengthHelper::Auto()})
            { ColumnDefinition column; column.Width(width); row.ColumnDefinitions().Append(column); }
            TextBlock label; label.Text(to_hstring(path)); label.TextWrapping(TextWrapping::Wrap);
            label.VerticalAlignment(VerticalAlignment::Center); label.IsTextSelectionEnabled(true);
            Automation::AutomationProperties::SetAutomationId(label, L"SavedScanPath" + to_hstring(index)); row.Children().Append(label);
            auto open = iconButton(L"\xE8B7", L"OpenScanPath" + to_hstring(index), catalog->text("plugins.openFolder", L"Open folder"));
            open.Click([weak, path](const auto&, const auto&) {
                if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", to_hstring(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                    if (auto state = weak.lock()) state->error("dialogs.scanPaths.openFailed", L"Could not open this folder. Check that it is available.");
            });
            Grid::SetColumn(open, 1); row.Children().Append(open);
            auto remove = iconButton(L"\xE74D", L"RemoveScanPath" + to_hstring(index), catalog->text("dialogs.scanPaths.remove", L"Remove path"));
            remove.Click([weak, path](const auto&, const auto&) {
                if (auto state = weak.lock()) {
                    auto next = state->paths;
                    const auto found = std::find(next.begin(), next.end(), path);
                    if (found == next.end()) return;
                    next.erase(found);
                    if (!state->persist(next)) { state->error("dialogs.scanPaths.saveFailed", L"Could not save the scan paths. Try again."); return; }
                    state->paths = std::move(next); state->render(); state->editor.Focus(FocusState::Programmatic);
                }
            });
            Grid::SetColumn(remove, 2); row.Children().Append(remove); card.Child(row); entries.Children().Append(card);
        }
        if (paths.empty()) {
            TextBlock empty; empty.Text(catalog->text("dialogs.scanPaths.empty", L"No folders added yet.")); entries.Children().Append(empty);
        }
    }
    ::LightHostModernWinUI::LocalizationCatalog* catalog{};
    std::vector<std::string> paths;
    Save persist;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel body;
    winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller;
    winrt::Microsoft::UI::Xaml::Controls::TextBox editor;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock message;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel entries;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock hint, addTitle, savedTitle;
    Button picker{nullptr};
    Button saveButton{nullptr};
};
}
