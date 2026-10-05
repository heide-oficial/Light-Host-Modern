#include "pch.h"
#include "DialogPresentation.h"
#include "HoverHelp.h"
#include "VisualPreferences.h"
#include "DisplayNameDialog.h"
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "WinUIDebug.h"
#include "HostTransport.h"
#include "HostJson.h"
#include "ScanFailureDialog.h"
#include "PluginDialogs.h"
#include "UiPreferences.h"
#include "SurfaceMaterials.h"
#include "ActionFeedback.h"
#include "PluginPageController.h"
#include "../../Source/RuntimeProfile.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.UI.Text.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;
using namespace Windows::ApplicationModel::DataTransfer;
using winrt::LightHostModernWinUI::implementation::ChannelRowData;
using namespace lightHostModern::ui;

namespace
{
    constexpr wchar_t GITHUB_REPOSITORY_URL[] = L"https://github.com/heide-oficial/Light-Host-Modern";
    constexpr wchar_t GITHUB_SHOWCASE_URL[] = L"https://github.com/heide-oficial/Light-Host-Modern/issues/new?title=%5BSHOWCASE%20VIDEO%5D%20Video%20title%20here&labels=showcase%20video&body=Here%27s%20my%20video%20showcasing%20or%20featuring%20the%20app%3A%20%5BINSERT%20LINK%20HERE%5D";
    constexpr wchar_t KOFI_URL[] = L"https://ko-fi.com/heide_oficial";
    constexpr wchar_t APP_VERSION[] = L"2.0.0";
    constexpr double COMPACT_CONTENT_MAX_WIDTH = 1000.0;
    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return (char) std::tolower(c);
        });
        return value;
    }

    std::string wideToUtf8(std::wstring const& value) { return winrt::to_string(value); }
    std::wstring utf8ToWide(std::string const& value) { return std::wstring(winrt::to_hstring(value)); }

    using namespace lightHostModern::ipc;

    hstring hs(std::string const& value) { return winrt::to_hstring(value); }

    hstring ipcErrorText(std::string const& json, ::LightHostModernWinUI::LocalizationCatalog& localization)
    {
        const auto code = extractString(json, "code");
        if (code == "operating_error") return localization.translatedSource(hs(extractString(json, "message", "Command failed")));
        if (code == "session_save_failed" || code == "session_read_only")
            return localization.text("ipc.error." + code, hs(extractString(json, "message", "Session operation failed")).c_str());
        if (code == "configuration_superseded" || code == "audio_configuration_failed")
            return localization.text("ipc." + code, hs(extractString(json, "message", "Audio selection failed")).c_str());
        if (code == "incompatible_version")
            return localization.text("ipc.incompatible", L"Host and UI protocol versions do not match. Restart both with the same version.");
        if (code == "invalid_instance_name")
            return localization.text("ipc.invalidInstanceName", L"Use a single-line name with at most 128 Unicode characters.");
        if (code == "invalid_arguments")
            return localization.text("ipc.invalidArguments", L"Invalid host command arguments.");
        if (code == "instance_not_found")
            return localization.text("ipc.instanceNotFound", L"This plugin instance no longer exists. Refresh the chain and try again.");
        if (code == "host_restarted")
            return localization.text("ipc.hostRestarted", L"The host restarted. The previous operation was not repeated; review the current state.");
        if (code == "operation_unknown")
            return localization.text("ipc.operationUnknown", L"The operation result is no longer available. Review the current state before trying again.");
        if (code == "request_id_conflict")
            return localization.text("ipc.requestConflict", L"This request ID already belongs to a different operation.");
        if (code == "operation_capacity")
            return localization.text("ipc.operationCapacity", L"Too many operations are pending. Wait for them to finish.");
        if (code == "shutting_down")
            return localization.text("ipc.shuttingDown", L"The host is shutting down.");
        return hs(extractString(json, "message", "Command failed"));
    }

    std::string formatNumber(double value, int precision = 0)
    {
        std::ostringstream stream;
        stream.setf(std::ios::fixed);
        stream.precision(precision);
        stream << value;
        return stream.str();
    }

    std::string comboItemText(winrt::Windows::Foundation::IInspectable const& item)
    {
        if (item == nullptr)
            return {};

        try
        {
            if (auto comboItem = item.try_as<ComboBoxItem>())
                return wideToUtf8(std::wstring(unbox_value<hstring>(comboItem.Content()).c_str()));

            return wideToUtf8(std::wstring(unbox_value<hstring>(item).c_str()));
        }
        catch (...)
        {
            return {};
        }
    }

    void setComboItems(ComboBox const& combo, std::vector<std::string> const& values, int selectedIndex)
    {
        if (!values.empty())
        {
            if (selectedIndex >= (int) values.size())
                selectedIndex = (int) values.size() - 1;
        }
        else
        {
            selectedIndex = -1;
        }

        bool itemsMatch = combo.Items().Size() == values.size();
        if (itemsMatch)
        {
            for (int i = 0; i < (int) values.size(); ++i)
            {
                if (comboItemText(combo.Items().GetAt((uint32_t) i)) != (values[(size_t) i].empty() ? "--" : values[(size_t) i]))
                {
                    itemsMatch = false;
                    break;
                }
            }
        }

        if (itemsMatch)
        {
            combo.IsEnabled(!values.empty());
            if (combo.SelectedIndex() != selectedIndex)
                combo.SelectedIndex(selectedIndex);
            return;
        }

        combo.Items().Clear();
        for (auto const& value : values)
        {
            auto item = ComboBoxItem();
            item.Content(box_value(hs(value.empty() ? "--" : value)));
            item.MinHeight(40);
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            combo.Items().Append(item);
        }

        combo.SelectedIndex(selectedIndex);

        combo.IsEnabled(!values.empty());
    }

    std::string selectedComboText(ComboBox const& combo)
    {
        if (combo.SelectedIndex() < 0 || combo.SelectedItem() == nullptr)
            return {};

        try
        {
            return comboItemText(combo.SelectedItem());
        }
        catch (...)
        {
            return {};
        }
    }

    int stringIndex(std::vector<std::string> const& values, std::string const& value, int fallback)
    {
        if (!value.empty())
        {
            for (int i = 0; i < (int) values.size(); ++i)
            {
                if (values[(size_t) i] == value)
                    return i;
            }
        }

        return fallback;
    }

    int audioPersistenceModeIndex(std::string mode)
    {
        std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        if (mode == "lastselected" || mode == "last-selected" || mode == "last_selected")
            return 1;
        if (mode == "custom")
            return 2;

        return 0;
    }

    std::string audioPersistenceModeValue(int index)
    {
        if (index == 1)
            return "lastSelected";
        if (index == 2)
            return "custom";

        return "disabled";
    }

    void styleNumberBox(NumberBox const& box)
    {
        box.HorizontalAlignment(HorizontalAlignment::Stretch);
        box.MinHeight(32);
        box.MinWidth(96);
        box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Hidden);
        box.SmallChange(1);
        box.LargeChange(5);
    }

    std::wstring environmentPath(wchar_t const* name)
    {
        wchar_t buffer[MAX_PATH] {};
        const auto length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        return buffer;
    }

    void appendPathIfAvailable(std::vector<std::string>& paths, std::wstring const& base, wchar_t const* suffix)
    {
        if (base.empty())
            return;

        auto path = base + suffix;
        const auto utf8Path = wideToUtf8(path);
        if (std::find(paths.begin(), paths.end(), utf8Path) == paths.end())
            paths.push_back(utf8Path);
    }

    std::vector<std::string> defaultPluginScanPaths()
    {
        std::vector<std::string> paths;
        const auto programFiles = environmentPath(L"ProgramFiles");
        const auto programFilesX86 = environmentPath(L"ProgramFiles(x86)");
        const auto commonProgramFiles = environmentPath(L"CommonProgramFiles");
        const auto commonProgramFilesX86 = environmentPath(L"CommonProgramFiles(x86)");
        const auto localAppData = environmentPath(L"LOCALAPPDATA");

        appendPathIfAvailable(paths, commonProgramFiles, L"\\VST3");
        appendPathIfAvailable(paths, commonProgramFilesX86, L"\\VST3");
        appendPathIfAvailable(paths, localAppData, L"\\Programs\\Common\\VST3");
        appendPathIfAvailable(paths, programFiles, L"\\VSTPlugins");
        appendPathIfAvailable(paths, programFiles, L"\\Steinberg\\VSTPlugins");
        appendPathIfAvailable(paths, programFiles, L"\\Common Files\\VSTPlugins");
        appendPathIfAvailable(paths, programFilesX86, L"\\VSTPlugins");
        appendPathIfAvailable(paths, programFilesX86, L"\\Steinberg\\VSTPlugins");
        appendPathIfAvailable(paths, programFilesX86, L"\\Common Files\\VSTPlugins");
        return paths;
    }

    std::wstring joinPaths(std::vector<std::string> const& paths)
    {
        std::wstring text;
        for (auto const& path : paths)
        {
            if (!text.empty())
                text += L"\r\n";
            text += utf8ToWide(path);
        }
        return text;
    }

    std::string trimPath(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::vector<std::string> parsePaths(std::wstring const& text)
    {
        std::vector<std::string> paths;
        std::wstringstream stream(text);
        std::wstring line;
        while (std::getline(stream, line))
        {
            auto path = trimPath(wideToUtf8(line));
            if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
                paths.push_back(path);
        }
        return paths;
    }

    std::wstring pickFolderPath()
    {
        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (FAILED(hr) || dialog == nullptr)
            return {};

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        hr = dialog->Show(GetActiveWindow());
        if (FAILED(hr))
        {
            dialog->Release();
            return {};
        }

        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        dialog->Release();
        if (FAILED(hr) || item == nullptr)
            return {};

        PWSTR path = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        item->Release();
        if (FAILED(hr) || path == nullptr)
            return {};

        std::wstring result(path);
        CoTaskMemFree(path);
        return result;
    }

    bool isChecked(CheckBox const& box)
    {
        const auto value = box.IsChecked();
        return value && value.Value();
    }

    void setAudioCheckBoxLabel(CheckBox const& box, hstring const& text)
    {
        auto label = box.Content().try_as<TextBlock>();
        if (!label)
        {
            label = TextBlock();
            label.VerticalAlignment(VerticalAlignment::Center);
            label.TextLineBounds(TextLineBounds::Tight);
            label.TextWrapping(TextWrapping::Wrap);
            box.Content(label);
        }
        label.Text(text);
        box.VerticalContentAlignment(VerticalAlignment::Center);
        box.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        box.Padding({8,0,0,0});
        box.MinHeight(40);
        Automation::AutomationProperties::SetName(box, text);
    }

    bool isChecked(ToggleSwitch const& toggle)
    {
        return toggle.IsOn();
    }

    struct AudioChoiceEntry
    {
        std::string backend;
        std::string role;
        std::string name;
    };

    AudioChoiceEntry parseAudioChoiceEntry(std::string const& value)
    {
        AudioChoiceEntry entry;

        const auto first = value.find('|');
        if (first == std::string::npos)
        {
            entry.name = value;
            return entry;
        }

        const auto second = value.find('|', first + 1);
        if (second == std::string::npos)
        {
            entry.backend = value.substr(0, first);
            entry.name = value.substr(first + 1);
            return entry;
        }

        entry.backend = value.substr(0, first);
        entry.role = value.substr(first + 1, second - first - 1);
        entry.name = value.substr(second + 1);
        return entry;
    }

    using namespace lightHostModern::ui;

    bool isGenericChannelName(std::string const& name)
    {
        if (name.empty())
            return true;

        const auto lower = toLower(name);
        return lower.find("channel") != std::string::npos
            || lower.find("input") == 0
            || lower.find("output") == 0
            || lower.find("in ") == 0
            || lower.find("out ") == 0;
    }

    std::string trailingSurroundToken(std::string const& name)
    {
        const auto lower = toLower(name);
        const std::pair<char const*, char const*> tokens[] = {
            { "left", "Left" },
            { "right", "Right" },
            { "center", "Center" },
            { "centre", "Centre" },
            { "lfe", "LFE" },
            { "sl", "SL" },
            { "sr", "SR" },
            { "rl", "RL" },
            { "rr", "RR" },
            { "rear left", "Rear Left" },
            { "rear right", "Rear Right" },
            { "side left", "Side Left" },
            { "side right", "Side Right" }
        };

        for (auto const& token : tokens)
        {
            if (lower.size() >= std::strlen(token.first)
                && lower.rfind(token.first) == lower.size() - std::strlen(token.first))
                return token.second;
        }

        return {};
    }

    std::string pairChannelLabel(std::string const& backend,
        std::string const& first,
        std::string const& second,
        int firstIndex,
        bool input)
    {
        const auto lowerBackend = toLower(backend);
        const auto direction = input ? "Input" : "Output";
        if (lowerBackend.find("directsound") != std::string::npos && firstIndex == 0)
            return "Left + Right";

        if (lowerBackend.find("windows audio") != std::string::npos
            && (isGenericChannelName(first) || isGenericChannelName(second)))
            return std::string(direction) + " channel " + std::to_string(firstIndex + 1) + " + " + std::to_string(firstIndex + 2);

        if (lowerBackend.find("asio") != std::string::npos)
        {
            const auto secondToken = trailingSurroundToken(second);
            if (!first.empty() && !secondToken.empty())
                return first + " + " + secondToken;
        }

        if (!first.empty() && !second.empty() && first != second)
            return first + " + " + second;

        return std::string(direction) + " channel " + std::to_string(firstIndex + 1) + " + " + std::to_string(firstIndex + 2);
    }

    std::vector<ChannelRowData> groupedChannelRows(std::string const& backend,
        std::vector<std::string> const& labels,
        std::vector<bool> const& activeStates,
        bool input, bool pairs, ::LightHostModernWinUI::LocalizationCatalog& catalog)
    {
        std::vector<ChannelRowData> rows;
        for (int i = 0; i < (int) labels.size(); i += pairs ? 2 : 1)
        {
            ChannelRowData row;
            row.startIndex = i;
            row.endIndex = pairs ? (std::min)(i + 1, (int) labels.size() - 1) : i;

            const auto first = labels[(size_t) i];
            const auto second = row.endIndex > i ? labels[(size_t) row.endIndex] : std::string();
            if (row.endIndex > i)
            {
                row.label = pairChannelLabel(backend, first, second, i, input);
                if (isGenericChannelName(first) || isGenericChannelName(second) || first.empty() || second.empty() || first == second)
                    row.label = to_string(catalog.format(input ? "audio.inputPair" : "audio.outputPair", L"Channels {0} + {1}", {std::to_wstring(i + 1), std::to_wstring(i + 2)}));
                const bool firstActive = i < (int) activeStates.size() && activeStates[(size_t) i];
                const bool secondActive = row.endIndex < (int) activeStates.size() && activeStates[(size_t) row.endIndex];
                row.active = firstActive && secondActive;
                row.partial = firstActive != secondActive;
            }
            else
            {
                row.label = first.empty()
                    ? to_string(catalog.format(input ? "audio.inputChannel" : "audio.outputChannel", L"Channel {0}", {std::to_wstring(i + 1)}))
                    : first;
                row.active = i < (int) activeStates.size() && activeStates[(size_t) i];
            }

            rows.push_back(row);
        }

        return rows;
    }

    std::vector<std::string> channelKeys(std::vector<ChannelRowData> const& rows)
    {
        std::vector<std::string> keys;
        keys.reserve(rows.size());
        for (auto const& row : rows)
            keys.push_back(row.label + "|" + std::to_string(row.startIndex) + "|" + std::to_string(row.endIndex));
        return keys;
    }

    Windows::UI::Color makeColor(uint8_t r, uint8_t g, uint8_t b);
    Windows::UI::Color makeColorA(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
    SolidColorBrush brush(Windows::UI::Color color);
    Brush resourceBrush(wchar_t const* key, Windows::UI::Color fallback);
    bool preferDarkFallback = true;
    bool highContrastActive = false;
    Windows::UI::Color themedFallback(Windows::UI::Color light, Windows::UI::Color dark);

    Button iconButton(std::wstring const& glyph, int index, std::wstring const& tooltip)
    {
        auto button = Button();
        auto icon = FontIcon();
        icon.Glyph(hstring(glyph.c_str()));
        icon.FontSize(18);
        button.Content(icon);
        button.Tag(box_value(index));
        button.Width(38);
        button.Height(38);
        button.Padding(ThicknessHelper::FromUniformLength(0));
        button.BorderThickness(ThicknessHelper::FromUniformLength(0));
        button.Background(resourceBrush(L"SubtleFillColorTransparentBrush", makeColorA(0, 0, 0, 0)));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, hstring(tooltip.c_str()));
        lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(hstring(tooltip.c_str())));
        return button;
    }

    template <typename Tag>
    MenuFlyoutItem actionMenuItem(std::wstring const& text,
        std::wstring const& glyph,
        Tag index,
        RoutedEventHandler const& handler)
    {
        auto item = MenuFlyoutItem();
        item.Text(hstring(text.c_str()));
        item.Tag(box_value(index));
        auto icon = FontIcon();
        icon.Glyph(hstring(glyph.c_str()));
        item.Icon(icon);
        item.Click(handler);
        return item;
    }


    TextBlock rowText(std::string const& text, double fontSize = 16.0)
    {
        auto label = TextBlock();
        label.Text(hs(text));
        label.FontSize(fontSize);
        label.TextWrapping(TextWrapping::Wrap);
        label.VerticalAlignment(VerticalAlignment::Center);
        return label;
    }





    Border pluginListItem(Grid const& row, int index)
    {
        auto item = Border();
        item.Child(row);
        item.Tag(box_value(index));
        item.MinHeight(72);
        item.Padding(ThicknessHelper::FromUniformLength(0));
        item.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        item.Background(resourceBrush(L"AppCardBrush", themedFallback(makeColor(255, 255, 255), makeColor(44, 44, 44))));
        item.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(226, 226, 226), makeColor(62, 62, 62))));
        item.BorderThickness(ThicknessHelper::FromUniformLength(1));
        return item;
    }

    void setPluginDropTargetVisual(Border const& item)
    {
        item.BorderBrush(brush(makeColor(96, 205, 255)));
        item.BorderThickness(ThicknessHelper::FromUniformLength(2));
    }

    Border pluginEmptyState(std::wstring const& text)
    {
        auto item = Border();
        item.MinHeight(160);
        item.Padding(ThicknessHelper::FromUniformLength(24));
        item.Background(brush(makeColorA(0, 0, 0, 0)));

        auto label = TextBlock();
        label.Text(hstring(text.c_str()));
        label.FontSize(16);
        label.Foreground(brush(themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
        label.HorizontalAlignment(HorizontalAlignment::Center);
        label.VerticalAlignment(VerticalAlignment::Center);
        label.TextAlignment(TextAlignment::Center);

        item.Child(label);
        return item;
    }

    void setChannelButtonVisual(Button const& button, std::string const& labelText, bool active, std::string const& tag)
    {
        button.Tag(box_value(hs(tag)));
        button.MinHeight(38);
        button.Padding(ThicknessHelper::FromLengths(0, 2, 0, 2));
        button.HorizontalAlignment(HorizontalAlignment::Left);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        button.BorderThickness(ThicknessHelper::FromUniformLength(0));
        button.Background(brush(makeColorA(0, 0, 0, 0)));

        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(12);
        row.VerticalAlignment(VerticalAlignment::Center);

        auto box = Border();
        box.Width(22);
        box.Height(22);
        box.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        box.BorderThickness(ThicknessHelper::FromUniformLength(active ? 0 : 1.5));
        box.BorderBrush(brush(themedFallback(makeColor(96, 96, 96), makeColorA(210, 154, 164, 176))));
        box.Background(active ? brush(themedFallback(makeColor(0, 120, 212), makeColor(96, 205, 255))) : brush(makeColorA(0, 0, 0, 0)));

        auto glyph = FontIcon();
        glyph.Glyph(active ? L"\xE73E" : L"");
        glyph.FontSize(13);
        glyph.Foreground(brush(themedFallback(makeColor(255, 255, 255), makeColor(5, 18, 31))));
        glyph.HorizontalAlignment(HorizontalAlignment::Center);
        glyph.VerticalAlignment(VerticalAlignment::Center);
        box.Child(glyph);

        auto label = TextBlock();
        label.Text(hs(labelText));
        label.FontSize(15);
        label.VerticalAlignment(VerticalAlignment::Center);
        label.TextWrapping(TextWrapping::NoWrap);

        row.Children().Append(box);
        row.Children().Append(label);
        button.Content(row);
    }

    void styleTitleBar(Microsoft::UI::Windowing::AppWindow const& appWindow, bool dark)
    {
        try
        {
            auto titleBar = appWindow.TitleBar();
            const auto systemColor = [](int index) { const auto color = GetSysColor(index); return makeColor(GetRValue(color), GetGValue(color), GetBValue(color)); };
            const auto background = highContrastActive ? systemColor(COLOR_WINDOW) : dark ? makeColor(32, 32, 32) : makeColor(243, 243, 243);
            const auto foreground = highContrastActive ? systemColor(COLOR_WINDOWTEXT) : dark ? makeColor(245, 247, 251) : makeColor(32, 32, 32);
            const auto inactiveForeground = highContrastActive ? systemColor(COLOR_GRAYTEXT) : dark ? makeColor(150, 160, 174) : makeColor(110, 110, 110);
            const auto hoverBackground = highContrastActive ? systemColor(COLOR_HIGHLIGHT) : dark ? makeColorA(24,255,255,255) : makeColorA(20,0,0,0);
            const auto pressedBackground = highContrastActive ? systemColor(COLOR_HIGHLIGHT) : dark ? makeColorA(38,255,255,255) : makeColorA(32,0,0,0);
            const auto hoverForeground = highContrastActive ? systemColor(COLOR_HIGHLIGHTTEXT) : foreground;
            titleBar.BackgroundColor(background);
            titleBar.ForegroundColor(foreground);
            titleBar.InactiveBackgroundColor(background);
            titleBar.InactiveForegroundColor(inactiveForeground);
            titleBar.ButtonBackgroundColor(highContrastActive ? background : makeColorA(0,0,0,0));
            titleBar.ButtonForegroundColor(foreground);
            titleBar.ButtonHoverBackgroundColor(hoverBackground);
            titleBar.ButtonHoverForegroundColor(hoverForeground);
            titleBar.ButtonPressedBackgroundColor(pressedBackground);
            titleBar.ButtonPressedForegroundColor(hoverForeground);
            titleBar.ButtonInactiveBackgroundColor(highContrastActive ? background : makeColorA(0,0,0,0));
            titleBar.ButtonInactiveForegroundColor(inactiveForeground);
        }
        catch (...)
        {
        }
    }

    Windows::UI::Color makeColor(uint8_t r, uint8_t g, uint8_t b)
    {
        return makeColorA(255, r, g, b);
    }

    Windows::UI::Color makeColorA(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        Windows::UI::Color color{};
        color.A = a;
        color.R = r;
        color.G = g;
        color.B = b;
        return color;
    }

    SolidColorBrush brush(Windows::UI::Color color)
    {
        return SolidColorBrush(color);
    }

    Windows::UI::Color themedFallback(Windows::UI::Color light, Windows::UI::Color dark)
    {
        return preferDarkFallback ? dark : light;
    }

    Brush resourceBrush(wchar_t const* key, Windows::UI::Color fallback)
    {
        try
        {
            const auto resources = Application::Current().Resources();
            const auto theme = box_value(hstring(highContrastActive ? L"HighContrast" : preferDarkFallback ? L"Dark" : L"Light"));
            if (resources.ThemeDictionaries().HasKey(theme))
            {
                const auto dictionary = resources.ThemeDictionaries().Lookup(theme).as<ResourceDictionary>();
                if (dictionary.HasKey(box_value(hstring(key))))
                    if (auto themed = dictionary.Lookup(box_value(hstring(key))).try_as<Brush>()) return themed;
            }
            if (auto brushResource = resources.Lookup(box_value(hstring(key))).try_as<Brush>())
                return brushResource;
        }
        catch (...)
        {
        }

        return brush(fallback);
    }

    void styleCombo(ComboBox const& combo)
    {
        lightHostModern::ui::SurfaceMaterials::current().watch(combo);
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        combo.MinHeight(36);
        combo.MinWidth(200);
        combo.Padding(ThicknessHelper::FromLengths(12, 0, 12, 0));
    }

    void styleButton(Button const& button)
    {
        button.MinHeight(36);
        button.Padding(ThicknessHelper::FromLengths(12, 0, 12, 0));
    }

    void styleIconOnlyButton(Button const& button)
    {
        button.Width(36);
        button.MinWidth(36);
        button.MinHeight(36);
        button.Padding(ThicknessHelper::FromUniformLength(0));
        button.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
    }

    void styleNavButton(Button const& button)
    {
        button.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        button.BorderThickness(ThicknessHelper::FromUniformLength(0));
        button.Background(resourceBrush(L"SubtleFillColorTransparentBrush", makeColorA(0, 0, 0, 0)));
        button.Padding(ThicknessHelper::FromLengths(14, 0, 14, 0));
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
    }

    void setNavButtonContent(Button const& button, std::wstring const& glyph, std::wstring const& text, bool compact = false)
    {
        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(compact ? 0 : 12);
        row.VerticalAlignment(VerticalAlignment::Center);
        row.HorizontalAlignment(compact ? HorizontalAlignment::Center : HorizontalAlignment::Left);

        auto icon = FontIcon();
        icon.Glyph(hstring(glyph.c_str()));
        icon.FontSize(18);
        icon.Width(22);

        auto label = TextBlock();
        label.Text(hstring(text.c_str()));
        label.FontSize(15);
        label.VerticalAlignment(VerticalAlignment::Center);
        label.Visibility(compact ? Visibility::Collapsed : Visibility::Visible);

        row.Children().Append(icon);
        row.Children().Append(label);
        button.Content(row);
        button.HorizontalContentAlignment(compact ? HorizontalAlignment::Center : HorizontalAlignment::Left);
    }

    bool pointerStartedInsideInteractiveControl(
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args,
        DependencyObject const& boundary)
    {
        auto current = args.OriginalSource().try_as<DependencyObject>();
        while (current && current != boundary)
        {
            if (current.try_as<TextBox>()
                || current.try_as<RichEditBox>()
                || current.try_as<PasswordBox>()
                || current.try_as<NumberBox>()
                || current.try_as<ComboBox>()
                || current.try_as<AutoSuggestBox>()
                || current.try_as<ToggleSwitch>()
                || current.try_as<ButtonBase>())
            {
                return true;
            }

            current = VisualTreeHelper::GetParent(current);
        }

        return false;
    }

    void enableDialogBackgroundDefocus(ContentDialog const& dialog)
    {
        dialog.PointerPressed([](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            // The dialog owns this delegate. Capturing it strongly creates a
            // reference cycle that retains the entire closed visual tree.
            if (const auto owner = sender.try_as<ContentDialog>(); owner && !pointerStartedInsideInteractiveControl(args, owner))
                owner.Focus(FocusState::Programmatic);
        });
    }

    void sizeDialogToViewport(ContentDialog const& dialog, FrameworkElement const& viewport, double fraction)
    {
        const double availableWidth = (std::max)(320.0, viewport.ActualWidth() - 48.0);
        const double targetWidth = (std::min)({640.0, availableWidth, (std::max)(320.0, viewport.ActualWidth() * fraction)});
        dialog.Resources().Insert(box_value(L"ContentDialogMinWidth"), box_value(targetWidth));
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(targetWidth));
    }

}

namespace winrt::LightHostModernWinUI::implementation
{
    void MainWindow::syncFluentDropdownLabel(FluentDropdown& dropdown)
    {
        if (dropdown.label == nullptr)
            return;

        std::string value = "--";
        if (dropdown.selectedIndex >= 0 && dropdown.selectedIndex < (int) dropdown.values.size())
            value = dropdown.values[(size_t) dropdown.selectedIndex];

        dropdown.label.Text(hs(value.empty() ? "--" : value));
    }

    void MainWindow::createFluentDropdown(FluentDropdown& dropdown,
        StackPanel const& host,
        std::string const& command)
    {
        dropdown.command = command;
        dropdown.button = Button();
        dropdown.popup = Popup();
        dropdown.popupCard = Border();
        dropdown.flyoutPanel = StackPanel();
        dropdown.label = TextBlock();

        dropdown.button.HorizontalAlignment(HorizontalAlignment::Stretch);
        dropdown.button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        dropdown.button.MinHeight(46);
        dropdown.button.Padding(ThicknessHelper::FromLengths(16, 0, 14, 0));
        dropdown.button.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        dropdown.button.BorderThickness(ThicknessHelper::FromUniformLength(1));
        dropdown.button.BorderBrush(resourceBrush(L"AppControlStrokeBrush", themedFallback(makeColor(211, 211, 211), makeColorA(125, 68, 84, 105))));
        dropdown.button.Background(resourceBrush(L"AppControlBrush", themedFallback(makeColor(249, 249, 249), makeColorA(150, 18, 29, 44))));
        dropdown.button.Tag(box_value(hs(command)));
        dropdown.button.Click({ this, &MainWindow::FluentDropdownButton_Click });

        auto content = Grid();
        content.ColumnDefinitions().Append(ColumnDefinition());
        content.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        content.ColumnDefinitions().Append(ColumnDefinition());
        content.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromPixels(24));

        dropdown.label.Text(L"--");
        dropdown.label.FontSize(15);
        dropdown.label.VerticalAlignment(VerticalAlignment::Center);
        dropdown.label.TextTrimming(TextTrimming::CharacterEllipsis);
        dropdown.label.IsHitTestVisible(false);
        content.Children().Append(dropdown.label);

        auto chevron = FontIcon();
        chevron.Glyph(L"\xE70D");
        chevron.FontSize(12);
        chevron.HorizontalAlignment(HorizontalAlignment::Center);
        chevron.VerticalAlignment(VerticalAlignment::Center);
        chevron.IsHitTestVisible(false);
        chevron.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColorA(210, 210, 218, 230))));
        Grid::SetColumn(chevron, 1);
        content.Children().Append(chevron);

        dropdown.popupCard.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        dropdown.popupCard.BorderThickness(ThicknessHelper::FromUniformLength(1));
        dropdown.popupCard.BorderBrush(resourceBrush(L"AppFlyoutStrokeBrush", themedFallback(makeColor(211, 211, 211), makeColorA(170, 70, 84, 102))));
        try
        {
            auto acrylic = AcrylicBrush();
            acrylic.TintColor(themedFallback(makeColor(252, 252, 252), makeColor(30, 35, 42)));
            acrylic.TintOpacity(0.76);
            acrylic.TintLuminosityOpacity(0.88);
            acrylic.FallbackColor(themedFallback(makeColor(252, 252, 252), makeColor(30, 35, 42)));
            dropdown.popupCard.Background(acrylic);
        }
        catch (...)
        {
            dropdown.popupCard.Background(resourceBrush(L"AppFlyoutBrush", themedFallback(makeColor(252, 252, 252), makeColorA(246, 30, 35, 42))));
        }
        dropdown.popupCard.Padding(ThicknessHelper::FromUniformLength(8));
        dropdown.popupCard.Margin(ThicknessHelper::FromUniformLength(0));
        dropdown.popupCard.Child(dropdown.flyoutPanel);
        SurfaceMaterials::current().attach(dropdown.popupCard);

        dropdown.button.Content(content);
        dropdown.popup.Child(dropdown.popupCard);
        dropdown.popup.IsLightDismissEnabled(true);
        dropdown.popup.Opened([this](IInspectable const&, IInspectable const&)
        {
            comboDropDownOpen = true;
        });
        dropdown.popup.Closed([this](IInspectable const&, IInspectable const&)
        {
            comboDropDownOpen = false;
        });

        host.Children().Append(dropdown.button);
    }

    void MainWindow::setFluentDropdownItems(FluentDropdown& dropdown,
        std::vector<std::string> const& values,
        int selectedIndex)
    {
        if (values == dropdown.values && selectedIndex == dropdown.selectedIndex)
            return;

        dropdown.values = values;
        if (!values.empty())
        {
            if (selectedIndex < 0)
                selectedIndex = 0;
            if (selectedIndex >= (int) values.size())
                selectedIndex = (int) values.size() - 1;
            dropdown.selectedIndex = selectedIndex;
        }
        else
        {
            dropdown.selectedIndex = -1;
        }

        dropdown.button.IsEnabled(!values.empty());
        dropdown.flyoutPanel.Children().Clear();
        const double targetWidth = dropdown.button.ActualWidth() > 0 ? dropdown.button.ActualWidth() : 320;
        dropdown.popupCard.MinWidth(targetWidth);
        dropdown.popupCard.MaxWidth((std::max)(360.0, targetWidth));
        dropdown.flyoutPanel.MinWidth(targetWidth - 16.0);

        for (int i = 0; i < (int) values.size(); ++i)
        {
            auto item = Button();
            item.Tag(box_value(hs(dropdown.command + ":" + std::to_string(i))));
            item.MinHeight(44);
            item.Padding(ThicknessHelper::FromLengths(12, 0, 12, 0));
            item.HorizontalAlignment(HorizontalAlignment::Stretch);
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            item.BorderThickness(ThicknessHelper::FromUniformLength(0));
            item.Background(i == dropdown.selectedIndex
                ? resourceBrush(L"SubtleFillColorSecondaryBrush", themedFallback(makeColor(237, 237, 237), makeColorA(70, 70, 82, 98)))
                : resourceBrush(L"SubtleFillColorTransparentBrush", makeColorA(0, 0, 0, 0)));

            auto row = Grid();
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(24));
            row.ColumnDefinitions().Append(ColumnDefinition());
            row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

            auto icon = FontIcon();
            icon.Glyph(i == dropdown.selectedIndex ? L"\xE73E" : L"");
            icon.FontSize(12);
            icon.Foreground(resourceBrush(L"AccentFillColorDefaultBrush", themedFallback(makeColor(0, 120, 212), makeColor(96, 205, 255))));
            icon.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(icon);

            auto label = TextBlock();
            label.Text(hs(values[(size_t) i].empty() ? "--" : values[(size_t) i]));
            label.FontSize(15);
            label.TextWrapping(TextWrapping::NoWrap);
            label.TextTrimming(TextTrimming::CharacterEllipsis);
            label.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(label, 1);
            row.Children().Append(label);

            item.Content(row);

            item.Click({ this, &MainWindow::FluentDropdownItem_Click });
            dropdown.flyoutPanel.Children().Append(item);
        }

        syncFluentDropdownLabel(dropdown);
    }

    void MainWindow::closeFluentDropdowns()
    {
        if (audioBackendDropdown.popup) audioBackendDropdown.popup.IsOpen(false);
        if (inputDropdown.popup) inputDropdown.popup.IsOpen(false);
        if (outputDropdown.popup) outputDropdown.popup.IsOpen(false);
        if (sampleRateDropdown.popup) sampleRateDropdown.popup.IsOpen(false);
        if (bufferSizeDropdown.popup) bufferSizeDropdown.popup.IsOpen(false);
        if (themeModeDropdown.popup) themeModeDropdown.popup.IsOpen(false);
        comboDropDownOpen = false;
    }

    void MainWindow::openFluentDropdown(FluentDropdown& dropdown)
    {
        if (!dropdown.popup || !dropdown.button || dropdown.values.empty())
            return;

        const bool wasOpen = dropdown.popup.IsOpen();
        closeFluentDropdowns();
        if (wasOpen)
            return;

        const auto transform = dropdown.button.TransformToVisual(RootLayout());
        const auto point = transform.TransformPoint({ 0.0f, static_cast<float>(dropdown.button.ActualHeight() + 6.0) });
        const double targetWidth = (std::max)(320.0, dropdown.button.ActualWidth());
        dropdown.popupCard.Width(targetWidth);
        dropdown.flyoutPanel.Width(targetWidth - 16.0);
        dropdown.popup.HorizontalOffset(point.X);
        dropdown.popup.VerticalOffset(point.Y);
        dropdown.popup.IsOpen(true);
        comboDropDownOpen = true;
    }

    void MainWindow::showNotification(std::wstring const& message, bool important)
    {
        if (windowClosing || !(important ? VisualPreferences::current().warningNotifications : VisualPreferences::current().actionNotifications)) return;
        lastNotificationImportant = important;
        NotificationText().Text(localization.translatedSource(hstring(message.c_str())));
        NotificationToast().Visibility(Visibility::Visible);
        Automation::AutomationProperties::SetLiveSetting(NotificationText(), Automation::Peers::AutomationLiveSetting::Polite);
        if (auto peer = Automation::Peers::FrameworkElementAutomationPeer::FromElement(NotificationText()))
            peer.RaiseAutomationEvent(Automation::Peers::AutomationEvents::LiveRegionChanged);

        if (notificationTimer == nullptr)
        {
            notificationTimer = DispatcherTimer();
            notificationTimer.Interval(std::chrono::seconds(3));
            notificationTimer.Tick([this](IInspectable const&, IInspectable const&)
            {
                NotificationToast().Visibility(Visibility::Collapsed);
                if (notificationTimer != nullptr)
                    notificationTimer.Stop();
            });
        }

        notificationTimer.Stop();
        notificationTimer.Start();
    }

    void MainWindow::createDynamicControls(std::wstring const& section)
    {
        if (section == L"Dashboard")
        {
            inputMeter.create(InputMeterBarHost(), L"InputMeter", localization);
            outputMeter.create(OutputMeterBarHost(), L"OutputMeter", localization);
        }
        if (section == L"Audio")
        {
            SurfaceMaterials::current().watch(InputGroupingBox());
            SurfaceMaterials::current().watch(OutputGroupingBox());
            audioBackendBox = ComboBox();
            styleCombo(audioBackendBox);
            audioBackendBox.PlaceholderText(localization.text("common.none", L"None"));
            Automation::AutomationProperties::SetAutomationId(audioBackendBox, L"AudioBackend");
            Automation::AutomationProperties::SetName(audioBackendBox, localization.text("audio.backend", L"Audio backend"));
            AudioBackendBoxHost().Children().Append(audioBackendBox);

            inputBox = ComboBox();
            styleCombo(inputBox);
            inputBox.PlaceholderText(localization.text("common.none", L"None"));
            Automation::AutomationProperties::SetAutomationId(inputBox, L"AudioInputDevice");
            Automation::AutomationProperties::SetName(inputBox, localization.text("audio.inputDevice", L"Input device"));
            InputBoxHost().Children().Append(inputBox);

            outputBox = ComboBox();
            styleCombo(outputBox);
            outputBox.PlaceholderText(localization.text("common.none", L"None"));
            Automation::AutomationProperties::SetAutomationId(outputBox, L"AudioOutputDevice");
            Automation::AutomationProperties::SetName(outputBox, localization.text("audio.outputDevice", L"Output device"));
            OutputBoxHost().Children().Append(outputBox);

            sampleRateBox = ComboBox();
            styleCombo(sampleRateBox);
            sampleRateBox.PlaceholderText(localization.text("common.none", L"None"));
            Automation::AutomationProperties::SetAutomationId(sampleRateBox, L"AudioSampleRate");
            Automation::AutomationProperties::SetName(sampleRateBox, localization.text("audio.sampleRate", L"Sample rate"));
            SampleRateBoxHost().Children().Append(sampleRateBox);

            bufferSizeBox = ComboBox();
            styleCombo(bufferSizeBox);
            bufferSizeBox.PlaceholderText(localization.text("common.none", L"None"));
            Automation::AutomationProperties::SetAutomationId(bufferSizeBox, L"AudioBufferSize");
            Automation::AutomationProperties::SetName(bufferSizeBox, localization.text("audio.bufferSize", L"Buffer size"));
            BufferSizeBoxHost().Children().Append(bufferSizeBox);
        }
        if (section == L"Settings")
        {
            startWithWindowsCheckBox = ToggleSwitch();
            startWithWindowsCheckBox.Width(52);
            startWithWindowsCheckBox.MinWidth(0);
            startWithWindowsCheckBox.OnContent(box_value(hstring(L"")));
            startWithWindowsCheckBox.OffContent(box_value(hstring(L"")));
            startWithWindowsCheckBox.HorizontalAlignment(HorizontalAlignment::Right);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(startWithWindowsCheckBox, L"StartWithWindows");
            StartWithWindowsCheckBoxHost().Children().Append(startWithWindowsCheckBox);

            closeToTraySwitch = ToggleSwitch();
            closeToTraySwitch.Width(52);
            closeToTraySwitch.MinWidth(0);
            closeToTraySwitch.OnContent(box_value(hstring(L"")));
            closeToTraySwitch.OffContent(box_value(hstring(L"")));
            closeToTraySwitch.HorizontalAlignment(HorizontalAlignment::Right);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(closeToTraySwitch, L"CloseToTray");
            CloseToTraySwitchHost().Children().Append(closeToTraySwitch);

            themeModeBox = ComboBox();
            styleCombo(themeModeBox);
            winrt::get_self<SettingsPageView>(settingsPageView)->ThemeSelectorHost().Children().Append(themeModeBox);
            Automation::AutomationProperties::SetAutomationId(themeModeBox, L"AppTheme");
            Automation::AutomationProperties::SetName(themeModeBox, localization.text("settings.theme", L"Theme"));
            setComboItems(themeModeBox, { "System", "Light", "Dark" }, selectedTheme == ElementTheme::Default ? 0 : selectedTheme == ElementTheme::Light ? 1 : 2);
            themeModeBox.SelectionChanged({this, &MainWindow::ThemeModeBox_SelectionChanged});

            backdropModeBox = ComboBox();
            styleCombo(backdropModeBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(backdropModeBox, L"BackdropMode");
            BackdropModeBoxHost().Children().Append(backdropModeBox);
            setComboItems(backdropModeBox, { "Mica", "Mica Alt", "Acrylic", "Solid" }, loadBackdropModeIndex());

            layoutModeBox = ComboBox();
            styleCombo(layoutModeBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(layoutModeBox, L"LayoutMode");
            LayoutModeBoxHost().Children().Append(layoutModeBox);
            compactLayout = _wcsicmp(loadUiSetting(L"Appearance", L"LayoutMode", L"Expanded").c_str(), L"Compact") == 0;
            setComboItems(layoutModeBox, { "Compact", "Expanded" }, compactLayout ? 0 : 1);

            sidebarOnOpenBox = ComboBox();
            styleCombo(sidebarOnOpenBox);
            Automation::AutomationProperties::SetAutomationId(sidebarOnOpenBox, L"SidebarOnOpen");
            winrt::get_self<SettingsPageView>(settingsPageView)->SidebarOnOpenBoxHost().Children().Append(sidebarOnOpenBox);
            setComboItems(sidebarOnOpenBox, { "Collapsed", "Expanded" }, sidebarStartsCollapsed() ? 0 : 1);

            iconModeBox = ComboBox();
            styleCombo(iconModeBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(iconModeBox, L"IconMode");
            IconModeBoxHost().Children().Append(iconModeBox);
            setComboItems(iconModeBox, { "Color", "White", "Black" }, 0);

            enableVst2CheckBox = ToggleSwitch();
            enableVst2CheckBox.Width(52);
            enableVst2CheckBox.MinWidth(0);
            enableVst2CheckBox.OnContent(box_value(hstring(L"")));
            enableVst2CheckBox.OffContent(box_value(hstring(L"")));
            enableVst2CheckBox.HorizontalAlignment(HorizontalAlignment::Right);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(enableVst2CheckBox, L"EnableVst2");
            EnableVst2CheckBoxHost().Children().Append(enableVst2CheckBox);

            audioPersistenceModeBox = ComboBox();
            styleCombo(audioPersistenceModeBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(audioPersistenceModeBox, L"AudioPersistenceMode");
            AudioPersistenceModeBoxHost().Children().Append(audioPersistenceModeBox);
            setComboItems(audioPersistenceModeBox, { "Disabled", "Last selected device", "Custom device" }, 0);

            audioRecoveryRetrySecondsBox = NumberBox();
            audioRecoveryRetrySecondsBox.Minimum(1);
            audioRecoveryRetrySecondsBox.Maximum(60);
            audioRecoveryRetrySecondsBox.Value(5);
            styleNumberBox(audioRecoveryRetrySecondsBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(audioRecoveryRetrySecondsBox, L"AudioRecoveryRetrySeconds");
            AudioRecoveryRetrySecondsBoxHost().Children().Append(audioRecoveryRetrySecondsBox);

            audioRecoveryRetryAttemptsBox = NumberBox();
            audioRecoveryRetryAttemptsBox.Minimum(1);
            audioRecoveryRetryAttemptsBox.Maximum(100);
            audioRecoveryRetryAttemptsBox.Value(10);
            styleNumberBox(audioRecoveryRetryAttemptsBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(audioRecoveryRetryAttemptsBox, L"AudioRecoveryRetryAttempts");
            AudioRecoveryRetryAttemptsBoxHost().Children().Append(audioRecoveryRetryAttemptsBox);

            customRecoveryBackendBox = ComboBox();
            styleCombo(customRecoveryBackendBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(customRecoveryBackendBox, L"RecoveryAudioBackend");
            CustomRecoveryBackendBoxHost().Children().Append(customRecoveryBackendBox);

            customRecoveryInputBox = ComboBox();
            styleCombo(customRecoveryInputBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(customRecoveryInputBox, L"RecoveryInputDevice");
            CustomRecoveryInputBoxHost().Children().Append(customRecoveryInputBox);

            customRecoveryOutputBox = ComboBox();
            styleCombo(customRecoveryOutputBox);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(customRecoveryOutputBox, L"RecoveryOutputDevice");
            CustomRecoveryOutputBoxHost().Children().Append(customRecoveryOutputBox);

            styleButton(RetryAudioDeviceButton());
            styleButton(ChooseAudioDeviceButton());
        }
        if (section == L"Plugins")
        {
            scanVstCheckBox = CheckBox();
            scanVstCheckBox.Content(box_value(hstring(L"VST")));
            scanVstCheckBox.IsChecked(true);

            scanVst3CheckBox = CheckBox();
            scanVst3CheckBox.Content(box_value(hstring(L"VST3")));
            scanVst3CheckBox.IsChecked(true);
        }
        if (section == L"Settings")
        {
            closeQuitsAppRadioButton = RadioButton();
            closeQuitsAppRadioButton.GroupName(L"CloseBehavior");

            closeToTrayRadioButton = RadioButton();
            closeToTrayRadioButton.GroupName(L"CloseBehavior");
        }
}

    MainWindow::MainWindow()
    {
        winUILog("MainWindow constructor.");

        try
        {
            winUILog("InitializeComponent starting.");
            InitializeComponent();
            winUILog("InitializeComponent completed.");
        }
        catch (winrt::hresult_error const& e)
        {
            std::stringstream stream;
            stream << "MainWindow InitializeComponent failed: " << winrt::to_string(e.message())
                << " HRESULT=0x" << std::hex << static_cast<uint32_t>(e.code());
            winUILog(stream.str());
            throw;
        }
        catch (...)
        {
            winUILog("MainWindow InitializeComponent failed with unknown exception.");
            throw;
        }

        localization.load(loadUiSetting(L"Localization", L"Language", L"en-us"));
        SurfaceMaterials::current().attach(NotificationToast());
        SurfaceMaterials::current().attach(ReleaseNotificationSurface());
        for(auto bar:{DashboardAudioNotice(),SessionStatusBanner(),ReleaseNotification()}){
            bar.Visibility(bar.IsOpen()?Visibility::Visible:Visibility::Collapsed);
            bar.RegisterPropertyChangedCallback(InfoBar::IsOpenProperty(),[weak=get_weak()](DependencyObject const& sender,DependencyProperty const&){if(auto owner=weak.get()){auto notice=sender.as<InfoBar>();const bool pageVisible=notice!=owner->DashboardAudioNotice()||owner->currentSection==L"Dashboard";notice.Visibility(notice.IsOpen()&&pageVisible?Visibility::Visible:Visibility::Collapsed);if(notice==owner->ReleaseNotification())owner->ReleaseNotificationSurface().Visibility(notice.IsOpen()?Visibility::Visible:Visibility::Collapsed);}});
        }
        ViewReleaseNotificationButton().Click([weak=get_weak()](const auto&,const auto&){if(auto owner=weak.get()){owner->ReleaseNotification().IsOpen(false);owner->showSection(L"Settings");}});
        updateCheckTimer=DispatcherTimer();updateCheckTimer.Interval(std::chrono::hours(6));updateCheckTimer.Tick([weak=get_weak()](const auto&,const auto&){if(auto owner=weak.get();owner&&!owner->windowClosing&&owner->hasFullSnapshot)owner->checkForUpdatesAsync();});updateCheckTimer.Start();
        compactLayout = _wcsicmp(loadUiSetting(L"Appearance", L"LayoutMode", L"Expanded").c_str(), L"Compact") == 0;
        sidebarCollapsed = sidebarStartsCollapsed();
        createDynamicControls(L"Dashboard");

        styleButton(RefreshButton());
        styleButton(CopyLogsButton());
        styleButton(SaveLogButton());

        winUILog("Configuring window title.");
        Title(L"LightHostModern");
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        try
        {
            wchar_t modulePath[MAX_PATH] {};
            if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
            {
                std::wstring executablePath(modulePath);
                const auto separator = executablePath.find_last_of(L"\\/");
                if (separator != std::wstring::npos)
                    AppWindow().SetIcon(executablePath.substr(0, separator) + L"\\Assets\\logo.ico");
            }
        }
        catch (...) {}
        styleTitleBar(AppWindow(), true);
        applyLayoutMode();
        resetDefaultPluginScanPaths();
        AppWindow().Closing([weak=get_weak()](const auto&,const Microsoft::UI::Windowing::AppWindowClosingEventArgs& args){
            if(auto owner=weak.get();owner&&!owner->normalCloseReady){args.Cancel(true);owner->prepareNormalClose();}
        });
        Closed({ this, &MainWindow::Window_Closed });
        Activated([weak = get_weak()](const auto&, const WindowActivatedEventArgs& args) {
            if (auto owner = weak.get()){owner->windowMaterial.activated(args.WindowActivationState() != WindowActivationState::Deactivated);if(args.WindowActivationState()!=WindowActivationState::Deactivated)owner->notifyAvailableRelease();}
        });

        winUILog("Sizing window within the monitor work area.");
        HWND nativeWindow = nullptr;
        if (const auto windowNative = this->try_as<::IWindowNative>()) windowNative->get_WindowHandle(&nativeWindow);
        const auto dpi = nativeWindow ? GetDpiForWindow(nativeWindow) : GetDpiForSystem();
        const auto windowScale = static_cast<double>(dpi ? dpi : 96) / 96.0;
        const auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(AppWindow().Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        int32_t width = static_cast<int32_t>(1180.0 * windowScale), height = static_cast<int32_t>(760.0 * windowScale);
        if (display && display.WorkArea().Width > 0 && display.WorkArea().Height > 0)
        {
            const auto area = display.WorkArea();
            width = (std::min)(width, area.Width); height = (std::min)(height, area.Height);
            const auto offset = static_cast<int32_t>(80.0 * windowScale);
            // WorkArea coordinates are relative to DisplayArea, including a
            // taskbar docked on its top or left edge.
            AppWindow().MoveAndResize({area.X + (std::min)(offset, area.Width - width),
                area.Y + (std::min)(offset, area.Height - height), width, height}, display);
        }
        else AppWindow().Resize({width, height});

        winUILog("Reading host pipe option.");
        hostPipeName = commandLineOptionValue(L"--host-pipe");
        const auto& profile = lightHostModern::RuntimeProfile::current();
        if (profile.test)
        {
            hostPipeName = profile.pipeName();
            Title(profile.windowTitle());
            AppWindow().Title(profile.windowTitle());
        }
        winUILog("Host pipe: " + wideToUtf8(hostPipeName));
        hostConnection->pipeName = hostPipeName;

        winUILog("Attaching UI events.");
        RootLayout().Loaded([this](IInspectable const&, RoutedEventArgs const&)
        {
            contentFade=ScrollEdgeFade::attach(ContentFadeHost(),ContentScrollViewer());
            applyVisualPreferences();
            updateSidebarLayout();
            applyResponsiveLayout(RootLayout().ActualWidth());
            try
            {
                contrastChanged = accessibilitySettings.HighContrastChanged(winrt::auto_revoke, [weak = get_weak()](const auto&, const auto&) {
                    if (auto owner = weak.get()) owner->DispatcherQueue().TryEnqueue([weak] { if (auto current = weak.get()) current->queueThemeRefresh(); });
                });
            }
            catch (hresult_error const& error) { winUILog("Contrast notifications unavailable; heartbeat will check: " + to_string(error.message())); }
        });
        RootLayout().SizeChanged({ this, &MainWindow::RootLayout_SizeChanged });
        MainContent().SizeChanged([weak = get_weak()](const auto&, const auto&) {
            if (auto owner = weak.get()) owner->applyResponsiveLayout(owner->RootLayout().ActualWidth());
        });
        SidebarRail().SelectionChanged({ this, &MainWindow::Navigation_SelectionChanged });
        SidebarToggleButton().Click({ this, &MainWindow::SidebarToggle_Click });
        RefreshButton().Click({ this, &MainWindow::Refresh_Click });
        RetrySessionSaveButton().Click([weak = get_weak()](const auto&, const auto&) -> winrt::fire_and_forget {
            if (auto owner = weak.get()) {
                owner->RetrySessionSaveButton().IsEnabled(false);
                co_await owner->sendCommand("flush-session");
                if (!owner->windowClosing) owner->RetrySessionSaveButton().IsEnabled(true);
            }
        });
        DeletePluginStatesButton().Click({ this, &MainWindow::DeletePluginStates_Click });

        hideSupportTab = loadUiSetting(L"General", L"HideSupportTab", L"0") == L"1";
        SupportButton().Visibility(hideSupportTab ? Visibility::Collapsed : Visibility::Visible);
        applyLocalization();
        updateSidebarLayout();

        winUILog("Creating refresh timer.");
        refreshTimer = DispatcherTimer();
        refreshTimer.Interval(std::chrono::milliseconds(VisualPreferences::current().performance?100:50));
        refreshTimer.Tick([this](IInspectable const&, IInspectable const&)
        {
            try
            {
                SurfaceMaterials::current().poll(RootLayout());
                refreshMeterLevels();
                if (!comboDropDownOpen && !commandInProgress && !pluginDragInProgress)
                    refreshTelemetry();
            }
            catch (winrt::hresult_error const& e)
            {
                std::stringstream stream;
                stream << "Refresh timer failed: " << winrt::to_string(e.message())
                    << " HRESULT=0x" << std::hex << static_cast<uint32_t>(e.code());
                winUILog(stream.str());
            }
            catch (std::exception const& e)
            {
                winUILog(std::string("Refresh timer failed: ") + e.what());
            }
            catch (...)
            {
                winUILog("Refresh timer failed with unknown exception.");
            }
        });
        refreshTimer.Start();
        winUILog("Refresh timer started.");

        winUILog("Applying initial theme.");
        const auto savedTheme = loadUiSetting(L"Appearance", L"ThemeMode", L"Dark");
        applyTheme(savedTheme == L"Light" ? ElementTheme::Light : savedTheme == L"System" ? ElementTheme::Default : ElementTheme::Dark);
        winUILog("Initial theme applied.");
        RootLayout().ActualThemeChanged([weak = get_weak()](const auto&, const auto&) { if (auto owner = weak.get()) owner->queueThemeRefresh(); });
        winUILog("Applying initial responsive layout.");
        applyResponsiveLayout(1120.0);
        winUILog("Updating debug controls.");
        winUILog("Showing initial section.");
        SidebarRail().SelectedItem(DashboardButton());
        showSection(L"Dashboard");
        updateSidebarLayout();
        winUILog("Initial snapshot deferred until first timer tick.");
        operatingPresenter = std::make_shared<lightHostModern::ui::OperatingPresenter>();
        operatingPresenter->layoutChanged=[weak=get_weak()]{if(auto owner=weak.get())owner->applyLayoutMode();};
        HWND operatingWindow = nullptr; this->try_as<::IWindowNative>()->get_WindowHandle(&operatingWindow);
        operatingPresenter->create(RootLayout(), localization, hostConnection, operatingWindow,
            [weak=get_weak()](std::string command,bool chainPrepared)->winrt::Windows::Foundation::IAsyncOperation<bool> {
                auto owner=weak.get();if(!owner||owner->windowClosing)co_return false;
                if (command == "constrain-chain-spacing") { owner->ensurePage(L"Plugins");owner->operatingPresenter->applyDistancePreference();co_return true; }
                if (command == "refresh-audio-view") { co_await owner->refreshSnapshot(true); co_return true; }
                if (command == "show-plugin-scan") { owner->ScanForPlugins_Click(nullptr, nullptr); co_return true; }
                if(command.rfind("restart-host:",0)==0||command.rfind("factory-reset:",0)==0) {
                    auto reply=winrt::to_string(co_await owner->hostConnection->requestAsync(std::move(command)));
                    if(lightHostModern::ui::extractString(reply,"status")!="ok")co_return false;
                    owner->closeQuitsHost=false;co_await owner->finishNormalCloseAsync();co_return true;
                }
                co_return co_await owner->sendCommand(std::move(command),chainPrepared);
            });
        winUILog("MainWindow ready.");
        receiveHostEvents();
    }


    bool MainWindow::ensurePage(std::wstring const& section)
    {
        if (section == L"Audio" && !Pages().AudioLoaded()) {
            audioPageView = winrt::make<AudioPageView>();
            winrt::get_self<AudioPageView>(audioPageView)->owner = winrt::make_weak(get_strong().as<winrt::Windows::Foundation::IInspectable>());
            AudioPageHost().Content(audioPageView);
            Pages().AudioLoaded(true);
        }
        else if (section == L"Plugins" && !Pages().PluginsLoaded()) {
            winUILog("PluginsPageView construct begin");
            pluginsPageView = winrt::make<PluginsPageView>();
            winUILog("PluginsPageView construct end");
            winrt::get_self<PluginsPageView>(pluginsPageView)->owner = winrt::make_weak(get_strong().as<winrt::Windows::Foundation::IInspectable>());
            PluginsPageHost().Content(pluginsPageView);
            if (operatingPresenter) operatingPresenter->attachPlugins(pluginsPageView.as<Microsoft::UI::Xaml::Controls::UserControl>());
            Pages().PluginsLoaded(true);
        }
        else if (section == L"Profiles" && !ProfilesPageHost().Content()) {
            if (operatingPresenter) operatingPresenter->attachProfiles(ProfilesPageHost());
        }
        else if (section == L"Support me" && !Pages().SupportLoaded()) {
            supportPageView = winrt::make<SupportPageView>();
            winrt::get_self<SupportPageView>(supportPageView)->owner = winrt::make_weak(get_strong().as<winrt::Windows::Foundation::IInspectable>());
            SupportPageHost().Content(supportPageView);
            Pages().SupportLoaded(true);
        }
        else if (section == L"Diagnostics" && !diagnosticsPageView) {
            diagnosticsPageView = winrt::make<DiagnosticsPageView>();
            DiagnosticsPageHost().Content(diagnosticsPageView);
            diagnosticsPresenter.create(winrt::get_self<DiagnosticsPageView>(diagnosticsPageView)->DiagnosticsPanel(), localization);
            verboseLogsPresenter=std::make_shared<lightHostModern::ui::VerboseLogsPresenter>();
            HWND logsOwner=nullptr;this->try_as<::IWindowNative>()->get_WindowHandle(&logsOwner);
            verboseLogsPresenter->create(winrt::get_self<DiagnosticsPageView>(diagnosticsPageView)->DiagnosticsPanel(),localization,hostConnection,logsOwner,
                [weak=get_weak()](std::string command)->winrt::Windows::Foundation::IAsyncOperation<bool> {
                    auto owner=weak.get();if(!owner||owner->windowClosing)co_return false;
                    if(command.rfind("restart-host:",0)==0||command.rfind("factory-reset:",0)==0) {
                        auto reply=winrt::to_string(co_await owner->hostConnection->requestAsync(std::move(command)));
                        if(lightHostModern::ui::extractString(reply,"status")!="ok")co_return false;
                        owner->closeQuitsHost=false;co_await owner->finishNormalCloseAsync();co_return true;
                    }
                    co_return co_await owner->sendCommand(std::move(command));
                });
        }
        else if (section == L"Settings" && !Pages().SettingsLoaded()) {
            settingsPageView = winrt::make<SettingsPageView>();
            winrt::get_self<SettingsPageView>(settingsPageView)->owner = winrt::make_weak(get_strong().as<winrt::Windows::Foundation::IInspectable>());
            SettingsPageHost().Content(settingsPageView);
            if (operatingPresenter) {
                const auto page = winrt::get_self<SettingsPageView>(settingsPageView);
                operatingPresenter->attachSettings(page->OperatingSettingsHost(), page->AppearanceSection());
                operatingPresenter->attachDanger(page->DangerZone());
            }
            Pages().SettingsLoaded(true);
        }
        else return false;
        initializePage(section == L"Support me" ? L"Support" : section);
        applyLocalization();
        applyResponsiveLayout(RootLayout().ActualWidth());
        return true;
    }

    void MainWindow::initializePage(std::wstring const& section)
    {
        createDynamicControls(section);
        if (section == L"Audio")
        {
            InputModeBox().SelectionChanged({ this, &MainWindow::AudioMode_Changed });
            OutputModeBox().SelectionChanged({ this, &MainWindow::AudioMode_Changed });
            lightHostModern::ui::SurfaceMaterials::current().watch(InputModeBox());
            lightHostModern::ui::SurfaceMaterials::current().watch(OutputModeBox());
            InputGroupingBox().SelectionChanged({ this, &MainWindow::ChannelGrouping_Changed });
            OutputGroupingBox().SelectionChanged({ this, &MainWindow::ChannelGrouping_Changed });
            styleButton(InputChannelsToggleAllButton());
            styleButton(OutputChannelsToggleAllButton());
            InputChannelsToggleAllButton().Click({ this, &MainWindow::InputChannelsToggleAll_Click });
            OutputChannelsToggleAllButton().Click({ this, &MainWindow::OutputChannelsToggleAll_Click });
            AudioBackendBox().SelectionChanged({ this, &MainWindow::AudioBackendBox_SelectionChanged });
            InputBox().SelectionChanged({ this, &MainWindow::InputBox_SelectionChanged });
            OutputBox().SelectionChanged({ this, &MainWindow::OutputBox_SelectionChanged });
            SampleRateBox().SelectionChanged({ this, &MainWindow::SampleRateBox_SelectionChanged });
            BufferSizeBox().SelectionChanged({ this, &MainWindow::BufferSizeBox_SelectionChanged });
            AudioBackendBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            InputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            OutputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            SampleRateBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            BufferSizeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            AudioBackendBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            InputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            OutputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            SampleRateBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            BufferSizeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        }
        if (section == L"Plugins")
        {
            winrt::get_self<PluginsPageView>(pluginsPageView)->PluginSectionSelector().SelectionChanged(
                [weak = get_weak()](SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&) {
                    if (auto owner = weak.get()) owner->showPluginSubsection(sender.SelectedItem() == owner->RunningPluginsTabButton() ? L"Running" : L"Installed");
                });
            installedGrouped = loadUiSetting(L"Plugins", L"GroupByManufacturer", L"0") == L"1";
            RunningPluginSearchBox().TextChanged({ this, &MainWindow::PluginSearchBox_TextChanged });
            InstalledPluginSearchBox().TextChanged({ this, &MainWindow::PluginSearchBox_TextChanged });
            for(auto search:{RunningPluginSearchBox(),InstalledPluginSearchBox()}){
                search.TextMemberPath(L"Text");lightHostModern::ui::dismissSuggestionsOutside(search);
                search.SuggestionChosen([weak=get_weak()](AutoSuggestBox const& box,AutoSuggestBoxSuggestionChosenEventArgs const& args){if(auto self=weak.get()){
                    const auto suggestion=args.SelectedItem().as<TextBlock>();const auto id=unbox_value<hstring>(suggestion.Tag());
                    box.Text(suggestion.Text());
                    const bool running=box==self->RunningPluginSearchBox();
                    (running?self->runningPluginSearch:self->installedPluginSearch)=std::wstring(box.Text());self->refreshPluginViews();
                    auto view=running?self->RunningPluginsListView():self->InstalledPluginsListView();
                    for(const auto& value:view.Items()){auto item=value.try_as<winrt::LightHostModernWinUI::PluginItem>();if(item&&!item.IsGroupHeader()&&item.Id()==id){view.SelectedItem(item);view.ScrollIntoView(item,ScrollIntoViewAlignment::Leading);break;}}
                    box.IsSuggestionListOpen(false);
                }});
            }
            configurePluginSortMenus();
            winrt::get_self<PluginsPageView>(pluginsPageView)->ScanForPluginsButton().Click({ this, &MainWindow::ScanForPlugins_Click });
            RunningPluginsListView().ItemsSource(runningPage.items);
            InstalledPluginsListView().ItemsSource(installedPage.items);
            RunningPluginsListView().DragOver({ this, &MainWindow::RunningPluginItem_DragOver });
            RunningPluginsListView().Drop({ this, &MainWindow::RunningPluginItem_Drop });
            showPluginSubsection(L"Running");
        }
        if (section == L"Support")
        {
            styleButton(KoFiButton());
            styleButton(SupportRepositoryButton());
            styleButton(SupportShowcaseButton());
            KoFiButton().Click({ this, &MainWindow::KoFi_Click });
            SupportRepositoryButton().Click({ this, &MainWindow::SupportRepository_Click });
            SupportShowcaseButton().Click({ this, &MainWindow::SupportShowcase_Click });
        }
        if (section == L"Settings")
        {
            auto settingsView=winrt::get_self<SettingsPageView>(settingsPageView);
            const auto preference=[this](ToggleSwitch toggle,TextBlock state,bool value,const wchar_t* section,const wchar_t* key,bool VisualPreferences::* field){
                toggle.OnContent(box_value(L""));toggle.OffContent(box_value(L""));toggle.IsOn(value);
                state.Text(localization.translatedSource(value?L"On":L"Off"));
                toggle.Toggled([weak=get_weak(),state,section,key,field](const IInspectable& sender,const auto&){if(auto owner=weak.get()){
                    const bool on=sender.as<ToggleSwitch>().IsOn();VisualPreferences::current().*field=on;saveUiSetting(section,key,on?L"1":L"0");
                    state.Text(owner->localization.translatedSource(on?L"On":L"Off"));owner->applyVisualPreferences();
                }});
            };
            preference(settingsView->ActionNotificationsSwitch(),settingsView->ActionNotificationsState(),VisualPreferences::current().actionNotifications,L"General",L"ActionNotifications",&VisualPreferences::actionNotifications);
            preference(settingsView->WarningNotificationsSwitch(),settingsView->WarningNotificationsState(),VisualPreferences::current().warningNotifications,L"General",L"WarningNotifications",&VisualPreferences::warningNotifications);
            preference(settingsView->HoverTooltipsSwitch(),settingsView->HoverTooltipsState(),VisualPreferences::current().hoverTooltips,L"General",L"HoverTooltips",&VisualPreferences::hoverTooltips);
            preference(settingsView->PerformanceModeSwitch(),settingsView->PerformanceModeState(),VisualPreferences::current().performance,L"General",L"PerformanceMode",&VisualPreferences::performance);
            preference(settingsView->EdgeFadeSwitch(),settingsView->EdgeFadeState(),VisualPreferences::current().fade,L"Appearance",L"EdgeFade",&VisualPreferences::fade);
            settingsView->EdgeFadeSwitch().IsEnabled(!VisualPreferences::current().performance);
            BackdropModeBox().IsEnabled(!VisualPreferences::current().performance);
            settingsPageView.Loaded([weak=get_weak()](const auto&,const auto&){if(auto owner=weak.get())owner->applyLocalization();});
            auto hideSidebar=settingsView->HideSidebarToggleSwitch();hideSidebar.OnContent(box_value(L""));hideSidebar.OffContent(box_value(L""));hideSidebar.IsOn(loadUiSetting(L"Appearance",L"HideSidebarToggle",L"0")==L"1");settingsView->HideSidebarToggleState().Text(localization.translatedSource(hideSidebar.IsOn()?L"On":L"Off"));
            hideSidebar.Toggled([weak=get_weak()](const IInspectable& sender,const auto&){if(auto owner=weak.get()){const bool hidden=sender.as<ToggleSwitch>().IsOn();saveUiSetting(L"Appearance",L"HideSidebarToggle",hidden?L"1":L"0");winrt::get_self<SettingsPageView>(owner->settingsPageView)->HideSidebarToggleState().Text(owner->localization.translatedSource(hidden?L"On":L"Off"));owner->updateSidebarLayout();owner->showNotification(hidden?L"Sidebar button hidden.":L"Sidebar button shown.",false);}});
            settingsView->ShowDangerOptions().OnContent(box_value(L""));settingsView->ShowDangerOptions().OffContent(box_value(L""));
            settingsView->ShowDangerOptions().Toggled([weak=get_weak()](const IInspectable& sender,const auto&){if(auto owner=weak.get()){const auto page=winrt::get_self<SettingsPageView>(owner->settingsPageView);const bool enabled=sender.as<ToggleSwitch>().IsOn();page->DangerZone().Visibility(enabled?Visibility::Visible:Visibility::Collapsed);page->DangerOptionsState().Text(owner->localization.translatedSource(enabled?L"On":L"Off"));}});
            const auto releasePreference=[this](ToggleSwitch toggle,TextBlock label,const wchar_t* key,bool inApp){
                toggle.OnContent(box_value(L""));toggle.OffContent(box_value(L""));
                toggle.IsOn(loadUiSetting(L"Updates",key,loadUiSetting(L"Updates",L"NotifyNewReleases",L"1").c_str())!=L"0");
                label.Text(localization.translatedSource(toggle.IsOn()?L"On":L"Off"));
                toggle.Toggled([weak=get_weak(),label,key,inApp](const IInspectable& sender,const auto&){if(auto owner=weak.get()){
                    const bool on=sender.as<ToggleSwitch>().IsOn();saveUiSetting(L"Updates",key,on?L"1":L"0");
                    label.Text(owner->localization.translatedSource(on?L"On":L"Off"));
                    if(inApp){owner->appNotifiedRelease.clear();if(!on)owner->ReleaseNotification().IsOpen(false);}
                    if(on){owner->notifyAvailableRelease();owner->checkForUpdatesAsync();}
                }});
            };
            releasePreference(settingsView->AppReleaseNotificationsSwitch(),settingsView->AppReleaseNotificationsState(),L"NotifyReleasesInApp",true);
            releasePreference(settingsView->WindowsReleaseNotificationsSwitch(),settingsView->WindowsReleaseNotificationsState(),L"NotifyReleasesOnWindows",false);
            styleButton(RemoveMissingPluginsButton());
            styleButton(ClearPluginDatabaseButton());
            RemoveMissingPluginsButton().Click({ this, &MainWindow::RemoveMissingPlugins_Click });
            ClearPluginDatabaseButton().Click({ this, &MainWindow::ClearPluginDatabase_Click });
            updateInstalledPluginActions();
            styleButton(OpenWindowsSoundSettingsButton());
            styleButton(RepositoryButton());
            styleButton(OriginalRepositoryButton());
            styleButton(DownloadUpdateButton());
            UpdateAvailableCard().Visibility(updateService->latest.available ? Visibility::Visible : Visibility::Collapsed);
            styleButton(ManageEnabledAudioDevicesButton());
            styleButton(PreferredDeviceButton());
            applyBackdrop(BackdropModeBox().SelectedIndex() < 0 ? loadBackdropModeIndex() : BackdropModeBox().SelectedIndex());
            if (lightHostModern::RuntimeProfile::current().test) StartWithWindowsCheckBox().IsEnabled(false);
            OpenWindowsSoundSettingsButton().Click({ this, &MainWindow::OpenWindowsSoundSettings_Click });
            DownloadUpdateButton().Click({ this, &MainWindow::DownloadUpdate_Click });
            UpdateResultLog().Click([weak = get_weak()](const auto&, const auto&) {
                if (auto owner = weak.get(); owner && !owner->updateService->applicationLog.empty()) {
                    const auto folder = owner->updateService->applicationLog.parent_path().wstring();
                    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            });
            CancelUpdateButton().Click([weak = get_weak()](const auto&, const auto&) {
                if (auto owner = weak.get()) { owner->updateService->cancel(); owner->CancelUpdateButton().IsEnabled(false); }
            });
            auto diagnosticsSwitch = winrt::get_self<SettingsPageView>(settingsPageView)->DiagnosticsEnabledSwitch();
            diagnosticsSwitch.IsOn(diagnosticsEnabled);
            diagnosticsSwitch.Toggled({this, &MainWindow::DiagnosticsEnabled_Toggled});
            HideSupportTabSwitch().Toggled({ this, &MainWindow::HideSupportTabSwitch_Toggled });
            LanguageBox().SelectionChanged({ this, &MainWindow::LanguageBox_SelectionChanged });
            SurfaceMaterials::current().watch(LanguageBox());
            StartWithWindowsCheckBox().Toggled({ this, &MainWindow::StartWithWindowsCheckBox_Changed });
            CloseToTraySwitch().Toggled({ this, &MainWindow::CloseToTraySwitch_Toggled });
            EnableVst2CheckBox().Toggled({ this, &MainWindow::EnableVst2CheckBox_Changed });
            AudioPersistenceModeBox().SelectionChanged({ this, &MainWindow::AudioPersistenceModeBox_SelectionChanged });
            AudioRecoveryRetrySecondsBox().ValueChanged({ this, &MainWindow::AudioRecoveryRetrySecondsBox_ValueChanged });
            AudioRecoveryRetryAttemptsBox().ValueChanged({ this, &MainWindow::AudioRecoveryRetryAttemptsBox_ValueChanged });
            RetryAudioDeviceButton().Click({ this, &MainWindow::RetryAudioDevice_Click });
            ChooseAudioDeviceButton().Click({ this, &MainWindow::ChooseAudioDevice_Click });
            ManageEnabledAudioDevicesButton().Click({ this, &MainWindow::ManageEnabledAudioDevices_Click });
            CloseQuitsAppRadioButton().Checked({ this, &MainWindow::CloseBehaviorRadioButton_Checked });
            CloseToTrayRadioButton().Checked({ this, &MainWindow::CloseBehaviorRadioButton_Checked });
            refreshLanguageItems();
            HideSupportTabSwitch().IsOn(hideSupportTab);
            IconModeBox().SelectionChanged({ this, &MainWindow::IconModeBox_SelectionChanged });
            BackdropModeBox().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
            {
                if (BackdropModeBox() && BackdropModeBox().SelectedIndex() >= 0)
                {
                    saveBackdropModeIndex(BackdropModeBox().SelectedIndex());
                    applyBackdrop(BackdropModeBox().SelectedIndex());
                    showNotification(L"Window material updated.",false);
                }
            });
            LayoutModeBox().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
            {
                if (!LayoutModeBox() || LayoutModeBox().SelectedIndex() < 0)
                    return;

                compactLayout = LayoutModeBox().SelectedIndex() == 0;
                saveUiSetting(L"Appearance", L"LayoutMode", compactLayout ? L"Compact" : L"Expanded");
                applyLayoutMode();
                applyResponsiveLayout(RootLayout().ActualWidth());
            });
            BackdropModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            sidebarOnOpenBox.SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
                if (syncingSidebarPreference || sidebarOnOpenBox.SelectedIndex() < 0) return;
                saveUiSetting(L"Appearance", L"SidebarOnOpen", sidebarOnOpenBox.SelectedIndex() == 1 ? L"Expanded" : L"Collapsed");
            });
            sidebarOnOpenBox.DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            sidebarOnOpenBox.DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            LayoutModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            AudioPersistenceModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            CustomRecoveryBackendBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            CustomRecoveryInputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            CustomRecoveryOutputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
            BackdropModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            LayoutModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            AudioPersistenceModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            CustomRecoveryBackendBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            CustomRecoveryInputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            CustomRecoveryOutputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
            updateDebugControls();
        }
    }

    void MainWindow::Dashboard_Click(IInspectable const&, RoutedEventArgs const&) { showSection(L"Dashboard"); }
    void MainWindow::Preferences_Click(IInspectable const&, RoutedEventArgs const&) { showSection(L"Audio"); }
    void MainWindow::Plugins_Click(IInspectable const&, RoutedEventArgs const&) { showSection(L"Plugins"); }
    void MainWindow::Support_Click(IInspectable const&, RoutedEventArgs const&) { showSection(L"Support me"); }
    void MainWindow::Config_Click(IInspectable const&, RoutedEventArgs const&) { showSection(L"Settings"); }

    void MainWindow::Navigation_SelectionChanged(NavigationView const&,
                                                  NavigationViewSelectionChangedEventArgs const& args)
    {
        const auto item = args.SelectedItem().try_as<NavigationViewItem>();
        if (!item)
            return;

        const auto section = unbox_value_or<hstring>(item.Tag(), L"");
        if (!section.empty())
            showSection(std::wstring(section.c_str()));
    }

    void MainWindow::KoFi_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::SupportRepository_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", GITHUB_REPOSITORY_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::SupportShowcase_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", GITHUB_SHOWCASE_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::DownloadUpdate_Click(IInspectable const&, RoutedEventArgs const&)
    {
        chooseUpdateMethodAsync();
    }

    fire_and_forget MainWindow::chooseUpdateMethodAsync()
    {
        if(updateChoiceOpen || updateInstallInProgress || windowClosing)co_return;
        auto lifetime=get_strong();updateChoiceOpen=true;
        try {
            ContentDialog dialog;
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("update.choice.title",L"Update LightHostModern")));
            dialog.Content(box_value(localization.text("update.choice.body",L"Update here, or download the package from GitHub. Updating here briefly stops audio and reopens the app. Your settings are kept.")));
            dialog.PrimaryButtonText(localization.text("update.choice.direct",L"Update in app"));
            dialog.SecondaryButtonText(localization.text("update.choice.browser",L"GitHub download"));
            dialog.CloseButtonText(localization.text("common.cancel",L"Cancel"));
            dialog.IsPrimaryButtonEnabled(!updateService->latest.artifactUrl.empty());
            dialog.DefaultButton(ContentDialogButton::Close);
            enableDialogBackgroundDefocus(dialog);sizeDialogToViewport(dialog,RootLayout(),0.65);
            const auto choice=co_await lightHostModern::ui::showAppDialog(dialog);
            updateChoiceOpen=false;
            if(windowClosing)co_return;
            if(choice==ContentDialogResult::Primary)downloadAndInstallUpdateAsync();
            else if(choice==ContentDialogResult::Secondary) {
                const auto& url=updateService->latest.artifactUrl.empty()?updateService->latest.releaseUrl:updateService->latest.artifactUrl;
                if(!url.empty())ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            }
        }catch(...){updateChoiceOpen=false;winUILog("Could not open update options.");}
    }

    void MainWindow::syncDiagnosticsSetting(bool enabled)
    {
        if (diagnosticsEnabled != enabled) { diagnosticsPresenter.resetCpuSampler(); dashboardCpuSampler.reset(); }
        diagnosticsEnabled = enabled;
        if (!enabled) updateDashboardPerformance("{}");
        const auto captureState=lightHostModern::verbose::status();
        const bool capturePending=captureState.phase!="off";
        DiagnosticsButton().Visibility(enabled||capturePending ? Visibility::Visible : Visibility::Collapsed);
        if (settingsPageView) {
            syncingDiagnosticsControls = true;
            auto page = winrt::get_self<SettingsPageView>(settingsPageView);
            page->DiagnosticsEnabledSwitch().IsOn(enabled);
            page->DiagnosticsEnabledSwitch().IsEnabled(!diagnosticsChangePending);
            page->DiagnosticsStateText().Text(localization.text(enabled ? "common.on" : "common.off", enabled ? L"On" : L"Off"));
            syncingDiagnosticsControls = false;
        }
        // The log controls remain accessible independently of performance metrics.
    }

    winrt::fire_and_forget MainWindow::DiagnosticsEnabled_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (syncingDiagnosticsControls || diagnosticsChangePending || windowClosing) co_return;
        auto lifetime = get_strong();
        const auto toggle = winrt::get_self<SettingsPageView>(settingsPageView)->DiagnosticsEnabledSwitch();
        const bool enabled = toggle.IsOn();
        if (enabled == diagnosticsEnabled) co_return;
        diagnosticsChangePending = true;
        toggle.IsEnabled(false);
        try {
            bool confirmed = enabled;
            if (!enabled) {
                ContentDialog dialog;
                dialog.XamlRoot(RootLayout().XamlRoot()); dialog.RequestedTheme(RootLayout().ActualTheme());
                dialog.Title(box_value(localization.text("settings.diagnostics.disableTitle", L"Disable diagnostics?")));
                TextBlock message; message.TextWrapping(TextWrapping::Wrap);
                message.Text(localization.text("settings.diagnostics.disableDescription", L"This hides the Diagnostics page and stops collecting DSP load, CPU usage and processing statistics. Audio processing and dashboard volume bars continue to work."));
                dialog.Content(message);
                dialog.PrimaryButtonText(localization.text("settings.diagnostics.disable", L"Disable diagnostics"));
                dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
                Automation::AutomationProperties::SetAutomationId(dialog, L"DisableDiagnosticsDialog");
                confirmed = co_await lightHostModern::ui::showAppDialog(dialog) == ContentDialogResult::Primary;
            }
            if (confirmed && !windowClosing) co_await sendCommand(std::string("set-diagnostics-enabled:") + (enabled ? "1" : "0"));
        } catch (const hresult_error& error) { winUILog("Diagnostics setting: " + to_string(error.message())); }
        diagnosticsChangePending = false;
        if (!windowClosing) syncDiagnosticsSetting(diagnosticsEnabled);
    }

    void MainWindow::HideSupportTabSwitch_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        hideSupportTab = HideSupportTabSwitch().IsOn();
        updateToggleStateLabels();
        saveUiSetting(L"General", L"HideSupportTab", hideSupportTab ? L"1" : L"0");
        SupportButton().Visibility(hideSupportTab ? Visibility::Collapsed : Visibility::Visible);
        if (hideSupportTab && currentSection == L"Support me")
        {
            SidebarRail().SelectedItem(ConfigButton());
            showSection(L"Settings");
        }
    }

    void MainWindow::LanguageBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingLanguageControls || LanguageBox().SelectedIndex() < 0)
            return;
        const auto item = LanguageBox().SelectedItem().try_as<ComboBoxItem>();
        if (!item)
            return;
        const auto language = unbox_value_or<hstring>(item.Tag(), L"en-us");
        saveUiSetting(L"Localization", L"Language", language.c_str());
        pendingLanguageCode = language.c_str();
        if (languageChangeQueued)
            return;

        languageChangeQueued = true;
        auto lifetime = get_strong();
        const bool queued = DispatcherQueue().TryEnqueue([this, lifetime]()
        {
            languageChangeQueued = false;
            try
            {
                localization.load(pendingLanguageCode);
                applyLocalization();
                refreshPluginViews();
                renderedRunningPluginLabels.clear();
                renderedInstalledPluginLabels.clear();
                refreshSnapshot(true);
            }
            catch (hresult_error const& error)
            {
                winUILog("Language change failed: " + to_string(error.message()));
                showNotification(localization.text("settings.language.failed", L"The language could not be applied. Reopen the interface and try again.").c_str());
            }
            catch (std::exception const& error)
            {
                winUILog(std::string("Language change failed: ") + error.what());
                showNotification(localization.text("settings.language.failed", L"The language could not be applied. Reopen the interface and try again.").c_str());
            }
            catch (...)
            {
                winUILog("Language change failed with an unknown exception.");
                showNotification(localization.text("settings.language.failed", L"The language could not be applied. Reopen the interface and try again.").c_str());
            }
        });
        if (!queued)
            languageChangeQueued = false;
    }

    void MainWindow::PluginSearchBox_TextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if(args.Reason()==AutoSuggestionBoxTextChangeReason::SuggestionChosen)return;
        if (sender == RunningPluginSearchBox())
            runningPluginSearch = sender.Text().c_str();
        else
            installedPluginSearch = sender.Text().c_str();
        refreshPluginViews();
        auto suggestions=single_threaded_observable_vector<IInspectable>();
        const bool running=sender==RunningPluginSearchBox();
        const auto& page=running?runningPage:installedPage;
        const auto rows=lightHostModern::ui::filterAndSortPluginRows(page.source,std::wstring(sender.Text()),running?runningPluginSortMode:installedPluginSortMode,running);
        for(const auto& row:rows){if(suggestions.Size()>=30)break;TextBlock item;item.Text(hs(row.name));item.IsHitTestVisible(false);item.Tag(box_value(hs(running?row.instanceId:row.knownId)));suggestions.Append(item);}
        sender.ItemsSource(suggestions);sender.IsSuggestionListOpen(!sender.Text().empty()&&suggestions.Size()>0);
    }
    void MainWindow::SidebarToggle_Click(IInspectable const&, RoutedEventArgs const&)
    {
        sidebarCollapsed = SidebarRail().IsPaneOpen();
        updateSidebarLayout();
    }
    void MainWindow::Refresh_Click(IInspectable const&, RoutedEventArgs const&) { refreshSnapshot(); }
    void MainWindow::RunningPluginsTab_Click(IInspectable const&, RoutedEventArgs const&) { showPluginSubsection(L"Running"); }
    void MainWindow::InstalledPluginsTab_Click(IInspectable const&, RoutedEventArgs const&) { showPluginSubsection(L"Installed"); }
    void MainWindow::RunningPluginsListView_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) { updateRunningPluginActions(); }

    void MainWindow::RunningPluginsListView_DragItemsStarting(IInspectable const&, DragItemsStartingEventArgs const& args)
    {
        if (runningPluginSortMode != 0 || !runningPluginSearch.empty() || args.Items().Size() != 1)
        { args.Cancel(true); return; }
        const auto item = args.Items().GetAt(0).as<winrt::LightHostModernWinUI::PluginItem>();
        draggedPluginSourceId = to_string(item.Id());
        draggedPluginSourceIndex = item.OriginalIndex();
        pluginDragInProgress = true;
        args.Data().RequestedOperation(DataPackageOperation::Move);
        args.Data().SetText(item.Id());
    }

    void MainWindow::RunningPluginsListView_DragItemsCompleted(IInspectable const&, DragItemsCompletedEventArgs const&)
    { pluginDragInProgress = false; draggedPluginSourceId.clear(); draggedPluginSourceIndex = -1; }

    void MainWindow::RunningPluginItem_DragStarting(UIElement const&, DragStartingEventArgs const&) {}

    void MainWindow::RunningPluginItem_DragOver(IInspectable const& sender, DragEventArgs const& args)
    {
        const auto element = sender.try_as<FrameworkElement>();
        const auto item = element ? element.DataContext().try_as<winrt::LightHostModernWinUI::PluginItem>() : nullptr;
        if (!pluginDragInProgress || !item || !item.CanReorder()) return;
        args.AcceptedOperation(DataPackageOperation::Move);
        args.DragUIOverride().Caption(item.AccessibleName());
        args.Handled(true);
    }

    winrt::fire_and_forget MainWindow::RunningPluginItem_Drop(IInspectable sender, DragEventArgs args)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto item = element ? element.DataContext().try_as<winrt::LightHostModernWinUI::PluginItem>() : nullptr;
        if (!pluginDragInProgress || !item || !item.CanReorder()) co_return;
        args.Handled(true);
        const auto sourceId = draggedPluginSourceId;
        const auto targetId = to_string(item.Id());
        pluginDragInProgress = false;
        draggedPluginSourceId.clear();
        if (!sourceId.empty() && sourceId != targetId) co_await sendCommand("move-plugin-to:" + sourceId + ":" + targetId);
    }

    void MainWindow::FluentDropdownButton_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto button = sender.try_as<Button>();
        if (!button)
            return;

        std::string command;
        try
        {
            command = wideToUtf8(std::wstring(unbox_value<hstring>(button.Tag()).c_str()));
        }
        catch (...)
        {
            return;
        }

        if (command == audioBackendDropdown.command) openFluentDropdown(audioBackendDropdown);
        else if (command == inputDropdown.command) openFluentDropdown(inputDropdown);
        else if (command == outputDropdown.command) openFluentDropdown(outputDropdown);
        else if (command == sampleRateDropdown.command) openFluentDropdown(sampleRateDropdown);
        else if (command == bufferSizeDropdown.command) openFluentDropdown(bufferSizeDropdown);
        else if (command == themeModeDropdown.command) openFluentDropdown(themeModeDropdown);
    }

    winrt::fire_and_forget MainWindow::FluentDropdownItem_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingHostControls || syncingThemeControls)
            co_return;

        auto item = sender.try_as<Button>();
        if (!item)
            co_return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(item.Tag()).c_str()));
        }
        catch (...)
        {
            co_return;
        }

        const auto separator = tag.find(':');
        if (separator == std::string::npos)
            co_return;

        const auto command = tag.substr(0, separator);
        const int index = std::atoi(tag.substr(separator + 1).c_str());

        closeFluentDropdowns();

        auto updateLocal = [index](FluentDropdown& dropdown)
        {
            if (index >= 0 && index < (int) dropdown.values.size())
                dropdown.selectedIndex = index;
        };

        auto selectedValue = [index](FluentDropdown const& dropdown) -> std::string
        {
            if (index >= 0 && index < (int) dropdown.values.size())
                return dropdown.values[(size_t) index];
            return {};
        };

        if (command == "theme")
        {
            updateLocal(themeModeDropdown);
            syncFluentDropdownLabel(themeModeDropdown);
            auto theme = ElementTheme::Default;
            if (index == 1)
                theme = ElementTheme::Light;
            else if (index == 2)
                theme = ElementTheme::Dark;
            applyTheme(theme);
            co_return;
        }

        if (command == "set-audio-backend")
        {
            updateLocal(audioBackendDropdown);
            syncFluentDropdownLabel(audioBackendDropdown);
            co_await changeAudioSelection("backend", selectedValue(audioBackendDropdown));
            co_return;
        }

        if (command == "set-audio-input")
        {
            updateLocal(inputDropdown);
            syncFluentDropdownLabel(inputDropdown);
            co_await changeAudioSelection("input", selectedValue(inputDropdown));
            co_return;
        }

        if (command == "set-audio-output")
        {
            updateLocal(outputDropdown);
            syncFluentDropdownLabel(outputDropdown);
            co_await changeAudioSelection("output", selectedValue(outputDropdown));
            co_return;
        }

        if (command == "set-sample-rate")
        {
            updateLocal(sampleRateDropdown);
            syncFluentDropdownLabel(sampleRateDropdown);
            const auto value = selectedValue(sampleRateDropdown);
            if (!value.empty())
                co_await changeAudioSelection("sampleRate", value);
            co_return;
        }

        if (command == "set-buffer-size")
        {
            updateLocal(bufferSizeDropdown);
            syncFluentDropdownLabel(bufferSizeDropdown);
            const auto value = selectedValue(bufferSizeDropdown);
            if (!value.empty())
                co_await changeAudioSelection("bufferSize", value);
        }
    }

    void MainWindow::ComboBox_DropDownOpened(IInspectable const&, IInspectable const&)
    {
        comboDropDownOpen = true;
        winUILog("ComboBox dropdown opened; automatic snapshot refresh paused.");
    }

    void MainWindow::ComboBox_DropDownClosed(IInspectable const&, IInspectable const&)
    {
        comboDropDownOpen = false;
        winUILog("ComboBox dropdown closed; automatic snapshot refresh resumed.");
    }

    void MainWindow::OpenWindowsSoundSettings_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::RepositoryButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", GITHUB_REPOSITORY_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::OriginalRepositoryButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", L"https://github.com/opencma/LightHostModern", nullptr, nullptr, SW_SHOWNORMAL);
    }

    Windows::Foundation::IAsyncAction MainWindow::changeAudioSelection(std::string field, std::string value)
    {
        auto lifetime=get_strong();
        try {
            auto snapshot=lightHostModern::ipc::parseSnapshotObject(hostConnection->snapshotJson);
            lightHostModern::ipc::JsonObject intent;
            intent.SetNamedValue(L"field",lightHostModern::ipc::JsonValue::CreateStringValue(to_hstring(field)));
            intent.SetNamedValue(L"value",lightHostModern::ipc::JsonValue::CreateStringValue(to_hstring(value)));
            lightHostModern::ui::stampAudioIntent(intent,snapshot);
            co_await sendCommand("ui-audio-intent:"+to_string(intent.Stringify()));
        } catch (...) { if(!windowClosing)showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }
    }

    Windows::Foundation::IAsyncAction MainWindow::changeAudioChannels(bool input,int first,int last,bool enabled)
    {
        auto lifetime=get_strong();
        try {
            auto snapshot=lightHostModern::ipc::parseSnapshotObject(hostConnection->snapshotJson);
            lightHostModern::ipc::JsonObject intent;
            intent.SetNamedValue(L"field",lightHostModern::ipc::JsonValue::CreateStringValue(input?L"inputChannels":L"outputChannels"));
            intent.SetNamedValue(L"first",lightHostModern::ipc::JsonValue::CreateNumberValue(first));
            intent.SetNamedValue(L"last",lightHostModern::ipc::JsonValue::CreateNumberValue(last));
            intent.SetNamedValue(L"enabled",lightHostModern::ipc::JsonValue::CreateBooleanValue(enabled));
            lightHostModern::ui::stampAudioIntent(intent,snapshot);
            co_await sendCommand("ui-audio-intent:"+to_string(intent.Stringify()));
        } catch (...) { if(!windowClosing)showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }
    }

    winrt::fire_and_forget MainWindow::AudioBackendBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (!syncingHostControls && AudioBackendBox().SelectedIndex() >= 0)
            co_await changeAudioSelection("backend", selectedComboText(AudioBackendBox()));
    }

    winrt::fire_and_forget MainWindow::InputBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (!syncingHostControls && InputBox().SelectedIndex() >= 0)
            co_await changeAudioSelection("input", to_string(lightHostModern::ui::AudioPageController::deviceName(InputBox())));
    }

    winrt::fire_and_forget MainWindow::OutputBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (!syncingHostControls && OutputBox().SelectedIndex() >= 0)
            co_await changeAudioSelection("output", to_string(lightHostModern::ui::AudioPageController::deviceName(OutputBox())));
    }

    winrt::fire_and_forget MainWindow::SampleRateBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (!syncingHostControls && SampleRateBox().SelectedIndex() >= 0)
            co_await changeAudioSelection("sampleRate", selectedComboText(SampleRateBox()));
    }

    winrt::fire_and_forget MainWindow::BufferSizeBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (!syncingHostControls && BufferSizeBox().SelectedIndex() >= 0)
            co_await changeAudioSelection("bufferSize", selectedComboText(BufferSizeBox()));
    }

    winrt::fire_and_forget MainWindow::ChannelCheckBox_Changed(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingHostControls)
            co_return;

        const auto box = sender.try_as<CheckBox>();
        if (!box)
            co_return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(box.Tag()).c_str()));
        }
        catch (...)
        {
            co_return;
        }

        const auto firstSeparator = tag.find(':');
        if (firstSeparator == std::string::npos)
            co_return;

        const auto secondSeparator = tag.find(':', firstSeparator + 1);
        const auto command = tag.substr(0, firstSeparator);
        const int first = std::atoi(tag.substr(firstSeparator + 1).c_str());
        const int last = secondSeparator == std::string::npos ? first : std::atoi(tag.substr(secondSeparator + 1).c_str());
        if (!hasFullSnapshot || pendingNormalClose) { co_await refreshSnapshot(true); co_return; }
        const auto& rows = command == "set-input-channel" ? currentInputChannelRows : currentOutputChannelRows;
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& value) { return value.startIndex == first && value.endIndex == last; });
        if (row == rows.end()) co_return;
        const auto stateSeparator = secondSeparator == std::string::npos ? std::string::npos : tag.find(':', secondSeparator + 1);
        const bool wasPartial = stateSeparator != std::string::npos && tag.substr(stateSeparator + 1) == "partial";
        const bool desired = wasPartial || isChecked(box);
        try {
            if (wasPartial) {
                // WinUI toggles Indeterminate to Off. A partially selected pair
                // enables both channels instead. Consume the presented state
                // now so another click can turn it off before the IPC completes.
                struct RestoreSync { bool& flag; bool previous; ~RestoreSync() { flag = previous; } } restore{syncingHostControls, syncingHostControls};
                syncingHostControls = true;
                box.Tag(box_value(hs(tag.substr(0, stateSeparator))));
                box.IsChecked(true);
            }
            co_await changeAudioChannels(command == "set-input-channel", first, last, desired);
        }
        catch (...) { showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }

    }

    winrt::fire_and_forget MainWindow::InputChannelsToggleAll_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        bool shouldCheck = true;
        try
        {
            shouldCheck = unbox_value<hstring>(InputChannelsToggleAllButton().Tag()) == L"check";
        }
        catch (...) {}

        const auto rows = currentInputChannelRows;
        if (rows.empty()) co_return;
        try { co_await changeAudioChannels(true, rows.front().startIndex, rows.back().endIndex, shouldCheck); }
        catch (...) { showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }

    }

    winrt::fire_and_forget MainWindow::OutputChannelsToggleAll_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        bool shouldCheck = true;
        try
        {
            shouldCheck = unbox_value<hstring>(OutputChannelsToggleAllButton().Tag()) == L"check";
        }
        catch (...) {}

        const auto rows = currentOutputChannelRows;
        if (rows.empty()) co_return;
        try { co_await changeAudioChannels(false, rows.front().startIndex, rows.back().endIndex, shouldCheck); }
        catch (...) { showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }

    }

    winrt::fire_and_forget MainWindow::ChannelButton_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingHostControls)
            co_return;

        const auto button = sender.try_as<Button>();
        if (!button)
            co_return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(button.Tag()).c_str()));
        }
        catch (...)
        {
            co_return;
        }

        const auto first = tag.find(':');
        const auto second = first == std::string::npos ? std::string::npos : tag.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            co_return;

        const auto command = tag.substr(0, first);
        const auto channelIndex = tag.substr(first + 1, second - first - 1);
        const bool currentlyActive = tag.substr(second + 1) == "1";
        const int index = std::atoi(channelIndex.c_str());
        try { co_await changeAudioChannels(command == "set-input-channel", index, index, !currentlyActive); }
        catch (...) { showNotification(localization.text("audio.selectionFailed", L"Could not apply this audio selection. Refresh the device list and try again.").c_str()); }
    }

    void MainWindow::showSection(std::wstring const& section)
    {
        if (currentSection != section && currentSection != L"Plugins") pageScrollOffsets[currentSection] = ContentScrollViewer().VerticalOffset();
        currentSection = section;
        if (operatingPresenter) operatingPresenter->visible(section == L"Plugins");
        if (section == L"Diagnostics") diagnosticsPresenter.resetCpuSampler();
        if (section == L"Dashboard") { dashboardCpuSampler.reset(); lastDiagnosticTick = 0; }
        DashboardAudioNotice().Visibility(section == L"Dashboard" && DashboardAudioNotice().IsOpen() ? Visibility::Visible : Visibility::Collapsed);
        const bool created = ensurePage(section);
        applyLayoutMode();
        DiagnosticsPageHost().Visibility(section == L"Diagnostics" ? Visibility::Visible : Visibility::Collapsed);
        SettingsPageHost().Visibility(section == L"Settings" ? Visibility::Visible : Visibility::Collapsed);
        SupportPageHost().Visibility(section == L"Support me" ? Visibility::Visible : Visibility::Collapsed);
        PluginsPageHost().Visibility(section == L"Plugins" ? Visibility::Visible : Visibility::Collapsed);
        AudioPageHost().Visibility(section == L"Audio" ? Visibility::Visible : Visibility::Collapsed);
        ProfilesPageHost().Visibility(section == L"Profiles" ? Visibility::Visible : Visibility::Collapsed);
        DashboardPanel().Visibility(section == L"Dashboard" ? Visibility::Visible : Visibility::Collapsed);
        if (PreferencesPanel()) PreferencesPanel().Visibility(section == L"Audio" ? Visibility::Visible : Visibility::Collapsed);
        if (PluginsPanel()) PluginsPanel().Visibility(section == L"Plugins" ? Visibility::Visible : Visibility::Collapsed);
        if (SupportPanel()) SupportPanel().Visibility(section == L"Support me" ? Visibility::Visible : Visibility::Collapsed);
        if (ConfigPanel()) ConfigPanel().Visibility(section == L"Settings" ? Visibility::Visible : Visibility::Collapsed);
        ContentScrollViewer().Visibility(section == L"Plugins" ? Visibility::Collapsed : Visibility::Visible);
        ContentScrollViewer().VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

        if (section == L"Dashboard")
            PageTitleText().Text(localization.text("nav.dashboard", L"Dashboard"));
        else if (section == L"Audio")
            PageTitleText().Text(localization.text("nav.audio", L"Audio"));
        else if (section == L"Plugins")
            PageTitleText().Text(localization.text("nav.plugins", L"Plugins"));
        else if (section == L"Profiles")
            PageTitleText().Text(localization.text("nav.profiles", L"Profiles"));
        else if (section == L"Support me")
            PageTitleText().Text(localization.text("nav.support", L"Support me"));
        else if (section == L"Diagnostics")
            PageTitleText().Text(localization.text("nav.diagnostics", L"Diagnostics"));
        else
            PageTitleText().Text(localization.text("nav.settings", L"Settings"));

        if (section == L"Dashboard")
            PageSubtitleText().Text(localization.text("page.dashboard.subtitle", L"Live audio host status and current routing."));
        else if (section == L"Audio")
            PageSubtitleText().Text(localization.text("page.audio.subtitle", L"Configure audio devices and stream format."));
        else if (section == L"Plugins")
            PageSubtitleText().Text(operatingPresenter && operatingPresenter->chainMode()
                ? localization.translatedSource(L"Connect plugins and shape your audio flow.")
                : localization.text("page.plugins.subtitle", L"Manage the running chain and installed plugin database."));
        else if (section == L"Profiles")
            PageSubtitleText().Text(localization.text("page.profiles.subtitle", L"Save and manage your List and Chain setups."));
        else if (section == L"Support me")
            PageSubtitleText().Text(localization.text("page.support.subtitle", L"Support the project and help LightHostModern keep improving."));
        else if (section == L"Diagnostics")
            PageSubtitleText().Text(localization.text("page.diagnostics.subtitle", L"Audio performance, driver settings and processing activity."));
        else
            PageSubtitleText().Text(localization.text("page.settings.subtitle", L"Configure app behavior, audio recovery, and appearance."));

        NavigationViewItem selectedItem{ nullptr };
        if (section == L"Dashboard") selectedItem = DashboardButton();
        else if (section == L"Audio") selectedItem = PreferencesButton();
        else if (section == L"Plugins") selectedItem = PluginsButton();
        else if (section == L"Profiles") selectedItem = ProfilesButton();
        else if (section == L"Support me") selectedItem = SupportButton();
        else if (section == L"Diagnostics") selectedItem = DiagnosticsButton();
        else selectedItem = ConfigButton();

        if (SidebarRail().SelectedItem() != selectedItem)
            SidebarRail().SelectedItem(selectedItem);

        if (section != L"Plugins") ContentScrollViewer().ChangeView(nullptr, pageScrollOffsets[section], nullptr, true);
        if (created && !hostConnection->snapshotJson.empty()) refreshSnapshot(true);
    }

    void MainWindow::configurePluginSortMenus()
    {
        if (!Pages().PluginsLoaded()) return;
        auto configure = [this](Button const& button, bool running)
        {
            auto flyout = MenuFlyout();
            flyout.Placement(FlyoutPlacementMode::BottomEdgeAlignedRight);
            if (!running)
            {
                auto grouping = ToggleMenuFlyoutItem();
                grouping.Text(localization.text("plugins.groupManufacturer", L"Group by manufacturer"));
                grouping.IsChecked(installedGrouped);
                Automation::AutomationProperties::SetAutomationId(grouping, L"InstalledGroupByManufacturer");
                grouping.Click([weak = get_weak()](IInspectable const& sender, RoutedEventArgs const&) {
                    if (auto owner = weak.get())
                    {
                        owner->installedGrouped = sender.as<ToggleMenuFlyoutItem>().IsChecked();
                        saveUiSetting(L"Plugins", L"GroupByManufacturer", owner->installedGrouped ? L"1" : L"0");
                        owner->refreshPluginViews();
                    }
                });
                flyout.Items().Append(grouping);
                flyout.Items().Append(MenuFlyoutSeparator());
            }
            auto add = [this, &flyout, running](std::wstring const& label, int mode)
            {
                auto item = MenuFlyoutItem();
                item.Text(label);
                item.Click([this, running, mode](IInspectable const&, RoutedEventArgs const&)
                {
                    if (running)
                        runningPluginSortMode = mode;
                    else
                        installedPluginSortMode = mode;
                    refreshPluginViews();
                });
                flyout.Items().Append(item);
            };

            if (running)
                add(localization.text("plugins.sort.chain", L"Chain order").c_str(), 0);
            add(localization.text("plugins.sort.pluginAsc", L"Plugin name (A-Z)").c_str(), 1);
            add(localization.text("plugins.sort.pluginDesc", L"Plugin name (Z-A)").c_str(), 2);
            add(localization.text("plugins.sort.manufacturerAsc", L"Manufacturer (A-Z)").c_str(), 3);
            add(localization.text("plugins.sort.manufacturerDesc", L"Manufacturer (Z-A)").c_str(), 4);
            add(localization.text(running ? "plugins.sort.activeFirst" : "plugins.sort.availableFirst",
                                  running ? L"Active first" : L"Available first").c_str(), 5);
            add(localization.text(running ? "plugins.sort.disabledFirst" : "plugins.sort.runningFirst",
                                  running ? L"Disabled first" : L"Running first").c_str(), 6);
            add(localization.text("plugins.sort.vst3First", L"VST3 then VST2").c_str(), 7);
            add(localization.text("plugins.sort.vst2First", L"VST2 then VST3").c_str(), 8);
            button.Flyout(flyout);
        };

        configure(RunningPluginSortButton(), true);
        configure(InstalledPluginSortButton(), false);
    }

    void MainWindow::applyLocalization()
    {
        updateDashboardSummary(hostConnection->snapshotJson);
        if(verboseLogsPresenter)verboseLogsPresenter->translate(localization);
        const auto accessibleName = [this](FrameworkElement const& element, const char* key, const wchar_t* fallback)
        {
            if (element) Automation::AutomationProperties::SetName(element, localization.text(key, fallback));
        };
        accessibleName(AudioBackendBox(), "audio.backend", L"Audio backend");
        accessibleName(InputBox(), "audio.inputDevice", L"Input device");
        accessibleName(OutputBox(), "audio.outputDevice", L"Output device");
        accessibleName(SampleRateBox(), "audio.sampleRate", L"Sample rate");
        accessibleName(BufferSizeBox(), "audio.bufferSize", L"Buffer size");
        accessibleName(StartWithWindowsCheckBox(), "settings.startWindows", L"Start with Windows");
        accessibleName(CloseToTraySwitch(), "settings.closeToTray", L"Close to tray");
        accessibleName(EnableVst2CheckBox(), "settings.enableVst2", L"Enable VST2 plugins");
        accessibleName(AudioPersistenceModeBox(), "settings.persistence.title", L"Device persistence");
        accessibleName(AudioRecoveryRetrySecondsBox(), "settings.persistence.retryInterval", L"Device persistence: retry interval");
        accessibleName(AudioRecoveryRetryAttemptsBox(), "settings.persistence.maxAttempts", L"Device persistence: max attempts");
        accessibleName(ThemeModeBox(), "settings.theme", L"Theme");
        accessibleName(BackdropModeBox(), "settings.material", L"Window material");
        accessibleName(IconModeBox(), "settings.icon", L"App icon");

        if (databasePageView)
        {
            auto scanView = winrt::get_self<DatabasePageView>(databasePageView);
            scanView->PluginScanCompletedLabel().Text(localization.text("scan.processed", L"Processed"));
            scanView->PluginScanCachedLabel().Text(localization.text("scan.cached", L"Cached"));
            scanView->PluginScanFailureLabel().Text(localization.text("scan.failures", L"Failures"));
            Automation::AutomationProperties::SetName(PluginScanProgress(), localization.text("scan.progress", L"Plugin scan progress"));
            if (databasePaths) databasePaths->localize();
        }
        if (pluginScanDialog) {
            pluginScanDialog.Title(box_value(localization.text("plugins.scanForPlugins", L"Scan for plugins")));
            pluginScanDialog.CloseButtonText(localization.text("common.close", L"Close"));
            updateScanDialogActions();
        }

        for (auto button : { RunningGlobalMuteButton() })
        {
            if (!button) continue;
            const auto label = localization.text("audio.globalMute", L"Mute output");
            button.Label(label);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, label);
            lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(localization.text("audio.globalMute.help", L"Silence output while plugins keep processing.")));
        }
        for (auto button : { RunningGlobalBypassButton() })
        {
            if (!button) continue;
            const auto label = localization.text("audio.globalBypass", L"Bypass chain");
            button.Label(label);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, label);
            lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(localization.text("audio.globalBypass.help", L"Listen to latency-compensated dry audio while plugins keep processing.")));
        }

        HoverHelp::current().localize(localization);
        localizeVisualTree(RootLayout());
        BrandTitleText().Text(localization.text("app.title", L"LightHostModern"));
        DashboardButton().Content(box_value(localization.text("nav.dashboard", L"Dashboard")));
        PreferencesButton().Content(box_value(localization.text("nav.audio", L"Audio")));
        PluginsButton().Content(box_value(localization.text("nav.plugins", L"Plugins")));
        ProfilesButton().Content(box_value(localization.text("nav.profiles", L"Profiles")));
        if (operatingPresenter) operatingPresenter->localize(localization);
        SupportButton().Content(box_value(localization.text("nav.support", L"Support me")));
        ConfigButton().Content(box_value(localization.text("nav.settings", L"Settings")));
        DiagnosticsButton().Content(box_value(localization.text("nav.diagnostics", L"Diagnostics")));
        inputMeter.localize(localization); outputMeter.localize(localization);
        updateAudioAvailability();
        if (diagnosticsEnabled && diagnosticsPageView) diagnosticsPresenter.update(hostConnection->snapshotJson, localization);
        if (Pages().SettingsLoaded()) {
            RepositoryButton().Content(box_value(localization.text("support.repository.action", L"Go to repo")));
            OriginalRepositoryButton().Content(box_value(localization.text("support.repository.action", L"Go to repo")));
        }
        if (Pages().PluginsLoaded()) {
            RunningPluginSortButton().Label(localization.text("plugins.sort.action", L"Sort"));
            InstalledPluginSortButton().Label(localization.text("plugins.sort.action", L"Sort"));
            winrt::get_self<PluginsPageView>(pluginsPageView)->ScanForPluginsButton().Label(localization.text("plugins.scanForPlugins", L"Scan for plugins"));
        }
        if (Pages().SettingsLoaded()) {
            auto page = winrt::get_self<SettingsPageView>(settingsPageView);
            page->PluginDatabaseTitle().Text(localization.translatedSource(L"Danger zone"));
            page->RemoveMissingTitle().Text(localization.text("settings.removeMissingTitle", L"Remove missing plugins"));
            page->ClearDatabaseTitle().Text(localization.text("settings.clearDatabaseTitle", L"Clear plugin database"));
            page->RemoveMissingDescription().Text(localization.text("plugins.removeMissingDescription", L"Remove database entries whose plugin files are no longer available."));
            page->ClearDatabaseDescription().Text(localization.text("plugins.clearDescription", L"Clear all installed entries and the running chain. You will be asked to confirm."));
            RemoveMissingPluginsButton().Content(box_value(localization.text("plugins.removeMissing", L"Remove missing")));
            ClearPluginDatabaseButton().Content(box_value(localization.text("plugins.clearDatabase", L"Clear database")));
        }
        if (Pages().PluginsLoaded())
        {
            RunningPluginSearchBox().PlaceholderText(localization.text("plugins.search.running", L"Search running plugins"));
        }
        if (Pages().PluginsLoaded())
        {
            InstalledPluginSearchBox().PlaceholderText(localization.text("plugins.search.installed", L"Search installed plugins"));
        }
        if (Pages().PluginsLoaded())
        {
            RunningPluginsEmptyText().Text(localization.text("plugins.empty.running", L"No plugins running"));
        }
        if (Pages().PluginsLoaded())
        {
            InstalledPluginsEmptyText().Text(localization.text("plugins.empty.installed", L"No plugins installed"));
        }
        if (Pages().SupportLoaded())
        {
            SupportDonateTitleText().Text(localization.text("support.donate.title", L"Support by donating via Ko-fi"));
        }
        if (Pages().SupportLoaded())
        {
            SupportDonateDescriptionText().Text(localization.text("support.donate.description", L"If you enjoy the app and would like to support its continued development, you can make a donation through Ko-fi. Every contribution, no matter the amount, helps me dedicate more time to improving the app, fixing issues, and building new features.\n\nThank you so much for your support! It truly helps keep the project moving forward."));
        }
        if (Pages().SupportLoaded())
        {
            SupportRepositoryTitleText().Text(localization.text("support.repository.title", L"Support by starring the GitHub repository"));
        }
        if (Pages().SupportLoaded())
        {
            SupportRepositoryDescriptionText().Text(localization.text("support.repository.description", L"If you enjoy the app and want to support the project, consider giving the repository a star on GitHub. It’s a simple way to show your support, help the project gain visibility, and make it easier for others to discover.\n\nThank you for supporting the project!"));
        }
        if (Pages().SupportLoaded())
        {
            SupportRepositoryButton().Content(box_value(localization.text("support.repository.action", L"Go to repo")));
        }
        if (Pages().SupportLoaded())
        {
            SupportShowcaseTitleText().Text(localization.text("support.showcase.title", L"Support by showcasing the app"));
        }
        if (Pages().SupportLoaded())
        {
            SupportShowcaseDescriptionText().Text(localization.text("support.showcase.description", L"If you create a video about the app, I’d be happy to help give it more visibility. Once your video is published, open an issue on GitHub and send me the link. I may feature your video on the project page, helping promote your content while you help introduce the app to more people.\n\nIt’s a simple way for us to support each other: you showcase the app, and I help showcase your work."));
        }
        if (Pages().SupportLoaded())
        {
            SupportShowcaseButton().Content(box_value(localization.text("support.showcase.action", L"Create video showcase post")));
        }
        if (Pages().SettingsLoaded())
        {
            UpdateAvailableTitleText().Text(localization.text("settings.update.title", L"Update available"));
        }
        if (Pages().SettingsLoaded())
        {
            if (!updateService->latest.version.empty())
                UpdateAvailableBodyText().Text(localization.format(
                    "settings.update.bodyVersion",
                    L"LightHostModern {0} is available.",
                    { updateService->latest.version }));
        }
        if (Pages().SettingsLoaded())
        {
            updateDownloadButtonText();
        }
        if (Pages().SettingsLoaded())
        {
            auto settingsPage = winrt::get_self<SettingsPageView>(settingsPageView);
            settingsPage->DiagnosticsSettingTitle().Text(localization.text("nav.diagnostics", L"Diagnostics"));
            settingsPage->SidebarOnOpenTitle().Text(localization.text("settings.sidebar.title", L"Sidebar on open"));
            settingsPage->SidebarOnOpenDescription().Text(localization.text("settings.sidebar.description", L"Choose whether the sidebar is collapsed or expanded each time you open the app window."));
            Automation::AutomationProperties::SetName(sidebarOnOpenBox, localization.text("settings.sidebar.title", L"Sidebar on open"));
            syncingSidebarPreference = true;
            setComboItems(sidebarOnOpenBox, {
                to_string(localization.text("settings.sidebar.collapsed", L"Collapsed")),
                to_string(localization.text("settings.sidebar.expanded", L"Expanded")) }, sidebarStartsCollapsed() ? 0 : 1);
            syncingSidebarPreference = false;
            settingsPage->DiagnosticsSettingDescription().Text(localization.text("settings.diagnostics.description", L"Show the Diagnostics page and collect performance data."));
            Automation::AutomationProperties::SetName(settingsPage->DiagnosticsEnabledSwitch(), localization.text("settings.diagnostics.enable", L"Enable diagnostics"));
            syncDiagnosticsSetting(diagnosticsEnabled);
            HideSupportTitleText().Text(localization.text("settings.support.hide", L"Hide the Support me tab"));
        }
        if (Pages().SettingsLoaded())
        {
            LanguageTitleText().Text(localization.text("settings.language.title", L"Language"));
        }
        if (Pages().SettingsLoaded())
        {
            LanguageDescriptionText().Text(localization.text("settings.language.description", L"Choose the language used by the app."));
        }
        if (Pages().SettingsLoaded())
        {
            LayoutModeTitleText().Text(localization.text("settings.layout.title", L"Layout mode"));
        }
        if (Pages().SettingsLoaded())
        {
            LayoutModeDescriptionText().Text(localization.text("settings.layout.description", L"Compact limits page width; Expanded uses all available space."));
        }
        if (Pages().SettingsLoaded())
        {
            setComboItems(LayoutModeBox(), {
                to_string(localization.text("settings.layout.compact", L"Compact")),
                to_string(localization.text("settings.layout.expanded", L"Expanded")) }, compactLayout ? 0 : 1);
        }
        const auto koFiAutomationName = localization.text("support.kofi", L"Support me on Ko-fi");
        if (Pages().SupportLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(KoFiButton(), koFiAutomationName);
        }
        if (Pages().SupportLoaded())
        {
            lightHostModern::ui::HoverHelp::SetToolTip(KoFiButton(), box_value(koFiAutomationName));
        }
        if (Pages().SupportLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SupportRepositoryButton(), localization.text("support.repository.automation", L"Go to the LightHostModern repository"));
        }
        if (Pages().SupportLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SupportShowcaseButton(), localization.text("support.showcase.automation", L"Create a video showcase post"));
        }
        if (Pages().PluginsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(RunningPluginSearchBox(), localization.text("plugins.search.running", L"Search running plugins"));
        }
        if (Pages().PluginsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(InstalledPluginSearchBox(), localization.text("plugins.search.installed", L"Search installed plugins"));
            winrt::get_self<PluginsPageView>(pluginsPageView)->refreshSearchAccessibility();
        }
        if (Pages().PluginsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(RunningPluginSortButton(), localization.text("plugins.sort.label", L"Sort plugins"));
        }
        if (Pages().PluginsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(InstalledPluginSortButton(), localization.text("plugins.sort.label", L"Sort plugins"));
        }
        if (Pages().SettingsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(HideSupportTabSwitch(), localization.text("settings.support.hide", L"Hide the Support me tab"));
        }
        if (Pages().SettingsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(LanguageBox(), localization.text("settings.language.automation", L"App language"));
        }
        if (Pages().SettingsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(LayoutModeBox(), localization.text("settings.layout.title", L"Layout mode"));
        }
        if (Pages().SettingsLoaded())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PreferredDeviceButton(), localization.text("settings.persistence.choose", L"Choose preferred device"));
        }
        updateToggleStateLabels();
        configurePluginSortMenus();
        showSection(currentSection);
    }

    void MainWindow::refreshLanguageItems()
    {
        const bool wasSyncing = syncingLanguageControls;
        syncingLanguageControls = true;
        LanguageBox().Items().Clear();

        const auto languages = ::LightHostModernWinUI::LocalizationCatalog::availableLanguages();
        int selectedLanguage = 0;
        for (size_t index = 0; index < languages.size(); ++index)
        {
            auto item = ComboBoxItem();
            item.Content(box_value(hstring(languages[index].second)));
            item.Tag(box_value(hstring(languages[index].first)));
            LanguageBox().Items().Append(item);
            if (_wcsicmp(languages[index].first.c_str(), localization.languageCode().c_str()) == 0)
                selectedLanguage = static_cast<int>(index);
        }

        LanguageBox().SelectedIndex(selectedLanguage);
        syncingLanguageControls = wasSyncing;
    }

    void MainWindow::localizeVisualTree(DependencyObject const& root)
    {
        if (!root)
            return;

        HoverHelp::current().describe(root);
        if (root == LanguageBox())
            return;

        if(auto bar=root.try_as<CommandBar>()){
            for(auto command:bar.PrimaryCommands())if(auto control=command.try_as<DependencyObject>())HoverHelp::current().describe(control);
            if(auto content=bar.Content().try_as<DependencyObject>())HoverHelp::current().describe(content);
        }
        // These controls localize their public properties or observable models.
        // Writing into their template TextBlocks would replace TemplateBindings,
        // leaving old labels on screen after a language change.
        if (root.try_as<SelectorBar>() || root.try_as<CommandBar>()
            || root == RunningPluginsListView() || root == InstalledPluginsListView())
            return;

        if (const auto textBlock = root.try_as<TextBlock>())
            textBlock.Text(localization.translatedSource(textBlock.Text()));

        if (const auto contentControl = root.try_as<ContentControl>())
        {
            const auto content = contentControl.Content();
            if (const auto value = content.try_as<Windows::Foundation::IPropertyValue>();
                value && value.Type() == Windows::Foundation::PropertyType::String)
            {
                contentControl.Content(box_value(localization.translatedSource(value.GetString())));
            }
        }

        if (const auto toggle = root.try_as<ToggleSwitch>())
        {
            const auto localizeValue = [this](IInspectable const& value) -> IInspectable
            {
                if (const auto property = value.try_as<Windows::Foundation::IPropertyValue>();
                    property && property.Type() == Windows::Foundation::PropertyType::String)
                    return box_value(localization.translatedSource(property.GetString()));
                return value;
            };
            toggle.Header(localizeValue(toggle.Header()));
            toggle.OnContent(localizeValue(toggle.OnContent()));
            toggle.OffContent(localizeValue(toggle.OffContent()));
        }

        if (const auto numberBox = root.try_as<NumberBox>())
        {
            if (const auto property = numberBox.Header().try_as<Windows::Foundation::IPropertyValue>();
                property && property.Type() == Windows::Foundation::PropertyType::String)
                numberBox.Header(box_value(localization.translatedSource(property.GetString())));
        }

        if (const auto comboBox = root.try_as<ComboBox>())
        {
            for (auto const& item : comboBox.Items())
            {
                if (const auto comboItem = item.try_as<ComboBoxItem>())
                {
                    if (const auto property = comboItem.Content().try_as<Windows::Foundation::IPropertyValue>();
                        property && property.Type() == Windows::Foundation::PropertyType::String)
                        comboItem.Content(box_value(localization.translatedSource(property.GetString())));
                }
            }
        }

        const auto childCount = VisualTreeHelper::GetChildrenCount(root);
        for (int childIndex = 0; childIndex < childCount; ++childIndex)
            localizeVisualTree(VisualTreeHelper::GetChild(root, childIndex));
    }

    void MainWindow::notifyAvailableRelease()
    {
        if(windowClosing||!updateService->latest.available)return;
        const auto legacy=loadUiSetting(L"Updates",L"NotifyNewReleases",L"1");
        if(loadUiSetting(L"Updates",L"NotifyReleasesOnWindows",legacy.c_str())!=L"0")notifyWindowsRelease(updateService->latest.version);
        if(loadUiSetting(L"Updates",L"NotifyReleasesInApp",legacy.c_str())==L"0")return;
        HWND hwnd=nullptr;try_as<::IWindowNative>()->get_WindowHandle(&hwnd);if(!IsWindowVisible(hwnd)||IsIconic(hwnd))return;
        const auto version=updateService->latest.version;if(appNotifiedRelease==version)return;
        ReleaseNotification().Title(localization.translatedSource(L"Update available"));ReleaseNotification().Message(localization.format("settings.update.bodyVersion",L"LightHostModern {0} is available.",{version}));ViewReleaseNotificationButton().Content(box_value(localization.translatedSource(L"View update")));ReleaseNotification().IsOpen(true);
        appNotifiedRelease=version;
    }

    fire_and_forget MainWindow::notifyWindowsRelease(std::wstring version)
    {
        if(windowsNoticePending || windowsNotifiedRelease==version)co_return;
        auto lifetime=get_strong();windowsNoticePending=true;
        try {
            const auto response=co_await hostConnection->requestAsync("notify-release:"+wideToUtf8(version));
            if(extractString(to_string(response),"status")!="ok")winUILog("Windows release notification was not submitted.");
            else windowsNotifiedRelease=version;
        }catch(...){winUILog("Windows release notification unavailable.");}
        windowsNoticePending=false;
    }

    fire_and_forget MainWindow::checkForUpdatesAsync()
    {
        if(windowClosing||updateCheckInProgress||updateInstallInProgress||updateChoiceOpen)co_return;
        auto lifetime = get_strong();updateCheckInProgress=true;
        winUILog("Checking for a newer stable release.");
        struct FinishCheck{bool& pending;~FinishCheck(){pending=false;}} finish{updateCheckInProgress};
        try
        {
            if (windowClosing) co_return;
            co_await updateService->loadApplicationResultAsync();
            if (windowClosing) co_return;
            presentUpdateResult();
            const auto hostPid = lightHostModern::ipc::extractNumber(hostConnection->snapshotJson, "hostPid");
            if (hostPid <= 0 || hostPid > UINT32_MAX) { winUILog("Update check deferred: host process identity unavailable."); updateCheckStarted=false; co_return; }
            co_await updateService->checkAsync(APP_VERSION, static_cast<uint32_t>(hostPid));
            if (windowClosing) co_return;
            winUILog("Release check completed: " + to_string(updateService->latest.version) + (updateService->latest.available ? " (newer release)." : " (no newer release)."));
            notifyAvailableRelease();
            if (updateService->latest.available && Pages().SettingsLoaded())
            {
                UpdateAvailableBodyText().Text(localization.format(
                    "settings.update.bodyVersion",
                    L"LightHostModern {0} is available.",
                    { updateService->latest.version }));
                UpdateAvailableCard().Visibility(Visibility::Visible);
                updateDownloadButtonText();
                winUILog("Update available: " + to_string(updateService->latest.version));
            }
        }
        catch (hresult_error const& error)
        {
            winUILog("Update check failed: " + to_string(error.message()));
        }
        catch (...)
        {
            winUILog("Update check failed with an unknown error.");
        }
    }

    void MainWindow::updateDownloadButtonText()
    {
        if (!Pages().SettingsLoaded()) return;
        DownloadUpdateButton().Content(box_value(localization.text("update.choice.button", L"Update options")));
        CancelUpdateButton().Content(box_value(localization.text("common.cancel", L"Cancel")));
        Automation::AutomationProperties::SetName(UpdateTransferProgress(), localization.text("update.progress", L"Download progress"));
        presentUpdateResult();
    }

    void MainWindow::presentUpdateResult()
    {
        if (!Pages().SettingsLoaded() || updateService->applicationState.empty()) return;
        const auto& state = updateService->applicationState;
        UpdateResultCard().Visibility(Visibility::Visible);
        UpdateResultCard().Severity(state == "completed" ? InfoBarSeverity::Success
            : state == "restart_required" || state == "cancelled" ? InfoBarSeverity::Warning : InfoBarSeverity::Error);
        UpdateResultCard().Message(localization.text("update.result." + state,
            L"The update did not finish. Open the log for details."));
        UpdateResultLog().Content(box_value(localization.text("update.viewLog", L"View update log")));
    }

    fire_and_forget MainWindow::downloadAndInstallUpdateAsync()
    {
        auto lifetime = get_strong();
        if (updateInstallInProgress || updateService->latest.artifactUrl.empty()) co_return;
        updateInstallInProgress = true;
        DownloadUpdateButton().IsEnabled(false);
        CancelUpdateButton().IsEnabled(true);
        CancelUpdateButton().Visibility(Visibility::Visible);
        UpdateTransferProgress().Visibility(Visibility::Visible);
        UpdateTransferProgress().Value(0);
        UpdateTransferBytes().Visibility(Visibility::Visible);
        UpdateProgressRing().Visibility(Visibility::Visible);
        UpdateProgressRing().IsActive(true);
        std::string failure;
        try
        {
            UpdateAvailableBodyText().Text(localization.text("update.downloading", L"Downloading and verifying the package…"));
            const auto queue = DispatcherQueue();
            auto lastProgress = std::make_shared<uint64_t>(0);
            struct ProgressState { std::mutex mutex; lightHostModern::update::Progress latest{}; bool queued = false; };
            auto progressState = std::make_shared<ProgressState>();
            co_await updateService->downloadAsync([weak = get_weak(), queue, lastProgress, progressState](lightHostModern::update::Progress progress) {
                const auto now = GetTickCount64();
                if (progress.received != progress.expected && now - *lastProgress < 200) return;
                *lastProgress = now;
                {
                    const std::lock_guard<std::mutex> guard(progressState->mutex);
                    progressState->latest = progress;
                    if (progressState->queued) return;
                    progressState->queued = true;
                }
                if (!queue.TryEnqueue([weak, progressState] {
                    lightHostModern::update::Progress progress;
                    {
                        const std::lock_guard<std::mutex> guard(progressState->mutex);
                        progress = progressState->latest; progressState->queued = false;
                    }
                    if (auto owner = weak.get(); owner && !owner->windowClosing && owner->updateInstallInProgress) {
                        owner->UpdateTransferProgress().Value(100.0 * progress.received / progress.expected);
                        owner->UpdateTransferBytes().Text(owner->localization.format("update.bytes", L"{0} / {1} bytes",
                            {std::to_wstring(progress.received), std::to_wstring(progress.expected)}));
                    }
                })) {
                    const std::lock_guard<std::mutex> guard(progressState->mutex);
                    progressState->queued = false;
                }
            });
            if (windowClosing) co_return;
            CancelUpdateButton().IsEnabled(false);
            UpdateAvailableBodyText().Text(localization.text("update.preparing", L"Preparing the update..."));
            if (!(co_await sendCommand("flush-session"))) throw lightHostModern::update::Error("session_save_failed");
            if (windowClosing) co_return;
            co_await updateService->prepareUpdateAsync();
            if (windowClosing) { updateService->cancel(); co_return; }
            UpdateAvailableBodyText().Text(localization.text("update.applying", L"LightHostModern will reopen when the update is ready."));
            // Host shutdown also posts WM_CLOSE to this window. Arm first so
            // that normal window cleanup cannot cancel the prepared update.
            // The helper still requires both exact processes to exit.
            updateService->armUpdate();
            const auto previousCloseBehavior=closeQuitsHost;
            closeQuitsHost = false;
            const auto shutdown=to_string(co_await hostConnection->requestAsync("quit-host"));
            if(windowClosing)co_return;
            if(extractString(shutdown,"status")!="ok") {
                updateService->abortUpdate();closeQuitsHost=previousCloseBehavior;
                throw lightHostModern::update::Error("shutdown_failed");
            }
            co_await finishNormalCloseAsync();
            co_return;
        }
        catch (const lightHostModern::update::Error& error) { failure = error.code; }
        catch (const hresult_error& error) { failure = updateService->lastError.empty() ? "network_failed" : updateService->lastError; winUILog(to_string(error.message())); }
        catch (const std::exception& error) { failure = "package_invalid"; winUILog(error.what()); }
        catch (...) { failure = "package_invalid"; }
        if (!failure.empty()) { if(!windowClosing) updateService->abortUpdate(); updateService->cancel(); }
        if (windowClosing) co_return;
        if (!failure.empty())
        {
            winUILog("Update operation failed: " + failure);
            UpdateAvailableBodyText().Text(localization.text("update.error." + failure,
                L"The update could not be completed. Try again or open the release page."));
        }
        DownloadUpdateButton().IsEnabled(true);
        CancelUpdateButton().Visibility(Visibility::Collapsed);
        UpdateProgressRing().IsActive(false);
        UpdateProgressRing().Visibility(Visibility::Collapsed);
        UpdateTransferProgress().Visibility(Visibility::Collapsed);
        UpdateTransferBytes().Visibility(Visibility::Collapsed);
        updateInstallInProgress = false;
        updateDownloadButtonText();
    }

    void MainWindow::showPluginSubsection(std::wstring const& section)
    {
        if (!Pages().PluginsLoaded()) return;
        RunningPluginsPanel().Visibility(section == L"Running" ? Visibility::Visible : Visibility::Collapsed);
        InstalledPluginsPanel().Visibility(section == L"Installed" ? Visibility::Visible : Visibility::Collapsed);

        if (section == L"Running" && !RunningPluginsTabButton().IsSelected()) RunningPluginsTabButton().IsSelected(true);
        if (section == L"Installed" && !InstalledPluginsTabButton().IsSelected()) InstalledPluginsTabButton().IsSelected(true);
    }

    void MainWindow::ThemeModeBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingThemeControls || !themeModeBox || themeModeBox.SelectedIndex() < 0) return;
        const auto index = themeModeBox.SelectedIndex();
        saveUiSetting(L"Appearance", L"ThemeMode", index == 0 ? L"System" : index == 1 ? L"Light" : L"Dark");
        // SelectionChanged can run while the native ComboBox popup is updating its
        // containers. Finish that traversal before changing inherited theme resources.
        DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak(), index] { if (auto owner = weak.get(); owner && !owner->windowClosing)
                {owner->applyTheme(index == 0 ? ElementTheme::Default : index == 1 ? ElementTheme::Light : ElementTheme::Dark);owner->showNotification(L"Theme updated.",false);} });
    }

    void MainWindow::RootLayout_SizeChanged(IInspectable const&, SizeChangedEventArgs const& args)
    {
        applyResponsiveLayout(args.NewSize().Width);
    }

    void MainWindow::RootLayout_PointerPressed(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto current = args.OriginalSource().try_as<DependencyObject>();
        while (current && current != RootLayout())
        {
            if (current.try_as<NumberBox>()
                || current.try_as<TextBox>()
                || current.try_as<ComboBox>()
                || current.try_as<AutoSuggestBox>()
                || current.try_as<ToggleSwitch>()
                || current.try_as<ButtonBase>())
            {
                return;
            }

            current = VisualTreeHelper::GetParent(current);
        }

        ContentScrollViewer().Focus(FocusState::Programmatic);
    }

    void MainWindow::applyTheme(ElementTheme theme)
    {
        selectedTheme = theme;
        RootLayout().RequestedTheme(theme);
        updateThemeVisuals();
    }

    void MainWindow::queueThemeRefresh()
    {
        if (windowClosing || themeRefreshQueued) return;
        themeRefreshQueued = true;
        if (!DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [weak = get_weak()] {
            if (auto owner = weak.get()) {
                owner->themeRefreshQueued = false;
                owner->updateThemeVisuals();
            }
        })) themeRefreshQueued = false;
    }

    void MainWindow::updateThemeVisuals()
    {
        if (windowClosing) return;
        highContrastActive = accessibilitySettings.HighContrast();
        preferDarkFallback = RootLayout().ActualTheme() == ElementTheme::Dark;
        styleTitleBar(AppWindow(), preferDarkFallback);
        applyBackdrop(BackdropModeBox() && BackdropModeBox().SelectedIndex() >= 0 ? BackdropModeBox().SelectedIndex() : loadBackdropModeIndex());

        renderedRunningPluginLabels.clear();
        renderedInstalledPluginLabels.clear();
        renderedInputChannelKeys.clear();
        renderedOutputChannelKeys.clear();
        currentInputChannelRows.clear();
        currentOutputChannelRows.clear();
        if (Pages().PluginsLoaded()) showPluginSubsection(RunningPluginsPanel().Visibility() == Visibility::Visible ? L"Running" : L"Installed");
        refreshSnapshot(true);
    }

    void MainWindow::applyBackdrop(int selectedIndex)
    {
        try
        {
            preferDarkFallback = RootLayout().ActualTheme() == ElementTheme::Dark;
            const int effective=VisualPreferences::current().performance?3:selectedIndex;
            SurfaceMaterials::current().configure(effective,RootLayout().ActualTheme(),highContrastActive);
            if(contentFade)contentFade->configure(VisualPreferences::current().fade,highContrastActive);
            if(pluginsPageView)winrt::get_self<PluginsPageView>(pluginsPageView)->configureEffects(highContrastActive);
            // Controllers observe activation explicitly and receive theme changes
            // after the XAML theme traversal through updateThemeVisuals().
            MainContent().Background(nullptr);
            SidebarRail().Background(nullptr);
            if (windowMaterial.apply(*this, highContrastActive ? 3 : effective,
                                     RootLayout().ActualTheme(), highContrastActive))
                RootLayout().Background(brush(makeColorA(0, 0, 0, 0)));
            else
                RootLayout().ClearValue(Panel::BackgroundProperty());
        }
        catch (...)
        {
            windowMaterial.close();
            RootLayout().ClearValue(Panel::BackgroundProperty());
            winUILog("System backdrop unavailable; using solid fallback.");
        }
    }

    void MainWindow::applyVisualPreferences()
    {
        HoverHelp::current().configure(VisualPreferences::current().hoverTooltips);
        if(!(lastNotificationImportant?VisualPreferences::current().warningNotifications:VisualPreferences::current().actionNotifications))NotificationToast().Visibility(Visibility::Collapsed);
        if(refreshTimer)refreshTimer.Interval(std::chrono::milliseconds(VisualPreferences::current().performance?100:50));
        applyBackdrop(loadBackdropModeIndex());
        if(Pages().SettingsLoaded()){
            const auto page=winrt::get_self<SettingsPageView>(settingsPageView);
            page->EdgeFadeSwitch().IsEnabled(!VisualPreferences::current().performance);
            BackdropModeBox().IsEnabled(!VisualPreferences::current().performance);
        }
    }

    void MainWindow::applyLayoutMode()
    {
        // Only the content is centered. Scroll viewports stay window-wide, so
        // compact mode never pulls scrollbars into the card column.
        MainContent().MaxWidth(std::numeric_limits<double>::infinity());
        const double width = MainContent().ActualWidth();
        const double edge = RootLayout().ActualWidth() < 840.0 ? 16.0 : 24.0;
        const bool expandChain=currentSection==L"Plugins"&&operatingPresenter&&operatingPresenter->chainMode()&&loadUiSetting(L"Appearance",L"ExpandChain",L"1")!=L"0";
        const double inset = compactLayout && !expandChain ? (std::max)(edge, (width - COMPACT_CONTENT_MAX_WIDTH) / 2.0) : edge;
        HeaderGrid().Margin({inset, 0, inset, 0});
        PageContentStack().Margin({inset, 0, inset, 0});
        if (pluginsPageView) winrt::get_self<PluginsPageView>(pluginsPageView)->setContentInsets(inset);
        if (operatingPresenter) operatingPresenter->setContentInsets(inset);

    }

    void MainWindow::applyResponsiveLayout(double width)
    {
        if (width <= 0.0)
            return;

        MainContent().Padding(width < 840.0 ? Thickness{0,48,0,16} : Thickness{0,48,0,24});
        applyLayoutMode();
        const auto horizontalPadding = PageContentStack().Margin().Left + PageContentStack().Margin().Right;
        const auto measuredContentWidth = MainContent().ActualWidth() > 0.0
            ? (std::max)(0.0, MainContent().ActualWidth() - horizontalPadding)
            : width;
        const bool useNarrowSupportLayout = measuredContentWidth < 760.0;
        for (const auto& pair : { std::pair{DashboardInputDeviceGrid(), InputMeterBarHost()},
                                 std::pair{DashboardOutputDeviceGrid(), OutputMeterBarHost()} })
        {
            const bool narrow = (compactLayout ? (std::min)(measuredContentWidth, COMPACT_CONTENT_MAX_WIDTH) : measuredContentWidth) < 760.0;
            pair.first.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromPixels(narrow ? 0 : 380));
            Grid::SetRow(pair.second, narrow ? 1 : 0);
            Grid::SetColumn(pair.second, narrow ? 0 : 1);
            Grid::SetColumnSpan(pair.second, narrow ? 2 : 1);
            pair.second.Margin(narrow ? ThicknessHelper::FromLengths(0, 12, 0, 0) : ThicknessHelper::FromLengths(24, 0, 0, 0));
        }
        const auto configureSupportAction = [useNarrowSupportLayout](ColumnDefinition const& actionColumn,
                                                                     Button const& button)
        {
            actionColumn.Width(GridLengthHelper::FromPixels(useNarrowSupportLayout ? 0.0 : 294.0));
            Grid::SetRow(button, useNarrowSupportLayout ? 1 : 0);
            Grid::SetColumn(button, useNarrowSupportLayout ? 0 : 1);
            Grid::SetColumnSpan(button, useNarrowSupportLayout ? 2 : 1);
            button.Width(useNarrowSupportLayout ? std::numeric_limits<double>::quiet_NaN() : 294.0);
            button.HorizontalAlignment(useNarrowSupportLayout ? HorizontalAlignment::Stretch : HorizontalAlignment::Right);
            button.Margin(useNarrowSupportLayout
                              ? ThicknessHelper::FromLengths(0, 12, 0, 0)
                              : ThicknessHelper::FromUniformLength(0));
        };
        if (Pages().SupportLoaded())
        {
            configureSupportAction(SupportDonateActionColumn(), KoFiButton());
        }
        if (Pages().SupportLoaded())
        {
            configureSupportAction(SupportRepositoryActionColumn(), SupportRepositoryButton());
        }
        if (Pages().SupportLoaded())
        {
            configureSupportAction(SupportShowcaseActionColumn(), SupportShowcaseButton());
        }

        const bool useCompactPluginCards = measuredContentWidth < 680.0;
        if (compactPluginCards != useCompactPluginCards)
        {
            compactPluginCards = useCompactPluginCards;
            renderedRunningPluginLabels.clear();
            renderedInstalledPluginLabels.clear();
            if (currentSection == L"Plugins")
                refreshPluginViews();
        }

        diagnosticsPresenter.resize(measuredContentWidth);
        const bool stackResources = measuredContentWidth < 560;
        DashboardResourcesGrid().RowSpacing(stackResources ? 14 : 0);
        int resourceIndex = 0;
        for (auto card : { DashboardCpuCard(), DashboardVramCard(), DashboardRamCard() }) {
            Grid::SetColumn(card, stackResources ? 0 : resourceIndex);
            Grid::SetRow(card, stackResources ? resourceIndex : 0);
            Grid::SetColumnSpan(card, stackResources ? 3 : 1);
            ++resourceIndex;
        }
        if (Pages().AudioLoaded())
        {
            const bool hasInput = InputChannelGroup().Visibility() == Visibility::Visible;
            const bool hasOutput = OutputChannelGroup().Visibility() == Visibility::Visible;
            const bool sideBySide = measuredContentWidth >= 900 && hasInput && hasOutput;
            const auto rows = ChannelsCard().RowDefinitions();
            const uint32_t rowCount = !sideBySide && hasInput && hasOutput ? 4 : 2;
            while (rows.Size() > rowCount) rows.RemoveAtEnd();
            while (rows.Size() < rowCount) {
                RowDefinition row;
                row.Height(GridLengthHelper::Auto());
                rows.Append(row);
            }
            for (auto card : { InputSettingsCard(), OutputSettingsCard(), InputChannelGroup(), OutputChannelGroup() })
                Grid::SetColumnSpan(card, sideBySide ? 1 : 2);
            Grid::SetColumn(InputSettingsCard(), 0);
            Grid::SetColumn(InputChannelGroup(), 0);
            Grid::SetRow(InputSettingsCard(), 0);
            Grid::SetRow(InputChannelGroup(), 1);
            Grid::SetColumn(OutputSettingsCard(), sideBySide ? 1 : 0);
            Grid::SetColumn(OutputChannelGroup(), sideBySide ? 1 : 0);
            Grid::SetRow(OutputSettingsCard(), sideBySide || !hasInput ? 0 : 2);
            Grid::SetRow(OutputChannelGroup(), sideBySide || !hasInput ? 1 : 3);
        }
        if (Pages().SettingsLoaded())
        {
            // Named elements exist during lazy page creation; Parent() is not
            // assigned until the visual tree has been attached.
            auto page = winrt::get_self<SettingsPageView>(settingsPageView);
            // Keep the picker alongside the heading in both layouts. Reserve
            // room for the description and truncate long driver/device names.
            const auto pickerWidth = (std::clamp)(measuredContentWidth - 360.0, 160.0, 360.0);
            CustomAudioPersistenceGrid().ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromPixels(pickerWidth));
            PreferredDeviceButton().Width(pickerWidth);
            configureSupportAction(page->ModernRepositoryGrid().ColumnDefinitions().GetAt(1), RepositoryButton());
            configureSupportAction(page->OriginalRepositoryGrid().ColumnDefinitions().GetAt(1), OriginalRepositoryButton());
        }
        HeaderActions().Orientation(width < 840.0 ? Orientation::Vertical : Orientation::Horizontal);
    }

    void MainWindow::updateSidebarLayout()
    {
        SidebarToggleButton().Visibility(loadUiSetting(L"Appearance",L"HideSidebarToggle",L"0")==L"1"?Visibility::Collapsed:Visibility::Visible);
        SidebarRail().IsPaneOpen(!sidebarCollapsed);
        BrandTitleText().Visibility(sidebarCollapsed ? Visibility::Collapsed : Visibility::Visible);
        CompactLogoNavItem().Visibility(sidebarCollapsed ? Visibility::Visible : Visibility::Collapsed);
        SidebarToggleText().Visibility(sidebarCollapsed ? Visibility::Collapsed : Visibility::Visible);
        SidebarToggleIcon().Glyph(sidebarCollapsed ? L"\xE72A" : L"\xE72B");
        const auto accessibleName = localization.text(
            sidebarCollapsed ? "navigation.expand" : "navigation.collapse",
            sidebarCollapsed ? L"Expand sidebar" : L"Collapse sidebar");
        SidebarToggleText().Text(accessibleName);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SidebarToggleButton(), accessibleName);
        lightHostModern::ui::HoverHelp::SetToolTip(SidebarToggleButton(), box_value(accessibleName));
    }

    void MainWindow::updateToggleStateLabels()
    {
        const auto onText = localization.text("common.on", L"On");
        const auto offText = localization.text("common.off", L"Off");
        const auto update = [&onText, &offText](TextBlock const& label, ToggleSwitch const& toggle)
        {
            if (label && toggle)
                label.Text(toggle.IsOn() ? onText : offText);
        };

        update(StartWithWindowsStateText(), StartWithWindowsCheckBox());
        update(CloseToTrayStateText(), CloseToTraySwitch());
        update(EnableVst2StateText(), EnableVst2CheckBox());
        update(HideSupportStateText(), HideSupportTabSwitch());
    }

    void MainWindow::syncThemeSelectors(int selectedIndex)
    {
        syncingThemeControls = true;
        if (ThemeModeBox())
            ThemeModeBox().SelectedIndex(selectedIndex);
        syncingThemeControls = false;
    }

    void MainWindow::applyIconMode(std::string const& mode)
    {
        std::wstring asset = L"logo.png";
        std::wstring iconAsset = L"logo.ico";
        if (mode == "white")
        {
            asset = L"logo-white.png";
            iconAsset = L"logo-white.ico";
        }
        else if (mode == "black")
        {
            asset = L"logo-black.png";
            iconAsset = L"logo-black.ico";
        }

        try
        {
            BrandLogoImage().Source(BitmapImage(Windows::Foundation::Uri(L"ms-appx:///Assets/" + asset)));
            CompactLogoIcon().UriSource(Windows::Foundation::Uri(L"ms-appx:///Assets/" + asset));
        }
        catch (...)
        {
        }

        try
        {
            wchar_t modulePath[MAX_PATH] {};
            if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
            {
                std::wstring executablePath(modulePath);
                const auto separator = executablePath.find_last_of(L"\\/");
                if (separator != std::wstring::npos)
                    AppWindow().SetIcon(executablePath.substr(0, separator) + L"\\Assets\\" + iconAsset);
            }
        }
        catch (...)
        {
        }
    }

    void MainWindow::syncIconMode(std::string const& mode)
    {
        if (!IconModeBox()) { if (currentIconMode != mode) { currentIconMode = mode; applyIconMode(mode); } return; }

        int index = 0;
        if (mode == "white")
            index = 1;
        else if (mode == "black")
            index = 2;

        if (currentIconMode == mode && IconModeBox().SelectedIndex() == index)
            return;

        const bool wasSyncing = syncingConfigControls;
        syncingConfigControls = true;
        IconModeBox().SelectedIndex(index);
        syncingConfigControls = wasSyncing;
        currentIconMode = mode;
        applyIconMode(mode);
    }

    void MainWindow::setVisible(UIElement const& element, bool visible)
    {
        if (element) element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::resetRunningPluginDragVisuals()
    {
        for (auto const& item : runningPluginItemBorders)
        {
            if (!item)
                continue;

            item.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(224, 224, 224), makeColorA(160, 82, 101, 125))));
            item.BorderThickness(ThicknessHelper::FromUniformLength(1));
        }
    }

    winrt::fire_and_forget MainWindow::renameAudioChannel(bool input,int channel,int width,hstring label)
    {
        auto lifetime=get_strong();
        try{
            const auto generation=lightHostModern::ipc::parseSnapshotObject(hostConnection->snapshotJson).GetNamedObject(L"audioSelection").GetNamedValue(L"generation");
            const auto result=co_await lightHostModern::ui::editDisplayName(RootLayout(),localization,localization.translatedSource(L"Rename channel"),label);
            if(result){Windows::Data::Json::JsonObject request;using Windows::Data::Json::JsonValue;
                request.SetNamedValue(L"direction",JsonValue::CreateStringValue(input?L"input":L"output"));request.SetNamedValue(L"channel",JsonValue::CreateNumberValue(channel));request.SetNamedValue(L"width",JsonValue::CreateNumberValue(width));request.SetNamedValue(L"name",JsonValue::CreateStringValue(unbox_value<hstring>(result)));request.SetNamedValue(L"expectedGeneration",generation);
                co_await sendCommand("rename-audio-channel:"+to_string(request.Stringify()));
            }
        }catch(...){}
    }

    void MainWindow::syncChannelCheckBoxes(StackPanel const& panel,
        std::vector<ChannelRowData> const& rows,
        std::string const& commandPrefix,
        std::vector<std::string>& renderedKeys)
    {
        const auto keys = channelKeys(rows);
        if (keys == renderedKeys)
        {
            const auto children = panel.Children();
            for (int i = 0; i < (int) rows.size() && i < (int) children.Size(); ++i)
            {
                if (auto rowGrid = children.GetAt(i).try_as<Grid>(); rowGrid && rowGrid.Children().Size()>0)
                if (auto box = rowGrid.Children().GetAt(0).try_as<CheckBox>())
                {
                    auto const& row = rows[(size_t) i];
                    setAudioCheckBoxLabel(box, hs(row.label));
                    box.Tag(box_value(hs(commandPrefix + ":" + std::to_string(row.startIndex) + ":" + std::to_string(row.endIndex) + (row.partial ? ":partial" : ""))));
                    if (row.partial) box.IsChecked(nullptr); else box.IsChecked(row.active);
                }
            }
            return;
        }

        panel.Children().Clear();

        if (rows.empty())
        {
            auto text = TextBlock();
            text.Text(localization.text("audio.channelsUnavailable", L"No channel data available for the current device."));
            text.FontSize(13);
            text.TextWrapping(TextWrapping::Wrap);
            panel.Children().Append(text);
            renderedKeys = keys;
            return;
        }

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto const& row = rows[(size_t) i];
            auto box = CheckBox();
            setAudioCheckBoxLabel(box, hs(row.label));
            box.Tag(box_value(hs(commandPrefix + ":" + std::to_string(row.startIndex) + ":" + std::to_string(row.endIndex) + (row.partial ? ":partial" : ""))));
            if (row.partial) box.IsChecked(nullptr); else box.IsChecked(row.active);
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(box,
                hs(commandPrefix + "-" + std::to_string(row.startIndex) + "-" + std::to_string(row.endIndex)));
            box.HorizontalAlignment(HorizontalAlignment::Stretch);
            box.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
            box.Checked({ this, &MainWindow::ChannelCheckBox_Changed });
            box.Unchecked({ this, &MainWindow::ChannelCheckBox_Changed });
            Grid channelRow;ColumnDefinition labelColumn,actionColumn;labelColumn.Width({1,GridUnitType::Star});actionColumn.Width(GridLengthHelper::Auto());channelRow.ColumnDefinitions().Append(labelColumn);channelRow.ColumnDefinitions().Append(actionColumn);channelRow.Children().Append(box);
            Button rename;rename.Style(Application::Current().Resources().Lookup(box_value(L"SubtleButtonStyle")).as<Style>());FontIcon pencil;pencil.Glyph(L"\xE70F");pencil.FontSize(14);rename.Content(pencil);rename.Width(32);rename.Height(32);rename.Padding({0,0,0,0});rename.VerticalAlignment(VerticalAlignment::Center);
            lightHostModern::ui::HoverHelp::SetToolTip(rename,box_value(localization.translatedSource(L"Rename channel")));Automation::AutomationProperties::SetName(rename,localization.translatedSource(L"Rename channel")+L" "+hs(row.label));
            rename.Click([weak=get_weak(),input=commandPrefix=="set-input-channel",first=row.startIndex,width=row.endIndex-row.startIndex+1,label=hs(row.label)](const auto&,const auto&){if(auto owner=weak.get())owner->renameAudioChannel(input,first,width,label);});
            HoverHelp::current().describe(box);
            channelRow.Children().Append(rename);Grid::SetColumn(rename,1);panel.Children().Append(channelRow);
        }

        renderedKeys = keys;
    }

    void MainWindow::syncChannelToggleButton(Button const& button,
        std::vector<ChannelRowData> const& rows,
        std::string const&)
    {
        const bool hasRows = !rows.empty();
        const bool allActive = hasRows && std::all_of(rows.begin(), rows.end(), [](ChannelRowData const& row)
        {
            return row.active;
        });

        button.IsEnabled(hasRows);
        button.Content(box_value(localization.text(allActive ? "audio.uncheckAll" : "audio.checkAll",
            allActive ? L"Uncheck all" : L"Check all")));
        button.Tag(box_value(hstring(allActive ? L"uncheck" : L"check")));
        lightHostModern::ui::HoverHelp::SetToolTip(button, box_value(allActive ? localization.text("audio.disableChannels", L"Disable every visible channel") : localization.text("audio.enableChannels", L"Enable every visible channel")));
    }

    void MainWindow::syncEnabledAudioChoicesSummary()
    {
        if (!Pages().SettingsLoaded()) return;
        EnabledAudioChoicesSummaryText().Text(localization.text(
            "settings.devices.allEnabled",
            L"Choose which audio backends and devices LightHostModern is allowed to use."));
    }

    void MainWindow::updateDebugControls()
    {
        if (!Pages().SettingsLoaded()) return;
        const bool debug = winUIDebugEnabled();
        DebugSettingsPanel().Visibility(debug ? Visibility::Visible : Visibility::Collapsed);
        DebugStatusText().Text(debug ? localization.text("settings.debugActive", L"Debug console is active for this session. Host and WinUI logs are printed to the attached console.") : localization.text("settings.debugInactive", L"Debug logging is available when launched with --debug."));
        CopyLogsButton().IsEnabled(false);
        SaveLogButton().IsEnabled(false);
    }

    void MainWindow::updateAudioAvailability()
    {
        // Localize named page controls even before its lazy visual tree is attached.
        if (audioPageView) {
            const auto page = winrt::get_self<AudioPageView>(audioPageView);
            page->DevicesTitle().Text(localization.text("audio.devices", L"Devices"));
            page->InputSettingsTitle().Text(localization.text("audio.inputSettings", L"Input settings"));
            page->OutputSettingsTitle().Text(localization.text("audio.outputSettings", L"Output settings"));
            page->BackendLabel().Text(localization.text("dashboard.audioBackend", L"Audio backend"));
            page->InputChannelsTitle().Text(localization.text("audio.inputChannels", L"Input channels"));
            page->OutputChannelsTitle().Text(localization.text("audio.outputChannels", L"Output channels"));
            page->FormatTitle().Text(localization.translatedSource(L"Format"));
            page->OutputDeviceLabel().Text(localization.text("dashboard.outputDevice", L"Output device"));
            page->SampleRateLabel().Text(localization.translatedSource(L"Sample rate"));
            page->BufferSizeLabel().Text(localization.translatedSource(L"Buffer size"));
        }
        const auto state = lightHostModern::ipc::field(hostConnection->snapshotJson, "audioSelection");
        if (state.ValueType() != lightHostModern::ipc::JsonValueType::Object) return;
        const auto selection = state.GetObject();
        const bool available = selection.GetNamedBoolean(L"processingAvailable", false);
        const bool suspended = selection.GetNamedBoolean(L"suspended", false);
        const bool unconfigured = selection.GetNamedString(L"recoveryState", L"") == L"unconfigured";
        const auto message = localization.text(suspended ? "audio.suspendedNotice" : unconfigured ? "audio.unconfiguredNotice" : "audio.unavailableNotice",
            suspended ? L"Audio is stopped. Select a device to start processing." : unconfigured ? L"Select an audio device to start processing." : L"The selected audio device is unavailable. LightHostModern is not processing audio.");
        for (auto notice : { DashboardAudioNotice(), AudioUnavailableNotice() })
            if (notice) { notice.Message(message); notice.IsOpen(!available && hostConnection->connected); }
        const auto previous = syncingHostControls; syncingHostControls = true;
        for(auto box:{audioBackendBox,inputBox,outputBox,sampleRateBox,bufferSizeBox})if(box)HoverHelp::current().describe(box);
        const auto key = selection.GetNamedString(L"preferenceKey", L"");
        channelPreferenceKey = std::wstring(key);
        const auto section = L"AudioChannels." + channelPreferenceKey;
        inputChannelPairs = !key.empty() && loadUiSetting(section.c_str(), L"InputMode", L"Individual") == L"Pairs";
        outputChannelPairs = key.empty() || loadUiSetting(section.c_str(), L"OutputMode", L"Pairs") != L"Individual";
        for (bool input : {true, false}) {
            const auto control = input ? InputModeBox() : OutputModeBox();
            if (!control) continue;
            const bool pending = commandIntents.pending(input ? "ui-audio-intent:monoInputs" : "ui-audio-intent:monoOutput");
            const bool mono = extractBool(hostConnection->snapshotJson, input ? "monoInputs" : "monoOutput");
            control.Items().GetAt(0).as<ComboBoxItem>().Content(box_value(localization.translatedSource(L"Stereo")));
            control.Items().GetAt(1).as<ComboBoxItem>().Content(box_value(localization.translatedSource(L"Mono")));
            if (!pending) control.SelectedIndex(mono ? 1 : 0);
            control.IsEnabled(hasFullSnapshot && !windowClosing && !pendingNormalClose && !key.empty());
            const auto title = localization.text(input ? "audio.inputMode" : "audio.mainOutputMode", input ? L"Input mode" : L"Output mode");
            const auto page = winrt::get_self<AudioPageView>(audioPageView);
            (input ? page->InputMonoLabel() : page->OutputMonoLabel()).Text(title);
            auto help = localization.text(input ? "audio.inputMode.tooltip" : "audio.mainOutputMode.tooltip", L"");
            if (!input && !selection.GetNamedBoolean(L"mainOutputPairActive", false))
                help = help + L" " + localization.text("audio.monoOutput.inactive", L"The main pair is not fully active. This preference is saved; no output mixing is applied.");
            lightHostModern::ui::HoverHelp::SetToolTip(control, box_value(help));
            Automation::AutomationProperties::SetHelpText(control, help);
            Automation::AutomationProperties::SetName(control, title);
            const auto grouping = input ? InputGroupingBox() : OutputGroupingBox();
            (input ? page->InputSelectionLabel() : page->OutputSelectionLabel()).Text(localization.text("audio.channelSelection", L"Channel selection"));
            grouping.Items().GetAt(0).as<ComboBoxItem>().Content(box_value(localization.text("audio.channelsPairs", L"Pairs")));
            grouping.Items().GetAt(1).as<ComboBoxItem>().Content(box_value(localization.text("audio.channelsIndividual", L"Individual")));
            grouping.SelectedIndex((input ? inputChannelPairs : outputChannelPairs) ? 0 : 1);
            grouping.IsEnabled(!key.empty());
            const auto count = extractStringArray(hostConnection->snapshotJson, input ? "inputChannelNames" : "outputChannelNames").size();
            grouping.Visibility(count > 1 ? Visibility::Visible : Visibility::Collapsed);
            const auto groupingHelp = localization.text("audio.channelSelection.tooltip", L"Choose channels individually or in consecutive pairs. Changing this view preserves the enabled channels.");
            lightHostModern::ui::HoverHelp::SetToolTip(grouping, box_value(groupingHelp));
            Automation::AutomationProperties::SetHelpText(grouping, groupingHelp);
            Automation::AutomationProperties::SetName(grouping, localization.text(input ? "audio.inputSelection" : "audio.outputSelection", L"Channel selection"));
        }
        syncingHostControls = previous;
    }

    winrt::fire_and_forget MainWindow::AudioMode_Changed(IInspectable sender, SelectionChangedEventArgs)
    { if (sender.as<ComboBox>().SelectedIndex() >= 0) changeMono(sender.as<ComboBox>() == InputModeBox()); co_return; }

    winrt::fire_and_forget MainWindow::changeMono(bool input)
    {
        auto lifetime=get_strong();
        if(syncingHostControls||!hasFullSnapshot||pendingNormalClose)co_return;
        const bool desired=(input?InputModeBox():OutputModeBox()).SelectedIndex()==1;
        co_await changeAudioSelection(input?"monoInputs":"monoOutput",desired?"1":"0");
        if(!windowClosing)updateAudioAvailability();
    }

    winrt::fire_and_forget MainWindow::ChannelGrouping_Changed(IInspectable sender, SelectionChangedEventArgs)
    {
        if (syncingHostControls || channelPreferenceKey.empty() || !hasFullSnapshot) co_return;
        auto lifetime = get_strong();
        const auto box = sender.as<ComboBox>();
        if (box.SelectedIndex() < 0) co_return;
        const bool input = box == InputGroupingBox(), pairs = box.SelectedIndex() == 0;
        if (pairs == (input ? inputChannelPairs : outputChannelPairs)) co_return;
        (input ? inputChannelPairs : outputChannelPairs) = pairs;
        saveUiSetting((L"AudioChannels." + channelPreferenceKey).c_str(), input ? L"InputMode" : L"OutputMode", pairs ? L"Pairs" : L"Individual");
        // The grouping preference is already saved locally. A later snapshot
        // also adopts it; it must never wait behind an outstanding host command.
        if (!windowClosing && !pendingNormalClose) co_await refreshSnapshot(true);
    }

    void MainWindow::updateGlobalAudioControls()
    {
        for (auto button : { RunningGlobalMuteButton() })
        {
            if (!button) continue;
            button.IsChecked(globalMuted);
            // Keep keyboard focus while the serialized command is pending.
            // The click handler already rejects competing commands.
            button.IsEnabled(hasFullSnapshot && !windowClosing);
        }
        for (auto button : { RunningGlobalBypassButton() })
        {
            if (!button) continue;
            button.IsChecked(globalBypassed);
            button.IsEnabled(hasFullSnapshot && !windowClosing);
        }
    }

    winrt::fire_and_forget MainWindow::GlobalAudioControl_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        auto button = sender.try_as<ToggleButton>();
        if (!button) co_return;
        const bool value = unbox_value_or<bool>(button.IsChecked(), false);
        const auto tag = unbox_value_or<hstring>(button.Tag(), L"");
        if (!hasFullSnapshot || pendingNormalClose)
        {
            updateGlobalAudioControls();
            co_return;
        }
        const auto command = tag == L"mute" ? "set-global-mute:" : "set-global-bypass:";
        co_await sendCommand(std::string(command) + (value ? "1" : "0"));

    }

    bool MainWindow::isMinimized() const
    {
        const auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        return !AppWindow().IsVisible() || (presenter && presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Minimized);
    }

    winrt::fire_and_forget MainWindow::heartbeat()
    {
        auto lifetime = get_strong();
        if (heartbeatInProgress || windowClosing) co_return;
        heartbeatInProgress = true;
        const auto response = winrt::to_string(co_await hostConnection->requestAsync("hello"));
        heartbeatInProgress = false;
        if (windowClosing) co_return;
        if (highContrastActive != accessibilitySettings.HighContrast() || preferDarkFallback != (RootLayout().ActualTheme() == ElementTheme::Dark)) updateThemeVisuals();
        if (response.empty() || extractString(response, "status") == "error")
        { hostConnection->connected = false; hostConnection->snapshotNeeded = true; hasFullSnapshot = false; updateGlobalAudioControls(); }
        else if (extractString(response, "hostSession") != hostConnection->session) hostConnection->snapshotNeeded = true;
    }

    winrt::fire_and_forget MainWindow::receiveHostEvents()
    {
        auto lifetime = get_strong();
        const winrt::apartment_context uiApartment;
        while (!windowClosing)
        {
            try
            {
                if (hostConnection->session.empty() || !hostConnection->connected)
                {
                    co_await winrt::resume_after(std::chrono::seconds(1));
                    co_await uiApartment;
                    continue;
                }
                const auto response = winrt::to_string(co_await hostConnection->pollEventsAsync());
                if (windowClosing) co_return;
                if (hostConnection->snapshotNeeded && !commandInProgress && !snapshotInProgress && !comboDropDownOpen)
                    co_await refreshSnapshot();
                if (!isMinimized() && scanDialogOpen && GetTickCount64() - lastScanStatusTick >= 200)
                { lastScanStatusTick = GetTickCount64(); refreshPluginScanStatus(); }
                if (response.empty())
                {
                    co_await winrt::resume_after(std::chrono::seconds(1));
                    co_await uiApartment;
                }
            }
            catch (...) { hostConnection->connected = false; hostConnection->snapshotNeeded = true; }
        }
    }

    bool MainWindow::isControlVisible(FrameworkElement control)
    {
        if (!control || !control.IsLoaded() || control.Visibility() != Visibility::Visible || control.ActualHeight() <= 0) return false;
        const auto point = control.TransformToVisual(ContentScrollViewer()).TransformPoint({0, 0});
        return point.Y + control.ActualHeight() > 0 && point.Y < ContentScrollViewer().ActualHeight();
    }

    void MainWindow::updateMeters(const std::string& json)
    {
        // Structural snapshots may arrive later than live level readings.
        // Only the dedicated meter transport updates the two volume bars.
        sessionStatusPresenter.update(json, localization, SessionStatusBanner(), SessionPendingText(), RetrySessionSaveButton());
    }

    winrt::fire_and_forget MainWindow::refreshMeterLevels()
    {
        auto lifetime = get_strong();
        if (windowClosing || meterReadInProgress || isMinimized() || currentSection != L"Dashboard"
            || !(isControlVisible(InputMeterBarHost()) || isControlVisible(OutputMeterBarHost()))) co_return;
        meterReadInProgress = true;
        const auto started = GetTickCount64();
        const auto session = hostConnection->session;
        std::string json;
        try { json = to_string(co_await hostConnection->meterLevelsAsync()); } catch (...) {}
        meterReadInProgress = false;
        if (!windowClosing && (!hostConnection->connected || json.empty())) { inputMeter.unavailable(); outputMeter.unavailable(); }
        if (windowClosing || isMinimized() || currentSection != L"Dashboard" || GetTickCount64() - started > 150
            || extractString(json, "status") != "ok" || session != hostConnection->session
            || extractString(json, "hostSession") != session) co_return;
        const auto selection = lightHostModern::ipc::field(hostConnection->snapshotJson, "audioSelection");
        if (!hostConnection->connected || (selection.ValueType() == lightHostModern::ipc::JsonValueType::Object
            && !selection.GetObject().GetNamedBoolean(L"processingAvailable", false)))
        { inputMeter.unavailable(); outputMeter.unavailable(); co_return; }
        if (isControlVisible(InputMeterBarHost())) inputMeter.update(extractNumber(json, "inputPeak"));
        if (isControlVisible(OutputMeterBarHost())) outputMeter.update(extractNumber(json, "outputPeak"));
    }

    void MainWindow::updateDashboardSummary(const std::string& json)
    {
        const auto operating = lightHostModern::ipc::parseSnapshotObject(json).GetNamedObject(L"operating", JsonObject{});
        DashboardOperatingModeText().Text(localization.translatedSource(operating.GetNamedString(L"mode", L"list") == L"chain" ? L"Chain" : L"List"));
        const auto active = operating.GetNamedString(L"activeProfile", L"");
        auto profileName = localization.text("common.unavailable", L"Unavailable");
        for (const auto& entry : operating.GetNamedArray(L"profiles", JsonArray{})) {
            const auto profile = entry.GetObject();
            if (profile.GetNamedString(L"id", L"") == active) {
                profileName = profile.GetNamedBoolean(L"isDefault", false) ? localization.translatedSource(L"Default") : profile.GetNamedString(L"name", L"");
                break;
            }
        }
        DashboardActiveProfileText().Text(profileName);
        lightHostModern::ui::HoverHelp::SetToolTip(DashboardActiveProfileText(), box_value(profileName));
        DashboardVersionText().Text(hstring(L"v") + APP_VERSION);
        const bool enabled = diagnosticsEnabled;
        const auto help = [&](const FrameworkElement& card, const char* key) {
            const auto description = localization.text(enabled ? key : "dashboard.metricsDisabled.tooltip");
            lightHostModern::ui::HoverHelp::SetToolTip(card, box_value(description));
            Automation::AutomationProperties::SetHelpText(card, description);
        };
        help(DashboardCpuCard(), "dashboard.cpuUsage.tooltip");
        help(DashboardRamCard(), "dashboard.ramUsage.tooltip");
        help(DashboardVramCard(), "dashboard.vramUsage.tooltip");
    }

    void MainWindow::updateDashboardPerformance(const std::string& json)
    {
        const auto unavailable = localization.text("common.unavailable", L"Unavailable");
        if (!diagnosticsEnabled) {
            const auto disabled = localization.text("dashboard.metricsDisabled", L"Disabled");
            for (auto value : { DashboardCpuText(), DashboardRamText(), DashboardVramText() }) value.Text(disabled);
            return;
        }
        const auto cpu = dashboardCpuSampler.sample(lightHostModern::processCpuTicks(), GetTickCount64(), lightHostModern::processorCount());
        const auto memory = lightHostModern::processMemory();
        const auto total = [&](const char* host, const char* worker, std::optional<double> local) -> std::optional<double> {
            const auto h = lightHostModern::ipc::field(json, host), w = lightHostModern::ipc::field(json, worker);
            if (!local || h.ValueType() != JsonValueType::Number || w.ValueType() != JsonValueType::Number) return {};
            return h.GetNumber() + w.GetNumber() + *local;
        };
        const auto appCpu = total("hostCpuPercent", "workerCpuPercent", cpu);
        const auto appRam = total("hostResidentMiB", "workerResidentMiB", memory.resident ? std::optional<double>(*memory.resident / 1048576.0) : std::nullopt);
        DashboardCpuText().Text(appCpu ? hs(formatNumber(*appCpu, 1) + "%") : unavailable);
        DashboardRamText().Text(appRam ? hs(formatNumber(*appRam, 1) + " MiB") : unavailable);
    }

    winrt::fire_and_forget MainWindow::refreshDashboardGpu(const std::string& json)
    {
        if (dashboardGpuInProgress || windowClosing || !diagnosticsEnabled) co_return;
        auto lifetime = get_strong();
        dashboardGpuInProgress = true;
        const winrt::apartment_context ui;
        const auto hostPid = extractNumber(hostConnection->snapshotJson, "hostPid");
        std::set<DWORD> processes{GetCurrentProcessId()};
        if (hostPid > 0 && hostPid <= UINT32_MAX) processes.insert(static_cast<DWORD>(hostPid));
        for (const auto& value : lightHostModern::ipc::extractArray(json, "isolatedPlugins")) {
            if (value.ValueType() != JsonValueType::Object) continue;
            const auto pid = value.GetObject().GetNamedNumber(L"pid", 0);
            if (pid > 0 && pid <= UINT32_MAX) processes.insert(static_cast<DWORD>(pid));
        }
        std::optional<uint64_t> bytes;
        try { co_await winrt::resume_background(); bytes = dashboardGpuSampler.sample(processes); } catch (...) {}
        co_await ui;
        dashboardGpuInProgress = false;
        if (windowClosing || !diagnosticsEnabled || currentSection != L"Dashboard" || isMinimized()
            || hostPid != extractNumber(hostConnection->snapshotJson, "hostPid")) co_return;
        DashboardVramText().Text(bytes ? hs(formatNumber(*bytes / 1048576.0, 1) + " MiB") : localization.text("common.unavailable", L"Unavailable"));
    }

    winrt::fire_and_forget MainWindow::refreshTelemetry()
    {
        auto lifetime = get_strong();
        if (windowClosing) co_return;
        const auto tick = GetTickCount64();
        if (tick - lastHeartbeatTick >= 5000) { lastHeartbeatTick = tick; heartbeat(); }
        if (hostConnection->snapshotNeeded && !snapshotInProgress && !commandInProgress && !comboDropDownOpen
            && tick - lastSnapshotAttemptTick >= (hostConnection->connected ? 100 : 5000))
            refreshSnapshot();
        if (isMinimized()) co_return;
        if (scanDialogOpen && tick - lastScanStatusTick >= 200)
        { lastScanStatusTick = tick; refreshPluginScanStatus(); }
        const bool metersVisible = (currentSection == L"Dashboard" && (isControlVisible(InputMeterBarHost()) || isControlVisible(OutputMeterBarHost())));
        const bool diagnosticsVisible = currentSection == L"Diagnostics" && diagnosticsPageView
            && isControlVisible(winrt::get_self<DiagnosticsPageView>(diagnosticsPageView)->DiagnosticsPanel());
        const bool dashboardVisible = currentSection == L"Dashboard" && isControlVisible(DashboardResourcesGrid());
        const bool diagnosticsDue = diagnosticsVisible && tick - lastDiagnosticTick >= 1000;
        if (!diagnosticsEnabled || (!diagnosticsVisible && !dashboardVisible) || tick - lastDiagnosticTick < 1000) co_return;
        if (telemetryInProgress || windowClosing || commandInProgress) co_return;
        lastDiagnosticTick = tick;
        telemetryInProgress = true;
        const auto json = winrt::to_string(co_await hostConnection->requestAsync("telemetry"));
        telemetryInProgress = false;
        if (windowClosing) co_return;
        if (extractString(json, "status") == "error")
        {
            if (dashboardVisible) { updateDashboardPerformance("{}"); DashboardVramText().Text(localization.text("common.unavailable", L"Unavailable")); }
            HeaderStatusText().Text(ipcErrorText(json, localization));
            hasFullSnapshot = false;
            updateGlobalAudioControls();
            co_return;
        }
        if (json.empty())
        {
            if (dashboardVisible) { updateDashboardPerformance("{}"); DashboardVramText().Text(localization.text("common.unavailable", L"Unavailable")); }
            if (!hasFullSnapshot)
                refreshSnapshot();
            co_return;
        }

        globalMuted = extractBool(json, "globalMuted", globalMuted);
        const auto previousBypass=globalBypassed;
        globalBypassed = extractBool(json, "globalBypassed", globalBypassed);
        if(previousBypass!=globalBypassed)refreshPluginViews();
        updateGlobalAudioControls();
        const auto status = extractString(json, "status", "unknown");
        const auto backend = extractString(json, "backend", "none");
        const auto device = extractString(json, "deviceName", "none");
        const auto activePlugins = (int) extractNumber(json, "activePluginCount", activePluginCount);
        const auto knownPlugins = (int) extractNumber(json, "knownPlugins", installedPluginCount);
        const auto sampleRate = extractNumber(json, "sampleRate");
        const auto bufferSize = (int) extractNumber(json, "bufferSize");
        const auto inputChannels = (int) extractNumber(json, "inputChannels");
        const auto outputChannels = (int) extractNumber(json, "outputChannels");
        const auto chainVersion = (int64_t) extractNumber(json, "chainVersion", -1);
        const auto pluginDbVersion = (int64_t) extractNumber(json, "pluginDbVersion", -1);
        const auto audioConfigVersion = (int64_t) extractNumber(json, "audioConfigVersion", -1);

        ConnectionStatusText().Text(status == "online" ? localization.text("connection.online", L"Online") : localization.text("connection.offline", L"Disconnected"));
        HeaderStatusText().Text(status == "online" ? localization.text("connection.online", L"Online") : localization.text("connection.offline", L"Disconnected"));
        SidebarStatusDetailText().Text(hs(backend + " - " + device));

        DashboardDeviceTypeText().Text(backend == "none" ? localization.text("common.none", L"None") : hs(backend));
        DashboardDeviceText().Text(device == "none" || device.empty() ? localization.text("common.none", L"None") : hs(device));
        DashboardRoutingText().Text(device == "none" || device.empty() ? localization.text("common.none", L"None") : hs(device));
        DashboardChannelsText().Text(localization.format("audio.channelSummary", L"Input: {0} ch / Output: {1} ch", { std::to_wstring(inputChannels), std::to_wstring(outputChannels) }));
        DashboardFormatText().Text(sampleRate > 0 && bufferSize > 0 ? hs(formatNumber(sampleRate, 0) + " Hz / " + std::to_string(bufferSize) + " " + to_string(localization.text("common.samples", L"samples"))) : localization.text("common.unavailable", L"Unavailable"));
        ActivePluginsText().Text(hs(std::to_string(activePlugins)));
        KnownPluginsText().Text(hs(std::to_string(knownPlugins)));

        // Navigation, scrolling or minimization may change while IPC is pending.
        if (!isMinimized())
        {
            if (dashboardVisible && currentSection == L"Dashboard" && isControlVisible(DashboardResourcesGrid())) {
                updateDashboardPerformance(json);
                refreshDashboardGpu(json);
            }
            if (metersVisible && currentSection == L"Dashboard"
                && (isControlVisible(InputMeterBarHost()) || isControlVisible(OutputMeterBarHost()))) updateMeters(json);
            if (diagnosticsEnabled && diagnosticsDue && currentSection == L"Diagnostics" && diagnosticsPageView
                && isControlVisible(winrt::get_self<DiagnosticsPageView>(diagnosticsPageView)->DiagnosticsPanel()))
                diagnosticsPresenter.update(json, localization);
        }

        const bool stateChanged = !hasFullSnapshot
            || chainVersion != lastChainVersion
            || pluginDbVersion != lastPluginDbVersion
            || audioConfigVersion != lastAudioConfigVersion;
        if (stateChanged)
            refreshSnapshot();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::refreshSnapshot(bool fromCache, uint64_t deadline)
    {
        auto lifetime = get_strong();
        if (snapshotInProgress || windowClosing) co_return;
        snapshotInProgress = true;
        lastSnapshotAttemptTick = GetTickCount64();
        std::string json;
        try { json = fromCache && !hostConnection->snapshotJson.empty() ? hostConnection->snapshotJson
            : winrt::to_string(co_await hostConnection->snapshotAsync(deadline)); }
        catch (...) { hostConnection->snapshotNeeded = true; }
        snapshotInProgress = false;
        if (windowClosing) co_return;
        for (const auto& result : hostTransport->takeLateResults())
            if (extractString(result, "status") == "error") showNotification(ipcErrorText(result, localization).c_str());
        if (Pages().PluginsLoaded())
        {
            RunningPluginsListView().IsHitTestVisible(!commandInProgress);
        }
        if (Pages().PluginsLoaded())
        {
            InstalledPluginsListView().IsHitTestVisible(!commandInProgress);
        }
        if (extractString(json, "status") == "error")
        {
            HeaderStatusText().Text(ipcErrorText(json, localization));
            hasFullSnapshot = false;
            updateGlobalAudioControls();
            co_return;
        }
        if (json.empty())
        {
            hasFullSnapshot = false;
            hostConnection->connected = false;
            hostConnection->snapshotNeeded = true;
            updateGlobalAudioControls();
            ConnectionStatusText().Text(localization.text("ipc.unavailable", L"Host unavailable"));
            HeaderStatusText().Text(localization.text("ipc.unavailable", L"Host unavailable"));
            co_return;
        }

        syncDiagnosticsSetting(extractBool(json, "diagnosticsEnabled", true));
        if (operatingPresenter) operatingPresenter->update(json);
        applyLayoutMode();
        globalMuted = extractBool(json, "globalMuted", globalMuted);
        globalBypassed = extractBool(json, "globalBypassed", globalBypassed);
        updateGlobalAudioControls();
        const auto status = extractString(json, "status", "unknown");
        const auto backend = extractString(json, "backend", "none");
        const auto device = extractString(json, "deviceName", "none");
        const auto activePlugins = (int) extractNumber(json, "activePluginCount");
        const auto knownPlugins = (int) extractNumber(json, "knownPlugins");
        const auto sampleRate = extractNumber(json, "sampleRate");
        const auto bufferSize = (int) extractNumber(json, "bufferSize");
        const auto inputChannels = (int) extractNumber(json, "inputChannels");
        const auto outputChannels = (int) extractNumber(json, "outputChannels");
        const auto reloads = (int) extractNumber(json, "chainReloads");
        const auto flushes = (int) extractNumber(json, "settingsFlushes");
        lastChainVersion = (int64_t) extractNumber(json, "chainVersion", (double) lastChainVersion);
        lastPluginDbVersion = (int64_t) extractNumber(json, "pluginDbVersion", (double) lastPluginDbVersion);
        lastAudioConfigVersion = (int64_t) extractNumber(json, "audioConfigVersion", (double) lastAudioConfigVersion);
        hasFullSnapshot = true;
        updateAudioAvailability();
        if (!updateCheckStarted) { updateCheckStarted = true; checkForUpdatesAsync(); }
        updateGlobalAudioControls();
        const auto startWithWindows = extractBool(json, "startWithWindows");
        const auto closeBehavior = extractString(json, "closeBehavior", "tray");
        const auto trayIconMode = extractString(json, "trayIconMode", "color");
        const auto vst2HostAvailable = extractBool(json, "vst2HostAvailable");
        const auto vst2RuntimeEnabled = extractBool(json, "vst2RuntimeEnabled");
        const auto vst2HostEnabled = extractBool(json, "vst2HostEnabled");
        auto allPluginRows = extractActivePluginRows(json);
        const auto sessionState = lightHostModern::ipc::field(json, "session");
        sessionWritable = sessionState.ValueType() == lightHostModern::ipc::JsonValueType::Object
            && extractBool(to_string(sessionState.Stringify()), "writable");
        runningPage.allowChanges = installedPage.allowChanges = sessionWritable;
        auto allKnownPluginRows = extractKnownPluginRows(json);
        applyInstalledPluginRuntimeStatus(allKnownPluginRows, allPluginRows);
        activePluginIdentityKeys.clear();
        for (auto const& plugin : allPluginRows)
            activePluginIdentityKeys.push_back(pluginIdentityKey(plugin));
        knownPluginIdentityKeys.clear();
        knownPluginDisplayNames.clear();
        for (auto const& plugin : allKnownPluginRows)
        {
            knownPluginIdentityKeys.push_back(pluginIdentityKey(plugin));
            knownPluginDisplayNames.push_back(utf8ToWide(plugin.name));
        }
        const auto backendNames = extractStringArray(json, "backendNames");
        const auto inputDeviceNames = extractStringArray(json, "inputDeviceNames");
        const auto outputDeviceNames = extractStringArray(json, "outputDeviceNames");
        const auto inputChannelNames = extractStringArray(json, "inputChannelNames");
        const auto outputChannelNames = extractStringArray(json, "outputChannelNames");
        const auto activeInputChannels = extractBoolArray(json, "activeInputChannels");
        const auto activeOutputChannels = extractBoolArray(json, "activeOutputChannels");
        const auto sampleRateValues = extractNumberArray(json, "sampleRates");
        const auto bufferSizeValues = extractNumberArray(json, "bufferSizes");
        const auto currentBackendIndex = (int) extractNumber(json, "currentBackendIndex", -1);
        const auto currentInputDeviceIndex = (int) extractNumber(json, "currentInputDeviceIndex", -1);
        const auto currentOutputDeviceIndex = (int) extractNumber(json, "currentOutputDeviceIndex", -1);
        const auto audioPersistenceMode = extractString(json, "audioPersistenceMode", "disabled");
        const auto audioPersistenceRetrySeconds = (int) extractNumber(json, "audioPersistenceRetrySeconds", 5);
        const auto audioPersistenceRetryAttempts = (int) extractNumber(json, "audioPersistenceRetryAttempts", 10);
        const auto audioPersistenceCustomBackend = extractString(json, "audioPersistenceCustomBackend");
        const auto audioPersistenceCustomInputDevice = extractString(json, "audioPersistenceCustomInputDevice");
        const auto audioPersistenceCustomOutputDevice = extractString(json, "audioPersistenceCustomOutputDevice");
        const auto recoveryState = extractString(json, "recoveryState", "running");
        const auto recoveryMessage = extractString(json, "recoveryMessage");
        const auto recoveryAttempt = (int) extractNumber(json, "recoveryAttempt", 0);
        const auto recoveryMaxAttempts = (int) extractNumber(json, "recoveryMaxAttempts", audioPersistenceRetryAttempts);
        const auto recoveryTargetBackend = extractString(json, "recoveryTargetBackend");
        const auto recoveryTargetInputDevice = extractString(json, "recoveryTargetInputDevice");
        const auto recoveryTargetOutputDevice = extractString(json, "recoveryTargetOutputDevice");
        ConnectionStatusText().Text(status == "online" ? localization.text("connection.online", L"Online") : localization.text("connection.offline", L"Disconnected"));
        HeaderStatusText().Text(status == "online" ? localization.text("connection.online", L"Online") : localization.text("connection.offline", L"Disconnected"));
        SidebarStatusDetailText().Text(hs(backend + " - " + device));

        DashboardDeviceTypeText().Text(backend == "none" ? localization.text("common.none", L"None") : hs(backend));
        DashboardDeviceText().Text(device == "none" || device.empty() ? localization.text("common.none", L"None") : hs(device));
        const auto isAsioBackend = backend == "ASIO";
        const auto editingAsioBackend = currentBackendIndex >= 0
            && currentBackendIndex < static_cast<int>(backendNames.size())
            && backendNames[static_cast<size_t>(currentBackendIndex)] == "ASIO";
        asioDeviceMode = editingAsioBackend;
        const auto inputDeviceText = inputDeviceNames.empty() || currentInputDeviceIndex < 0 || currentInputDeviceIndex >= (int) inputDeviceNames.size()
            ? device
            : inputDeviceNames[(size_t) currentInputDeviceIndex];
        const auto outputDeviceText = outputDeviceNames.empty() || currentOutputDeviceIndex < 0 || currentOutputDeviceIndex >= (int) outputDeviceNames.size()
            ? device
            : outputDeviceNames[(size_t) currentOutputDeviceIndex];
        currentAudioBackendName = backend;
        currentAudioInputDeviceName = isAsioBackend ? device : inputDeviceText;
        currentAudioOutputDeviceName = isAsioBackend ? device : outputDeviceText;

        DashboardRoutingTitleText().Text(localization.text(isAsioBackend ? "audio.device" : "dashboard.routing",
            isAsioBackend ? L"Device" : L"Input and Output"));
        DashboardInputDeviceLabel().Text(localization.text(isAsioBackend ? "audio.device" : "dashboard.inputDevice",
            isAsioBackend ? L"Device" : L"Input device"));
        DashboardOutputDeviceLabel().Text(localization.text("dashboard.outputDevice", L"Output device"));
        DashboardInputDeviceText().Text(device == "none" ? localization.text("common.none", L"None") : hs(isAsioBackend ? device : inputDeviceText));
        DashboardOutputDeviceText().Text(device == "none" ? localization.text("common.none", L"None") : hs(outputDeviceText));
        setVisible(DashboardOutputDeviceGrid(), true);
        DashboardRoutingText().Text(device == "none" || device.empty() ? localization.text("common.none", L"None") : hs(device));
        DashboardChannelsText().Text(localization.format("audio.channelSummary", L"Input: {0} ch / Output: {1} ch", { std::to_wstring(inputChannels), std::to_wstring(outputChannels) }));
        DashboardFormatText().Text(sampleRate > 0 && bufferSize > 0 ? hs(formatNumber(sampleRate, 0) + " Hz / " + std::to_string(bufferSize) + " " + to_string(localization.text("common.samples", L"samples"))) : localization.text("common.unavailable", L"Unavailable"));
        ActivePluginsText().Text(hs(std::to_string(activePlugins)));
        updateDashboardSummary(json);
        KnownPluginsText().Text(hs(std::to_string(knownPlugins)));

        updateMeters(json);

        syncingHostControls = true;
        if (Pages().AudioLoaded())
        {
            setComboItems(AudioBackendBox(), backendNames, currentBackendIndex);
        }
        const auto& asioDeviceNames = outputDeviceNames.empty() ? inputDeviceNames : outputDeviceNames;
        const auto asioDeviceIndex = currentOutputDeviceIndex >= 0 ? currentOutputDeviceIndex : currentInputDeviceIndex;
        if (Pages().AudioLoaded())
        {
            const auto& names = editingAsioBackend ? asioDeviceNames : inputDeviceNames;
            const int selected = editingAsioBackend ? asioDeviceIndex : currentInputDeviceIndex;
            lightHostModern::ui::AudioPageController::devices(InputBox(), names,
                selected >= 0 && selected < static_cast<int>(names.size()) ? names[selected] : "", localization.text("common.none", L"None"));
        }
        if (Pages().AudioLoaded())
        {
            lightHostModern::ui::AudioPageController::devices(OutputBox(), outputDeviceNames,
                currentOutputDeviceIndex >= 0 && currentOutputDeviceIndex < static_cast<int>(outputDeviceNames.size()) ? outputDeviceNames[currentOutputDeviceIndex] : "",
                localization.text("common.none", L"None"));
        }
        if (Pages().AudioLoaded())
        {
            // Backend selection is also the entry point out of a suspended or
            // unavailable device. Keep it reachable when there are no I/O rows.
            setVisible(DeviceRoutingCard(), true);
        }
        if (Pages().AudioLoaded())
        {
            InputDeviceLabel().Text(localization.text(editingAsioBackend ? "audio.device" : "dashboard.inputDevice",
                editingAsioBackend ? L"Device" : L"Input device"));
        }
        if (Pages().AudioLoaded())
        {
            setVisible(InputDeviceRow(), editingAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(InputDeviceLabel(), editingAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(InputBox(), editingAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(OutputDeviceRow(), !editingAsioBackend && !outputDeviceNames.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(OutputDeviceLabel(), !editingAsioBackend && !outputDeviceNames.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(OutputBox(), !editingAsioBackend && !outputDeviceNames.empty());
        }

        const auto deviceAliases=lightHostModern::ipc::parseSnapshotObject(json).GetNamedObject(L"audioDeviceAliases",JsonObject{});
        const auto deviceLabel=[&](std::string const& backend,std::string const& role,std::string const& original){auto alias=deviceAliases.GetNamedString(hs(backend+"|"+role+"|"+original),L"");return alias.empty()?hs(original):alias;};
        for(bool input:{true,false}){auto box=input?InputBox():OutputBox();if(!box)continue;for(const auto& value:box.Items())if(auto item=value.try_as<ComboBoxItem>()){const auto original=to_string(unbox_value<hstring>(item.Tag()));if(!original.empty())item.Content(box_value(deviceLabel(currentAudioBackendName,editingAsioBackend?"device":input?"input":"output",original)));}}
        if(device!="none"){DashboardInputDeviceText().Text(deviceLabel(backend,isAsioBackend?"device":"input",isAsioBackend?device:inputDeviceText));DashboardOutputDeviceText().Text(deviceLabel(backend,isAsioBackend?"device":"output",outputDeviceText));}
        auto groupedOutputChannels = groupedChannelRows(backend, outputChannelNames, activeOutputChannels, false, outputChannelPairs, localization);
        auto groupedInputChannels = groupedChannelRows(backend, inputChannelNames, activeInputChannels, true, inputChannelPairs, localization);
        const auto aliases=lightHostModern::ipc::parseSnapshotObject(json).GetNamedObject(L"audioConfig",Windows::Data::Json::JsonObject{}).GetNamedObject(L"channelAliases",Windows::Data::Json::JsonObject{});
        const auto applyAliases=[&](auto& rows,const wchar_t* direction){const bool input=std::wstring(direction)==L"input";auto names=aliases.GetNamedObject(direction,JsonObject{});JsonArray source;for(auto const& name:input?inputChannelNames:outputChannelNames)source.Append(JsonValue::CreateStringValue(hs(name)));for(auto& row:rows)row.label=to_string(lightHostModern::ui::audioChannelName(source,names,row.startIndex,row.endIndex-row.startIndex+1,input,localization));};
        applyAliases(groupedInputChannels,L"input");applyAliases(groupedOutputChannels,L"output");
        currentOutputChannelRows = groupedOutputChannels;
        currentInputChannelRows = groupedInputChannels;
        if (Pages().AudioLoaded())
        {
            setVisible(ChannelsCard(), !channelPreferenceKey.empty() || !groupedOutputChannels.empty() || !groupedInputChannels.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(OutputChannelGroup(), !channelPreferenceKey.empty() || !groupedOutputChannels.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(InputChannelGroup(), !channelPreferenceKey.empty() || !groupedInputChannels.empty());
            applyResponsiveLayout(RootLayout().ActualWidth());
        }
        if (Pages().AudioLoaded())
        {
            syncChannelCheckBoxes(OutputChannelsPanel(), groupedOutputChannels, "set-output-channel", renderedOutputChannelKeys);
        }
        if (Pages().AudioLoaded())
        {
            syncChannelCheckBoxes(InputChannelsPanel(), groupedInputChannels, "set-input-channel", renderedInputChannelKeys);
        }
        if (Pages().AudioLoaded())
        {
            syncChannelToggleButton(OutputChannelsToggleAllButton(), groupedOutputChannels, "set-all-output-channels");
        }
        if (Pages().AudioLoaded())
        {
            syncChannelToggleButton(InputChannelsToggleAllButton(), groupedInputChannels, "set-all-input-channels");
        }

        std::vector<std::string> sampleRateItems;
        int selectedSampleRateIndex = -1;
        for (int i = 0; i < (int) sampleRateValues.size(); ++i)
        {
            sampleRateItems.push_back(formatNumber(sampleRateValues[(size_t) i], 0) + " Hz");
            if ((int) sampleRateValues[(size_t) i] == (int) sampleRate)
                selectedSampleRateIndex = i;
        }
        if (Pages().AudioLoaded())
        {
            setComboItems(SampleRateBox(), sampleRateItems, selectedSampleRateIndex);
        }

        std::vector<std::string> bufferSizeItems;
        int selectedBufferSizeIndex = -1;
        for (int i = 0; i < (int) bufferSizeValues.size(); ++i)
        {
            bufferSizeItems.push_back(formatNumber(bufferSizeValues[(size_t) i], 0) + " " + to_string(localization.text("common.samples", L"samples")));
            if ((int) bufferSizeValues[(size_t) i] == bufferSize)
                selectedBufferSizeIndex = i;
        }
        if (Pages().AudioLoaded())
        {
            setComboItems(BufferSizeBox(), bufferSizeItems, selectedBufferSizeIndex);
        }
        if (Pages().AudioLoaded())
        {
            setVisible(FormatCard(), !sampleRateItems.empty() || !bufferSizeItems.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(SampleRateLabel(), !sampleRateItems.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(SampleRateBox(), !sampleRateItems.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(BufferSizeLabel(), !bufferSizeItems.empty());
        }
        if (Pages().AudioLoaded())
        {
            setVisible(BufferSizeBox(), !bufferSizeItems.empty());
        }
        syncingHostControls = false;

        syncingConfigControls = true;
        if (Pages().SettingsLoaded())
        {
            StartWithWindowsCheckBox().IsOn(startWithWindows);
        }
        closeQuitsHost = closeBehavior == "quit";
        if (Pages().SettingsLoaded())
        {
            CloseQuitsAppRadioButton().IsChecked(closeQuitsHost);
        }
        if (Pages().SettingsLoaded())
        {
            CloseToTrayRadioButton().IsChecked(!closeQuitsHost);
        }
        if (Pages().SettingsLoaded())
        {
            CloseToTraySwitch().IsOn(!closeQuitsHost);
        }
        if (Pages().SettingsLoaded())
        {
            EnableVst2CheckBox().IsEnabled(vst2HostAvailable);
        }
        if (Pages().SettingsLoaded())
        {
            EnableVst2CheckBox().IsOn(vst2RuntimeEnabled);
        }
        syncIconMode(trayIconMode);
        if (Pages().SettingsLoaded())
        {
            if (!vst2HostAvailable)
                Vst2StatusText().Text(localization.text("settings.vst2NotIncluded", L"VST2 support is not included in this build."));
            else if (vst2RestartRequired)
                Vst2StatusText().Text(localization.text("settings.restartSession", L"Restart this session to apply the changes."));
            else if (vst2RuntimeEnabled)
                Vst2StatusText().Text(localization.text("settings.vst2Enabled", L"VST2 plugins are enabled for this session."));
            else
                Vst2StatusText().Text(localization.text("settings.vst2Disabled", L"VST2 plugins are disabled. Changes take effect after restart."));
        }
        if (Pages().PluginsLoaded())
        {
            ScanVstCheckBox().IsEnabled(vst2HostEnabled);
        }
        if (Pages().PluginsLoaded())
        {
            if (!vst2HostEnabled)
                ScanVstCheckBox().IsChecked(false);
        }

        const int persistenceIndex = audioPersistenceModeIndex(audioPersistenceMode);
        if (Pages().SettingsLoaded())
        {
            setComboItems(AudioPersistenceModeBox(), {
                to_string(localization.text("settings.persistence.disabled", L"Disabled")),
                to_string(localization.text("settings.persistence.lastSelected", L"Last selected device")),
                to_string(localization.text("settings.persistence.custom", L"Custom device")) }, persistenceIndex);
        }
        if (Pages().SettingsLoaded())
        {
            AudioRecoveryRetrySecondsBox().Value((std::max)(1, audioPersistenceRetrySeconds));
        }
        if (Pages().SettingsLoaded())
        {
            AudioRecoveryRetryAttemptsBox().Value((std::max)(1, audioPersistenceRetryAttempts));
        }

        const int customBackendIndex = stringIndex(backendNames, audioPersistenceCustomBackend, -1);
        if (Pages().SettingsLoaded())
        {
            setComboItems(CustomRecoveryBackendBox(), backendNames, customBackendIndex);
        }
        const auto customInputCandidates = extractStringArray(json, "customInputDeviceNames");
        const auto customOutputCandidates = extractStringArray(json, "customOutputDeviceNames");
        const int customInputIndex = stringIndex(customInputCandidates, audioPersistenceCustomInputDevice, -1);
        const int customOutputIndex = stringIndex(customOutputCandidates, audioPersistenceCustomOutputDevice, -1);
        if (Pages().SettingsLoaded())
        {
            setComboItems(CustomRecoveryInputBox(), customInputCandidates, customInputIndex);
            CustomRecoveryInputBox().PlaceholderText(audioPersistenceCustomInputDevice.empty() ? localization.text("common.none", L"None") : hs(audioPersistenceCustomInputDevice));
        }
        if (Pages().SettingsLoaded())
        {
            setComboItems(CustomRecoveryOutputBox(), customOutputCandidates, customOutputIndex);
            CustomRecoveryOutputBox().PlaceholderText(audioPersistenceCustomOutputDevice.empty() ? localization.text("common.none", L"None") : hs(audioPersistenceCustomOutputDevice));
        }
        const auto retryVisibility = persistenceIndex == 0 ? Visibility::Collapsed : Visibility::Visible;
        if (Pages().SettingsLoaded())
        {
            AudioPersistenceRetryIntervalGrid().Visibility(retryVisibility);
        }
        if (Pages().SettingsLoaded())
        {
            AudioPersistenceRetryAttemptsGrid().Visibility(retryVisibility);
        }
        if (Pages().SettingsLoaded())
        {
            CustomAudioPersistenceCard().Visibility(persistenceIndex == 2 ? Visibility::Visible : Visibility::Collapsed);
        }
        if (Pages().SettingsLoaded())
        {
            CustomAudioPersistenceGrid().Visibility(persistenceIndex == 2 ? Visibility::Visible : Visibility::Collapsed);
        }
        const auto selectedCustomBackend = (customBackendIndex >= 0 && (size_t) customBackendIndex < backendNames.size())
            ? backendNames[(size_t) customBackendIndex]
            : backend;
        const bool customBackendIsAsio = selectedCustomBackend == "ASIO";
        if (Pages().SettingsLoaded())
        {
            CustomRecoveryInputLabel().Text(localization.text(customBackendIsAsio ? "audio.device" : "dashboard.inputDevice", customBackendIsAsio ? L"Device" : L"Input device"));
        }
        if (Pages().SettingsLoaded())
        {
            CustomRecoveryOutputRow().Visibility(customBackendIsAsio ? Visibility::Collapsed : Visibility::Visible);
        }
        updatePreferredDeviceSummary();
        if (Pages().SettingsLoaded())
        {
            RetryAudioDeviceButton().IsEnabled(persistenceIndex != 0);
        }
        syncEnabledAudioChoicesSummary();
        updateToggleStateLabels();

        if (Pages().SettingsLoaded())
        {
            hstring persistenceStatus;
            if (recoveryState == "suspended" || recoveryState == "blocked" || recoveryState == "failed")
                persistenceStatus = localization.text("audio.recovery." + recoveryState, L"Audio device is unavailable.");
            else if (recoveryState == "retrying")
                persistenceStatus = localization.format("audio.recovery.retrying", L"Retrying preferred audio device ({0}/{1}).",
                    { std::to_wstring(recoveryAttempt), std::to_wstring(recoveryMaxAttempts) });
            else
                persistenceStatus = localization.text(persistenceIndex == 0 ? "audio.recovery.disabled" : "audio.recovery.running", L"Audio device is available.");
            if (persistenceIndex != 0 && (!recoveryTargetBackend.empty() || !recoveryTargetInputDevice.empty() || !recoveryTargetOutputDevice.empty()))
            {
                std::string target = recoveryTargetBackend;
                if (!recoveryTargetInputDevice.empty()) target += " / " + recoveryTargetInputDevice;
                if (!recoveryTargetOutputDevice.empty() && recoveryTargetOutputDevice != recoveryTargetInputDevice)
                    target += " / " + recoveryTargetOutputDevice;
                persistenceStatus = localization.format("audio.recovery.target", L"{0} Target: {1}.",
                    { std::wstring(persistenceStatus), std::wstring(hs(target)) });
            }
            AudioRecoveryStatusText().Text(persistenceStatus);
        }
        syncingConfigControls = false;
        activePluginCount = (int) allPluginRows.size();
        installedPluginCount = (int) allKnownPluginRows.size();
        updateInstalledPluginActions();
        runningPage.adopt(std::move(allPluginRows));
        installedPage.adopt(std::move(allKnownPluginRows));
        refreshPluginViews();

        (void) reloads;
        (void) flushes;
    }

    void MainWindow::refreshPluginViews()
    {
        if (!Pages().PluginsLoaded() || windowClosing) return;
        // Language changes render the current models without waiting for a
        // structural host snapshot, so section labels belong to this refresh.
        RunningPluginsTabButton().Text(hstring(std::wstring(localization.text("plugins.running", L"Running").c_str())
            + L" (" + std::to_wstring(activePluginCount) + L")"));
        InstalledPluginsTabButton().Text(hstring(std::wstring(localization.text("plugins.installed", L"Installed").c_str())
            + L" (" + std::to_wstring(installedPluginCount) + L")"));
        runningPage.render(runningPluginSearch, runningPluginSortMode, compactPluginCards, localization, RunningPluginsListView(),false,globalBypassed);
        installedPage.render(installedPluginSearch, installedPluginSortMode, compactPluginCards, localization, InstalledPluginsListView(), installedGrouped);
        RunningPluginsSummaryText().Text(L"");
        InstalledPluginsSummaryText().Text(L"");
        setVisible(RunningPluginsListView(), runningPage.items.Size() != 0);
        setVisible(RunningPluginsEmptyText(), runningPage.items.Size() == 0);
        setVisible(InstalledPluginsListView(), installedPage.items.Size() != 0);
        setVisible(InstalledPluginsEmptyText(), installedPage.items.Size() == 0);
        updateRunningPluginActions();
        updateInstalledPluginActions();
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::sendCommand(std::string command,bool chainPrepared)
    {
        auto lifetime=get_strong();winrt::apartment_context ui;
        auto deadline=pendingNormalClose?normalCloseDeadline:GetTickCount64()+120000;
        const auto verb=command.substr(0,command.find(':'));
        std::string key;
        const bool audioIntent=verb=="ui-audio-intent";
        lightHostModern::ipc::JsonObject intent;
        try {
            if(audioIntent){
                intent=lightHostModern::ipc::parseObject(command.substr(16));
                key=verb+":"+to_string(intent.GetNamedString(L"field"));
                if(intent.HasKey(L"first"))key+=":"+to_string(intent.GetNamedValue(L"first").Stringify())+":"+to_string(intent.GetNamedValue(L"last").Stringify());
                // Device transitions are barriers; only scalar controls coalesce.
                const auto field=intent.GetNamedString(L"field");if(field==L"backend"||field==L"input"||field==L"output")key.clear();
            }
            if(verb=="operating-command"){
                const auto request=lightHostModern::ipc::parseObject(command.substr(18));
                if(request.HasKey(L"uiDeadline"))deadline=(std::min)(deadline,std::stoull(to_string(request.GetNamedString(L"uiDeadline"))));
            }
        }catch(...){co_return false;}
        for(const auto* setter:{"set-audio-persistence-retry-seconds","set-audio-persistence-retry-attempts","set-audio-persistence-mode","set-close-behavior","set-enable-vst2","set-start-with-windows","set-tray-icon-mode","set-global-mute","set-global-bypass"})if(verb==setter)key=verb;
        const bool externalStructure=verb=="add-known-plugin"||verb=="remove-plugin"||verb=="duplicate-plugin"||verb=="move-plugin-to"||verb=="move-plugin-up"||verb=="move-plugin-down"||verb=="remove-known-plugin"||verb=="clear-known-plugins";
        struct UnfreezeChain { std::shared_ptr<lightHostModern::ui::OperatingPresenter> presenter;bool previous=false;~UnfreezeChain(){if(presenter)presenter->freezeEdits(previous);} } unfreezeChain;
        if(operatingPresenter&&externalStructure&&!chainPrepared){
            unfreezeChain.presenter=operatingPresenter;unfreezeChain.previous=operatingPresenter->editsFrozen();operatingPresenter->freezeEdits(true);
            if(!(co_await operatingPresenter->flushPending(deadline)))co_return false;
        }
        auto ticket=commandIntents.reserve(key,command.size());
        if(!ticket){if(!windowClosing)showNotification(localization.translatedSource(L"Too many pending changes. Wait for the host and try again.").c_str());co_return false;}
        struct ReleaseIntent { lightHostModern::ui::IntentAdmission& queue;lightHostModern::ui::IntentAdmission::Ticket ticket;~ReleaseIntent(){queue.release(ticket);} } releaseIntent{commandIntents,ticket};
        while(!windowClosing&&!ticket->superseded&&(!commandIntents.ready(ticket)||commandInProgress||snapshotInProgress)&&GetTickCount64()<deadline){
            co_await winrt::resume_after(std::chrono::milliseconds(10));co_await ui;
        }
        if(ticket->superseded)co_return true; // The newer explicit value owns acknowledgement/feedback.
        if(windowClosing)co_return false;
        if(GetTickCount64()>=deadline){showNotification(localization.translatedSource(L"The change expired before it could be sent. Try again after reconnecting.").c_str());co_return false;}
        ticket->started=true;
        if (hostPipeName.empty())
        {
            winUILog("Command skipped because host pipe is empty: " + command.substr(0,command.find(':')));
            HeaderStatusText().Text(localization.text("ipc.noPipe", L"No host pipe"));
            co_return false;
        }

        winUILog("Sending command: " + command.substr(0,command.find(':')));
        commandInProgress = true;
        if (Pages().PluginsLoaded()) RunningPluginsListView().IsHitTestVisible(false);
        if (Pages().PluginsLoaded()) InstalledPluginsListView().IsHitTestVisible(false);
        struct EndCommand
        {
            MainWindow& owner;
            ~EndCommand()
            {
                owner.commandInProgress = false;
                if (!owner.windowClosing)
                {
                    if (owner.Pages().PluginsLoaded()) owner.RunningPluginsListView().IsHitTestVisible(!owner.snapshotInProgress);
                    if (owner.Pages().PluginsLoaded()) owner.InstalledPluginsListView().IsHitTestVisible(!owner.snapshotInProgress);
                }
            }
        } endCommand{*this};
        const auto previousSnapshot=hostConnection->snapshotJson;
        std::string audioOrigin;
        if(audioIntent){
            bool valid=false;
            try {
                const auto snapshot=lightHostModern::ipc::parseSnapshotObject(previousSnapshot);
                const auto current=to_string(snapshot.GetNamedObject(L"audioSelection").GetNamedValue(L"generation").Stringify());
                audioOrigin=to_string(intent.GetNamedValue(L"origin").Stringify());
                valid=lightHostModern::ui::audioIntentMatchesDevice(intent,snapshot) &&
                    (current==audioOrigin||(current==audioLastGeneration&&audioOwnedGenerations.count(audioOrigin)));
                if(valid){
                    const auto field=intent.GetNamedString(L"field");
                    if(intent.HasKey(L"first"))command=to_string(lightHostModern::ui::AudioPageController::channelCommand(previousSnapshot,field==L"inputChannels",static_cast<int>(intent.GetNamedNumber(L"first")),static_cast<int>(intent.GetNamedNumber(L"last")),intent.GetNamedBoolean(L"enabled")));
                    else if(field==L"monoInputs"||field==L"monoOutput"){
                        lightHostModern::ipc::JsonObject request;
                        request.SetNamedValue(L"enabled",lightHostModern::ipc::JsonValue::CreateBooleanValue(intent.GetNamedString(L"value")==L"1"));
                        request.SetNamedValue(L"expectedGeneration",snapshot.GetNamedObject(L"audioSelection").GetNamedValue(L"generation"));
                        command=std::string(field==L"monoInputs"?"set-mono-inputs:":"set-mono-output:")+to_string(request.Stringify());
                    }
                    else command=to_string(co_await lightHostModern::ui::AudioPageController::selectionCommand(hostConnection,to_string(field),to_string(intent.GetNamedString(L"value"))));
                }
            }catch(...){valid=false;}
            if(!valid){showNotification(localization.text("audio.selectionFailed",L"Could not apply this audio selection. Refresh the device list and try again.").c_str());co_await refreshSnapshot(true);co_return false;}
        }
        const auto now=GetTickCount64();if(now>=deadline)co_return false;
        const auto response = winrt::to_string(co_await hostConnection->requestAsync(command,static_cast<DWORD>((std::min)(deadline-now,static_cast<ULONGLONG>(120000)))));
        lastCommandResponse = response;
        if (windowClosing) co_return !response.empty();
        if (response.empty())
        {
            winUILog("Command failed with empty response; the host pipe closed or the plugin load crashed/froze the host: " + command.substr(0,command.find(':')));
            HeaderStatusText().Text(localization.text("ipc.noResponseShort", L"Host did not respond"));
            showNotification(localization.text("ipc.noResponse", L"Host did not respond. An operation already started may still complete; its state will refresh after reconnection.").c_str());
            co_return false;
        }

        winUILog("Command response: " + extractString(response,"status") + " code=" + extractString(response,"code"));
        if (extractString(response, "status") == "error")
        {
            const auto message = ipcErrorText(response, localization);
            HeaderStatusText().Text(message);
            showNotification(message.c_str());
            if (!pendingNormalClose)
                co_await refreshSnapshot(false,deadline);
            co_return false;
        }

        if (!pendingNormalClose)
            co_await refreshSnapshot(false,deadline);

        const bool succeeded=extractString(response,"status")=="ok";
        if(succeeded&&!pendingNormalClose&&hostConnection->snapshotNeeded)co_return false;
        if(succeeded&&audioIntent){try{
            audioLastGeneration=to_string(lightHostModern::ipc::parseSnapshotObject(hostConnection->snapshotJson).GetNamedObject(L"audioSelection").GetNamedValue(L"generation").Stringify());
            audioOwnedGenerations.insert(audioOrigin);audioOwnedGenerations.insert(audioLastGeneration);
            while(audioOwnedGenerations.size()>128)audioOwnedGenerations.erase(audioOwnedGenerations.begin());
        }catch(...){audioOwnedGenerations.clear();}}
        if(succeeded){try{if(const auto message=actionFeedback(command,previousSnapshot))showNotification(message,false);}catch(...){winUILog("Action feedback unavailable.");}}
        co_return succeeded;
    }

    int MainWindow::selectedRunningPluginIndex()
    {
        const auto item = RunningPluginsListView().SelectedItem().try_as<winrt::LightHostModernWinUI::PluginItem>();
        return item ? item.OriginalIndex() : -1;
    }

    int MainWindow::taggedIndexOrSelected(IInspectable const& sender, int selectedIndex) const
    {
        if (auto element = sender.try_as<FrameworkElement>())
        {
            try
            {
                return unbox_value<int>(element.Tag());
            }
            catch (...) {}
        }

        return selectedIndex;
    }

    void MainWindow::updateRunningPluginActions()
    {
        if (!Pages().PluginsLoaded()) return;
        RunningPluginsListView().Opacity(activePluginCount > 0 ? 1.0 : 0.65);
    }

    void MainWindow::updateInstalledPluginActions()
    {
        if (Pages().PluginsLoaded()) InstalledPluginsListView().Opacity(installedPluginCount > 0 ? 1.0 : 0.65);
        if (Pages().SettingsLoaded()) {
            RemoveMissingPluginsButton().IsEnabled(installedPluginCount > 0);
            ClearPluginDatabaseButton().IsEnabled(sessionWritable && installedPluginCount > 0);
        }
    }

    void MainWindow::PluginActions_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto button = sender.as<Button>();
        const auto item = button.DataContext().as<winrt::LightHostModernWinUI::PluginItem>();
        auto menu = MenuFlyout();
        menu.Placement(FlyoutPlacementMode::BottomEdgeAlignedRight);
        const auto append = [&](const MenuFlyoutItem& entry, bool changesSession = true) {
            if (changesSession && !sessionWritable) entry.IsEnabled(false);
            menu.Items().Append(entry);
        };
        const auto dialogAction = [&](const char* action, const char* key, const wchar_t* fallback, const wchar_t* icon) {
            auto entry = actionMenuItem(localization.text(key, fallback).c_str(), icon, item.Id(),
                [weak = get_weak(), action = std::string(action), item, button](const auto&, const auto&) {
                    if (auto owner = weak.get()) owner->openPluginDialog(action, item, button);
                });
            Automation::AutomationProperties::SetAutomationId(entry, to_hstring(std::string("PluginAction-") + action));
            return entry;
        };
        const auto separator = [&] { menu.Items().Append(MenuFlyoutSeparator()); };
        const auto& source = item.Running() ? runningPage.source : installedPage.source;
        const auto row = std::find_if(source.begin(), source.end(), [&](const auto& value) {
            return (item.Running() ? value.instanceId : value.knownId) == to_string(item.Id());
        });
        const auto renameActions = [&] {
            append(dialogAction("rename", item.Running() ? "plugins.rename" : "plugins.renameInstalled",
                item.Running() ? L"Rename instance" : L"Rename plugin", L"\xE8AC"), item.Running());
        };
        if (item.Running())
            append(actionMenuItem(localization.text("plugins.openEditor", L"Open editor").c_str(), L"\xE8A7", item.Id(), {this, &MainWindow::OpenPluginEditor_Click}), false);
        if (item.Running()) append(actionMenuItem(localization.translatedSource(L"Configure plugin channels").c_str(),L"\xE713",item.Id(),
            [weak=get_weak(),id=item.Id()](const auto&,const auto&){if(auto owner=weak.get())if(owner->operatingPresenter)owner->operatingPresenter->configurePluginBuses(id);}),false);
        if (item.Running()) append(dialogAction("details", "plugins.details", L"Plugin details", L"\xE946"), false);
        if (item.Running())
        {
            separator();
            append(actionMenuItem(localization.translatedSource(row!=source.end()&&row->isolated?L"Run inside the host":L"Run in a separate process (experimental)").c_str(),L"\xE7F4",item.Id(),
                [weak=get_weak(),id=item.Id()](const auto&,const auto&){if(auto owner=weak.get())if(owner->operatingPresenter)owner->operatingPresenter->configureIsolation(id);}));
            append(actionMenuItem(localization.translatedSource(L"Retry loading").c_str(),L"\xE72C",item.Id(),
                [weak=get_weak(),id=item.Id()](const auto&,const auto&)->fire_and_forget{if(auto owner=weak.get()){Windows::Data::Json::JsonObject r;r.SetNamedValue(L"action",Windows::Data::Json::JsonValue::CreateStringValue(L"retry"));r.SetNamedValue(L"id",Windows::Data::Json::JsonValue::CreateStringValue(id));co_await owner->sendCommand("operating-command:"+to_string(r.Stringify()));}}));
            append(actionMenuItem(localization.text("plugins.duplicate", L"Duplicate").c_str(), L"\xE8C8", item.Id(), {this, &MainWindow::DuplicatePlugin_Click}));
            append(actionMenuItem((globalBypassed?localization.translatedSource(L"Disable bypass chain"):localization.text(item.Bypassed() ? "plugins.enable" : "plugins.bypass", item.Bypassed() ? L"Enable" : L"Bypass")).c_str(), L"\xE7E8", item.Id(), {this, &MainWindow::BypassPlugin_Click}));
            separator(); renameActions();
            append(actionMenuItem(localization.translatedSource(L"Card color").c_str(),L"\xE790",item.Id(),
                [weak=get_weak(),id=item.Id(),color=row!=source.end()?to_hstring(row->cardColor):hstring{}](const auto&,const auto&)->fire_and_forget {
                    if(auto owner=weak.get()) { const auto targetId=id; const auto result=co_await lightHostModern::ui::chooseVisualColor(owner->RootLayout(),owner->localization,owner->localization.translatedSource(L"Card color"),color);
                        if(result&&!owner->windowClosing){Windows::Data::Json::JsonObject request;request.SetNamedValue(L"action",Windows::Data::Json::JsonValue::CreateStringValue(L"card-color"));request.SetNamedValue(L"id",Windows::Data::Json::JsonValue::CreateStringValue(targetId));request.SetNamedValue(L"color",Windows::Data::Json::JsonValue::CreateStringValue(unbox_value<hstring>(result)));co_await owner->sendCommand("operating-command:"+to_string(request.Stringify()));}
                    }
                })); separator();
            for (const bool up : {true, false})
            {
                const auto action = actionMenuItem(localization.text(up ? "plugins.moveUp" : "plugins.moveDown", up ? L"Move up" : L"Move down").c_str(), up ? L"\xE74A" : L"\xE74B", item.Id(),
                    [weak = get_weak(), id = to_string(item.Id()), up](IInspectable const&, RoutedEventArgs const&) -> fire_and_forget {
                        if (auto owner = weak.get()) co_await owner->sendCommand(std::string(up ? "move-plugin-up:" : "move-plugin-down:") + id);
                    });
                action.IsEnabled(up ? item.OriginalIndex() > 0 : item.OriginalIndex() + 1 < activePluginCount);
                append(action);
            }
            auto swap = dialogAction("swap", "plugins.swap", L"Swap position", L"\xE8AB");
            swap.IsEnabled(activePluginCount > 1); append(swap);
            separator();
            append(actionMenuItem(localization.text("common.remove", L"Remove").c_str(), L"\xE74D", item.Id(), {this, &MainWindow::RemovePlugin_Click}));
        }
        else
        {
            append(actionMenuItem(localization.text("plugins.addToChain", L"Add to chain").c_str(), L"\xE710", item.Id(), {this, &MainWindow::AddInstalledPlugin_Click}));
            separator();
            append(dialogAction("details", "plugins.details", L"Plugin details", L"\xE946"), false);
            append(actionMenuItem(localization.text("plugins.openFolder", L"Open folder").c_str(), L"\xE8B7", item.Id(), {this, &MainWindow::OpenInstalledPluginLocation_Click}), false);
            separator(); renameActions();
            separator();
            append(actionMenuItem(localization.text("plugins.removeDatabase", L"Remove from database").c_str(), L"\xE74D", item.Id(), {this, &MainWindow::RemoveInstalledPlugin_Click}));
        }
        menu.ShowAt(button);
    }

    winrt::fire_and_forget MainWindow::openPluginDialog(std::string action, winrt::LightHostModernWinUI::PluginItem item, Button button)
    {
        auto lifetime = get_strong();
        if (pluginDialogOpen || windowClosing) co_return;
        pluginDialogOpen = true;
        struct Finish { bool& flag; ~Finish() { flag = false; } } finish{pluginDialogOpen};
        const auto id = to_string(item.Id());
        try
        {
            {
                const auto details = to_string(co_await hostConnection->requestAsync(std::string(item.Running() ? "instance-details:" : "known-plugin-details:") + id));
                if (windowClosing) co_return;
                if (extractString(details, "status") != "ok") showNotification(ipcErrorText(details, localization).c_str());
                else
                {
                    const auto command = co_await lightHostModern::ui::showPluginDialog(RootLayout(), localization, action, id, details, runningPage.source, item.Running());
                    if (!windowClosing && !command.empty()) co_await sendCommand(to_string(command));
                }
            }
        }
        catch (hresult_error const& error) { winUILog("Plugin dialog: " + to_string(error.message())); }
        if (!windowClosing)
        {
            if (!button.Focus(FocusState::Programmatic)) (item.Running() ? RunningPluginsListView() : InstalledPluginsListView()).Focus(FocusState::Programmatic);
        }
    }

    winrt::fire_and_forget MainWindow::BypassPlugin_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
        {
            if ((co_await sendCommand(globalBypassed?"set-global-bypass:0":"toggle-bypass:" + id)))
            {
                // Feedback is emitted by sendCommand after acknowledgement.
            }
        }
    }

    winrt::fire_and_forget MainWindow::OpenPluginEditor_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
            (co_await sendCommand("open-plugin-editor:" + id));
    }

    winrt::fire_and_forget MainWindow::DuplicatePlugin_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
        {
            if ((co_await sendCommand("duplicate-plugin:" + id)))
            {
                // Feedback is emitted by sendCommand after acknowledgement.
            }
        }
    }

    winrt::fire_and_forget MainWindow::RemovePlugin_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
        {
            if ((co_await sendCommand("remove-plugin:" + id)))
            {
                // Feedback is emitted by sendCommand after acknowledgement.
            }
        }
    }

    winrt::fire_and_forget MainWindow::ScanForPlugins_Click(IInspectable, RoutedEventArgs)
    {
        if (scanDialogOpen || windowClosing) co_return;
        auto lifetime = get_strong();
        scanDialogOpen = true; scanStatusKnown = false;
        try
        {
            if (!databasePageView)
            {
                databasePageView = winrt::make<DatabasePageView>();
                databasePaths = lightHostModern::ui::ScanPathsDialog::createPane(RootLayout(), localization, pluginScanPaths,
                    [] { return pickFolderPath(); },
                    [weak = get_weak()](const std::vector<std::string>& paths) {
                        if (auto owner = weak.get(); owner && !owner->windowClosing) {
                            lightHostModern::ipc::JsonArray saved;
                            lightHostModern::ipc::JsonArray optional;
                            std::set<std::string> retainedOptional;
                            for (const auto& path : paths) saved.Append(lightHostModern::ipc::JsonValue::CreateStringValue(to_hstring(path)));
                            for(const auto& path:paths)if(owner->optionalPluginScanPaths.count(path)){retainedOptional.insert(path);optional.Append(lightHostModern::ipc::JsonValue::CreateStringValue(to_hstring(path)));}
                            const auto json = saved.Stringify();
                            if(!WritePrivateProfileStringW(L"Plugins",L"OptionalScanPaths",optional.Stringify().c_str(),uiSettingsFilePath().c_str()))return false;
                            if (!WritePrivateProfileStringW(L"Plugins", L"ScanPaths", json.c_str(), uiSettingsFilePath().c_str())) return false;
                            owner->optionalPluginScanPaths=std::move(retainedOptional);
                            owner->pluginScanPaths = paths; owner->updateScanDialogActions(); return true;
                        }
                        return false;
                    });
                pluginScanDialog = ContentDialog();
                pluginScanDialog.DefaultButton(ContentDialogButton::Primary);
                pluginScanDialog.XamlRoot(RootLayout().XamlRoot());
                ContentControl pathsContent;
                pathsContent.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                pathsContent.Content(databasePaths->content());
                pluginScanDialog.Content(pathsContent);
                Automation::AutomationProperties::SetAutomationId(pluginScanDialog, L"PluginScanDialog");
                scanProgressDialog = ContentDialog();
                scanProgressDialog.XamlRoot(RootLayout().XamlRoot());
                scanProgressDialog.Content(databasePageView);
                Automation::AutomationProperties::SetAutomationId(scanProgressDialog, L"ScanProgressDialog");
                scanProgressDialog.PrimaryButtonClick([weak = get_weak()](const auto&, ContentDialogButtonClickEventArgs const& args) {
                    args.Cancel(true);
                    if (auto owner = weak.get()) {
                        if (owner->pluginScanActive || owner->scanQueuePending) owner->CancelPluginScan_Click(nullptr, RoutedEventArgs{});
                        else owner->RetryPluginScan_Click(nullptr, RoutedEventArgs{});
                    }
                });
                scanProgressDialog.SecondaryButtonClick([weak = get_weak()](const auto&, ContentDialogButtonClickEventArgs const& args) {
                    args.Cancel(true);
                    if (auto owner = weak.get()) owner->ViewScanFailures_Click(nullptr, RoutedEventArgs{});
                });
            }
            pluginScanDialog.RequestedTheme(RootLayout().ActualTheme());
            scanProgressDialog.RequestedTheme(RootLayout().ActualTheme());
            sizeDialogToViewport(pluginScanDialog, RootLayout(), 1.0);
            const double width = (std::clamp)(RootLayout().ActualWidth() - 48.0, 320.0, 480.0);
            scanProgressDialog.Resources().Insert(box_value(L"ContentDialogMinWidth"), box_value(width));
            scanProgressDialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(width));
            applyLocalization();
            co_await refreshPluginScanStatus();
            scanShowingProgress = pluginScanActive || scanQueuePending;
            while (!windowClosing)
            {
                scanFailuresRequested = false;
                if (!scanShowingProgress) {
                    const auto choice = co_await lightHostModern::ui::showAppDialog(pluginScanDialog);
                    if (choice == ContentDialogResult::None || windowClosing) break;
                    scanShowingProgress = true;
                    if (choice == ContentDialogResult::Primary) ScanDefaultPlugins_Click(nullptr, RoutedEventArgs{});
                }
                co_await lightHostModern::ui::showAppDialog(scanProgressDialog);
                // Complete each dialog before opening the next: WinUI permits
                // only one ContentDialog per XamlRoot.
                if (!scanFailuresRequested || windowClosing) break;
                co_await showScanFailures();
            }
        }
        catch (...)
        {
            if (!windowClosing) showNotification(localization.text("scan.openFailed", L"Could not open the plugin scan. Please try again.").c_str());
        }
        scanDialogOpen = false; scanShowingProgress = false;
        if (!windowClosing && Pages().PluginsLoaded())
            winrt::get_self<PluginsPageView>(pluginsPageView)->ScanForPluginsButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::updateScanDialogActions()
    {
        if (!pluginScanDialog) return;
        const bool busy = pluginScanActive || scanQueuePending;
        pluginScanDialog.PrimaryButtonText(localization.text("scan.start", L"Start scan"));
        pluginScanDialog.IsPrimaryButtonEnabled(!busy && scanStatusKnown && !pluginScanPaths.empty() && hostConnection->connected);
        pluginScanDialog.SecondaryButtonText(scanHasResult ? localization.text("scan.results", L"View scan results") : L"");
        pluginScanDialog.Content().as<Control>().IsEnabled(!busy);
        scanProgressDialog.PrimaryButtonText(busy ? localization.text("scan.cancel", L"Cancel scan") : localization.text("scan.retryShort", L"Retry failures"));
        scanProgressDialog.IsPrimaryButtonEnabled(hostConnection->connected && (busy ? !scanCancelRequested : scanStatusKnown && scanFailureCount > 0));
        scanProgressDialog.SecondaryButtonText(busy ? L"" : localization.text("scan.viewFailures", L"View failures"));
        scanProgressDialog.IsSecondaryButtonEnabled(!busy && scanStatusKnown && scanFailureCount > 0);
        scanProgressDialog.CloseButtonText(localization.text("common.close", L"Close"));
        if (busy) {
            scanProgressDialog.Title(box_value(localization.text("scan.running", L"Scanning plugins")));
            PluginScanProgress().Visibility(Visibility::Visible);
            PluginScanStatusText().Text(localization.text(scanCancelRequested ? "scan.cancelling" : "scan.preparing",
                scanCancelRequested ? L"Cancelling the scan..." : L"Searching the selected folders..."));
            if (scanQueuePending) PluginScanProgress().IsIndeterminate(true);
        }
    }

    winrt::fire_and_forget MainWindow::ScanDefaultPlugins_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (scanQueuePending || pluginScanActive) co_return;
        if (pluginScanPaths.empty()) {
            showNotification(localization.text("dialogs.scanPaths.addBeforeScan", L"Add a folder in Scan paths before starting a scan.").c_str());
            co_return;
        }
        scanCancelRequested = false;
        scanQueuePending = true;
        updateScanDialogActions();
        if (!(co_await sendCommand("begin-plugin-scan"))) { scanQueuePending = false; updateScanDialogActions(); co_return; }
        Windows::Data::Json::JsonArray roots;
        const auto defaults=defaultPluginScanPaths();
        for (const auto& path : pluginScanPaths) {
            const bool optional=optionalPluginScanPaths.count(path)!=0;
            Windows::Data::Json::JsonObject root;
            root.SetNamedValue(L"path",Windows::Data::Json::JsonValue::CreateStringValue(to_hstring(path)));
            root.SetNamedValue(L"optional",Windows::Data::Json::JsonValue::CreateBooleanValue(optional));
            root.SetNamedValue(L"format",Windows::Data::Json::JsonValue::CreateStringValue(optional?(path.size()>=4&&path.substr(path.size()-4)=="VST3"?L"VST3":L"VST"):L"all"));
            roots.Append(root);
        }
        Windows::Data::Json::JsonObject request;request.SetNamedValue(L"roots",roots);
        const bool queued=co_await sendCommand("scan-plugin-roots:"+to_string(request.Stringify()));
        scanQueuePending = false;
        updateScanDialogActions();
        if (windowClosing) co_return;
        (void) queued;
        refreshPluginScanStatus();
    }

    winrt::fire_and_forget MainWindow::CancelPluginScan_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        scanCancelRequested = true;
        updateScanDialogActions();
        co_await sendCommand("cancel-plugin-scan");
        if (!windowClosing) refreshPluginScanStatus();
    }

    winrt::fire_and_forget MainWindow::RetryPluginScan_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        scanCancelRequested = false;
        scanQueuePending = true;
        updateScanDialogActions();
        co_await sendCommand("retry-plugin-scan");
        scanQueuePending = false;
        updateScanDialogActions();
        if (!windowClosing) refreshPluginScanStatus();
    }

    Windows::Foundation::IAsyncAction MainWindow::refreshPluginScanStatus()
    {
        if (!databasePageView) co_return;
        auto lifetime = get_strong();
        if (scanStatusInProgress || windowClosing) co_return;
        scanStatusInProgress = true;
        std::string json;
        try { json = winrt::to_string(co_await hostConnection->requestAsync("plugin-scan-status")); }
        catch (...) { scanStatusInProgress = false; scanStatusKnown = false; updateScanDialogActions(); co_return; }
        scanStatusInProgress = false;
        if (windowClosing) co_return;
        scanStatusKnown = !json.empty() && extractString(json, "status") == "ok";
        if (!scanStatusKnown) { updateScanDialogActions(); co_return; }
        pluginScanActive = extractBool(json, "active");
        const int completed = (int) extractNumber(json, "completed"), total = (int) extractNumber(json, "total");
        const int failures = (int) extractNumber(json, "failureCount"), cached = (int) extractNumber(json, "cached");
        scanFailureCount = failures;
        scanHasResult = !pluginScanActive && (total > 0 || failures > 0 || extractBool(json, "cancelled"));
        const bool busy = pluginScanActive || scanQueuePending;
        const bool incomplete=extractBool(json,"incomplete");
        const auto key = busy ? "scan.running" : (extractBool(json, "cancelled") ? "scan.cancelled" : incomplete?"scan.incomplete":"scan.finished");
        updateScanDialogActions();
        scanProgressDialog.Title(box_value(localization.text(key, L"Plugin scan")));
        PluginScanStatusText().Text(localization.text(busy ? (scanCancelRequested ? "scan.cancelling" : "scan.preparing")
            : (extractBool(json, "cancelled") ? "scan.cancelledDescription"
                : (incomplete?"scan.incompleteDescription":failures > 0 ? "scan.finishedWithFailures" : "scan.finishedSuccessfully")),
            busy ? L"Searching the selected folders..." : L"The scan has finished."));
        auto view = winrt::get_self<DatabasePageView>(databasePageView);
        view->PluginScanCompletedValue().Text(to_hstring(completed) + L"/" + to_hstring(total));
        view->PluginScanCachedValue().Text(to_hstring(cached));
        view->PluginScanFailureValue().Text(to_hstring(failures));
        PluginScanProgress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        PluginScanProgress().Maximum((std::max)(1, total));
        PluginScanProgress().Value((std::min)(completed, total));
        PluginScanProgress().IsIndeterminate(busy && (extractBool(json,"enumerating") || total == 0 || scanQueuePending));
        const auto file = to_hstring(extractString(json, "currentFile"));
        view->PluginScanCurrentFileText().Text(file);
        view->PluginScanCurrentFileText().Visibility(busy && !file.empty() ? Visibility::Visible : Visibility::Collapsed);
        lightHostModern::ui::HoverHelp::SetToolTip(view->PluginScanCurrentFileText(), box_value(file));
        PluginScanFailuresText().Text(localization.format("scan.recognizedSummary",L"{0} plugins recognized. {1} items skipped.",
            {std::to_wstring((int)extractNumber(json,"recognized")),std::to_wstring((int)extractNumber(json,"ignored"))}));
        PluginScanFailuresText().Visibility(Visibility::Visible);
    }

    winrt::fire_and_forget MainWindow::ViewScanFailures_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (scanDialogOpen) {
            scanFailuresRequested = true;
            scanProgressDialog.Hide();
        } else co_await showScanFailures();
    }

    Windows::Foundation::IAsyncAction MainWindow::showScanFailures()
    {
        auto lifetime = get_strong();
        const auto status = to_string(co_await hostConnection->requestAsync("plugin-scan-status"));
        if (windowClosing || extractString(status, "status") != "ok" || extractBool(status, "active")) co_return;
        const auto selected = co_await lightHostModern::ui::ScanFailureDialog::show(RootLayout(), localization, hostTransport,
            hostPipeName, extractString(status, "scanId"), static_cast<uint64_t>(extractNumber(status, "revision")));
        if (windowClosing) co_return;
        if (!selected.empty()) {
            scanCancelRequested = false; scanQueuePending = true; updateScanDialogActions();
            co_await sendCommand("retry-plugin-scan-selection:" + to_string(selected));
            scanQueuePending = false;
        }
        co_await refreshPluginScanStatus();
    }

    winrt::fire_and_forget MainWindow::AddInstalledPlugin_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
        {
            if ((co_await sendCommand("add-known-plugin:" + id)))
            {
                // Feedback is emitted by sendCommand after acknowledgement.
            }
        }
    }

    winrt::fire_and_forget MainWindow::OpenInstalledPluginLocation_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (!id.empty())
            (co_await sendCommand("open-known-plugin-location:" + id));
    }

    winrt::fire_and_forget MainWindow::RemoveInstalledPlugin_Click(IInspectable sender, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        const auto element = sender.try_as<FrameworkElement>();
        const auto id = element ? winrt::to_string(unbox_value_or<hstring>(element.Tag(), L"")) : std::string();
        if (id.empty())
            co_return;

        const bool isRunning = std::find(activePluginIdentityKeys.begin(), activePluginIdentityKeys.end(), id) != activePluginIdentityKeys.end();
        const auto found = std::find(knownPluginIdentityKeys.begin(), knownPluginIdentityKeys.end(), id);
        const auto index = static_cast<size_t>(std::distance(knownPluginIdentityKeys.begin(), found));

        if (isRunning)
        {
            const auto pluginName = index < knownPluginDisplayNames.size()
                ? knownPluginDisplayNames[(size_t) index]
                : std::wstring(localization.text("plugins.thisPlugin", L"This plugin"));

            auto message = TextBlock();
            message.Text(localization.format(
                "dialogs.plugins.removeRunningMessage",
                L"{0} is currently running and will also be removed from the running chain.",
                { pluginName }));
            message.TextWrapping(TextWrapping::Wrap);

            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.plugins.removeRunningTitle", L"Remove running plugin?")));
            dialog.Content(message);
            dialog.PrimaryButtonText(localization.text("common.remove", L"Remove"));
            dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
            dialog.DefaultButton(ContentDialogButton::Close);

            if (co_await lightHostModern::ui::showAppDialog(dialog) != ContentDialogResult::Primary)
                co_return;
        }
        if ((co_await sendCommand("remove-known-plugin:" + id)))
        {
            const int removedActive = (int) extractNumber(lastCommandResponse, "removedActive", 0);
            showNotification((removedActive > 0 ? localization.text("plugins.removedFromBoth", L"Plugin removed from database and running chain.") : localization.text("plugins.removedFromDatabase", L"Plugin removed from database.")).c_str(),false);
        }
    }

    winrt::fire_and_forget MainWindow::RemoveMissingPlugins_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if ((co_await lightHostModern::ui::confirmDanger(RootLayout(),localization,localization.translatedSource(L"Remove missing plugins?"),localization.translatedSource(L"Remove database entries whose plugin files are no longer available."))) && (co_await sendCommand("remove-missing-known-plugins")))
        {
            showNotification(localization.format("plugins.missingRemoved", L"{0} missing plugins removed.", { std::to_wstring((int) extractNumber(lastCommandResponse, "removed", 0)) }).c_str(),false);
        }
    }

    winrt::fire_and_forget MainWindow::ClearPluginDatabase_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        co_await confirmClearPluginDatabase();
        if (!windowClosing && currentSection == L"Settings") ClearPluginDatabaseButton().Focus(FocusState::Programmatic);
    }

    Windows::Foundation::IAsyncAction MainWindow::confirmClearPluginDatabase()
    {
        auto lifetime = get_strong();
        if (!(co_await lightHostModern::ui::confirmDanger(RootLayout(),localization,localization.text("dialogs.plugins.clearTitle",L"Clear plugin database?"),localization.text("dialogs.plugins.clearMessage",L"This removes every installed plugin entry and clears the current running chain."),L"Clear"))) co_return;
        if ((co_await sendCommand("clear-known-plugins")))
        {
            const int removed = (int) extractNumber(lastCommandResponse, "removed", 0);
            const int removedActive = (int) extractNumber(lastCommandResponse, "removedActive", 0);
            showNotification(localization.format("plugins.databaseCleared", L"{0} installed plugins cleared. {1} running plugins removed.", { std::to_wstring(removed), std::to_wstring(removedActive) }).c_str(),false);
        }
    }

    winrt::fire_and_forget MainWindow::DeletePluginStates_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if ((co_await sendCommand("delete-plugin-states")))
            showNotification(localization.text("plugins.statesDeleted", L"Plugin states deleted.").c_str(),false);
    }

    winrt::fire_and_forget MainWindow::StartWithWindowsCheckBox_Changed(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        updateToggleStateLabels();
        if (syncingConfigControls)
            co_return;

        const bool enabled = isChecked(StartWithWindowsCheckBox());
        if (!(co_await sendCommand(std::string("set-start-with-windows:") + (enabled ? "1" : "0"))))
        {
            syncingConfigControls = true;
            StartWithWindowsCheckBox().IsOn(!enabled);
            syncingConfigControls = false;
            updateToggleStateLabels();
            showNotification(localization.text(
                "settings.startWindows.failed",
                L"LightHostModern could not update the Windows startup entry.").c_str());
        }
    }

    winrt::fire_and_forget MainWindow::CloseBehaviorRadioButton_Checked(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingConfigControls)
            co_return;

        const auto checked = CloseQuitsAppRadioButton().IsChecked();
        closeQuitsHost = checked && checked.Value();
        CloseToTraySwitch().IsOn(!closeQuitsHost);
        (co_await sendCommand(std::string("set-close-behavior:") + (closeQuitsHost ? "quit" : "tray")));
    }

    winrt::fire_and_forget MainWindow::CloseToTraySwitch_Toggled(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        updateToggleStateLabels();
        if (syncingConfigControls)
            co_return;

        closeQuitsHost = !CloseToTraySwitch().IsOn();
        CloseQuitsAppRadioButton().IsChecked(closeQuitsHost);
        CloseToTrayRadioButton().IsChecked(!closeQuitsHost);
        (co_await sendCommand(std::string("set-close-behavior:") + (closeQuitsHost ? "quit" : "tray")));
    }

    winrt::fire_and_forget MainWindow::EnableVst2CheckBox_Changed(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        updateToggleStateLabels();
        if (syncingConfigControls)
            co_return;

        vst2RestartRequired = true;
        (co_await sendCommand(std::string("set-enable-vst2:") + (isChecked(EnableVst2CheckBox()) ? "1" : "0")));
        Vst2StatusText().Text(localization.text("settings.restartSession", L"Restart this session to apply the changes."));
    }

    winrt::fire_and_forget MainWindow::AudioPersistenceModeBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingConfigControls || !AudioPersistenceModeBox() || AudioPersistenceModeBox().SelectedIndex() < 0)
            co_return;

        const auto mode = audioPersistenceModeValue(AudioPersistenceModeBox().SelectedIndex());
        (co_await sendCommand("set-audio-persistence-mode:" + mode));
        refreshSnapshot();
    }

    winrt::fire_and_forget MainWindow::AudioRecoveryRetrySecondsBox_ValueChanged(NumberBox, NumberBoxValueChangedEventArgs args)
    {
        auto lifetime = get_strong();
        if (syncingConfigControls)
            co_return;

        const auto value = args.NewValue();
        if (value != value)
            co_return;

        const int retrySeconds = (std::max)(1, (std::min)(60, (int) std::round(value)));
        (co_await sendCommand("set-audio-persistence-retry-seconds:" + std::to_string(retrySeconds)));
    }

    winrt::fire_and_forget MainWindow::AudioRecoveryRetryAttemptsBox_ValueChanged(NumberBox, NumberBoxValueChangedEventArgs args)
    {
        auto lifetime = get_strong();
        if (syncingConfigControls)
            co_return;

        const auto value = args.NewValue();
        if (value != value)
            co_return;

        const int retryAttempts = (std::max)(1, (std::min)(100, (int) std::round(value)));
        (co_await sendCommand("set-audio-persistence-retry-attempts:" + std::to_string(retryAttempts)));
    }

    void MainWindow::updatePreferredDeviceSummary()
    {
        if (!Pages().SettingsLoaded()) return;
        if (!PreferredDeviceSummaryText())
            return;

        const auto& snapshot = hostConnection->snapshotJson;
        const auto backend = extractString(snapshot, "audioPersistenceCustomBackend");
        const auto input = extractString(snapshot, "audioPersistenceCustomInputDevice");
        const auto output = extractString(snapshot, "audioPersistenceCustomOutputDevice");
        std::string summary = backend.empty() ? to_string(localization.text("settings.persistence.chooseDevice", L"Choose device")) : backend;
        if (!input.empty()) summary += " · " + input;
        if (backend != "ASIO" && !output.empty() && output != input) summary += " / " + output;
        PreferredDeviceSummaryText().Text(hs(summary));
        lightHostModern::ui::HoverHelp::SetToolTip(PreferredDeviceButton(), box_value(hs(summary)));
    }

    void MainWindow::PreferredDeviceButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        showPreferredDeviceDialogAsync();
    }

    fire_and_forget MainWindow::showPreferredDeviceDialogAsync()
    {
        if (preferredDeviceDialogOpen) co_return;
        auto lifetime = get_strong(); preferredDeviceDialogOpen = true;
        try
        {
            const auto command = co_await lightHostModern::ui::showPreferredAudioDialog(RootLayout(), localization, hostConnection);
            if (!windowClosing && !command.empty()) co_await sendCommand(to_string(command));
        }
        catch (...) { if (!windowClosing) showNotification(localization.text("audio.deviceListFailed", L"Could not read the device list. Select the backend again to retry.").c_str()); }
        preferredDeviceDialogOpen = false;
        if (!windowClosing) PreferredDeviceButton().Focus(FocusState::Programmatic);
    }

    winrt::fire_and_forget MainWindow::RetryAudioDevice_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if ((co_await sendCommand("retry-audio-device")))
            showNotification(localization.text("audio.retryStarted", L"Audio device retry started.").c_str(),false);
        refreshSnapshot();
    }

    void MainWindow::ChooseAudioDevice_Click(IInspectable const&, RoutedEventArgs const&)
    {
        showSection(L"Audio");
    }

    winrt::fire_and_forget MainWindow::ManageEnabledAudioDevices_Click(IInspectable, RoutedEventArgs)
    {
        auto lifetime = get_strong();
        if (enabledDevicesOpening || windowClosing || pendingNormalClose) co_return;
        enabledDevicesOpening = true;
        struct ReleaseOpening { bool& value; ~ReleaseOpening() { value = false; } } release{enabledDevicesOpening};
        try
        {
        if (hostPipeName.empty())
        {
            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
            dialog.Content(box_value(localization.text("dialogs.enabledDevices.disconnected", L"LightHostModern is not connected to the audio host.")));
            dialog.CloseButtonText(localization.text("common.close", L"Close"));
            co_await lightHostModern::ui::showAppDialog(dialog);
            co_return;
        }

        const auto choicesJson = winrt::to_string(co_await hostConnection->requestAsync("enabled-audio-choices"));
        if (windowClosing) co_return;
        if (choicesJson.empty() || extractString(choicesJson, "status") != "ok")
        {
            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
            dialog.Content(box_value(localization.text("dialogs.enabledDevices.loadFailed", L"Could not load the available audio device list.")));
            dialog.CloseButtonText(localization.text("common.close", L"Close"));
            co_await lightHostModern::ui::showAppDialog(dialog);
            co_return;
        }

        allAudioBackendNames = extractStringArray(choicesJson, "allAudioBackendNames");
        allAudioBackendEnabled = extractBoolArray(choicesJson, "allAudioBackendEnabled");
        allAudioDeviceChoices = extractStringArray(choicesJson, "allAudioDeviceChoices");
        allAudioDeviceChoiceEnabled = extractBoolArray(choicesJson, "allAudioDeviceChoiceEnabled");

        if (allAudioBackendNames.empty())
        {
            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
            dialog.Content(box_value(localization.text("dialogs.enabledDevices.noBackends", L"No audio backends were detected.")));
            dialog.CloseButtonText(localization.text("common.close", L"Close"));
            co_await lightHostModern::ui::showAppDialog(dialog);
            co_return;
        }

        auto backendEnabled = std::make_shared<std::vector<bool>>(allAudioBackendEnabled);
        auto deviceEnabled = std::make_shared<std::vector<bool>>(allAudioDeviceChoiceEnabled);
        backendEnabled->resize(allAudioBackendNames.size(), true);
        deviceEnabled->resize(allAudioDeviceChoices.size(), true);
        const auto originalBackendEnabled = *backendEnabled;
        const auto originalDeviceEnabled = *deviceEnabled;

        auto backendBox = ComboBox();
        styleCombo(backendBox);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(backendBox, L"EnabledAudioBackend");
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            backendBox,
            localization.text("settings.persistence.backend", L"Audio backend"));
        setComboItems(backendBox, allAudioBackendNames, 0);

        auto backendToggle = CheckBox();
        setAudioCheckBoxLabel(backendToggle, localization.text("dialogs.enabledDevices.enableBackend", L"Enable this audio backend"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(backendToggle, L"EnabledAudioBackendToggle");

        auto deviceSections = StackPanel();
        deviceSections.Spacing(12);

        auto renameRequest=std::make_shared<std::pair<hstring,hstring>>();auto requestRename=std::make_shared<std::function<void()>>();
        auto deviceAliases=JsonObject::Parse(lightHostModern::ipc::parseObject(choicesJson).GetNamedObject(L"audioDeviceAliases",JsonObject{}).Stringify());
        auto changedAliases=JsonObject{};
        auto updating = std::make_shared<bool>(false);
        auto rebuild = std::make_shared<std::function<void()>>();
        struct ReleaseDialogCallbacks { std::shared_ptr<std::function<void()>> callback; ~ReleaseDialogCallbacks() { *callback = {}; } } releaseCallbacks{rebuild};
        *rebuild = [this, backendBox, backendToggle, deviceSections, backendEnabled, deviceEnabled, updating, rebuild, deviceAliases, changedAliases, renameRequest, requestRename]()
        {
            int backendIndex = backendBox.SelectedIndex();
            if (backendIndex < 0 || backendIndex >= (int) allAudioBackendNames.size())
                backendIndex = 0;

            const auto backendName = allAudioBackendNames[(size_t) backendIndex];
            const bool enabled = (*backendEnabled)[(size_t) backendIndex];

            *updating = true;
            backendToggle.IsChecked(enabled);
            *updating = false;

            deviceSections.Children().Clear();

            auto appendSection = [this, backendName, enabled, deviceEnabled, deviceSections, updating, deviceAliases, changedAliases, renameRequest, requestRename](std::string const& role, hstring const& title)
            {
                auto section = StackPanel();
                section.Spacing(10);

                bool hasItems = false;
                auto titleBlock = TextBlock();
                titleBlock.Text(title);
                titleBlock.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                section.Children().Append(titleBlock);

                for (int i = 0; i < (int) allAudioDeviceChoices.size(); ++i)
                {
                    const auto entry = parseAudioChoiceEntry(allAudioDeviceChoices[(size_t) i]);
                    if (entry.backend != backendName || entry.role != role)
                        continue;

                    hasItems = true;
                    auto box = CheckBox();
                    Automation::AutomationProperties::SetName(box,hs(entry.name));
                    box.MinWidth(0); box.Width(20); box.Padding({0,0,0,0});
                    box.Tag(box_value(i));
                    box.IsChecked(static_cast<bool>((*deviceEnabled)[(size_t) i]));
                    box.IsEnabled(enabled);
                    box.VerticalAlignment(VerticalAlignment::Center);
                    box.HorizontalAlignment(HorizontalAlignment::Stretch);
                    lightHostModern::ui::HoverHelp::SetToolTip(box, box_value(lightHostModern::ui::HoverHelp::current().explanation(L"Enabled device")));
                    Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                        box,
                        hstring(L"EnabledAudioDevice" + std::to_wstring(i)));
                    box.Checked([deviceEnabled, updating, i](IInspectable const&, RoutedEventArgs const&)
                    {
                        if (*updating)
                            return;
                        if (i >= 0 && i < (int) deviceEnabled->size())
                            (*deviceEnabled)[(size_t) i] = true;
                    });
                    box.Unchecked([deviceEnabled, updating, i](IInspectable const&, RoutedEventArgs const&)
                    {
                        if (*updating)
                            return;
                        if (i >= 0 && i < (int) deviceEnabled->size())
                            (*deviceEnabled)[(size_t) i] = false;
                    });
                    Grid deviceRow;deviceRow.ColumnSpacing(8);ColumnDefinition checkColumn,nameColumn;checkColumn.Width(GridLengthHelper::Auto());nameColumn.Width({1,GridUnitType::Star});deviceRow.ColumnDefinitions().Append(checkColumn);deviceRow.ColumnDefinitions().Append(nameColumn);deviceRow.Children().Append(box);
                    TextBlock name;const auto key=hs(allAudioDeviceChoices[(size_t)i]);const auto alias=deviceAliases.GetNamedString(key,L"");name.Text(alias.empty()?hs(entry.name):alias);name.TextTrimming(TextTrimming::CharacterEllipsis);name.VerticalAlignment(VerticalAlignment::Center);
                    lightHostModern::ui::HoverHelp::SetToolTip(name,box_value(hs(entry.name)));deviceRow.Children().Append(name);Grid::SetColumn(name,1);
                    ColumnDefinition renameColumn;renameColumn.Width(GridLengthHelper::Auto());deviceRow.ColumnDefinitions().Append(renameColumn);
                    Button rename;rename.Style(Application::Current().Resources().Lookup(box_value(L"SubtleButtonStyle")).as<Style>());FontIcon pencil;pencil.Glyph(L"\xE70F");pencil.FontSize(14);rename.Content(pencil);rename.Width(32);rename.Height(32);rename.Padding({0,0,0,0});rename.VerticalAlignment(VerticalAlignment::Center);
                    lightHostModern::ui::HoverHelp::SetToolTip(rename,box_value(localization.translatedSource(L"Rename device")));Automation::AutomationProperties::SetName(rename,localization.translatedSource(L"Rename device")+L" "+name.Text());Automation::AutomationProperties::SetAutomationId(rename,L"RenameDevice-"+to_hstring(i));
                    rename.Click([renameRequest,requestRename,key,original=hs(entry.name)](auto const&,auto const&){*renameRequest={key,original};if(*requestRename)(*requestRename)();});deviceRow.Children().Append(rename);Grid::SetColumn(rename,2);section.Children().Append(deviceRow);
                }

                if (hasItems)
                {
                    auto sectionCard = Border();
                    sectionCard.Background(resourceBrush(L"AppCardBrush", themedFallback(makeColor(255, 255, 255), makeColor(39, 39, 39))));
                    sectionCard.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(225, 225, 225), makeColor(58, 58, 58))));
                    sectionCard.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    sectionCard.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                    sectionCard.Padding(ThicknessHelper::FromUniformLength(14));
                    sectionCard.Child(section);
                    deviceSections.Children().Append(sectionCard);
                }
            };

            const bool isAsio = backendName == "ASIO";
            if (isAsio)
            {
                appendSection("device", localization.text("dialogs.enabledDevices.devices", L"Devices"));
            }
            else
            {
                appendSection("input", localization.text("dialogs.enabledDevices.inputs", L"Input devices"));
                appendSection("output", localization.text("dialogs.enabledDevices.outputs", L"Output devices"));
            }

            if (deviceSections.Children().Size() == 0)
            {
                auto emptyText = TextBlock();
                emptyText.Text(localization.text("dialogs.enabledDevices.noDevices", L"No devices were detected for this backend."));
                emptyText.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
                deviceSections.Children().Append(emptyText);
            }
        };

        backendBox.SelectionChanged([rebuild](IInspectable const&, SelectionChangedEventArgs const&)
        {
            if(*rebuild)(*rebuild)();
        });
        backendToggle.Checked([backendBox, backendEnabled, updating, rebuild](IInspectable const&, RoutedEventArgs const&)
        {
            if (*updating)
                return;
            const int index = backendBox.SelectedIndex();
            if (index >= 0 && index < (int) backendEnabled->size())
                (*backendEnabled)[(size_t) index] = true;
            if(*rebuild)(*rebuild)();
        });
        backendToggle.Unchecked([backendBox, backendEnabled, updating, rebuild](IInspectable const&, RoutedEventArgs const&)
        {
            if (*updating)
                return;
            const int index = backendBox.SelectedIndex();
            if (index >= 0 && index < (int) backendEnabled->size())
                (*backendEnabled)[(size_t) index] = false;
            if(*rebuild)(*rebuild)();
        });
        if(*rebuild)(*rebuild)();

        auto modeCard = Border();
        modeCard.Background(resourceBrush(L"AppCardBrush", themedFallback(makeColor(255, 255, 255), makeColor(39, 39, 39))));
        modeCard.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(225, 225, 225), makeColor(58, 58, 58))));
        modeCard.BorderThickness(ThicknessHelper::FromUniformLength(1));
        modeCard.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        modeCard.Padding(ThicknessHelper::FromUniformLength(14));

        auto modeStack = StackPanel();
        modeStack.Spacing(10);
        auto hint = TextBlock();
        hint.Text(localization.text(
            "dialogs.enabledDevices.description",
            L"Choose an audio backend, then enable only the devices LightHostModern may use."));
        hint.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
        hint.TextWrapping(TextWrapping::Wrap);
        modeStack.Children().Append(hint);
        modeStack.Children().Append(backendBox);
        modeStack.Children().Append(backendToggle);
        modeCard.Child(modeStack);

        auto contentStack = StackPanel();
        contentStack.Spacing(12);
        contentStack.Children().Append(modeCard);


        auto scroller = ScrollViewer();
        scroller.VerticalScrollMode(ScrollMode::Enabled);
        scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroller.HorizontalScrollMode(ScrollMode::Disabled);
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        contentStack.Children().Append(deviceSections);
        scroller.MaxHeight((std::max)(120.0, (std::min)(520.0, RootLayout().XamlRoot().Size().Height - 240.0)));
        scroller.Content(contentStack);
        Automation::AutomationProperties::SetAutomationId(scroller, L"EnabledDevicesScroll");

        auto dialog = ContentDialog();
        dialog.XamlRoot(RootLayout().XamlRoot());
        dialog.RequestedTheme(RootLayout().ActualTheme());
        dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
        dialog.Content(scroller);
        dialog.PrimaryButtonText(localization.text("common.save", L"Save"));
        dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        sizeDialogToViewport(dialog, RootLayout(), 0.75);
        enableDialogBackgroundDefocus(dialog);

        *requestRename=[weakDialog=make_weak(dialog)]{if(auto parent=weakDialog.get())parent.Hide();};
        for(;;){
            const auto result=co_await lightHostModern::ui::showAppDialog(dialog);
            if(!renameRequest->first.empty()){
                const auto key=renameRequest->first,original=renameRequest->second;renameRequest->first=L"";
                const auto current=deviceAliases.GetNamedString(key,original);
                const auto edited=co_await lightHostModern::ui::editDisplayName(RootLayout(),localization,localization.translatedSource(L"Rename device"),current.empty()?original:current);
                if(edited){auto value=unbox_value<hstring>(edited);if(value==original)value=L"";deviceAliases.SetNamedValue(key,JsonValue::CreateStringValue(value));changedAliases.SetNamedValue(key,JsonValue::CreateStringValue(value));if(*rebuild)(*rebuild)();}
                continue;
            }
            if(result!=ContentDialogResult::Primary)co_return;break;
        }
        int changedCount = 0;
        JsonObject request, backendEdits, deviceEdits;
        request.SetNamedValue(L"token", JsonValue::CreateStringValue(hs(extractString(choicesJson,"choiceToken"))));
        for (size_t i=0;i<backendEnabled->size();++i) if ((*backendEnabled)[i]!=originalBackendEnabled[i]) {
            backendEdits.SetNamedValue(hs(allAudioBackendNames[i]),JsonValue::CreateBooleanValue((*backendEnabled)[i])); ++changedCount;
        }
        for (size_t i=0;i<deviceEnabled->size();++i) if ((*deviceEnabled)[i]!=originalDeviceEnabled[i]) {
            deviceEdits.SetNamedValue(hs(allAudioDeviceChoices[i]),JsonValue::CreateBooleanValue((*deviceEnabled)[i])); ++changedCount;
        }
        request.SetNamedValue(L"backends",backendEdits);request.SetNamedValue(L"devices",deviceEdits);request.SetNamedValue(L"names",changedAliases);
        changedCount+=changedAliases.Size();
        if(changedCount>0 && !(co_await sendCommand("update-enabled-audio-choices:"+to_string(request.Stringify()))))co_return;
        refreshSnapshot();
        showNotification(changedCount == 0
            ? localization.text("dialogs.enabledDevices.unchanged", L"Enabled devices unchanged.").c_str()
            : localization.format(
                "dialogs.enabledDevices.updated",
                L"{0} enabled device setting(s) updated.",
                { std::to_wstring(changedCount) }).c_str(),false);        }
        catch (...) { if (!windowClosing) showNotification(localization.text("dialogs.enabledDevices.loadFailed", L"Could not load the available audio device list.").c_str()); }
    }

    winrt::fire_and_forget MainWindow::IconModeBox_SelectionChanged(IInspectable, SelectionChangedEventArgs)
    {
        auto lifetime = get_strong();
        if (syncingConfigControls || !IconModeBox() || IconModeBox().SelectedIndex() < 0)
            co_return;

        std::string mode = "color";
        if (IconModeBox().SelectedIndex() == 1)
            mode = "white";
        else if (IconModeBox().SelectedIndex() == 2)
            mode = "black";

        currentIconMode = mode;
        applyIconMode(mode);
        if ((co_await sendCommand("set-tray-icon-mode:" + mode)))
            showNotification(localization.text("settings.iconUpdated", L"App icon updated.").c_str(),false);
    }

    void MainWindow::resetDefaultPluginScanPaths()
    {
        pluginScanPaths = defaultPluginScanPaths();
        optionalPluginScanPaths={pluginScanPaths.begin(),pluginScanPaths.end()};
        const auto optionalStored=loadUiSetting(L"Plugins",L"OptionalScanPaths",L"");
        if(!optionalStored.empty()) {
            lightHostModern::ipc::JsonArray optional;
            if(lightHostModern::ipc::JsonArray::TryParse(optionalStored,optional)) {
                optionalPluginScanPaths.clear();for(const auto& path:optional)if(path.ValueType()==lightHostModern::ipc::JsonValueType::String)optionalPluginScanPaths.insert(to_string(path.GetString()));
            }
        }
        const auto stored = loadUiSetting(L"Plugins", L"ScanPaths");
        if (!stored.empty()) {
            lightHostModern::ipc::JsonArray paths;
            if (lightHostModern::ipc::JsonArray::TryParse(stored, paths)) {
                std::vector<std::string> restored;
                bool valid = true;
                for (const auto& path : paths) {
                    if (path.ValueType() != lightHostModern::ipc::JsonValueType::String) { valid = false; break; }
                    restored.push_back(to_string(path.GetString()));
                }
                if (valid) pluginScanPaths = std::move(restored);
            }
        }
    }

    winrt::fire_and_forget MainWindow::prepareNormalClose()
    {
        auto lifetime=get_strong();if(pendingNormalClose||windowClosing)co_return;pendingNormalClose=true;
        normalCloseDeadline=GetTickCount64()+5000;RootLayout().IsHitTestVisible(false);
        if(operatingPresenter)operatingPresenter->freezeEdits(true);
        bool saved=true;try{if(operatingPresenter)saved=co_await operatingPresenter->flushPending(normalCloseDeadline);}catch(...){saved=false;}
        pendingNormalClose=false;normalCloseDeadline=0;
        if(windowClosing)co_return;
        if(!saved){RootLayout().IsHitTestVisible(true);if(operatingPresenter)operatingPresenter->freezeEdits(false);showNotification(localization.translatedSource(L"Pending chain edits could not be sent. A local recovery copy was kept. Try closing again after reconnecting.").c_str());co_return;}
        co_await finishNormalCloseAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::finishNormalCloseAsync()
    {
        auto lifetime=get_strong();if(windowClosing)co_return;
        // Keep the window/dispatcher alive until asynchronous shutdown drains.
        // Awaiting from Closed can strand the UI process after its HWND has
        // already gone away (the host has quit, but Application::Exit never runs).
        // Restart/reset/update already saved and stopped their host; they enter
        // here directly without attempting another flush against that host.
        windowClosing = true;
        if(verboseLogsPresenter)verboseLogsPresenter->close();
        if(operatingPresenter)operatingPresenter->close();
        updateService->cancel();
        if(updateCheckTimer)updateCheckTimer.Stop();
        if(notificationTimer)notificationTimer.Stop();
        if (refreshTimer) refreshTimer.Stop();
        hostConnection->close();
        if (closeQuitsHost)
        {
            // Closing must not wait behind a stalled plugin command in the UI
            // FIFO. Cancel it first, then make one bounded quit attempt.
            auto shutdownTransport = std::make_shared<lightHostModern::ipc::ClientState>();
            try { co_await lightHostModern::ipc::requestAsync(shutdownTransport, hostPipeName, "quit-host", 1000); }
            catch (...) { winUILog("Host quit request failed during normal close; continuing UI shutdown."); }
            shutdownTransport->close();
        }

        co_await updateService->cancelAndWaitAsync();
        normalCloseReady=true;Close();
    }

    void MainWindow::Window_Closed(IInspectable, WindowEventArgs)
    {
        windowMaterial.close();

        try
        {
            if (refreshTimer)
                refreshTimer.Stop();

            closeFluentDropdowns();
            SystemBackdrop(nullptr);
            Content(nullptr);
        }
        catch (...) {}

        // End task bypasses this path. Only acknowledge a completed normal close,
        // so a forced UI exit also stops the separate audio host.
        const auto closeEventName = commandLineOptionValue(L"--ui-close-event");
        if (!closeEventName.empty())
        {
            lightHostModern::ipc::Handle closeEvent(OpenEventW(EVENT_MODIFY_STATE, FALSE, closeEventName.c_str()));
            if (closeEvent) SetEvent(closeEvent.get());
        }
        Microsoft::UI::Xaml::Application::Current().Exit();
    }
}
