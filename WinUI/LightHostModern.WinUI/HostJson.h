#pragma once
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <string>
#include <vector>
#include "../../Source/JsonLimits.h"

namespace lightHostModern::ipc
{
using namespace winrt::Windows::Data::Json;

inline JsonObject parseObject(const std::string& text, size_t maximumBytes = maximumMessageJsonBytes)
{
    // Check even on cache hits: a document admitted as a logical snapshot must
    // not subsequently bypass the smaller limit of a wire/external-data reader.
    if (text.size() > maximumBytes) return JsonObject();
    // UI parsing occurs on one apartment. Keep only the most recent document.
    thread_local std::string cachedText;
    thread_local JsonObject cached{nullptr};
    if (!cached || cachedText != text)
    {
        if(!lightHostModern::boundedJsonStructure(text))return JsonObject();
        JsonObject object;
        if (!JsonObject::TryParse(winrt::to_hstring(text), object)) return JsonObject();
        cachedText = text;
        cached = object;
    }
    return cached;
}

inline JsonObject parseSnapshotObject(const std::string& text)
{ return parseObject(text, maximumSnapshotJsonBytes); }

inline IJsonValue field(const std::string& text, const std::string& key)
{
    // Field access is also used by presenters on fully reassembled host state.
    // Transport frames and external documents are checked separately at ingress.
    auto object = parseSnapshotObject(text);
    const auto name = winrt::to_hstring(key);
    if (object.HasKey(name)) return object.GetNamedValue(name);
    // These are the explicit sections of a host snapshot, never plugin entries.
    for (const auto section : {L"diagnostics", L"audioConfig", L"appConfig", L"error"})
    {
        if (!object.HasKey(section)) continue;
        auto value = object.GetNamedValue(section);
        if (value.ValueType() == JsonValueType::Object && value.GetObject().HasKey(name))
            return value.GetObject().GetNamedValue(name);
    }
    return JsonValue::CreateNullValue();
}

inline std::string extractString(const std::string& text, const std::string& key, const std::string& fallback = "")
{
    auto value = field(text, key);
    return value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : fallback;
}
inline double extractNumber(const std::string& text, const std::string& key, double fallback = 0.0)
{
    auto value = field(text, key);
    return value.ValueType() == JsonValueType::Number ? value.GetNumber() : fallback;
}
inline bool extractBool(const std::string& text, const std::string& key, bool fallback = false)
{
    auto value = field(text, key);
    return value.ValueType() == JsonValueType::Boolean ? value.GetBoolean() : fallback;
}
inline JsonArray extractArray(const std::string& text, const std::string& key)
{
    auto value = field(text, key);
    return value.ValueType() == JsonValueType::Array ? value.GetArray() : JsonArray();
}
inline std::vector<std::string> extractStringArray(const std::string& text, const std::string& key)
{
    std::vector<std::string> result;
    for (auto value : extractArray(text, key))
        if (value.ValueType() == JsonValueType::String) result.push_back(winrt::to_string(value.GetString()));
    return result;
}
inline std::vector<double> extractNumberArray(const std::string& text, const std::string& key)
{
    std::vector<double> result;
    for (auto value : extractArray(text, key))
        if (value.ValueType() == JsonValueType::Number) result.push_back(value.GetNumber());
    return result;
}
inline std::vector<bool> extractBoolArray(const std::string& text, const std::string& key)
{
    std::vector<bool> result;
    for (auto value : extractArray(text, key))
        if (value.ValueType() == JsonValueType::Boolean) result.push_back(value.GetBoolean());
    return result;
}
}
