#pragma once
#include "HostJson.h"
#include <algorithm>
#include <unordered_map>

namespace lightHostModern::ui
{
using namespace lightHostModern::ipc;
inline std::string rowString(JsonObject const& object, const wchar_t* key, const char* fallback = "")
{
    const auto value = object.HasKey(key) ? object.GetNamedValue(key) : JsonValue::CreateNullValue();
    return value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : fallback;
}
inline bool rowBoolean(JsonObject const& object, const wchar_t* key)
{
    const auto value = object.HasKey(key) ? object.GetNamedValue(key) : JsonValue::CreateNullValue();
    return value.ValueType() == JsonValueType::Boolean && value.GetBoolean();
}
inline std::string foldPluginText(const std::string& value)
{
    auto wide = winrt::to_hstring(value);
    std::wstring folded(wide.size(), L'\0');
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.c_str(), static_cast<int>(wide.size()),
        folded.data(), static_cast<int>(folded.size()), nullptr, nullptr, 0);
    return length > 0 ? winrt::to_string(folded) : value;
}
    struct PluginRowData
    {
        std::string instanceId;
        std::string knownId;
        std::string name;
        std::string originalName;
        std::string customName;
        std::string cardColor;
        std::string manufacturer;
        std::string format;
        std::string path;
        std::string status;
        bool bypassed = false;
        bool isolated = false;
        int originalIndex = -1;
    };

    inline std::vector<PluginRowData> filterAndSortPluginRows(std::vector<PluginRowData> rows,
                                                       std::wstring const& query,
                                                       int sortMode,
                                                       bool running)
    {
        const auto normalizedQuery = foldPluginText(winrt::to_string(query));
        if (!normalizedQuery.empty())
        {
            std::erase_if(rows, [&normalizedQuery](PluginRowData const& row)
            {
                return foldPluginText(row.name).find(normalizedQuery) == std::string::npos;
            });
        }

        auto compareText = [](std::string const& left, std::string const& right)
        {
            return foldPluginText(left) < foldPluginText(right);
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

    inline std::string pluginRowKey(PluginRowData const& row)
    {
        return row.instanceId + "|" + row.knownId + "|" + row.name + "|" + row.originalName + "|" + row.customName + "|" + row.cardColor + "|" + row.manufacturer + "|" + row.format + "|" + row.path + "|" + row.status + "|" + (row.bypassed ? "1" : "0");
    }

    inline std::string pluginIdentityKey(PluginRowData const& row)
    {
        return row.knownId;
    }

    inline void applyInstalledPluginRuntimeStatus(std::vector<PluginRowData>& installedRows,
        std::vector<PluginRowData> const& activeRows)
    {
        struct RuntimeStatus
        {
            int count = 0;
            bool anyBypassed = false;
            bool anyActive = false;
            bool anyLoaded = false;
        };

        std::unordered_map<std::string, RuntimeStatus> activeByIdentity;
        for (auto const& plugin : activeRows)
        {
            auto& status = activeByIdentity[pluginIdentityKey(plugin)];
            ++status.count;
            status.anyBypassed = status.anyBypassed || plugin.bypassed;
            status.anyActive = status.anyActive || !plugin.bypassed;
            status.anyLoaded = status.anyLoaded || plugin.status == "Active" || plugin.status == "Bypassed";
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

            if (!match->second.anyLoaded)
                plugin.status = "In chain (unavailable)";
            else if (match->second.count > 1)
                plugin.status = "Running (multiple)";
            else if (match->second.anyBypassed && !match->second.anyActive)
                plugin.status = "Running (bypassed)";
            else
                plugin.status = "Running";
        }
    }

    inline std::vector<PluginRowData> extractActivePluginRows(std::string const& json)
    {
        std::vector<PluginRowData> rows;
        for (const auto& value : extractArray(json, "activePlugins"))
        {
            if (value.ValueType() != JsonValueType::Object) continue;
            const auto object = value.GetObject();
            PluginRowData row;
            row.knownId = rowString(object, L"knownId");
            row.instanceId = rowString(object, L"instanceId");
            row.name = rowString(object, L"name", "Unknown");
            row.originalName = rowString(object, L"originalName", row.name.c_str());
            row.customName = rowString(object, L"customName");
            row.cardColor = rowString(object, L"cardColor");
            row.manufacturer = rowString(object, L"manufacturer");
            row.format = rowString(object, L"format");
            row.path = rowString(object, L"path");
            row.bypassed = rowBoolean(object, L"bypassed");
            row.isolated = rowBoolean(object, L"isolated");
            row.status = row.bypassed ? "Bypassed" : "Active";
            const auto loading = rowString(object, L"loading", "loaded");
            if (loading == "missing" || loading == "failed") row.status = "Error";
            else if (loading != "loaded") row.status = "Unavailable";
            row.originalIndex = static_cast<int>(rows.size());
            rows.push_back(row);
        }

        return rows;
    }

    inline std::vector<PluginRowData> extractKnownPluginRows(std::string const& json)
    {
        std::vector<PluginRowData> rows;
        for (const auto& value : extractArray(json, "knownPluginList"))
        {
            if (value.ValueType() != JsonValueType::Object) continue;
            const auto object = value.GetObject();
            PluginRowData row;
            row.knownId = rowString(object, L"knownId");
            row.name = rowString(object, L"name", "Unknown");
            row.originalName = rowString(object, L"originalName", row.name.c_str());
            row.customName = rowString(object, L"customName");
            row.cardColor = rowString(object, L"cardColor");
            row.manufacturer = rowString(object, L"manufacturer");
            row.format = rowString(object, L"format");
            row.path = rowString(object, L"path");
            row.status = row.name.empty() || row.name == "Unknown" ? "Error" : "Available";
            row.originalIndex = static_cast<int>(rows.size());
            rows.push_back(row);
        }

        return rows;
    }

    inline std::vector<std::string> pluginRowKeys(std::vector<PluginRowData> const& rows)
    {
        std::vector<std::string> keys;
        keys.reserve(rows.size());
        for (auto const& row : rows)
            keys.push_back(pluginRowKey(row));
        return keys;
    }

}
