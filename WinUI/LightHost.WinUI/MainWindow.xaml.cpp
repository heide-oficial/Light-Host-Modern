#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "WinUIDebug.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.UI.Text.h>
#include <algorithm>
#include <cctype>
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
using winrt::LightHostWinUI::implementation::ChannelRowData;

namespace
{
    constexpr wchar_t GITHUB_REPOSITORY_URL[] = L"https://github.com/heide-oficial/Light-Host-Modern";
    constexpr wchar_t GITHUB_SHOWCASE_URL[] = L"https://github.com/heide-oficial/Light-Host-Modern/issues/new?title=%5BSHOWCASE%20VIDEO%5D%20Video%20title%20here&labels=showcase%20video&body=Here%27s%20my%20video%20showcasing%20or%20featuring%20the%20app%3A%20%5BINSERT%20LINK%20HERE%5D";
    constexpr wchar_t KOFI_URL[] = L"https://ko-fi.com/heide_oficial";

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return (char) std::tolower(c);
        });
        return value;
    }

    std::string wideToUtf8(std::wstring const& value)
    {
        if (value.empty())
            return {};

        const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result((size_t) required - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
        return result;
    }

    std::wstring utf8ToWide(std::string const& value)
    {
        if (value.empty())
            return {};

        const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        std::wstring result((size_t) required - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
        return result;
    }

    std::string requestHost(std::wstring const& pipeName, std::string const& requestText)
    {
        const bool logTransport = requestText != "snapshot"
            && requestText != "state-snapshot"
            && requestText != "telemetry";

        try
        {
            if (pipeName.empty())
            {
                if (logTransport)
                    winUILog("IPC request failed before connect: empty pipe name for " + requestText);
                return {};
            }

            if (!WaitNamedPipeW(pipeName.c_str(), 50) && logTransport)
                winUILog("IPC WaitNamedPipe failed for " + requestText + ". win32=" + std::to_string((unsigned long) GetLastError()));

            HANDLE pipe = CreateFileW(pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                if (logTransport)
                    winUILog("IPC CreateFile failed for " + requestText + ". win32=" + std::to_string((unsigned long) GetLastError()));
                return {};
            }

            DWORD bytesWritten = 0;
            const auto request = requestText + "\n";
            if (!WriteFile(pipe, request.c_str(), (DWORD) request.size(), &bytesWritten, nullptr))
            {
                if (logTransport)
                    winUILog("IPC WriteFile failed for " + requestText + ". win32=" + std::to_string((unsigned long) GetLastError()));
                CloseHandle(pipe);
                return {};
            }

            std::string response;
            for (;;)
            {
                char buffer[64 * 1024] = {};
                DWORD bytesRead = 0;
                const BOOL readOk = ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr);
                const DWORD readError = readOk ? ERROR_SUCCESS : GetLastError();
                if (bytesRead > 0)
                    response.append(buffer, buffer + bytesRead);

                if (readOk)
                    break;

                if (readError != ERROR_MORE_DATA)
                {
                    if (logTransport)
                        winUILog("IPC ReadFile failed for " + requestText + ". win32=" + std::to_string((unsigned long) readError)
                            + " bytesRead=" + std::to_string((unsigned long) bytesRead));
                    response.clear();
                    break;
                }
            }

            CloseHandle(pipe);
            if (response.empty() && logTransport)
                winUILog("IPC response was empty for " + requestText + ".");

            return response;
        }
        catch (hresult_error const& e)
        {
            if (logTransport)
                winUILog("IPC request threw hresult_error for " + requestText + ": " + winrt::to_string(e.message()));
            return {};
        }
        catch (std::exception const& e)
        {
            if (logTransport)
                winUILog(std::string("IPC request threw std::exception for ") + requestText + ": " + e.what());
            return {};
        }
        catch (...)
        {
            if (logTransport)
                winUILog("IPC request threw unknown exception for " + requestText + ".");
            return {};
        }
    }

    std::string requestSnapshot(std::wstring const& pipeName)
    {
        return requestHost(pipeName, "state-snapshot");
    }

    std::string requestTelemetry(std::wstring const& pipeName)
    {
        return requestHost(pipeName, "telemetry");
    }

    std::string extractString(std::string const& json, std::string const& key, std::string const& fallback = "")
    {
        const auto marker = "\"" + key + "\":";
        auto pos = json.find(marker);
        if (pos == std::string::npos)
            return fallback;

        pos = json.find('"', pos + marker.size());
        if (pos == std::string::npos)
            return fallback;

        std::string result;
        bool escaped = false;
        for (auto i = pos + 1; i < json.size(); ++i)
        {
            const char c = json[i];
            if (escaped)
            {
                if (c == 'n') result.push_back('\n');
                else if (c == 'r') result.push_back('\r');
                else if (c == 't') result.push_back('\t');
                else result.push_back(c);
                escaped = false;
                continue;
            }

            if (c == '\\')
            {
                escaped = true;
                continue;
            }

            if (c == '"')
                return result;

            result.push_back(c);
        }

        return fallback;
    }

    double extractNumber(std::string const& json, std::string const& key, double fallback = 0.0)
    {
        const auto marker = "\"" + key + "\":";
        auto pos = json.find(marker);
        if (pos == std::string::npos)
            return fallback;

        pos += marker.size();
        auto end = pos;
        while (end < json.size() && (isdigit((unsigned char) json[end]) || json[end] == '-' || json[end] == '.'))
            ++end;

        try
        {
            return std::stod(json.substr(pos, end - pos));
        }
        catch (...)
        {
            return fallback;
        }
    }

    bool extractBool(std::string const& json, std::string const& key, bool fallback = false)
    {
        const auto marker = "\"" + key + "\":";
        auto pos = json.find(marker);
        if (pos == std::string::npos)
            return fallback;

        pos += marker.size();
        while (pos < json.size() && json[pos] == ' ')
            ++pos;

        if (json.compare(pos, 4, "true") == 0)
            return true;
        if (json.compare(pos, 5, "false") == 0)
            return false;

        return fallback;
    }

    std::string formatNumber(double value, int precision = 0)
    {
        std::ostringstream stream;
        stream.setf(std::ios::fixed);
        stream.precision(precision);
        stream << value;
        return stream.str();
    }

    std::string buildPluginList(std::string const& json)
    {
        const auto arrayPos = json.find("\"activePlugins\":[");
        if (arrayPos == std::string::npos)
            return "Aguardando dados.";

        const auto arrayEnd = json.find(']', arrayPos);
        if (arrayEnd == std::string::npos || arrayEnd <= arrayPos + 17)
            return "Nenhum plugin ativo.";

        std::string list;
        auto pos = arrayPos;
        while ((pos = json.find('{', pos)) != std::string::npos && pos < arrayEnd)
        {
            const auto objectEnd = json.find('}', pos);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
                break;

            const auto object = json.substr(pos, objectEnd - pos + 1);
            const auto name = extractString(object, "name", "Unknown");
            const auto manufacturer = extractString(object, "manufacturer", "");
            const auto format = extractString(object, "format", "");
            const auto order = (int) extractNumber(object, "order", 0);

            if (!list.empty())
                list += "\n";

            list += std::to_string(order) + ". " + name;
            if (!manufacturer.empty())
                list += " - " + manufacturer;
            if (!format.empty())
                list += " (" + format + ")";

            pos = objectEnd + 1;
        }

        return list.empty() ? "Nenhum plugin ativo." : list;
    }

    std::vector<std::string> extractActivePluginLabels(std::string const& json)
    {
        std::vector<std::string> labels;
        const auto arrayPos = json.find("\"activePlugins\":[");
        if (arrayPos == std::string::npos)
            return labels;

        const auto arrayEnd = json.find(']', arrayPos);
        if (arrayEnd == std::string::npos)
            return labels;

        auto pos = arrayPos;
        while ((pos = json.find('{', pos)) != std::string::npos && pos < arrayEnd)
        {
            const auto objectEnd = json.find('}', pos);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
                break;

            const auto object = json.substr(pos, objectEnd - pos + 1);
            const auto name = extractString(object, "name", "Unknown");
            const auto bypassed = object.find("\"bypassed\":true") != std::string::npos;
            labels.push_back(name + (bypassed ? " (bypassed)" : ""));
            pos = objectEnd + 1;
        }

        return labels;
    }

    std::vector<std::string> extractKnownPluginLabels(std::string const& json)
    {
        std::vector<std::string> labels;
        const auto arrayPos = json.find("\"knownPluginList\":[");
        if (arrayPos == std::string::npos)
            return labels;

        const auto arrayEnd = json.find(']', arrayPos);
        if (arrayEnd == std::string::npos)
            return labels;

        auto pos = arrayPos;
        while ((pos = json.find('{', pos)) != std::string::npos && pos < arrayEnd)
        {
            const auto objectEnd = json.find('}', pos);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
                break;

            const auto object = json.substr(pos, objectEnd - pos + 1);
            const auto name = extractString(object, "name", "Unknown");
            const auto manufacturer = extractString(object, "manufacturer", "");
            const auto format = extractString(object, "format", "");

            std::string label = name;
            if (!manufacturer.empty())
                label += " - " + manufacturer;
            if (!format.empty())
                label += " (" + format + ")";

            labels.push_back(label);
            pos = objectEnd + 1;
        }

        return labels;
    }

    std::vector<std::string> extractStringArray(std::string const& json, std::string const& key)
    {
        std::vector<std::string> values;
        const auto marker = "\"" + key + "\":[";
        const auto arrayPos = json.find(marker);
        if (arrayPos == std::string::npos)
            return values;

        const auto arrayEnd = json.find(']', arrayPos + marker.size());
        if (arrayEnd == std::string::npos)
            return values;

        auto pos = arrayPos + marker.size();
        while ((pos = json.find('"', pos)) != std::string::npos && pos < arrayEnd)
        {
            std::string value;
            bool escaped = false;
            for (auto i = pos + 1; i < arrayEnd; ++i)
            {
                const char c = json[i];
                if (escaped)
                {
                    if (c == 'n') value.push_back('\n');
                    else if (c == 'r') value.push_back('\r');
                    else if (c == 't') value.push_back('\t');
                    else value.push_back(c);
                    escaped = false;
                    continue;
                }

                if (c == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (c == '"')
                {
                    values.push_back(value);
                    pos = i + 1;
                    break;
                }

                value.push_back(c);
            }
        }

        return values;
    }

    std::vector<double> extractNumberArray(std::string const& json, std::string const& key)
    {
        std::vector<double> values;
        const auto marker = "\"" + key + "\":[";
        const auto arrayPos = json.find(marker);
        if (arrayPos == std::string::npos)
            return values;

        const auto arrayEnd = json.find(']', arrayPos + marker.size());
        if (arrayEnd == std::string::npos)
            return values;

        auto pos = arrayPos + marker.size();
        while (pos < arrayEnd)
        {
            while (pos < arrayEnd && (json[pos] == ' ' || json[pos] == ','))
                ++pos;

            auto end = pos;
            while (end < arrayEnd && (isdigit((unsigned char) json[end]) || json[end] == '-' || json[end] == '.'))
                ++end;

            if (end > pos)
            {
                try { values.push_back(std::stod(json.substr(pos, end - pos))); }
                catch (...) {}
            }

            pos = end + 1;
        }

        return values;
    }

    std::vector<bool> extractBoolArray(std::string const& json, std::string const& key)
    {
        std::vector<bool> values;
        const auto marker = "\"" + key + "\":[";
        const auto arrayPos = json.find(marker);
        if (arrayPos == std::string::npos)
            return values;

        const auto arrayEnd = json.find(']', arrayPos + marker.size());
        if (arrayEnd == std::string::npos)
            return values;

        auto pos = arrayPos + marker.size();
        while (pos < arrayEnd)
        {
            while (pos < arrayEnd && (json[pos] == ' ' || json[pos] == ','))
                ++pos;

            if (json.compare(pos, 4, "true") == 0)
            {
                values.push_back(true);
                pos += 4;
            }
            else if (json.compare(pos, 5, "false") == 0)
            {
                values.push_back(false);
                pos += 5;
            }
            else
            {
                ++pos;
            }
        }

        return values;
    }

    hstring hs(std::string const& value)
    {
        return hstring(utf8ToWide(value));
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

    std::wstring uiSettingsFilePath()
    {
        auto base = environmentPath(L"LOCALAPPDATA");
        if (base.empty())
            base = L".";

        const auto directory = base + L"\\LightHostModern";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\ui-settings.ini";
    }

    int clampBackdropModeIndex(int index)
    {
        if (index < 0 || index > 3)
            return 0;

        return index;
    }

    int loadBackdropModeIndex()
    {
        const auto settingsFile = uiSettingsFilePath();
        return clampBackdropModeIndex((int) GetPrivateProfileIntW(L"Appearance", L"BackdropMode", 0, settingsFile.c_str()));
    }

    void saveBackdropModeIndex(int index)
    {
        const auto value = std::to_wstring(clampBackdropModeIndex(index));
        const auto settingsFile = uiSettingsFilePath();
        WritePrivateProfileStringW(L"Appearance", L"BackdropMode", value.c_str(), settingsFile.c_str());
    }

    std::wstring loadUiSetting(wchar_t const* section, wchar_t const* key, wchar_t const* fallback = L"")
    {
        std::wstring value(2048, L'\0');
        const auto settingsFile = uiSettingsFilePath();
        const auto length = GetPrivateProfileStringW(section, key, fallback, value.data(), static_cast<DWORD>(value.size()), settingsFile.c_str());
        value.resize(length);
        return value;
    }

    void saveUiSetting(wchar_t const* section, wchar_t const* key, std::wstring const& value)
    {
        const auto settingsFile = uiSettingsFilePath();
        WritePrivateProfileStringW(section, key, value.c_str(), settingsFile.c_str());
    }

    std::array<int, 3> semanticVersion(std::wstring value)
    {
        if (!value.empty() && (value.front() == L'v' || value.front() == L'V'))
            value.erase(value.begin());

        std::array<int, 3> result {};
        std::wistringstream stream(value);
        wchar_t separator = 0;
        stream >> result[0] >> separator >> result[1] >> separator >> result[2];
        return result;
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

    struct PluginRowData
    {
        std::string name;
        std::string manufacturer;
        std::string format;
        std::string path;
        std::string status;
        bool bypassed = false;
        int originalIndex = -1;
    };

    std::vector<PluginRowData> filterAndSortPluginRows(std::vector<PluginRowData> rows,
                                                       std::wstring const& query,
                                                       int sortMode,
                                                       bool running)
    {
        const auto normalizedQuery = toLower(wideToUtf8(query));
        if (!normalizedQuery.empty())
        {
            std::erase_if(rows, [&normalizedQuery](PluginRowData const& row)
            {
                return toLower(row.name).find(normalizedQuery) == std::string::npos;
            });
        }

        auto compareText = [](std::string const& left, std::string const& right)
        {
            return toLower(left) < toLower(right);
        };
        auto statusRank = [running](PluginRowData const& row, bool primary)
        {
            if (running)
                return primary ? (row.bypassed ? 1 : 0) : (row.bypassed ? 0 : 1);
            const bool isRunning = row.status.rfind("Running", 0) == 0;
            return primary ? (isRunning ? 1 : 0) : (isRunning ? 0 : 1);
        };

        std::stable_sort(rows.begin(), rows.end(), [&](PluginRowData const& left, PluginRowData const& right)
        {
            switch (sortMode)
            {
                case 1: return compareText(left.name, right.name);
                case 2: return compareText(right.name, left.name);
                case 3: return compareText(left.manufacturer, right.manufacturer);
                case 4: return compareText(right.manufacturer, left.manufacturer);
                case 5: return statusRank(left, true) < statusRank(right, true);
                case 6: return statusRank(left, false) < statusRank(right, false);
                case 7: return left.format == right.format ? false : left.format == "VST3";
                case 8: return left.format == right.format ? false : left.format != "VST3";
                default: return false;
            }
        });
        return rows;
    }

    std::string pluginRowKey(PluginRowData const& row)
    {
        return row.name + "|" + row.manufacturer + "|" + row.format + "|" + row.path + "|" + row.status + "|" + (row.bypassed ? "1" : "0");
    }

    std::string pluginIdentityKey(PluginRowData const& row)
    {
        return row.name + "|" + row.manufacturer + "|" + row.format + "|" + row.path;
    }

    void applyInstalledPluginRuntimeStatus(std::vector<PluginRowData>& installedRows,
        std::vector<PluginRowData> const& activeRows)
    {
        struct RuntimeStatus
        {
            int count = 0;
            bool anyBypassed = false;
            bool anyActive = false;
        };

        std::unordered_map<std::string, RuntimeStatus> activeByIdentity;
        for (auto const& plugin : activeRows)
        {
            auto& status = activeByIdentity[pluginIdentityKey(plugin)];
            ++status.count;
            status.anyBypassed = status.anyBypassed || plugin.bypassed;
            status.anyActive = status.anyActive || !plugin.bypassed;
        }

        for (auto& plugin : installedRows)
        {
            if (plugin.status == "Error")
                continue;

            const auto match = activeByIdentity.find(pluginIdentityKey(plugin));
            if (match == activeByIdentity.end())
            {
                plugin.status = "Available";
                continue;
            }

            if (match->second.count > 1)
                plugin.status = "Running (multiple)";
            else if (match->second.anyBypassed && !match->second.anyActive)
                plugin.status = "Running (bypassed)";
            else
                plugin.status = "Running";
        }
    }

    std::vector<PluginRowData> extractActivePluginRows(std::string const& json)
    {
        std::vector<PluginRowData> rows;
        const auto arrayPos = json.find("\"activePlugins\":[");
        if (arrayPos == std::string::npos)
            return rows;

        const auto arrayEnd = json.find(']', arrayPos);
        if (arrayEnd == std::string::npos)
            return rows;

        auto pos = arrayPos;
        while ((pos = json.find('{', pos)) != std::string::npos && pos < arrayEnd)
        {
            const auto objectEnd = json.find('}', pos);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
                break;

            const auto object = json.substr(pos, objectEnd - pos + 1);
            PluginRowData row;
            row.name = extractString(object, "name", "Unknown");
            row.manufacturer = extractString(object, "manufacturer", "");
            row.format = extractString(object, "format", "");
            row.path = extractString(object, "path", "");
            row.bypassed = extractBool(object, "bypassed", false);
            row.status = row.bypassed ? "Bypassed" : "Active";
            row.originalIndex = static_cast<int>(rows.size());
            rows.push_back(row);
            pos = objectEnd + 1;
        }

        return rows;
    }

    std::vector<PluginRowData> extractKnownPluginRows(std::string const& json)
    {
        std::vector<PluginRowData> rows;
        const auto arrayPos = json.find("\"knownPluginList\":[");
        if (arrayPos == std::string::npos)
            return rows;

        const auto arrayEnd = json.find(']', arrayPos);
        if (arrayEnd == std::string::npos)
            return rows;

        auto pos = arrayPos;
        while ((pos = json.find('{', pos)) != std::string::npos && pos < arrayEnd)
        {
            const auto objectEnd = json.find('}', pos);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd)
                break;

            const auto object = json.substr(pos, objectEnd - pos + 1);
            PluginRowData row;
            row.name = extractString(object, "name", "Unknown");
            row.manufacturer = extractString(object, "manufacturer", "");
            row.format = extractString(object, "format", "");
            row.path = extractString(object, "path", "");
            row.status = row.name.empty() || row.name == "Unknown" ? "Error" : "Available";
            row.originalIndex = static_cast<int>(rows.size());
            rows.push_back(row);
            pos = objectEnd + 1;
        }

        return rows;
    }

    std::vector<std::string> pluginRowKeys(std::vector<PluginRowData> const& rows)
    {
        std::vector<std::string> keys;
        keys.reserve(rows.size());
        for (auto const& row : rows)
            keys.push_back(pluginRowKey(row));
        return keys;
    }

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
        bool input)
    {
        std::vector<ChannelRowData> rows;
        for (int i = 0; i < (int) labels.size(); i += 2)
        {
            ChannelRowData row;
            row.startIndex = i;
            row.endIndex = (std::min)(i + 1, (int) labels.size() - 1);

            const auto first = labels[(size_t) i];
            const auto second = row.endIndex > i ? labels[(size_t) row.endIndex] : std::string();
            if (row.endIndex > i)
            {
                row.label = pairChannelLabel(backend, first, second, i, input);
                const bool firstActive = i < (int) activeStates.size() && activeStates[(size_t) i];
                const bool secondActive = row.endIndex < (int) activeStates.size() && activeStates[(size_t) row.endIndex];
                row.active = firstActive && secondActive;
            }
            else
            {
                row.label = first.empty()
                    ? std::string(input ? "Input" : "Output") + " channel " + std::to_string(i + 1)
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
        ToolTipService::SetToolTip(button, box_value(hstring(tooltip.c_str())));
        return button;
    }

    MenuFlyoutItem actionMenuItem(std::wstring const& text,
        std::wstring const& glyph,
        int index,
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

    Button rowActionsMenuButton(int index, std::wstring const& tooltip = L"Actions")
    {
        auto button = iconButton(L"\xE712", index, tooltip);
        button.Width(40);
        button.Height(40);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, tooltip);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
            button, hstring(L"PluginActions-" + std::to_wstring(index)));
        return button;
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

    TextBlock pluginNameText(std::string const& text, double fontSize = 17.0)
    {
        auto label = rowText(text, fontSize);
        label.TextWrapping(TextWrapping::NoWrap);
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        ToolTipService::SetToolTip(label, box_value(hs(text)));
        return label;
    }

    TextBlock channelLabel(std::string const& text)
    {
        auto label = rowText(text, 15);
        label.Margin(ThicknessHelper::FromLengths(8, 0, 0, 0));
        label.VerticalAlignment(VerticalAlignment::Center);
        return label;
    }

    Border pill(std::string const& text, std::string const& displayText = {})
    {
        auto status = Border();
        status.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        status.Padding(ThicknessHelper::FromLengths(10, 5, 10, 5));
        status.BorderThickness(ThicknessHelper::FromUniformLength(1));
        auto foreground = resourceBrush(L"TextFillColorPrimaryBrush", themedFallback(makeColor(64, 64, 64), makeColor(240, 240, 240)));

        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8);
        row.VerticalAlignment(VerticalAlignment::Center);

        auto dot = FontIcon();
        dot.Glyph(L"\xE915");
        dot.FontSize(9);

        if (text == "Active" || text == "Running" || text == "Running (multiple)" || text == "Running (multiple instances)")
        {
            status.Background(resourceBrush(L"AppStatusSuccessBackgroundBrush", themedFallback(makeColor(223, 246, 221), makeColorA(95, 20, 82, 42))));
            status.BorderBrush(resourceBrush(L"AppStatusSuccessStrokeBrush", themedFallback(makeColor(16, 124, 16), makeColor(48, 209, 88))));
            foreground = resourceBrush(L"AppStatusSuccessStrokeBrush", themedFallback(makeColor(12, 97, 12), makeColor(220, 255, 226)));
            dot.Foreground(foreground);
        }
        else if (text == "Available")
        {
            status.Background(SolidColorBrush(makeColorA(70, 0, 120, 212)));
            status.BorderBrush(SolidColorBrush(makeColor(96, 205, 255)));
            foreground = SolidColorBrush(makeColor(205, 238, 255));
            dot.Foreground(foreground);
        }
        else if (text == "Error")
        {
            status.Background(resourceBrush(L"AppStatusCriticalBackgroundBrush", themedFallback(makeColor(253, 231, 233), makeColorA(95, 68, 22, 26))));
            status.BorderBrush(resourceBrush(L"AppStatusCriticalStrokeBrush", themedFallback(makeColor(196, 43, 28), makeColor(255, 99, 89))));
            foreground = resourceBrush(L"AppStatusCriticalStrokeBrush", themedFallback(makeColor(164, 38, 28), makeColor(255, 220, 218)));
            dot.Foreground(foreground);
        }
        else
        {
            status.Background(resourceBrush(L"AppControlBrush", themedFallback(makeColor(250, 250, 250), makeColorA(95, 52, 52, 52))));
            status.BorderBrush(resourceBrush(L"AppControlStrokeBrush", themedFallback(makeColor(218, 218, 218), makeColorA(140, 96, 96, 96))));
            foreground = resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(80, 80, 80), makeColor(218, 222, 228)));
            dot.Foreground(foreground);
        }

        row.Children().Append(dot);
        auto label = rowText(displayText.empty() ? text : displayText, 14);
        label.Foreground(foreground);
        row.Children().Append(label);
        status.Child(row);
        return status;
    }

    Border formatBadge(std::string const& text)
    {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Padding(ThicknessHelper::FromLengths(10, 5, 10, 5));
        border.BorderThickness(ThicknessHelper::FromUniformLength(1));
        border.Background(resourceBrush(L"AppControlBrush", themedFallback(makeColor(250, 250, 250), makeColorA(95, 44, 44, 44))));
        border.BorderBrush(resourceBrush(L"AppControlStrokeBrush", themedFallback(makeColor(218, 218, 218), makeColorA(140, 96, 96, 96))));
        border.Child(rowText(text.empty() ? "--" : text, 14));
        return border;
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
            const auto background = dark ? makeColor(32, 32, 32) : makeColor(243, 243, 243);
            const auto foreground = dark ? makeColor(245, 247, 251) : makeColor(32, 32, 32);
            const auto inactiveForeground = dark ? makeColor(150, 160, 174) : makeColor(110, 110, 110);
            const auto hoverBackground = dark ? makeColor(48, 48, 48) : makeColor(230, 230, 230);
            const auto pressedBackground = dark ? makeColor(60, 60, 60) : makeColor(218, 218, 218);
            titleBar.BackgroundColor(background);
            titleBar.ForegroundColor(foreground);
            titleBar.InactiveBackgroundColor(background);
            titleBar.InactiveForegroundColor(inactiveForeground);
            titleBar.ButtonBackgroundColor(background);
            titleBar.ButtonForegroundColor(foreground);
            titleBar.ButtonHoverBackgroundColor(hoverBackground);
            titleBar.ButtonHoverForegroundColor(foreground);
            titleBar.ButtonPressedBackgroundColor(pressedBackground);
            titleBar.ButtonPressedForegroundColor(foreground);
            titleBar.ButtonInactiveBackgroundColor(background);
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
            if (auto brushResource = Application::Current().Resources().Lookup(box_value(hstring(key))).try_as<Brush>())
                return brushResource;
        }
        catch (...)
        {
        }

        return brush(fallback);
    }

    void styleCombo(ComboBox const& combo)
    {
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
        dialog.PointerPressed([dialog](Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            if (!pointerStartedInsideInteractiveControl(args, dialog))
                dialog.Focus(FocusState::Programmatic);
        });
    }

    void sizeDialogToViewport(ContentDialog const& dialog, FrameworkElement const& viewport, double fraction)
    {
        const double availableWidth = (std::max)(320.0, viewport.ActualWidth() - 48.0);
        const double targetWidth = (std::min)(availableWidth, (std::max)(320.0, viewport.ActualWidth() * fraction));
        dialog.Resources().Insert(box_value(L"ContentDialogMinWidth"), box_value(targetWidth));
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(targetWidth));
    }

}

namespace winrt::LightHostWinUI::implementation
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

    void MainWindow::createMeterSegments(StackPanel const& host, std::vector<Border>& segments)
    {
        segments.clear();
        host.Children().Clear();

        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(4);
        row.VerticalAlignment(VerticalAlignment::Center);

        for (int i = 0; i < 28; ++i)
        {
            auto segment = Border();
            segment.Width(7);
            segment.Height(26);
            segment.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
            segment.Background(brush(themedFallback(makeColor(211, 216, 222), makeColor(68, 72, 78))));
            row.Children().Append(segment);
            segments.push_back(segment);
        }

        host.Children().Append(row);
    }

    void MainWindow::updateMeterSegments(std::vector<Border> const& segments, double level)
    {
        if (segments.empty())
            return;

        if (level < 0.0)
            level = 0.0;
        if (level > 1.0)
            level = 1.0;

        const int activeCount = (int) std::round(level * (double) segments.size());
        for (int i = 0; i < (int) segments.size(); ++i)
        {
            if (i >= activeCount)
            {
                segments[(size_t) i].Background(brush(themedFallback(makeColor(211, 216, 222), makeColor(68, 72, 78))));
            }
            else if (i > (int) segments.size() * 0.82)
            {
                segments[(size_t) i].Background(brush(themedFallback(makeColor(196, 43, 28), makeColor(255, 99, 89))));
            }
            else if (i > (int) segments.size() * 0.64)
            {
                segments[(size_t) i].Background(brush(themedFallback(makeColor(157, 93, 0), makeColor(255, 185, 40))));
            }
            else
            {
                segments[(size_t) i].Background(brush(themedFallback(makeColor(16, 137, 62), makeColor(77, 217, 100))));
            }
        }
    }

    void MainWindow::showNotification(std::wstring const& message)
    {
        NotificationText().Text(hstring(message.c_str()));
        NotificationToast().Visibility(Visibility::Visible);

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

    void MainWindow::createDynamicControls()
    {
        winUILog("Creating dynamic WinUI controls.");

        inputMeterBar = ProgressBar();
        inputMeterBar.Minimum(0);
        inputMeterBar.Maximum(1);
        inputMeterBar.Value(0);
        inputMeterBar.Height(10);
        createMeterSegments(InputMeterBarHost(), inputMeterSegments);

        outputMeterBar = ProgressBar();
        outputMeterBar.Minimum(0);
        outputMeterBar.Maximum(1);
        outputMeterBar.Value(0);
        outputMeterBar.Height(10);
        createMeterSegments(OutputMeterBarHost(), outputMeterSegments);

        audioBackendBox = ComboBox();
        styleCombo(audioBackendBox);
        AudioBackendBoxHost().Children().Append(audioBackendBox);

        inputBox = ComboBox();
        styleCombo(inputBox);
        InputBoxHost().Children().Append(inputBox);

        outputBox = ComboBox();
        styleCombo(outputBox);
        OutputBoxHost().Children().Append(outputBox);

        sampleRateBox = ComboBox();
        styleCombo(sampleRateBox);
        SampleRateBoxHost().Children().Append(sampleRateBox);

        bufferSizeBox = ComboBox();
        styleCombo(bufferSizeBox);
        BufferSizeBoxHost().Children().Append(bufferSizeBox);

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
        ThemeModeBoxHost().Children().Append(themeModeBox);
        setComboItems(themeModeBox, { "Dark" }, 0);

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

        scanVstCheckBox = CheckBox();
        scanVstCheckBox.Content(box_value(hstring(L"VST")));
        scanVstCheckBox.IsChecked(true);

        scanVst3CheckBox = CheckBox();
        scanVst3CheckBox.Content(box_value(hstring(L"VST3")));
        scanVst3CheckBox.IsChecked(true);

        closeQuitsAppRadioButton = RadioButton();
        closeQuitsAppRadioButton.GroupName(L"CloseBehavior");

        closeToTrayRadioButton = RadioButton();
        closeToTrayRadioButton.GroupName(L"CloseBehavior");

        winUILog("Dynamic WinUI controls created.");
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
        createDynamicControls();

        styleButton(RefreshButton());
        styleButton(OpenWindowsSoundSettingsButton());
        styleIconOnlyButton(RepositoryButton());
        styleIconOnlyButton(OriginalRepositoryButton());
        styleButton(RunningPluginsTabButton());
        styleButton(InstalledPluginsTabButton());
        styleButton(PluginActionsButton());
        styleIconOnlyButton(RunningPluginSortButton());
        styleIconOnlyButton(InstalledPluginSortButton());
        styleButton(DownloadUpdateButton());
        styleButton(KoFiButton());
        styleButton(SupportRepositoryButton());
        styleButton(SupportShowcaseButton());
        styleButton(ScanDefaultPluginsButton());
        styleButton(RemoveMissingPluginsButton());
        styleButton(ClearPluginDatabaseButton());
        styleButton(InputChannelsToggleAllButton());
        styleButton(OutputChannelsToggleAllButton());
        styleButton(ManageEnabledAudioDevicesButton());
        styleButton(PreferredDeviceButton());
        styleButton(CopyLogsButton());
        styleButton(SaveLogButton());

        winUILog("Configuring window title.");
        Title(L"Light Host Modern");
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
        applyBackdrop(BackdropModeBox().SelectedIndex() < 0 ? loadBackdropModeIndex() : BackdropModeBox().SelectedIndex());
        applyLayoutMode();
        resetDefaultPluginScanPaths();
        Closed({ this, &MainWindow::Window_Closed });

        winUILog("Resizing window.");
        const auto windowScale = static_cast<double>(GetDpiForSystem()) / 96.0;
        AppWindow().Resize({
            static_cast<int32_t>(1180.0 * windowScale),
            static_cast<int32_t>(760.0 * windowScale)
        });

        winUILog("Moving window.");
        AppWindow().Move({
            static_cast<int32_t>(80.0 * windowScale),
            static_cast<int32_t>(80.0 * windowScale)
        });

        winUILog("Reading host pipe option.");
        hostPipeName = commandLineOptionValue(L"--host-pipe");
        winUILog("Host pipe: " + wideToUtf8(hostPipeName));

        winUILog("Attaching UI events.");
        RootLayout().Loaded([this](IInspectable const&, RoutedEventArgs const&)
        {
            sidebarCollapsed = true;
            updateSidebarLayout();
        });
        RootLayout().SizeChanged({ this, &MainWindow::RootLayout_SizeChanged });
        SidebarRail().SelectionChanged({ this, &MainWindow::Navigation_SelectionChanged });
        SidebarToggleButton().Click({ this, &MainWindow::SidebarToggle_Click });
        RefreshButton().Click({ this, &MainWindow::Refresh_Click });
        OpenWindowsSoundSettingsButton().Click({ this, &MainWindow::OpenWindowsSoundSettings_Click });
        RunningPluginsTabButton().Click({ this, &MainWindow::RunningPluginsTab_Click });
        InstalledPluginsTabButton().Click({ this, &MainWindow::InstalledPluginsTab_Click });
        RunningPluginSearchBox().TextChanged({ this, &MainWindow::PluginSearchBox_TextChanged });
        InstalledPluginSearchBox().TextChanged({ this, &MainWindow::PluginSearchBox_TextChanged });
        KoFiButton().Click({ this, &MainWindow::KoFi_Click });
        SupportRepositoryButton().Click({ this, &MainWindow::SupportRepository_Click });
        SupportShowcaseButton().Click({ this, &MainWindow::SupportShowcase_Click });
        DownloadUpdateButton().Click({ this, &MainWindow::DownloadUpdate_Click });
        HideSupportTabSwitch().Toggled({ this, &MainWindow::HideSupportTabSwitch_Toggled });
        LanguageBox().SelectionChanged({ this, &MainWindow::LanguageBox_SelectionChanged });
        ScanDefaultPluginsButton().Click({ this, &MainWindow::ScanDefaultPlugins_Click });
        RemoveMissingPluginsButton().Click({ this, &MainWindow::RemoveMissingPlugins_Click });
        ClearPluginDatabaseButton().Click({ this, &MainWindow::ClearPluginDatabase_Click });
        DeletePluginStatesButton().Click({ this, &MainWindow::DeletePluginStates_Click });
        StartWithWindowsCheckBox().Toggled({ this, &MainWindow::StartWithWindowsCheckBox_Changed });
        CloseToTraySwitch().Toggled({ this, &MainWindow::CloseToTraySwitch_Toggled });
        EnableVst2CheckBox().Toggled({ this, &MainWindow::EnableVst2CheckBox_Changed });
        AudioPersistenceModeBox().SelectionChanged({ this, &MainWindow::AudioPersistenceModeBox_SelectionChanged });
        AudioRecoveryRetrySecondsBox().ValueChanged({ this, &MainWindow::AudioRecoveryRetrySecondsBox_ValueChanged });
        AudioRecoveryRetryAttemptsBox().ValueChanged({ this, &MainWindow::AudioRecoveryRetryAttemptsBox_ValueChanged });
        CustomRecoveryBackendBox().SelectionChanged({ this, &MainWindow::CustomRecoveryBackendBox_SelectionChanged });
        CustomRecoveryInputBox().SelectionChanged({ this, &MainWindow::CustomRecoveryInputBox_SelectionChanged });
        CustomRecoveryOutputBox().SelectionChanged({ this, &MainWindow::CustomRecoveryOutputBox_SelectionChanged });
        RetryAudioDeviceButton().Click({ this, &MainWindow::RetryAudioDevice_Click });
        ChooseAudioDeviceButton().Click({ this, &MainWindow::ChooseAudioDevice_Click });
        ManageEnabledAudioDevicesButton().Click({ this, &MainWindow::ManageEnabledAudioDevices_Click });
        InputChannelsToggleAllButton().Click({ this, &MainWindow::InputChannelsToggleAll_Click });
        OutputChannelsToggleAllButton().Click({ this, &MainWindow::OutputChannelsToggleAll_Click });
        CloseQuitsAppRadioButton().Checked({ this, &MainWindow::CloseBehaviorRadioButton_Checked });
        CloseToTrayRadioButton().Checked({ this, &MainWindow::CloseBehaviorRadioButton_Checked });

        configurePluginSortMenus();
        refreshLanguageItems();

        hideSupportTab = loadUiSetting(L"General", L"HideSupportTab", L"0") == L"1";
        HideSupportTabSwitch().IsOn(hideSupportTab);
        SupportButton().Visibility(hideSupportTab ? Visibility::Collapsed : Visibility::Visible);
        applyLocalization();
        sidebarCollapsed = true;
        updateSidebarLayout();
        AudioBackendBox().SelectionChanged({ this, &MainWindow::AudioBackendBox_SelectionChanged });
        InputBox().SelectionChanged({ this, &MainWindow::InputBox_SelectionChanged });
        OutputBox().SelectionChanged({ this, &MainWindow::OutputBox_SelectionChanged });
        SampleRateBox().SelectionChanged({ this, &MainWindow::SampleRateBox_SelectionChanged });
        BufferSizeBox().SelectionChanged({ this, &MainWindow::BufferSizeBox_SelectionChanged });
        IconModeBox().SelectionChanged({ this, &MainWindow::IconModeBox_SelectionChanged });
        BackdropModeBox().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&)
        {
            if (BackdropModeBox() && BackdropModeBox().SelectedIndex() >= 0)
            {
                saveBackdropModeIndex(BackdropModeBox().SelectedIndex());
                applyBackdrop(BackdropModeBox().SelectedIndex());
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
        AudioBackendBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        InputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        OutputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        SampleRateBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        BufferSizeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        BackdropModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        LayoutModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        AudioPersistenceModeBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        CustomRecoveryBackendBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        CustomRecoveryInputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        CustomRecoveryOutputBox().DropDownOpened({ this, &MainWindow::ComboBox_DropDownOpened });
        AudioBackendBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        InputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        OutputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        SampleRateBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        BufferSizeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        BackdropModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        LayoutModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        AudioPersistenceModeBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        CustomRecoveryBackendBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        CustomRecoveryInputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        CustomRecoveryOutputBox().DropDownClosed({ this, &MainWindow::ComboBox_DropDownClosed });
        RunningPluginsListView().AllowDrop(true);
        RunningPluginsListView().DragOver({ this, &MainWindow::RunningPluginItem_DragOver });
        RunningPluginsListView().Drop({ this, &MainWindow::RunningPluginItem_Drop });

        winUILog("Creating refresh timer.");
        refreshTimer = DispatcherTimer();
        refreshTimer.Interval(std::chrono::milliseconds(50));
        refreshTimer.Tick([this](IInspectable const&, IInspectable const&)
        {
            try
            {
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
        applyTheme(ElementTheme::Dark);
        winUILog("Applying initial responsive layout.");
        applyResponsiveLayout(1120.0);
        winUILog("Updating debug controls.");
        updateDebugControls();
        winUILog("Showing initial section.");
        SidebarRail().SelectedItem(DashboardButton());
        showSection(L"Dashboard");
        sidebarCollapsed = true;
        updateSidebarLayout();
        showPluginSubsection(L"Running");
        winUILog("Initial snapshot deferred until first timer tick.");
        winUILog("MainWindow ready.");
        checkForUpdatesAsync();
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
        if (!latestReleaseUrl.empty())
            ShellExecuteW(nullptr, L"open", latestReleaseUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::HideSupportTabSwitch_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        hideSupportTab = HideSupportTabSwitch().IsOn();
        updateToggleStateLabels();
        saveUiSetting(L"General", L"HideSupportTab", hideSupportTab ? L"1" : L"0");
        SupportButton().Visibility(hideSupportTab ? Visibility::Collapsed : Visibility::Visible);
        if (hideSupportTab && SupportPanel().Visibility() == Visibility::Visible)
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
        localization.load(language.c_str());
        applyLocalization();
        renderedRunningPluginLabels.clear();
        renderedInstalledPluginLabels.clear();
        refreshSnapshot();
    }

    void MainWindow::PluginSearchBox_TextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;
        if (sender == RunningPluginSearchBox())
            runningPluginSearch = sender.Text().c_str();
        else
            installedPluginSearch = sender.Text().c_str();
        renderedRunningPluginLabels.clear();
        renderedInstalledPluginLabels.clear();
        refreshSnapshot();
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

    void MainWindow::RunningPluginsListView_DragItemsStarting(IInspectable const&, DragItemsStartingEventArgs const&)
    {
        pluginDragInProgress = false;
        draggedPluginSourceIndex = -1;
        winUILog("Running plugin drag ignored; native ListView drag was removed from the WinUI shell.");
    }

    void MainWindow::RunningPluginsListView_DragItemsCompleted(IInspectable const&, DragItemsCompletedEventArgs const&)
    {
        draggedPluginSourceIndex = -1;
        pluginDragInProgress = false;
        winUILog("Running plugin drag completed without action.");
    }

    void MainWindow::RunningPluginItem_DragStarting(UIElement const& sender, DragStartingEventArgs const& args)
    {
        draggedPluginSourceIndex = -1;
        draggedPluginTargetIndex = -1;
        draggedPluginDropIndex = -1;
        resetRunningPluginDragVisuals();
        if (auto element = sender.try_as<FrameworkElement>())
        {
            try
            {
                draggedPluginSourceIndex = unbox_value<int>(element.Tag());
            }
            catch (...)
            {
                draggedPluginSourceIndex = -1;
            }
        }

        pluginDragInProgress = draggedPluginSourceIndex >= 0;
        args.AllowedOperations(DataPackageOperation::Move);
        args.Data().RequestedOperation(DataPackageOperation::Move);
        args.Data().SetText(hstring(std::to_wstring(draggedPluginSourceIndex)));
    }

    void MainWindow::RunningPluginItem_DragOver(IInspectable const& sender, DragEventArgs const& args)
    {
        if (draggedPluginSourceIndex < 0)
            return;

        args.AcceptedOperation(DataPackageOperation::Move);
        args.Handled(true);

        auto border = sender.try_as<Border>();
        if (!border)
        {
            auto element = sender.try_as<FrameworkElement>();
            while (element)
            {
                if (auto parentBorder = element.Parent().try_as<Border>())
                {
                    border = parentBorder;
                    break;
                }
                element = element.Parent().try_as<FrameworkElement>();
            }
        }
        if (!border)
            return;

        int targetIndex = -1;
        try
        {
            targetIndex = unbox_value<int>(border.Tag());
        }
        catch (...)
        {
            targetIndex = -1;
        }

        if (targetIndex < 0)
            return;

        resetRunningPluginDragVisuals();
        if (targetIndex != draggedPluginSourceIndex)
            setPluginDropTargetVisual(border);
        draggedPluginTargetIndex = targetIndex;
        draggedPluginDropIndex = targetIndex;
    }

    void MainWindow::RunningPluginItem_Drop(IInspectable const& sender, DragEventArgs const& args)
    {
        args.Handled(true);

        int targetIndex = -1;
        if (auto element = sender.try_as<FrameworkElement>())
        {
            try
            {
                targetIndex = unbox_value<int>(element.Tag());
            }
            catch (...)
            {
                targetIndex = -1;
            }
        }

        const int sourceIndex = draggedPluginSourceIndex;
        if (draggedPluginTargetIndex >= 0)
            targetIndex = draggedPluginTargetIndex;
        else if (draggedPluginDropIndex >= 0)
            targetIndex = draggedPluginDropIndex;
        draggedPluginSourceIndex = -1;
        draggedPluginTargetIndex = -1;
        draggedPluginDropIndex = -1;
        pluginDragInProgress = false;
        resetRunningPluginDragVisuals();

        if (sourceIndex < 0 || targetIndex < 0 || sourceIndex == targetIndex)
            return;

        winUILog("Swapping running plugin " + std::to_string(sourceIndex) + " with " + std::to_string(targetIndex) + ".");
        if (sendCommand("swap-plugin-with:" + std::to_string(sourceIndex) + ":" + std::to_string(targetIndex)))
        {
            showNotification(L"Plugin chain reordered.");
        }
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

    void MainWindow::FluentDropdownItem_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (syncingHostControls || syncingThemeControls)
            return;

        auto item = sender.try_as<Button>();
        if (!item)
            return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(item.Tag()).c_str()));
        }
        catch (...)
        {
            return;
        }

        const auto separator = tag.find(':');
        if (separator == std::string::npos)
            return;

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
            return;
        }

        if (command == "set-audio-backend")
        {
            updateLocal(audioBackendDropdown);
            syncFluentDropdownLabel(audioBackendDropdown);
            sendCommand(command + ":" + std::to_string(index));
            return;
        }

        if (command == "set-audio-input")
        {
            updateLocal(inputDropdown);
            syncFluentDropdownLabel(inputDropdown);
            sendCommand(command + ":" + std::to_string(index));
            return;
        }

        if (command == "set-audio-output")
        {
            updateLocal(outputDropdown);
            syncFluentDropdownLabel(outputDropdown);
            sendCommand(command + ":" + std::to_string(index));
            return;
        }

        if (command == "set-sample-rate")
        {
            updateLocal(sampleRateDropdown);
            syncFluentDropdownLabel(sampleRateDropdown);
            const auto value = selectedValue(sampleRateDropdown);
            if (!value.empty())
                sendCommand(command + ":" + value);
            return;
        }

        if (command == "set-buffer-size")
        {
            updateLocal(bufferSizeDropdown);
            syncFluentDropdownLabel(bufferSizeDropdown);
            const auto value = selectedValue(bufferSizeDropdown);
            if (!value.empty())
                sendCommand(command + ":" + value);
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
        showNotification(L"Modern repository link is not configured yet.");
    }

    void MainWindow::OriginalRepositoryButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"open", L"https://github.com/opencma/LightHost", nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::AudioBackendBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!syncingHostControls && AudioBackendBox().SelectedIndex() >= 0)
        {
            const auto index = AudioBackendBox().SelectedIndex();
            const auto label = selectedComboText(AudioBackendBox());
            winUILog("Audio backend selection changed index=" + std::to_string(index) + " label='" + label + "'");
            sendCommand("set-audio-backend:" + std::to_string(index));
        }
    }

    void MainWindow::InputBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!syncingHostControls && InputBox().SelectedIndex() >= 0)
        {
            const auto command = asioDeviceMode ? "set-audio-output:" : "set-audio-input:";
            sendCommand(std::string(command) + std::to_string(InputBox().SelectedIndex()));
        }
    }

    void MainWindow::OutputBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!syncingHostControls && OutputBox().SelectedIndex() >= 0)
            sendCommand("set-audio-output:" + std::to_string(OutputBox().SelectedIndex()));
    }

    void MainWindow::SampleRateBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingHostControls || SampleRateBox().SelectedIndex() < 0)
            return;

        const auto text = selectedComboText(SampleRateBox());
        if (!text.empty())
            sendCommand("set-sample-rate:" + text);
    }

    void MainWindow::BufferSizeBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingHostControls || BufferSizeBox().SelectedIndex() < 0)
            return;

        const auto text = selectedComboText(BufferSizeBox());
        if (!text.empty())
            sendCommand("set-buffer-size:" + text);
    }

    void MainWindow::ChannelCheckBox_Changed(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (syncingHostControls)
            return;

        const auto box = sender.try_as<CheckBox>();
        if (!box)
            return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(box.Tag()).c_str()));
        }
        catch (...)
        {
            return;
        }

        const auto firstSeparator = tag.find(':');
        if (firstSeparator == std::string::npos)
            return;

        const auto secondSeparator = tag.find(':', firstSeparator + 1);
        const auto command = tag.substr(0, firstSeparator);
        const auto checkedValue = isChecked(box) ? "1" : "0";
        if (secondSeparator == std::string::npos)
        {
            const auto channelIndex = tag.substr(firstSeparator + 1);
            sendCommand(command + ":" + channelIndex + ":" + checkedValue);
            return;
        }

        const int startIndex = (std::max)(0, std::atoi(tag.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1).c_str()));
        const int endIndex = (std::max)(startIndex, std::atoi(tag.substr(secondSeparator + 1).c_str()));
        for (int channelIndex = startIndex; channelIndex <= endIndex; ++channelIndex)
            sendCommand(command + ":" + std::to_string(channelIndex) + ":" + checkedValue);
    }

    void MainWindow::InputChannelsToggleAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool shouldCheck = true;
        try
        {
            shouldCheck = unbox_value<hstring>(InputChannelsToggleAllButton().Tag()) == L"check";
        }
        catch (...) {}

        bool succeeded = !currentInputChannelRows.empty();
        for (auto const& row : currentInputChannelRows)
        {
            for (int channelIndex = row.startIndex; channelIndex <= row.endIndex; ++channelIndex)
                succeeded = sendCommand("set-input-channel:" + std::to_string(channelIndex) + ":" + (shouldCheck ? "1" : "0")) && succeeded;
        }

        if (succeeded)
            showNotification(shouldCheck ? L"All input channels enabled." : L"All input channels disabled.");
    }

    void MainWindow::OutputChannelsToggleAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        bool shouldCheck = true;
        try
        {
            shouldCheck = unbox_value<hstring>(OutputChannelsToggleAllButton().Tag()) == L"check";
        }
        catch (...) {}

        bool succeeded = !currentOutputChannelRows.empty();
        for (auto const& row : currentOutputChannelRows)
        {
            for (int channelIndex = row.startIndex; channelIndex <= row.endIndex; ++channelIndex)
                succeeded = sendCommand("set-output-channel:" + std::to_string(channelIndex) + ":" + (shouldCheck ? "1" : "0")) && succeeded;
        }

        if (succeeded)
            showNotification(shouldCheck ? L"All output channels enabled." : L"All output channels disabled.");
    }

    void MainWindow::ChannelButton_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (syncingHostControls)
            return;

        const auto button = sender.try_as<Button>();
        if (!button)
            return;

        std::string tag;
        try
        {
            tag = wideToUtf8(std::wstring(unbox_value<hstring>(button.Tag()).c_str()));
        }
        catch (...)
        {
            return;
        }

        const auto first = tag.find(':');
        const auto second = first == std::string::npos ? std::string::npos : tag.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos)
            return;

        const auto command = tag.substr(0, first);
        const auto channelIndex = tag.substr(first + 1, second - first - 1);
        const bool currentlyActive = tag.substr(second + 1) == "1";
        sendCommand(command + ":" + channelIndex + ":" + (currentlyActive ? "0" : "1"));
    }

    void MainWindow::showSection(std::wstring const& section)
    {
        currentSection = section;
        DashboardPanel().Visibility(section == L"Dashboard" ? Visibility::Visible : Visibility::Collapsed);
        PreferencesPanel().Visibility(section == L"Audio" ? Visibility::Visible : Visibility::Collapsed);
        PluginsPanel().Visibility(section == L"Plugins" ? Visibility::Visible : Visibility::Collapsed);
        SupportPanel().Visibility(section == L"Support me" ? Visibility::Visible : Visibility::Collapsed);
        ConfigPanel().Visibility(section == L"Settings" ? Visibility::Visible : Visibility::Collapsed);
        ContentScrollViewer().VerticalScrollBarVisibility(ScrollBarVisibility::Hidden);

        if (section == L"Dashboard")
            PageTitleText().Text(localization.text("nav.dashboard", L"Dashboard"));
        else if (section == L"Audio")
            PageTitleText().Text(localization.text("nav.audio", L"Audio"));
        else if (section == L"Plugins")
            PageTitleText().Text(localization.text("nav.plugins", L"Plugins"));
        else if (section == L"Support me")
            PageTitleText().Text(localization.text("nav.support", L"Support me"));
        else
            PageTitleText().Text(localization.text("nav.settings", L"Settings"));

        if (section == L"Dashboard")
            PageSubtitleText().Text(localization.text("page.dashboard.subtitle", L"Live audio host status and current routing."));
        else if (section == L"Audio")
            PageSubtitleText().Text(localization.text("page.audio.subtitle", L"Configure audio devices and stream format."));
        else if (section == L"Plugins")
            PageSubtitleText().Text(localization.text("page.plugins.subtitle", L"Manage the running chain and installed plugin database."));
        else if (section == L"Support me")
            PageSubtitleText().Text(localization.text("page.support.subtitle", L"Support the project and help Light Host Modern keep improving."));
        else
            PageSubtitleText().Text(localization.text("page.settings.subtitle", L"Configure app behavior, audio recovery, and appearance."));

        NavigationViewItem selectedItem{ nullptr };
        if (section == L"Dashboard") selectedItem = DashboardButton();
        else if (section == L"Audio") selectedItem = PreferencesButton();
        else if (section == L"Plugins") selectedItem = PluginsButton();
        else if (section == L"Support me") selectedItem = SupportButton();
        else selectedItem = ConfigButton();

        if (SidebarRail().SelectedItem() != selectedItem)
            SidebarRail().SelectedItem(selectedItem);

    }

    void MainWindow::configurePluginSortMenus()
    {
        auto configure = [this](Button const& button, bool running)
        {
            auto flyout = MenuFlyout();
            flyout.Placement(FlyoutPlacementMode::BottomEdgeAlignedRight);
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
                    renderedRunningPluginLabels.clear();
                    renderedInstalledPluginLabels.clear();
                    refreshSnapshot();
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
        localizeVisualTree(RootLayout());
        refreshLanguageItems();
        BrandTitleText().Text(localization.text("app.title", L"Light Host Modern"));
        DashboardButton().Content(box_value(localization.text("nav.dashboard", L"Dashboard")));
        PreferencesButton().Content(box_value(localization.text("nav.audio", L"Audio")));
        PluginsButton().Content(box_value(localization.text("nav.plugins", L"Plugins")));
        SupportButton().Content(box_value(localization.text("nav.support", L"Support me")));
        ConfigButton().Content(box_value(localization.text("nav.settings", L"Settings")));
        RunningPluginSearchBox().PlaceholderText(localization.text("plugins.search.running", L"Search running plugins"));
        InstalledPluginSearchBox().PlaceholderText(localization.text("plugins.search.installed", L"Search installed plugins"));
        RunningPluginsEmptyText().Text(localization.text("plugins.empty.running", L"No plugins running"));
        InstalledPluginsEmptyText().Text(localization.text("plugins.empty.installed", L"No plugins installed"));
        SupportDonateTitleText().Text(localization.text("support.donate.title", L"Support by donating via Ko-fi"));
        SupportDonateDescriptionText().Text(localization.text("support.donate.description", L"If you enjoy the app and would like to support its continued development, you can make a donation through Ko-fi. Every contribution, no matter the amount, helps me dedicate more time to improving the app, fixing issues, and building new features.\n\nThank you so much for your support! It truly helps keep the project moving forward."));
        SupportRepositoryTitleText().Text(localization.text("support.repository.title", L"Support by starring the GitHub repository"));
        SupportRepositoryDescriptionText().Text(localization.text("support.repository.description", L"If you enjoy the app and want to support the project, consider giving the repository a star on GitHub. It’s a simple way to show your support, help the project gain visibility, and make it easier for others to discover.\n\nThank you for supporting the project!"));
        SupportRepositoryButton().Content(box_value(localization.text("support.repository.action", L"Go to repo")));
        SupportShowcaseTitleText().Text(localization.text("support.showcase.title", L"Support by showcasing the app"));
        SupportShowcaseDescriptionText().Text(localization.text("support.showcase.description", L"If you create a video about the app, I’d be happy to help give it more visibility. Once your video is published, open an issue on GitHub and send me the link. I may feature your video on the project page, helping promote your content while you help introduce the app to more people.\n\nIt’s a simple way for us to support each other: you showcase the app, and I help showcase your work."));
        SupportShowcaseButton().Content(box_value(localization.text("support.showcase.action", L"Create video showcase post")));
        UpdateAvailableTitleText().Text(localization.text("settings.update.title", L"Update available"));
        if (!latestReleaseTag.empty())
        {
            std::wstring message = localization.text("settings.update.body", L"A newer version of Light Host Modern is available.").c_str();
            message += L" ";
            message += latestReleaseTag;
            UpdateAvailableBodyText().Text(message);
        }
        DownloadUpdateButton().Content(box_value(localization.text("settings.update.download", L"Download update")));
        HideSupportTitleText().Text(localization.text("settings.support.hide", L"Hide the Support me tab"));
        LanguageTitleText().Text(localization.text("settings.language.title", L"Language"));
        LanguageDescriptionText().Text(localization.text("settings.language.description", L"Choose the language used by the app."));
        LayoutModeTitleText().Text(localization.text("settings.layout.title", L"Layout mode"));
        LayoutModeDescriptionText().Text(localization.text("settings.layout.description", L"Compact limits page width; Expanded uses all available space."));
        setComboItems(LayoutModeBox(), {
            to_string(localization.text("settings.layout.compact", L"Compact")),
            to_string(localization.text("settings.layout.expanded", L"Expanded")) }, compactLayout ? 0 : 1);
        ScanDefaultPluginsMenuItem().Text(localization.text("plugins.scan", L"Scan"));
        ScanPathsMenuItem().Text(localization.text("plugins.scanPaths", L"Scan paths"));
        RemoveMissingPluginsMenuItem().Text(localization.text("plugins.removeMissing", L"Remove missing"));
        ClearPluginDatabaseMenuItem().Text(localization.text("common.clear", L"Clear"));
        const auto koFiAutomationName = localization.text("support.kofi", L"Support me on Ko-fi");
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(KoFiButton(), koFiAutomationName);
        ToolTipService::SetToolTip(KoFiButton(), box_value(koFiAutomationName));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SupportRepositoryButton(), localization.text("support.repository.automation", L"Go to the Light Host Modern repository"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SupportShowcaseButton(), localization.text("support.showcase.automation", L"Create a video showcase post"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(RunningPluginSearchBox(), localization.text("plugins.search.running", L"Search running plugins"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(InstalledPluginSearchBox(), localization.text("plugins.search.installed", L"Search installed plugins"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(RunningPluginSortButton(), localization.text("plugins.sort.label", L"Sort plugins"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(InstalledPluginSortButton(), localization.text("plugins.sort.label", L"Sort plugins"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(HideSupportTabSwitch(), localization.text("settings.support.hide", L"Hide the Support me tab"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(LanguageBox(), localization.text("settings.language.automation", L"App language"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(LayoutModeBox(), localization.text("settings.layout.title", L"Layout mode"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PreferredDeviceButton(), localization.text("settings.persistence.choose", L"Choose preferred device"));
        updateToggleStateLabels();
        configurePluginSortMenus();
        showSection(currentSection);
    }

    void MainWindow::refreshLanguageItems()
    {
        const bool wasSyncing = syncingLanguageControls;
        syncingLanguageControls = true;
        LanguageBox().Items().Clear();

        const auto languages = ::LightHostWinUI::LocalizationCatalog::availableLanguages();
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

        if (root == LanguageBox())
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

    fire_and_forget MainWindow::checkForUpdatesAsync()
    {
        auto lifetime = get_strong();
        try
        {
            Windows::Web::Http::HttpClient client;
            client.DefaultRequestHeaders().UserAgent().ParseAdd(L"LightHostModern/1.2.0");
            const auto response = co_await client.GetStringAsync(Windows::Foundation::Uri(L"https://api.github.com/repos/heide-oficial/Light-Host-Modern/releases/latest"));
            const auto json = Windows::Data::Json::JsonObject::Parse(response);
            const auto latestTag = json.GetNamedString(L"tag_name", L"");
            const auto releaseUrl = json.GetNamedString(L"html_url", L"");
            if (semanticVersion(latestTag.c_str()) > semanticVersion(L"1.2.0") && !releaseUrl.empty())
            {
                latestReleaseUrl = releaseUrl.c_str();
                latestReleaseTag = latestTag.c_str();
                std::wstring message = localization.text("settings.update.body", L"A newer version of Light Host Modern is available.").c_str();
                message += L" ";
                message += latestTag.c_str();
                UpdateAvailableBodyText().Text(message);
                UpdateAvailableCard().Visibility(Visibility::Visible);
                winUILog("Update available: " + to_string(latestTag));
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

    void MainWindow::showPluginSubsection(std::wstring const& section)
    {
        RunningPluginsPanel().Visibility(section == L"Running" ? Visibility::Visible : Visibility::Collapsed);
        InstalledPluginsPanel().Visibility(section == L"Installed" ? Visibility::Visible : Visibility::Collapsed);
        PluginActionsButton().Visibility(section == L"Installed" ? Visibility::Visible : Visibility::Collapsed);
        PluginActionsButton().IsEnabled(section == L"Installed");

        const auto selectedBrush = resourceBrush(L"SubtleFillColorSecondaryBrush", themedFallback(makeColorA(24, 0, 0, 0), makeColorA(42, 255, 255, 255)));
        const auto defaultBrush = resourceBrush(L"SubtleFillColorTransparentBrush", makeColorA(0, 0, 0, 0));
        RunningPluginsTabButton().Background(section == L"Running" ? selectedBrush : defaultBrush);
        InstalledPluginsTabButton().Background(section == L"Installed" ? selectedBrush : defaultBrush);
    }

    void MainWindow::ThemeModeBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        syncThemeSelectors(0);
        applyTheme(ElementTheme::Dark);
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

    void MainWindow::applyTheme(ElementTheme)
    {
        selectedTheme = ElementTheme::Dark;
        RootLayout().RequestedTheme(ElementTheme::Dark);
        MainContent().RequestedTheme(ElementTheme::Dark);
        SidebarRail().RequestedTheme(ElementTheme::Dark);

        preferDarkFallback = true;
        styleTitleBar(AppWindow(), true);
        if (BackdropModeBox())
            applyBackdrop(BackdropModeBox().SelectedIndex() < 0 ? 0 : BackdropModeBox().SelectedIndex());

        renderedRunningPluginLabels.clear();
        renderedInstalledPluginLabels.clear();
        renderedInputChannelKeys.clear();
        renderedOutputChannelKeys.clear();
        currentInputChannelRows.clear();
        currentOutputChannelRows.clear();
        createMeterSegments(InputMeterBarHost(), inputMeterSegments);
        createMeterSegments(OutputMeterBarHost(), outputMeterSegments);
        showSection(currentSection);
        refreshSnapshot();
    }

    void MainWindow::applyBackdrop(int selectedIndex)
    {
        try
        {
            preferDarkFallback = true;
            RootLayout().RequestedTheme(ElementTheme::Dark);
            MainContent().RequestedTheme(ElementTheme::Dark);
            SidebarRail().RequestedTheme(ElementTheme::Dark);

            RootLayout().Background(nullptr);
            MainContent().Background(nullptr);
            SidebarRail().Background(nullptr);

            if (selectedIndex == 1)
            {
                auto backdrop = MicaBackdrop();
                backdrop.Kind(winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::BaseAlt);
                SystemBackdrop(backdrop);
                RootLayout().Background(brush(makeColorA(0, 0, 0, 0)));
                winUILog("Mica Alt backdrop enabled.");
                return;
            }

            if (selectedIndex == 2)
            {
                SystemBackdrop(DesktopAcrylicBackdrop());
                RootLayout().Background(brush(makeColorA(0, 0, 0, 0)));
                winUILog("Desktop Acrylic backdrop enabled.");
                return;
            }

            if (selectedIndex == 3)
            {
                SystemBackdrop(nullptr);
                RootLayout().Background(resourceBrush(L"AppSolidBackgroundBrush", themedFallback(makeColor(243, 243, 243), makeColor(32, 32, 32))));
                SidebarRail().Background(resourceBrush(L"AppSidebarBrush", themedFallback(makeColor(243, 243, 243), makeColor(32, 32, 32))));
                winUILog("Solid backdrop enabled.");
                return;
            }

            auto backdrop = MicaBackdrop();
            backdrop.Kind(winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind::Base);
            SystemBackdrop(backdrop);
            RootLayout().Background(brush(makeColorA(0, 0, 0, 0)));
            winUILog("Mica backdrop enabled.");
        }
        catch (...)
        {
            SystemBackdrop(nullptr);
            RootLayout().Background(resourceBrush(L"AppSolidBackgroundBrush", themedFallback(makeColor(243, 243, 243), makeColor(32, 32, 32))));
            winUILog("System backdrop unavailable; using solid fallback.");
        }
    }

    void MainWindow::applyLayoutMode()
    {
        const auto availableWidth = MainContent().ActualWidth()
            - MainContent().Padding().Left
            - MainContent().Padding().Right;
        if (availableWidth > 0.0)
        {
            const auto viewportWidth = (std::max)(0.0, availableWidth - 16.0);
            PageContentStack().Width(compactLayout ? (std::min)(viewportWidth, 1040.0) : viewportWidth);
        }
        PageContentStack().MaxWidth(compactLayout ? 1040.0 : std::numeric_limits<double>::infinity());
        PageContentStack().HorizontalAlignment(HorizontalAlignment::Stretch);
    }

    void MainWindow::applyResponsiveLayout(double width)
    {
        if (width <= 0.0)
            return;

        const auto horizontalPadding = MainContent().Padding().Left + MainContent().Padding().Right;
        const auto measuredContentWidth = MainContent().ActualWidth() > 0.0
            ? (std::max)(0.0, MainContent().ActualWidth() - horizontalPadding)
            : width;
        const bool useNarrowSupportLayout = measuredContentWidth < 760.0;
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

        configureSupportAction(SupportDonateActionColumn(), KoFiButton());
        configureSupportAction(SupportRepositoryActionColumn(), SupportRepositoryButton());
        configureSupportAction(SupportShowcaseActionColumn(), SupportShowcaseButton());

        const bool useCompactPluginCards = measuredContentWidth < 680.0;
        if (compactPluginCards != useCompactPluginCards)
        {
            compactPluginCards = useCompactPluginCards;
            renderedRunningPluginLabels.clear();
            renderedInstalledPluginLabels.clear();
            if (currentSection == L"Plugins")
                refreshSnapshot();
        }

        if (width < 840.0)
        {
            MainContent().Padding(ThicknessHelper::FromLengths(16, 48, 16, 16));
            applyLayoutMode();
            HeaderActions().Orientation(Orientation::Vertical);
        }
        else if (width < 1180.0)
        {
            MainContent().Padding(ThicknessHelper::FromLengths(24, 48, 24, 24));
            applyLayoutMode();
            HeaderActions().Orientation(Orientation::Horizontal);
        }
        else
        {
            MainContent().Padding(ThicknessHelper::FromLengths(24, 48, 24, 24));
            applyLayoutMode();
            HeaderActions().Orientation(Orientation::Horizontal);
        }
    }

    void MainWindow::updateSidebarLayout()
    {
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
        ToolTipService::SetToolTip(SidebarToggleButton(), box_value(accessibleName));
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
        if (!IconModeBox())
            return;

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
        element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
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
                if (auto box = children.GetAt(i).try_as<CheckBox>())
                {
                    auto const& row = rows[(size_t) i];
                    box.Content(box_value(hs(row.label)));
                    box.Tag(box_value(hs(commandPrefix + ":" + std::to_string(row.startIndex) + ":" + std::to_string(row.endIndex))));
                    box.IsChecked(row.active);
                }
            }
            return;
        }

        panel.Children().Clear();

        if (rows.empty())
        {
            auto text = TextBlock();
            text.Text(L"No channel data available for the current device.");
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
            box.Content(box_value(hs(row.label)));
            box.Tag(box_value(hs(commandPrefix + ":" + std::to_string(row.startIndex) + ":" + std::to_string(row.endIndex))));
            box.IsChecked(row.active);
            box.MinHeight(40);
            box.HorizontalAlignment(HorizontalAlignment::Left);
            box.VerticalContentAlignment(VerticalAlignment::Center);
            box.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
            box.Checked({ this, &MainWindow::ChannelCheckBox_Changed });
            box.Unchecked({ this, &MainWindow::ChannelCheckBox_Changed });
            panel.Children().Append(box);
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
        ToolTipService::SetToolTip(button, box_value(hstring(allActive
            ? L"Disable every visible channel"
            : L"Enable every visible channel")));
    }

    void MainWindow::syncEnabledAudioChoicesSummary()
    {
        EnabledAudioChoicesSummaryText().Text(localization.text(
            "settings.devices.allEnabled",
            L"Choose which audio backends and devices Light Host Modern is allowed to use."));
    }

    void MainWindow::updateDebugControls()
    {
        const bool debug = winUIDebugEnabled();
        DebugSettingsPanel().Visibility(debug ? Visibility::Visible : Visibility::Collapsed);
        DebugStatusText().Text(debug
            ? L"Debug console is active for this session. Host and WinUI logs are printed to the attached console."
            : L"Debug logging is available when launched with --debug.");
        CopyLogsButton().IsEnabled(false);
        SaveLogButton().IsEnabled(false);
    }

    void MainWindow::refreshTelemetry()
    {
        const auto json = requestTelemetry(hostPipeName);
        if (json.empty())
        {
            if (!hasFullSnapshot)
                refreshSnapshot();
            return;
        }

        const auto status = extractString(json, "status", "unknown");
        const auto backend = extractString(json, "backend", "none");
        const auto device = extractString(json, "deviceName", "none");
        const auto activePlugins = (int) extractNumber(json, "activePluginCount", activePluginCount);
        const auto knownPlugins = (int) extractNumber(json, "knownPlugins", installedPluginCount);
        const auto sampleRate = extractNumber(json, "sampleRate");
        const auto bufferSize = (int) extractNumber(json, "bufferSize");
        const auto inputChannels = (int) extractNumber(json, "inputChannels");
        const auto outputChannels = (int) extractNumber(json, "outputChannels");
        const auto inputLevel = extractNumber(json, "inputLevel");
        const auto outputLevel = extractNumber(json, "outputLevel");
        const auto chainVersion = (int64_t) extractNumber(json, "chainVersion", -1);
        const auto pluginDbVersion = (int64_t) extractNumber(json, "pluginDbVersion", -1);
        const auto audioConfigVersion = (int64_t) extractNumber(json, "audioConfigVersion", -1);

        ConnectionStatusText().Text(hs(status == "online" ? "Online" : status));
        HeaderStatusText().Text(hs(status == "online" ? "Online" : status));
        SidebarStatusDetailText().Text(hs(backend + " - " + device));

        DashboardDeviceTypeText().Text(hs(backend));
        DashboardDeviceText().Text(hs(device));
        DashboardRoutingText().Text(hs(device));
        DashboardChannelsText().Text(hs("Input: " + std::to_string(inputChannels) + " ch / Output: " + std::to_string(outputChannels) + " ch"));
        DashboardFormatText().Text(hs(formatNumber(sampleRate, 0) + " Hz / " + std::to_string(bufferSize) + " samples"));
        ActivePluginsText().Text(hs(std::to_string(activePlugins)));
        KnownPluginsText().Text(hs(std::to_string(knownPlugins)));

        InputMeterBar().Value(inputLevel);
        OutputMeterBar().Value(outputLevel);
        updateMeterSegments(inputMeterSegments, inputLevel);
        updateMeterSegments(outputMeterSegments, outputLevel);

        const bool stateChanged = !hasFullSnapshot
            || chainVersion != lastChainVersion
            || pluginDbVersion != lastPluginDbVersion
            || audioConfigVersion != lastAudioConfigVersion;
        if (stateChanged)
            refreshSnapshot();
    }

    void MainWindow::refreshSnapshot()
    {
        const auto json = requestSnapshot(hostPipeName);
        if (json.empty())
        {
            hasFullSnapshot = false;
            lastChainVersion = -1;
            lastPluginDbVersion = -1;
            lastAudioConfigVersion = -1;
            activePluginCount = 0;
            installedPluginCount = 0;
            ConnectionStatusText().Text(L"Host unavailable");
            HeaderStatusText().Text(L"Unavailable");
            SidebarStatusDetailText().Text(L"No IPC snapshot");
            ActivePluginsText().Text(L"--");
            KnownPluginsText().Text(L"--");
            DashboardDeviceTypeText().Text(L"--");
            DashboardDeviceText().Text(L"No host connection");
            DashboardInputDeviceText().Text(L"--");
            DashboardOutputDeviceText().Text(L"--");
            DashboardRoutingText().Text(L"--");
            DashboardChannelsText().Text(L"--");
            DashboardFormatText().Text(L"--");
            InputMeterBar().Value(0);
            OutputMeterBar().Value(0);
            updateMeterSegments(inputMeterSegments, 0.0);
            updateMeterSegments(outputMeterSegments, 0.0);
            RunningPluginsSummaryText().Text(L"Host unavailable.");
            InstalledPluginsSummaryText().Text(L"Host unavailable.");
            RunningPluginsListView().Children().Clear();
            InstalledPluginsListView().Children().Clear();
            runningPluginItemBorders.clear();
            installedPluginItemBorders.clear();
            InputChannelsPanel().Children().Clear();
            OutputChannelsPanel().Children().Clear();
            renderedRunningPluginLabels.clear();
            renderedInstalledPluginLabels.clear();
            activePluginIdentityKeys.clear();
            knownPluginIdentityKeys.clear();
            knownPluginDisplayNames.clear();
            renderedInputChannelKeys.clear();
            renderedOutputChannelKeys.clear();
            currentInputChannelRows.clear();
            currentOutputChannelRows.clear();
            updateRunningPluginActions();
            updateInstalledPluginActions();
            return;
        }

        const auto status = extractString(json, "status", "unknown");
        const auto backend = extractString(json, "backend", "none");
        const auto device = extractString(json, "deviceName", "none");
        const auto activePlugins = (int) extractNumber(json, "activePluginCount");
        const auto knownPlugins = (int) extractNumber(json, "knownPlugins");
        const auto sampleRate = extractNumber(json, "sampleRate");
        const auto bufferSize = (int) extractNumber(json, "bufferSize");
        const auto inputChannels = (int) extractNumber(json, "inputChannels");
        const auto outputChannels = (int) extractNumber(json, "outputChannels");
        const auto inputLevel = extractNumber(json, "inputLevel");
        const auto outputLevel = extractNumber(json, "outputLevel");
        const auto reloads = (int) extractNumber(json, "chainReloads");
        const auto flushes = (int) extractNumber(json, "settingsFlushes");
        lastChainVersion = (int64_t) extractNumber(json, "chainVersion", (double) lastChainVersion);
        lastPluginDbVersion = (int64_t) extractNumber(json, "pluginDbVersion", (double) lastPluginDbVersion);
        lastAudioConfigVersion = (int64_t) extractNumber(json, "audioConfigVersion", (double) lastAudioConfigVersion);
        hasFullSnapshot = true;
        const auto startWithWindows = extractBool(json, "startWithWindows");
        const auto closeBehavior = extractString(json, "closeBehavior", "tray");
        const auto trayIconMode = extractString(json, "trayIconMode", "color");
        const auto vst2HostAvailable = extractBool(json, "vst2HostAvailable");
        const auto vst2RuntimeEnabled = extractBool(json, "vst2RuntimeEnabled");
        const auto vst2HostEnabled = extractBool(json, "vst2HostEnabled");
        const auto allPluginRows = extractActivePluginRows(json);
        auto allKnownPluginRows = extractKnownPluginRows(json);
        applyInstalledPluginRuntimeStatus(allKnownPluginRows, allPluginRows);
        const auto pluginRows = filterAndSortPluginRows(allPluginRows, runningPluginSearch, runningPluginSortMode, true);
        const auto knownPluginRows = filterAndSortPluginRows(allKnownPluginRows, installedPluginSearch, installedPluginSortMode, false);
        const auto pluginKeys = pluginRowKeys(pluginRows);
        const auto knownPluginKeys = pluginRowKeys(knownPluginRows);
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
        RunningPluginsTabButton().Content(box_value(hstring(std::wstring(localization.text("plugins.running", L"Running").c_str())
            + L" (" + std::to_wstring((int) allPluginRows.size()) + L")")));
        InstalledPluginsTabButton().Content(box_value(hstring(std::wstring(localization.text("plugins.installed", L"Installed").c_str())
            + L" (" + std::to_wstring((int) allKnownPluginRows.size()) + L")")));

        ConnectionStatusText().Text(hs(status == "online" ? "Online" : status));
        HeaderStatusText().Text(hs(status == "online" ? "Online" : status));
        SidebarStatusDetailText().Text(hs(backend + " - " + device));

        DashboardDeviceTypeText().Text(hs(backend));
        DashboardDeviceText().Text(hs(device));
        const auto isAsioBackend = backend == "ASIO";
        asioDeviceMode = isAsioBackend;
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
        DashboardInputDeviceText().Text(hs(isAsioBackend ? device : inputDeviceText));
        DashboardOutputDeviceText().Text(hs(outputDeviceText));
        setVisible(DashboardOutputDeviceGrid(), !isAsioBackend);
        DashboardRoutingText().Text(hs(device));
        DashboardChannelsText().Text(hs("Input: " + std::to_string(inputChannels) + " ch / Output: " + std::to_string(outputChannels) + " ch"));
        DashboardFormatText().Text(hs(formatNumber(sampleRate, 0) + " Hz / " + std::to_string(bufferSize) + " samples"));
        ActivePluginsText().Text(hs(std::to_string(activePlugins)));
        KnownPluginsText().Text(hs(std::to_string(knownPlugins)));

        InputMeterBar().Value(inputLevel);
        OutputMeterBar().Value(outputLevel);
        updateMeterSegments(inputMeterSegments, inputLevel);
        updateMeterSegments(outputMeterSegments, outputLevel);

        syncingHostControls = true;
        setComboItems(AudioBackendBox(), backendNames, currentBackendIndex);
        const auto& asioDeviceNames = outputDeviceNames.empty() ? inputDeviceNames : outputDeviceNames;
        const auto asioDeviceIndex = currentOutputDeviceIndex >= 0 ? currentOutputDeviceIndex : currentInputDeviceIndex;
        setComboItems(InputBox(), isAsioBackend ? asioDeviceNames : inputDeviceNames, isAsioBackend ? asioDeviceIndex : currentInputDeviceIndex);
        setComboItems(OutputBox(), outputDeviceNames, currentOutputDeviceIndex);
        setVisible(DeviceRoutingCard(), !inputDeviceNames.empty() || !outputDeviceNames.empty());
        InputDeviceLabel().Text(localization.text(isAsioBackend ? "audio.device" : "dashboard.inputDevice",
            isAsioBackend ? L"Device" : L"Input device"));
        setVisible(InputDeviceRow(), isAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        setVisible(InputDeviceLabel(), isAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        setVisible(InputBox(), isAsioBackend ? !asioDeviceNames.empty() : !inputDeviceNames.empty());
        setVisible(OutputDeviceRow(), !isAsioBackend && !outputDeviceNames.empty());
        setVisible(OutputDeviceLabel(), !isAsioBackend && !outputDeviceNames.empty());
        setVisible(OutputBox(), !isAsioBackend && !outputDeviceNames.empty());

        const auto groupedOutputChannels = groupedChannelRows(backend, outputChannelNames, activeOutputChannels, false);
        const auto groupedInputChannels = groupedChannelRows(backend, inputChannelNames, activeInputChannels, true);
        currentOutputChannelRows = groupedOutputChannels;
        currentInputChannelRows = groupedInputChannels;
        setVisible(ChannelsCard(), !groupedOutputChannels.empty() || !groupedInputChannels.empty());
        setVisible(OutputChannelGroup(), !groupedOutputChannels.empty());
        setVisible(InputChannelGroup(), !groupedInputChannels.empty());
        syncChannelCheckBoxes(OutputChannelsPanel(), groupedOutputChannels, "set-output-channel", renderedOutputChannelKeys);
        syncChannelCheckBoxes(InputChannelsPanel(), groupedInputChannels, "set-input-channel", renderedInputChannelKeys);
        syncChannelToggleButton(OutputChannelsToggleAllButton(), groupedOutputChannels, "set-all-output-channels");
        syncChannelToggleButton(InputChannelsToggleAllButton(), groupedInputChannels, "set-all-input-channels");

        std::vector<std::string> sampleRateItems;
        int selectedSampleRateIndex = -1;
        for (int i = 0; i < (int) sampleRateValues.size(); ++i)
        {
            sampleRateItems.push_back(formatNumber(sampleRateValues[(size_t) i], 0) + " Hz");
            if ((int) sampleRateValues[(size_t) i] == (int) sampleRate)
                selectedSampleRateIndex = i;
        }
        setComboItems(SampleRateBox(), sampleRateItems, selectedSampleRateIndex);

        std::vector<std::string> bufferSizeItems;
        int selectedBufferSizeIndex = -1;
        for (int i = 0; i < (int) bufferSizeValues.size(); ++i)
        {
            bufferSizeItems.push_back(formatNumber(bufferSizeValues[(size_t) i], 0) + " samples");
            if ((int) bufferSizeValues[(size_t) i] == bufferSize)
                selectedBufferSizeIndex = i;
        }
        setComboItems(BufferSizeBox(), bufferSizeItems, selectedBufferSizeIndex);
        setVisible(FormatCard(), !sampleRateItems.empty() || !bufferSizeItems.empty());
        setVisible(SampleRateLabel(), !sampleRateItems.empty());
        setVisible(SampleRateBox(), !sampleRateItems.empty());
        setVisible(BufferSizeLabel(), !bufferSizeItems.empty());
        setVisible(BufferSizeBox(), !bufferSizeItems.empty());
        syncingHostControls = false;

        syncingConfigControls = true;
        StartWithWindowsCheckBox().IsOn(startWithWindows);
        closeQuitsHost = closeBehavior == "quit";
        CloseQuitsAppRadioButton().IsChecked(closeQuitsHost);
        CloseToTrayRadioButton().IsChecked(!closeQuitsHost);
        CloseToTraySwitch().IsOn(!closeQuitsHost);
        EnableVst2CheckBox().IsEnabled(vst2HostAvailable);
        EnableVst2CheckBox().IsOn(vst2RuntimeEnabled);
        syncIconMode(trayIconMode);
        if (!vst2HostAvailable)
            Vst2StatusText().Text(L"VST2 is not available in this build. Rebuild Light Host Modern with VST2 headers to enable it.");
        else if (vst2RestartRequired)
            Vst2StatusText().Text(L"Restart this session to apply the changes.");
        else if (vst2RuntimeEnabled)
            Vst2StatusText().Text(L"VST2 plugins are enabled for this session.");
        else
            Vst2StatusText().Text(L"VST2 plugins are disabled. Changes take effect after restart.");
        ScanVstCheckBox().IsEnabled(vst2HostEnabled);
        if (!vst2HostEnabled)
            ScanVstCheckBox().IsChecked(false);

        const int persistenceIndex = audioPersistenceModeIndex(audioPersistenceMode);
        setComboItems(AudioPersistenceModeBox(), {
            to_string(localization.text("settings.persistence.disabled", L"Disabled")),
            to_string(localization.text("settings.persistence.lastSelected", L"Last selected device")),
            to_string(localization.text("settings.persistence.custom", L"Custom device")) }, persistenceIndex);
        AudioRecoveryRetrySecondsBox().Value((std::max)(1, audioPersistenceRetrySeconds));
        AudioRecoveryRetryAttemptsBox().Value((std::max)(1, audioPersistenceRetryAttempts));

        const int customBackendIndex = stringIndex(backendNames, audioPersistenceCustomBackend, currentBackendIndex);
        setComboItems(CustomRecoveryBackendBox(), backendNames, customBackendIndex);
        const auto& customInputCandidates = inputDeviceNames.empty() ? outputDeviceNames : inputDeviceNames;
        const auto& customOutputCandidates = outputDeviceNames.empty() ? inputDeviceNames : outputDeviceNames;
        const int customInputIndex = stringIndex(customInputCandidates, audioPersistenceCustomInputDevice, currentInputDeviceIndex);
        const int customOutputIndex = stringIndex(customOutputCandidates, audioPersistenceCustomOutputDevice, currentOutputDeviceIndex);
        setComboItems(CustomRecoveryInputBox(), customInputCandidates, customInputIndex);
        setComboItems(CustomRecoveryOutputBox(), customOutputCandidates, customOutputIndex);
        const auto retryVisibility = persistenceIndex == 0 ? Visibility::Collapsed : Visibility::Visible;
        AudioPersistenceRetryIntervalGrid().Visibility(retryVisibility);
        AudioPersistenceRetryAttemptsGrid().Visibility(retryVisibility);
        CustomAudioPersistenceCard().Visibility(persistenceIndex == 2 ? Visibility::Visible : Visibility::Collapsed);
        CustomAudioPersistenceGrid().Visibility(persistenceIndex == 2 ? Visibility::Visible : Visibility::Collapsed);
        const auto selectedCustomBackend = (customBackendIndex >= 0 && (size_t) customBackendIndex < backendNames.size())
            ? backendNames[(size_t) customBackendIndex]
            : backend;
        const bool customBackendIsAsio = selectedCustomBackend == "ASIO";
        CustomRecoveryInputLabel().Text(customBackendIsAsio ? L"Device" : L"Input device");
        CustomRecoveryOutputRow().Visibility(customBackendIsAsio ? Visibility::Collapsed : Visibility::Visible);
        updatePreferredDeviceSummary();
        RetryAudioDeviceButton().IsEnabled(persistenceIndex != 0);
        syncEnabledAudioChoicesSummary();
        updateToggleStateLabels();

        std::string persistenceStatus;
        if (persistenceIndex == 0)
        {
            persistenceStatus = "Device persistence is disabled. Light Host Modern will use the default audio recovery behavior.";
        }
        else if (!recoveryMessage.empty())
        {
            persistenceStatus = recoveryMessage;
        }
        else if (recoveryState == "retrying")
        {
            persistenceStatus = "Retrying preferred audio device (" + std::to_string(recoveryAttempt) + "/" + std::to_string(recoveryMaxAttempts) + ").";
        }
        else if (recoveryState == "failed")
        {
            persistenceStatus = "Preferred audio device is unavailable. Choose another device or retry manually.";
        }
        else
        {
            persistenceStatus = "Preferred audio device is available.";
        }

        if (persistenceIndex != 0 && (!recoveryTargetBackend.empty() || !recoveryTargetInputDevice.empty() || !recoveryTargetOutputDevice.empty()))
        {
            persistenceStatus += " Target: ";
            persistenceStatus += recoveryTargetBackend.empty() ? backend : recoveryTargetBackend;
            const auto targetInput = recoveryTargetInputDevice.empty() ? inputDeviceText : recoveryTargetInputDevice;
            const auto targetOutput = recoveryTargetOutputDevice.empty() ? outputDeviceText : recoveryTargetOutputDevice;
            if (!targetInput.empty())
                persistenceStatus += " / " + targetInput;
            if (!targetOutput.empty() && targetOutput != targetInput)
                persistenceStatus += " -> " + targetOutput;
            persistenceStatus += ".";
        }
        AudioRecoveryStatusText().Text(hs(persistenceStatus));
        syncingConfigControls = false;

        RunningPluginsSummaryText().Text(L"");
        InstalledPluginsSummaryText().Text(L"");

        setVisible(RunningPluginsListCard(), !pluginRows.empty());
        setVisible(RunningPluginsEmptyText(), pluginRows.empty());
        if (renderedRunningPluginLabels != pluginKeys)
        {
            RunningPluginsListView().Children().Clear();
            runningPluginItemBorders.clear();

            for (int i = 0; i < (int) pluginRows.size(); ++i)
            {
                const auto& plugin = pluginRows[(size_t) i];
                const auto sourceIndex = plugin.originalIndex;
                const bool canReorder = runningPluginSortMode == 0 && runningPluginSearch.empty();
                auto row = Grid();
                row.Tag(box_value(sourceIndex));
                row.CanDrag(canReorder);
                row.AllowDrop(canReorder);
                row.DragStarting({ this, &MainWindow::RunningPluginItem_DragStarting });
                row.DragOver({ this, &MainWindow::RunningPluginItem_DragOver });
                row.Drop({ this, &MainWindow::RunningPluginItem_Drop });
                row.Padding(ThicknessHelper::FromLengths(16, 0, 12, 0));
                row.ColumnSpacing(12);
                if (compactPluginCards)
                {
                    row.RowDefinitions().Append(RowDefinition());
                    row.RowDefinitions().Append(RowDefinition());
                }
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(40));
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                if (!compactPluginCards)
                {
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(3).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
                }
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(compactPluginCards ? 2 : 4).Width(GridLengthHelper::FromPixels(48));

                auto orderText = rowText(std::to_string(sourceIndex + 1), 14);
                orderText.HorizontalAlignment(HorizontalAlignment::Center);
                orderText.Opacity(0.72);
                row.Children().Append(orderText);

                auto identity = StackPanel();
                identity.VerticalAlignment(VerticalAlignment::Center);
                identity.Spacing(2);
                identity.Children().Append(pluginNameText(plugin.name, 16));

                const auto manufacturerLabel = plugin.manufacturer.empty()
                    ? to_string(localization.text("plugins.unknownManufacturer", L"Unknown manufacturer"))
                    : plugin.manufacturer;
                auto manufacturerText = rowText(manufacturerLabel, 13);
                manufacturerText.Opacity(plugin.manufacturer.empty() ? 0.55 : 0.85);
                manufacturerText.TextWrapping(TextWrapping::NoWrap);
                manufacturerText.TextTrimming(TextTrimming::CharacterEllipsis);
                ToolTipService::SetToolTip(manufacturerText, box_value(hs(manufacturerLabel)));
                identity.Children().Append(manufacturerText);
                Grid::SetColumn(identity, 1);
                row.Children().Append(identity);

                auto format = formatBadge(plugin.format);
                format.HorizontalAlignment(HorizontalAlignment::Left);
                format.VerticalAlignment(VerticalAlignment::Center);
                auto statusBadge = pill(plugin.status, to_string(localization.translatedSource(hs(plugin.status))));
                statusBadge.HorizontalAlignment(HorizontalAlignment::Left);
                statusBadge.VerticalAlignment(VerticalAlignment::Center);
                if (compactPluginCards)
                {
                    auto metadata = StackPanel();
                    metadata.Orientation(Orientation::Horizontal);
                    metadata.Spacing(8);
                    metadata.Margin(ThicknessHelper::FromLengths(0, 4, 0, 10));
                    metadata.Children().Append(format);
                    metadata.Children().Append(statusBadge);
                    Grid::SetRow(metadata, 1);
                    Grid::SetColumn(metadata, 1);
                    Grid::SetColumnSpan(metadata, 2);
                    row.Children().Append(metadata);
                }
                else
                {
                    Grid::SetColumn(format, 2);
                    Grid::SetColumn(statusBadge, 3);
                    row.Children().Append(format);
                    row.Children().Append(statusBadge);
                }

                auto actionsButton = rowActionsMenuButton(sourceIndex);
                auto actionsMenu = MenuFlyout();
                actionsMenu.Placement(FlyoutPlacementMode::BottomEdgeAlignedRight);
                actionsMenu.Items().Append(actionMenuItem(localization.text("plugins.openEditor", L"Open editor").c_str(), L"\xE8A7", sourceIndex, { this, &MainWindow::OpenPluginEditor_Click }));
                actionsMenu.Items().Append(actionMenuItem(localization.text("plugins.duplicate", L"Duplicate").c_str(), L"\xE8C8", sourceIndex, { this, &MainWindow::DuplicatePlugin_Click }));
                actionsMenu.Items().Append(actionMenuItem(localization.text(plugin.bypassed ? "plugins.enable" : "plugins.bypass", plugin.bypassed ? L"Enable" : L"Bypass").c_str(), L"\xE7E8", sourceIndex, { this, &MainWindow::BypassPlugin_Click }));
                actionsMenu.Items().Append(actionMenuItem(localization.text("common.remove", L"Remove").c_str(), L"\xE74D", sourceIndex, { this, &MainWindow::RemovePlugin_Click }));
                actionsButton.Flyout(actionsMenu);
                actionsButton.HorizontalAlignment(HorizontalAlignment::Right);
                actionsButton.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(actionsButton, compactPluginCards ? 2 : 4);
                row.Children().Append(actionsButton);

                auto item = pluginListItem(row, sourceIndex);
                if (compactPluginCards)
                    item.MinHeight(96);
                item.CanDrag(canReorder);
                item.AllowDrop(canReorder);
                item.DragStarting({ this, &MainWindow::RunningPluginItem_DragStarting });
                item.DragOver({ this, &MainWindow::RunningPluginItem_DragOver });
                item.Drop({ this, &MainWindow::RunningPluginItem_Drop });
                runningPluginItemBorders.push_back(item);
                RunningPluginsListView().Children().Append(item);
            }

            renderedRunningPluginLabels = pluginKeys;
        }

        activePluginCount = (int) allPluginRows.size();
        updateRunningPluginActions();

        installedPluginCount = (int) allKnownPluginRows.size();
        setVisible(InstalledPluginsListCard(), !knownPluginRows.empty());
        setVisible(InstalledPluginsEmptyText(), knownPluginRows.empty());
        if (renderedInstalledPluginLabels != knownPluginKeys)
        {
            InstalledPluginsListView().Children().Clear();
            installedPluginItemBorders.clear();

            for (int i = 0; i < (int) knownPluginRows.size(); ++i)
            {
                const auto& plugin = knownPluginRows[(size_t) i];
                const auto sourceIndex = plugin.originalIndex;
                auto row = Grid();
                row.Padding(ThicknessHelper::FromLengths(16, 0, 12, 0));
                row.ColumnSpacing(12);
                if (compactPluginCards)
                {
                    row.RowDefinitions().Append(RowDefinition());
                    row.RowDefinitions().Append(RowDefinition());
                }
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                if (!compactPluginCards)
                {
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
                }
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(compactPluginCards ? 1 : 3).Width(GridLengthHelper::FromPixels(48));

                auto identity = StackPanel();
                identity.VerticalAlignment(VerticalAlignment::Center);
                identity.Spacing(2);
                identity.Children().Append(pluginNameText(plugin.name, 16));

                const auto manufacturerLabel = plugin.manufacturer.empty()
                    ? to_string(localization.text("plugins.unknownManufacturer", L"Unknown manufacturer"))
                    : plugin.manufacturer;
                auto manufacturerText = rowText(manufacturerLabel, 13);
                manufacturerText.Opacity(plugin.manufacturer.empty() ? 0.55 : 0.85);
                manufacturerText.TextWrapping(TextWrapping::NoWrap);
                manufacturerText.TextTrimming(TextTrimming::CharacterEllipsis);
                ToolTipService::SetToolTip(manufacturerText, box_value(hs(manufacturerLabel)));
                identity.Children().Append(manufacturerText);
                row.Children().Append(identity);

                auto format = formatBadge(plugin.format);
                format.HorizontalAlignment(HorizontalAlignment::Left);
                format.VerticalAlignment(VerticalAlignment::Center);
                auto statusBadge = pill(plugin.status, to_string(localization.translatedSource(hs(plugin.status))));
                statusBadge.HorizontalAlignment(HorizontalAlignment::Left);
                statusBadge.VerticalAlignment(VerticalAlignment::Center);
                if (compactPluginCards)
                {
                    auto metadata = StackPanel();
                    metadata.Orientation(Orientation::Horizontal);
                    metadata.Spacing(8);
                    metadata.Margin(ThicknessHelper::FromLengths(0, 4, 0, 10));
                    metadata.Children().Append(format);
                    metadata.Children().Append(statusBadge);
                    Grid::SetRow(metadata, 1);
                    Grid::SetColumn(metadata, 0);
                    Grid::SetColumnSpan(metadata, 2);
                    row.Children().Append(metadata);
                }
                else
                {
                    Grid::SetColumn(format, 1);
                    Grid::SetColumn(statusBadge, 2);
                    row.Children().Append(format);
                    row.Children().Append(statusBadge);
                }

                auto actionsButton = rowActionsMenuButton(sourceIndex);
                auto actionsMenu = MenuFlyout();
                actionsMenu.Placement(FlyoutPlacementMode::BottomEdgeAlignedRight);
                actionsMenu.Items().Append(actionMenuItem(localization.text("plugins.addToChain", L"Add to chain").c_str(), L"\xE710", sourceIndex, { this, &MainWindow::AddInstalledPlugin_Click }));
                actionsMenu.Items().Append(actionMenuItem(localization.text("plugins.openFolder", L"Open folder").c_str(), L"\xE8B7", sourceIndex, { this, &MainWindow::OpenInstalledPluginLocation_Click }));
                actionsMenu.Items().Append(actionMenuItem(localization.text("plugins.removeDatabase", L"Remove from database").c_str(), L"\xE74D", sourceIndex, { this, &MainWindow::RemoveInstalledPlugin_Click }));
                actionsButton.Flyout(actionsMenu);
                actionsButton.HorizontalAlignment(HorizontalAlignment::Right);
                actionsButton.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(actionsButton, compactPluginCards ? 1 : 3);
                row.Children().Append(actionsButton);

                auto item = pluginListItem(row, sourceIndex);
                if (compactPluginCards)
                    item.MinHeight(96);
                installedPluginItemBorders.push_back(item);
                InstalledPluginsListView().Children().Append(item);
            }

            renderedInstalledPluginLabels = knownPluginKeys;
        }

        updateInstalledPluginActions();

        (void) reloads;
        (void) flushes;
    }

    bool MainWindow::sendCommand(std::string const& command)
    {
        if (hostPipeName.empty())
        {
            winUILog("Command skipped because host pipe is empty: " + command);
            HeaderStatusText().Text(L"No host pipe");
            return false;
        }

        winUILog("Sending command: " + command);
        commandInProgress = true;
        const auto response = requestHost(hostPipeName, command);
        lastCommandResponse = response;
        commandInProgress = false;
        if (response.empty())
        {
            winUILog("Command failed with empty response; the host pipe closed or the plugin load crashed/froze the host: " + command);
            HeaderStatusText().Text(L"Host did not respond");
            showNotification(L"Host did not respond. The plugin may have crashed or frozen during load.");
            return false;
        }

        winUILog("Command response: " + response);
        if (response.find("\"status\":\"error\"") != std::string::npos)
        {
            const auto message = extractString(response, "message", "Command failed");
            HeaderStatusText().Text(hs(message));
            showNotification(utf8ToWide(message));
            if (!comboDropDownOpen)
                refreshSnapshot();
            return false;
        }

        if (!comboDropDownOpen)
            refreshSnapshot();

        return response.find("\"status\":\"ok\"") != std::string::npos;
    }

    int MainWindow::selectedRunningPluginIndex()
    {
        return -1;
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
        RunningPluginsListView().Opacity(activePluginCount > 0 ? 1.0 : 0.65);
    }

    void MainWindow::updateInstalledPluginActions()
    {
        InstalledPluginsListView().Opacity(installedPluginCount > 0 ? 1.0 : 0.65);
        RemoveMissingPluginsButton().IsEnabled(installedPluginCount > 0);
        ClearPluginDatabaseButton().IsEnabled(installedPluginCount > 0);
        RemoveMissingPluginsMenuItem().IsEnabled(installedPluginCount > 0);
        ClearPluginDatabaseMenuItem().IsEnabled(installedPluginCount > 0);
    }

    void MainWindow::BypassPlugin_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, selectedRunningPluginIndex());
        if (index >= 0)
        {
            if (sendCommand("toggle-bypass:" + std::to_string(index)))
            {
                showNotification(L"Plugin bypass updated.");
            }
        }
    }

    void MainWindow::OpenPluginEditor_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, selectedRunningPluginIndex());
        if (index >= 0)
            sendCommand("open-plugin-editor:" + std::to_string(index));
    }

    void MainWindow::DuplicatePlugin_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, selectedRunningPluginIndex());
        if (index >= 0)
        {
            if (sendCommand("duplicate-plugin:" + std::to_string(index)))
            {
                showNotification(L"Plugin duplicated.");
            }
        }
    }

    void MainWindow::RemovePlugin_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, selectedRunningPluginIndex());
        if (index >= 0)
        {
            if (sendCommand("remove-plugin:" + std::to_string(index)))
            {
                showNotification(L"Plugin removed from chain.");
            }
        }
    }

    void MainWindow::ScanDefaultPlugins_Click(IInspectable const&, RoutedEventArgs const&)
    {
        const bool scanVst = isChecked(ScanVstCheckBox());
        const bool scanVst3 = isChecked(ScanVst3CheckBox());
        if (scanVst && scanVst3)
        {
            if (!pluginScanPaths.empty())
            {
                int added = 0;
                bool anyScanSucceeded = false;
                for (auto const& path : pluginScanPaths)
                {
                    if (sendCommand("scan-plugin-path:" + path))
                    {
                        anyScanSucceeded = true;
                        added += (int) extractNumber(lastCommandResponse, "added", 0);
                    }
                }

                if (anyScanSucceeded)
                {
                    showNotification(L"Plugin scan completed. " + std::to_wstring(added) + L" new plugins found.");
                }
            }
            else if (sendCommand("scan-default-plugins:all"))
            {
                showNotification(L"Plugin scan completed. " + std::to_wstring((int) extractNumber(lastCommandResponse, "added", 0)) + L" new plugins found.");
            }
        }
        else if (scanVst3)
        {
            if (sendCommand("scan-default-plugins:vst3"))
            {
                showNotification(L"VST3 scan completed. " + std::to_wstring((int) extractNumber(lastCommandResponse, "added", 0)) + L" new plugins found.");
            }
        }
        else if (scanVst)
        {
            if (sendCommand("scan-default-plugins:vst"))
            {
                showNotification(L"VST scan completed. " + std::to_wstring((int) extractNumber(lastCommandResponse, "added", 0)) + L" new plugins found.");
            }
        }
    }

    void MainWindow::ScanPaths_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (pluginScanPaths.empty())
            resetDefaultPluginScanPaths();

        auto pathRows = StackPanel();
        pathRows.Spacing(8);
        auto pathEntries = StackPanel();
        pathEntries.Spacing(8);

        auto editors = std::make_shared<std::vector<TextBox>>();
        auto values = std::make_shared<std::vector<std::shared_ptr<std::wstring>>>();
        for (auto const& path : pluginScanPaths)
            values->push_back(std::make_shared<std::wstring>(utf8ToWide(path)));

        auto addPathButton = Button();
        addPathButton.HorizontalAlignment(HorizontalAlignment::Stretch);
        addPathButton.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        addPathButton.MinHeight(64);
        addPathButton.Padding(ThicknessHelper::FromUniformLength(16));
        addPathButton.Background(resourceBrush(L"AppCardBrush", themedFallback(makeColor(255, 255, 255), makeColor(39, 39, 39))));
        addPathButton.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(225, 225, 225), makeColor(58, 58, 58))));
        addPathButton.BorderThickness(ThicknessHelper::FromUniformLength(1));
        addPathButton.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(addPathButton, L"AddNewScanPath");
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            addPathButton,
            localization.text("dialogs.scanPaths.add", L"Add new path"));

        auto addPathContent = Grid();
        addPathContent.ColumnSpacing(12);
        addPathContent.ColumnDefinitions().Append(ColumnDefinition());
        addPathContent.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(32));
        addPathContent.ColumnDefinitions().Append(ColumnDefinition());
        addPathContent.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

        auto addPathIcon = FontIcon();
        addPathIcon.Glyph(L"\xE710");
        addPathIcon.VerticalAlignment(VerticalAlignment::Center);
        addPathContent.Children().Append(addPathIcon);

        auto addPathText = StackPanel();
        addPathText.Spacing(2);
        auto addPathTitle = TextBlock();
        addPathTitle.Text(localization.text("dialogs.scanPaths.add", L"Add new path"));
        addPathTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        auto addPathDescription = TextBlock();
        addPathDescription.Text(localization.text("dialogs.scanPaths.addDescription", L"Create another folder entry and edit it immediately."));
        addPathDescription.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
        addPathDescription.TextWrapping(TextWrapping::Wrap);
        addPathText.Children().Append(addPathTitle);
        addPathText.Children().Append(addPathDescription);
        Grid::SetColumn(addPathText, 1);
        addPathContent.Children().Append(addPathText);
        addPathButton.Content(addPathContent);
        pathRows.Children().Append(addPathButton);
        pathRows.Children().Append(pathEntries);

        auto rebuildRows = std::make_shared<std::function<void()>>();
        auto weakRebuildRows = std::weak_ptr<std::function<void()>>(rebuildRows);
        *rebuildRows = [this, values, editors, pathEntries, weakRebuildRows]()
        {
            pathEntries.Children().Clear();
            editors->clear();

            for (int i = 0; i < (int) values->size(); ++i)
            {
                const auto entry = (*values)[(size_t) i];
                auto row = Grid();
                row.ColumnSpacing(10);
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromPixels(44));
                row.ColumnDefinitions().Append(ColumnDefinition());
                row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromPixels(44));

                auto editor = TextBox();
                editor.Text(hstring(*entry));
                editor.PlaceholderText(localization.text("dialogs.scanPaths.placeholder", L"Enter the path here"));
                editor.MinHeight(40);
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                    editor,
                    hstring(L"ScanPathEditor" + std::to_wstring(i)));
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                    editor,
                    localization.text("dialogs.scanPaths.path", L"Plugin scan path"));
                const auto weakEditor = make_weak(editor);
                editor.TextChanged([entry, weakEditor](IInspectable const&, TextChangedEventArgs const&)
                {
                    if (const auto currentEditor = weakEditor.get())
                        *entry = std::wstring(currentEditor.Text().c_str());
                });
                editor.LostFocus([values, entry, weakRebuildRows, weakEditor](IInspectable const&, RoutedEventArgs const&)
                {
                    const auto currentEditor = weakEditor.get();
                    if (!currentEditor || !trimPath(wideToUtf8(currentEditor.Text().c_str())).empty())
                        return;

                    currentEditor.DispatcherQueue().TryEnqueue([values, entry, weakRebuildRows]()
                    {
                        if (!trimPath(wideToUtf8(*entry)).empty())
                            return;

                        const auto match = std::find(values->begin(), values->end(), entry);
                        if (match == values->end())
                            return;

                        values->erase(match);
                        if (const auto rebuild = weakRebuildRows.lock())
                            (*rebuild)();
                    });
                });
                editors->push_back(editor);
                row.Children().Append(editor);

                auto browseButton = iconButton(
                    L"\xE8B7",
                    i,
                    localization.text("dialogs.scanPaths.browse", L"Browse folder").c_str());
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                    browseButton,
                    hstring(L"BrowseScanPath" + std::to_wstring(i)));
                browseButton.Click([entry, weakRebuildRows](IInspectable const&, RoutedEventArgs const&)
                {
                    const auto picked = pickFolderPath();
                    if (picked.empty())
                        return;

                    *entry = picked;
                    if (const auto rebuild = weakRebuildRows.lock())
                        (*rebuild)();
                });
                Grid::SetColumn(browseButton, 1);
                row.Children().Append(browseButton);

                auto removeButton = iconButton(
                    L"\xE74D",
                    i,
                    localization.text("dialogs.scanPaths.remove", L"Remove path").c_str());
                Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(
                    removeButton,
                    hstring(L"RemoveScanPath" + std::to_wstring(i)));
                removeButton.Click([values, entry, weakRebuildRows](IInspectable const&, RoutedEventArgs const&)
                {
                    const auto match = std::find(values->begin(), values->end(), entry);
                    if (match != values->end())
                        values->erase(match);
                    if (const auto rebuild = weakRebuildRows.lock())
                        (*rebuild)();
                });
                Grid::SetColumn(removeButton, 2);
                row.Children().Append(removeButton);

                auto card = Border();
                card.Background(resourceBrush(L"AppCardBrush", themedFallback(makeColor(255, 255, 255), makeColor(39, 39, 39))));
                card.BorderBrush(resourceBrush(L"AppCardStrokeBrush", themedFallback(makeColor(225, 225, 225), makeColor(58, 58, 58))));
                card.BorderThickness(ThicknessHelper::FromUniformLength(1));
                card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
                card.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
                card.Child(row);
                pathEntries.Children().Append(card);
            }
        };
        addPathButton.Click([values, editors, rebuildRows](IInspectable const&, RoutedEventArgs const&)
        {
            values->insert(values->begin(), std::make_shared<std::wstring>());
            (*rebuildRows)();
            if (editors->empty())
                return;

            const auto editor = editors->front();
            editor.Focus(FocusState::Programmatic);
            editor.SelectAll();
        });
        (*rebuildRows)();

        auto hint = TextBlock();
        hint.Text(localization.text(
            "dialogs.scanPaths.description",
            L"Scan uses these folders when VST and VST3 scanning are both enabled."));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(hint, L"ScanPathsHint");
        hint.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
        hint.TextWrapping(TextWrapping::Wrap);

        auto pathScroller = ScrollViewer();
        pathScroller.VerticalScrollMode(ScrollMode::Enabled);
        pathScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Visible);
        pathScroller.HorizontalScrollMode(ScrollMode::Disabled);
        pathScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        pathScroller.Height(340);
        pathScroller.Content(pathRows);

        auto content = StackPanel();
        content.Spacing(12);
        content.Children().Append(hint);
        content.Children().Append(pathScroller);

        auto dialog = ContentDialog();
        dialog.XamlRoot(RootLayout().XamlRoot());
        dialog.Title(box_value(localization.text("dialogs.scanPaths.title", L"Scan paths")));
        dialog.Content(content);
        dialog.PrimaryButtonText(localization.text("common.save", L"Save"));
        dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        sizeDialogToViewport(dialog, RootLayout(), 0.75);
        enableDialogBackgroundDefocus(dialog);

        auto operation = dialog.ShowAsync();
        operation.Completed([this, dialog, values](auto const& async, winrt::Windows::Foundation::AsyncStatus const status)
        {
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
                return;

            if (async.GetResults() != ContentDialogResult::Primary)
                return;

            DispatcherQueue().TryEnqueue([this, values]()
            {
                std::wstring text;
                for (auto const& value : *values)
                {
                    if (trimPath(wideToUtf8(*value)).empty())
                        continue;
                    if (!text.empty())
                        text += L"\r\n";
                    text += *value;
                }
                pluginScanPaths = parsePaths(text);
                showNotification(localization.format(
                    "dialogs.scanPaths.saved",
                    L"{0} scan paths saved.",
                    { std::to_wstring((int) pluginScanPaths.size()) }).c_str());
            });
        });
    }

    void MainWindow::AddInstalledPlugin_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, -1);
        if (index >= 0)
        {
            if (sendCommand("add-known-plugin:" + std::to_string(index)))
            {
                showNotification(L"Plugin added to chain.");
            }
        }
    }

    void MainWindow::OpenInstalledPluginLocation_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, -1);
        if (index >= 0)
            sendCommand("open-known-plugin-location:" + std::to_string(index));
    }

    void MainWindow::RemoveInstalledPlugin_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto index = taggedIndexOrSelected(sender, -1);
        if (index < 0)
            return;

        auto removePlugin = [this, index]()
        {
            if (sendCommand("remove-known-plugin:" + std::to_string(index)))
            {
                const int removedActive = (int) extractNumber(lastCommandResponse, "removedActive", 0);
                showNotification(removedActive > 0
                    ? L"Plugin removed from database and running chain."
                    : L"Plugin removed from database.");
            }
        };

        bool isRunning = false;
        if (index < (int) knownPluginIdentityKeys.size())
        {
            const auto& identity = knownPluginIdentityKeys[(size_t) index];
            isRunning = std::find(activePluginIdentityKeys.begin(), activePluginIdentityKeys.end(), identity) != activePluginIdentityKeys.end();
        }

        if (!isRunning)
        {
            removePlugin();
            return;
        }

        const auto pluginName = index < (int) knownPluginDisplayNames.size()
            ? knownPluginDisplayNames[(size_t) index]
            : std::wstring(L"This plugin");

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

        auto operation = dialog.ShowAsync();
        operation.Completed([this, dialog, removePlugin](auto const& async, winrt::Windows::Foundation::AsyncStatus const status)
        {
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
                return;

            if (async.GetResults() != ContentDialogResult::Primary)
                return;

            DispatcherQueue().TryEnqueue([removePlugin]()
            {
                removePlugin();
            });
        });
    }

    void MainWindow::RemoveMissingPlugins_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (sendCommand("remove-missing-known-plugins"))
        {
            showNotification(std::to_wstring((int) extractNumber(lastCommandResponse, "removed", 0)) + L" missing plugins removed.");
        }
    }

    void MainWindow::ClearPluginDatabase_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto dialog = ContentDialog();
        dialog.XamlRoot(RootLayout().XamlRoot());
        dialog.Title(box_value(localization.text("dialogs.plugins.clearTitle", L"Clear plugin database?")));
        dialog.Content(box_value(localization.text("dialogs.plugins.clearMessage", L"This removes every installed plugin entry and clears the current running chain.")));
        dialog.PrimaryButtonText(localization.text("common.clear", L"Clear"));
        dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Close);

        auto operation = dialog.ShowAsync();
        operation.Completed([this, dialog](auto const& async, winrt::Windows::Foundation::AsyncStatus const status)
        {
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
                return;

            if (async.GetResults() != ContentDialogResult::Primary)
                return;

            DispatcherQueue().TryEnqueue([this]()
            {
                if (sendCommand("clear-known-plugins"))
                {
                    const int removed = (int) extractNumber(lastCommandResponse, "removed", 0);
                    const int removedActive = (int) extractNumber(lastCommandResponse, "removedActive", 0);
                    showNotification(std::to_wstring(removed) + L" installed plugins cleared. "
                        + std::to_wstring(removedActive) + L" running plugins removed.");
                }
            });
        });
    }

    void MainWindow::DeletePluginStates_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (sendCommand("delete-plugin-states"))
            showNotification(L"Plugin states deleted.");
    }

    void MainWindow::StartWithWindowsCheckBox_Changed(IInspectable const&, RoutedEventArgs const&)
    {
        updateToggleStateLabels();
        if (syncingConfigControls)
            return;

        sendCommand(std::string("set-start-with-windows:") + (isChecked(StartWithWindowsCheckBox()) ? "1" : "0"));
    }

    void MainWindow::CloseBehaviorRadioButton_Checked(IInspectable const&, RoutedEventArgs const&)
    {
        if (syncingConfigControls)
            return;

        const auto checked = CloseQuitsAppRadioButton().IsChecked();
        closeQuitsHost = checked && checked.Value();
        CloseToTraySwitch().IsOn(!closeQuitsHost);
        sendCommand(std::string("set-close-behavior:") + (closeQuitsHost ? "quit" : "tray"));
    }

    void MainWindow::CloseToTraySwitch_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        updateToggleStateLabels();
        if (syncingConfigControls)
            return;

        closeQuitsHost = !CloseToTraySwitch().IsOn();
        CloseQuitsAppRadioButton().IsChecked(closeQuitsHost);
        CloseToTrayRadioButton().IsChecked(!closeQuitsHost);
        sendCommand(std::string("set-close-behavior:") + (closeQuitsHost ? "quit" : "tray"));
    }

    void MainWindow::EnableVst2CheckBox_Changed(IInspectable const&, RoutedEventArgs const&)
    {
        updateToggleStateLabels();
        if (syncingConfigControls)
            return;

        vst2RestartRequired = true;
        sendCommand(std::string("set-enable-vst2:") + (isChecked(EnableVst2CheckBox()) ? "1" : "0"));
        Vst2StatusText().Text(L"Restart this session to apply the changes.");
    }

    void MainWindow::AudioPersistenceModeBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingConfigControls || !AudioPersistenceModeBox() || AudioPersistenceModeBox().SelectedIndex() < 0)
            return;

        const auto mode = audioPersistenceModeValue(AudioPersistenceModeBox().SelectedIndex());
        sendCommand("set-audio-persistence-mode:" + mode);
        refreshSnapshot();
    }

    void MainWindow::AudioRecoveryRetrySecondsBox_ValueChanged(NumberBox const&, NumberBoxValueChangedEventArgs const& args)
    {
        if (syncingConfigControls)
            return;

        const auto value = args.NewValue();
        if (value != value)
            return;

        const int retrySeconds = (std::max)(1, (std::min)(60, (int) std::round(value)));
        sendCommand("set-audio-persistence-retry-seconds:" + std::to_string(retrySeconds));
    }

    void MainWindow::AudioRecoveryRetryAttemptsBox_ValueChanged(NumberBox const&, NumberBoxValueChangedEventArgs const& args)
    {
        if (syncingConfigControls)
            return;

        const auto value = args.NewValue();
        if (value != value)
            return;

        const int retryAttempts = (std::max)(1, (std::min)(100, (int) std::round(value)));
        sendCommand("set-audio-persistence-retry-attempts:" + std::to_string(retryAttempts));
    }

    void MainWindow::CustomRecoveryBackendBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingConfigControls || !CustomRecoveryBackendBox() || CustomRecoveryBackendBox().SelectedIndex() < 0)
            return;

        sendCommand("set-audio-persistence-custom-backend:" + std::to_string(CustomRecoveryBackendBox().SelectedIndex()));
        refreshSnapshot();
    }

    void MainWindow::CustomRecoveryInputBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingConfigControls || !CustomRecoveryInputBox() || CustomRecoveryInputBox().SelectedIndex() < 0)
            return;

        sendCommand("set-audio-persistence-custom-input:" + std::to_string(CustomRecoveryInputBox().SelectedIndex()));
        refreshSnapshot();
    }

    void MainWindow::CustomRecoveryOutputBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingConfigControls || !CustomRecoveryOutputBox() || CustomRecoveryOutputBox().SelectedIndex() < 0)
            return;

        sendCommand("set-audio-persistence-custom-output:" + std::to_string(CustomRecoveryOutputBox().SelectedIndex()));
        refreshSnapshot();
    }

    void MainWindow::updatePreferredDeviceSummary()
    {
        if (!PreferredDeviceSummaryText())
            return;

        const auto backend = selectedComboText(CustomRecoveryBackendBox());
        const auto input = selectedComboText(CustomRecoveryInputBox());
        const auto output = selectedComboText(CustomRecoveryOutputBox());
        std::string summary = backend.empty() ? "Choose device" : backend;
        if (!input.empty())
            summary += " · " + input;
        if (backend != "ASIO" && !output.empty() && output != input)
            summary += " / " + output;
        PreferredDeviceSummaryText().Text(hs(summary));
        ToolTipService::SetToolTip(PreferredDeviceButton(), box_value(hs(summary)));
    }

    void MainWindow::PreferredDeviceButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        showPreferredDeviceDialogAsync();
    }

    fire_and_forget MainWindow::showPreferredDeviceDialogAsync()
    {
        if (preferredDeviceDialogOpen)
            co_return;

        preferredDeviceDialogOpen = true;
        auto lifetime = get_strong();
        auto picker = StackPanel();
        picker.Spacing(16);
        picker.MinWidth(440);
        const int originalBackendIndex = CustomRecoveryBackendBox().SelectedIndex();
        const int originalInputIndex = CustomRecoveryInputBox().SelectedIndex();
        const int originalOutputIndex = CustomRecoveryOutputBox().SelectedIndex();
        ContentDialogResult result = ContentDialogResult::None;

        try
        {
            CustomRecoveryBackendBoxHost().Children().Clear();
            CustomRecoveryInputBoxHost().Children().Clear();
            CustomRecoveryOutputBoxHost().Children().Clear();

            CustomRecoveryBackendBox().Header(box_value(localization.text("settings.persistence.backend", L"Audio backend")));
            const bool isAsio = selectedComboText(CustomRecoveryBackendBox()) == "ASIO";
            CustomRecoveryInputBox().Header(box_value(localization.text(
                isAsio ? "settings.persistence.device" : "settings.persistence.inputDevice",
                isAsio ? L"Device" : L"Input device")));
            CustomRecoveryOutputBox().Header(box_value(localization.text("settings.persistence.outputDevice", L"Output device")));
            CustomRecoveryOutputBox().Visibility(isAsio ? Visibility::Collapsed : Visibility::Visible);

            auto hint = TextBlock();
            hint.Text(localization.text(
                "settings.persistence.preferredDescription",
                L"Select the backend and device Light Host Modern should retry."));
            hint.TextWrapping(TextWrapping::Wrap);
            hint.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(hint, L"PreferredDeviceDialogHint");
            picker.Children().Append(hint);
            picker.Children().Append(CustomRecoveryBackendBox());
            picker.Children().Append(CustomRecoveryInputBox());
            picker.Children().Append(CustomRecoveryOutputBox());

            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("settings.persistence.preferred", L"Preferred device")));
            dialog.Content(picker);
            dialog.PrimaryButtonText(localization.text("common.save", L"Save"));
            dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
            dialog.DefaultButton(ContentDialogButton::Primary);
            enableDialogBackgroundDefocus(dialog);
            result = co_await dialog.ShowAsync();
            dialog.Content(nullptr);
        }
        catch (hresult_error const& error)
        {
            winUILog("Preferred device dialog failed: " + to_string(error.message()));
        }
        catch (...)
        {
            winUILog("Preferred device dialog failed with an unknown error.");
        }

        picker.Children().Clear();
        CustomRecoveryBackendBox().Header(nullptr);
        CustomRecoveryInputBox().Header(nullptr);
        CustomRecoveryOutputBox().Header(nullptr);
        CustomRecoveryOutputBox().Visibility(Visibility::Visible);
        CustomRecoveryBackendBoxHost().Children().Append(CustomRecoveryBackendBox());
        CustomRecoveryInputBoxHost().Children().Append(CustomRecoveryInputBox());
        CustomRecoveryOutputBoxHost().Children().Append(CustomRecoveryOutputBox());

        if (result != ContentDialogResult::Primary)
        {
            if (originalBackendIndex >= 0)
                sendCommand("set-audio-persistence-custom-backend:" + std::to_string(originalBackendIndex));
            if (originalInputIndex >= 0)
                sendCommand("set-audio-persistence-custom-input:" + std::to_string(originalInputIndex));
            if (originalOutputIndex >= 0)
                sendCommand("set-audio-persistence-custom-output:" + std::to_string(originalOutputIndex));
            refreshSnapshot();
        }

        updatePreferredDeviceSummary();
        preferredDeviceDialogOpen = false;
    }

    void MainWindow::RetryAudioDevice_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (sendCommand("retry-audio-device"))
            showNotification(L"Audio device retry started.");
        refreshSnapshot();
    }

    void MainWindow::ChooseAudioDevice_Click(IInspectable const&, RoutedEventArgs const&)
    {
        showSection(L"Audio");
    }

    void MainWindow::ManageEnabledAudioDevices_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (hostPipeName.empty())
        {
            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
            dialog.Content(box_value(localization.text("dialogs.enabledDevices.disconnected", L"Light Host Modern is not connected to the audio host.")));
            dialog.CloseButtonText(localization.text("common.close", L"Close"));
            dialog.ShowAsync();
            return;
        }

        const auto choicesJson = requestHost(hostPipeName, "enabled-audio-choices");
        if (choicesJson.empty() || choicesJson.find("\"status\":\"ok\"") == std::string::npos)
        {
            auto dialog = ContentDialog();
            dialog.XamlRoot(RootLayout().XamlRoot());
            dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
            dialog.Content(box_value(localization.text("dialogs.enabledDevices.loadFailed", L"Could not load the available audio device list.")));
            dialog.CloseButtonText(localization.text("common.close", L"Close"));
            dialog.ShowAsync();
            return;
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
            dialog.ShowAsync();
            return;
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
        backendToggle.Content(box_value(localization.text("dialogs.enabledDevices.enableBackend", L"Enable this audio backend")));
        backendToggle.MinHeight(40);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(backendToggle, L"EnabledAudioBackendToggle");

        auto deviceSections = StackPanel();
        deviceSections.Spacing(12);

        auto updating = std::make_shared<bool>(false);
        auto rebuild = std::make_shared<std::function<void()>>();
        *rebuild = [this, backendBox, backendToggle, deviceSections, backendEnabled, deviceEnabled, updating, rebuild]()
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

            auto appendSection = [this, backendName, enabled, deviceEnabled, deviceSections, updating](std::string const& role, hstring const& title)
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
                    auto row = Grid();
                    row.MinHeight(40);
                    row.ColumnSpacing(10);
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(34));
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

                    auto box = CheckBox();
                    box.Tag(box_value(i));
                    box.IsChecked(static_cast<bool>((*deviceEnabled)[(size_t) i]));
                    box.IsEnabled(enabled);
                    box.VerticalAlignment(VerticalAlignment::Center);
                    box.HorizontalAlignment(HorizontalAlignment::Center);
                    box.MinHeight(32);
                    box.MinWidth(32);
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
                    Grid::SetColumn(box, 0);
                    row.Children().Append(box);

                    auto label = TextBlock();
                    label.Text(hs(entry.name));
                    label.VerticalAlignment(VerticalAlignment::Center);
                    label.TextTrimming(TextTrimming::CharacterEllipsis);
                    ToolTipService::SetToolTip(label, box_value(hs(entry.name)));
                    Grid::SetColumn(label, 1);
                    row.Children().Append(label);
                    section.Children().Append(row);
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
            (*rebuild)();
        });
        backendToggle.Checked([backendBox, backendEnabled, updating, rebuild](IInspectable const&, RoutedEventArgs const&)
        {
            if (*updating)
                return;
            const int index = backendBox.SelectedIndex();
            if (index >= 0 && index < (int) backendEnabled->size())
                (*backendEnabled)[(size_t) index] = true;
            (*rebuild)();
        });
        backendToggle.Unchecked([backendBox, backendEnabled, updating, rebuild](IInspectable const&, RoutedEventArgs const&)
        {
            if (*updating)
                return;
            const int index = backendBox.SelectedIndex();
            if (index >= 0 && index < (int) backendEnabled->size())
                (*backendEnabled)[(size_t) index] = false;
            (*rebuild)();
        });
        (*rebuild)();

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
            L"Choose an audio backend, then enable only the devices Light Host Modern may use."));
        hint.Foreground(resourceBrush(L"AppTextSecondaryBrush", themedFallback(makeColor(96, 96, 96), makeColor(190, 200, 214))));
        hint.TextWrapping(TextWrapping::Wrap);
        modeStack.Children().Append(hint);
        modeStack.Children().Append(backendBox);
        modeStack.Children().Append(backendToggle);
        modeCard.Child(modeStack);

        auto contentStack = StackPanel();
        contentStack.Spacing(12);
        contentStack.Children().Append(modeCard);
        contentStack.Children().Append(deviceSections);

        auto scroller = ScrollViewer();
        scroller.VerticalScrollMode(ScrollMode::Enabled);
        scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroller.HorizontalScrollMode(ScrollMode::Disabled);
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroller.Height(480);
        scroller.Content(contentStack);

        auto dialog = ContentDialog();
        dialog.XamlRoot(RootLayout().XamlRoot());
        dialog.Title(box_value(localization.text("dialogs.enabledDevices.title", L"Enabled devices")));
        dialog.Content(scroller);
        dialog.PrimaryButtonText(localization.text("common.save", L"Save"));
        dialog.CloseButtonText(localization.text("common.cancel", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);
        sizeDialogToViewport(dialog, RootLayout(), 0.75);
        enableDialogBackgroundDefocus(dialog);

        auto operation = dialog.ShowAsync();
        operation.Completed([this, backendEnabled, deviceEnabled, originalBackendEnabled, originalDeviceEnabled](auto const& async, winrt::Windows::Foundation::AsyncStatus const status)
        {
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
                return;

            if (async.GetResults() != ContentDialogResult::Primary)
                return;

            DispatcherQueue().TryEnqueue([this, backendEnabled, deviceEnabled, originalBackendEnabled, originalDeviceEnabled]()
            {
                int changedCount = 0;
                for (int i = 0; i < (int) backendEnabled->size() && i < (int) originalBackendEnabled.size(); ++i)
                {
                    if ((*backendEnabled)[(size_t) i] == originalBackendEnabled[(size_t) i])
                        continue;

                    if (sendCommand("set-enabled-audio-backend:" + std::to_string(i) + ":" + ((*backendEnabled)[(size_t) i] ? "1" : "0")))
                        ++changedCount;
                }

                for (int i = 0; i < (int) deviceEnabled->size() && i < (int) originalDeviceEnabled.size(); ++i)
                {
                    if ((*deviceEnabled)[(size_t) i] == originalDeviceEnabled[(size_t) i])
                        continue;

                    if (sendCommand("set-enabled-audio-device:" + std::to_string(i) + ":" + ((*deviceEnabled)[(size_t) i] ? "1" : "0")))
                        ++changedCount;
                }

                refreshSnapshot();
                showNotification(changedCount == 0
                    ? localization.text("dialogs.enabledDevices.unchanged", L"Enabled devices unchanged.").c_str()
                    : localization.format(
                        "dialogs.enabledDevices.updated",
                        L"{0} enabled device setting(s) updated.",
                        { std::to_wstring(changedCount) }).c_str());
            });
        });
    }

    void MainWindow::IconModeBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (syncingConfigControls || !IconModeBox() || IconModeBox().SelectedIndex() < 0)
            return;

        std::string mode = "color";
        if (IconModeBox().SelectedIndex() == 1)
            mode = "white";
        else if (IconModeBox().SelectedIndex() == 2)
            mode = "black";

        currentIconMode = mode;
        applyIconMode(mode);
        if (sendCommand("set-tray-icon-mode:" + mode))
            showNotification(L"App icon updated.");
    }

    void MainWindow::resetDefaultPluginScanPaths()
    {
        pluginScanPaths = defaultPluginScanPaths();
    }

    void MainWindow::Window_Closed(IInspectable const&, WindowEventArgs const&)
    {
        if (closeQuitsHost)
            sendCommand("quit-host");

        try
        {
            if (refreshTimer)
                refreshTimer.Stop();

            closeFluentDropdowns();
            SystemBackdrop(nullptr);
            Content(nullptr);
        }
        catch (...) {}

        Microsoft::UI::Xaml::Application::Current().Exit();
    }
}
