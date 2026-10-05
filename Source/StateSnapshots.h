#include "BoundedInput.h"
#pragma once
#include <juce_core/juce_core.h>
#include <chrono>
#include <deque>
#include <functional>

namespace lightHostModern::ipc
{
// Accessed only by the serialized controller. Every entry is an owned immutable
// JSON value; callers only receive newly constructed manifests/pages.
class StateSnapshots
{
public:
    using Clock = std::chrono::steady_clock;
    explicit StateSnapshots(std::function<Clock::time_point()> now = [] { return std::chrono::steady_clock::now(); },
        size_t maximumBytes = maximumSnapshotJsonBytes, size_t maximumEntries = 8)
        : clock(std::move(now)), byteLimit(maximumBytes), entryLimit(maximumEntries) {}

    juce::var capture(const juce::String& json, const juce::String& session, uint64_t sequence, const juce::var& revisions)
    {
        prune();
        if (json.getNumBytesAsUTF8() > byteLimit || entryLimit == 0) return error("snapshot_capacity", "The logical snapshot exceeds the retention limit");
        auto snapshot = lightHostModern::parseBoundedJson(json, std::min(byteLimit, maximumSnapshotJsonBytes));
        if (!snapshot.isObject()) return error("internal_error", "Invalid logical snapshot");
        const auto id = juce::Uuid().toString();
        snapshot.getDynamicObject()->setProperty("snapshotId", id);
        snapshot.getDynamicObject()->setProperty("hostSession", session);
        snapshot.getDynamicObject()->setProperty("eventSequence", static_cast<juce::int64>(sequence));
        snapshot.getDynamicObject()->setProperty("revisions", revisions.clone());
        // Account for the retained tree as well as text. Many small JSON values
        // occupy considerably more memory than their wire representation.
        const auto size = retainedSize(snapshot) + 256;
        if (size > byteLimit) return error("snapshot_capacity", "The logical snapshot exceeds the retention limit");
        while (!entries.empty() && (entries.size() >= entryLimit || bytes + size > byteLimit)) pop();
        bytes += size;
        entries.push_back({id, std::move(snapshot), size, clock()});
        auto* manifest = new juce::DynamicObject();
        for (const auto& property : entries.back().value.getDynamicObject()->getProperties())
            if (property.name != juce::Identifier("activePlugins") && property.name != juce::Identifier("knownPluginList"))
                manifest->setProperty(property.name, property.value.clone());
        auto* collections = new juce::DynamicObject();
        for (const auto* name : {"activePlugins", "knownPluginList"})
        {
            const auto* list = entries.back().value[name].getArray();
            collections->setProperty(name, list ? list->size() : 0);
        }
        manifest->setProperty("collections", juce::var(collections));
        return juce::var(manifest);
    }

    juce::var page(const juce::String& id, const juce::String& collection, size_t offset, size_t limit)
    {
        prune();
        if ((collection != "activePlugins" && collection != "knownPluginList") || limit < 1 || limit > 100)
            return error("invalid_arguments", "Invalid collection or page size");
        for (const auto& entry : entries) if (entry.id == id)
        {
            const auto* list = entry.value[juce::Identifier(collection)].getArray();
            auto* result = new juce::DynamicObject();
            result->setProperty("status", "ok"); result->setProperty("snapshotId", id);
            result->setProperty("hostSession", entry.value["hostSession"]);
            result->setProperty("collection", collection); result->setProperty("offset", static_cast<juce::int64>(offset));
            result->setProperty("total", list ? list->size() : 0);
            juce::Array<juce::var> items;
            size_t pageBytes = 1024;
            if (list) for (size_t index = offset; index < static_cast<size_t>(list->size()) && static_cast<size_t>(items.size()) < limit; ++index)
            {
                const auto& item = list->getReference(static_cast<int>(index));
                const auto itemBytes = juce::JSON::toString(item, true).getNumBytesAsUTF8() + 1;
                if (pageBytes + itemBytes > 3 * 1024 * 1024)
                {
                    if (items.isEmpty()) { delete result; return error("message_too_large", "A collection item exceeds the message limit"); }
                    break;
                }
                items.add(item.clone()); pageBytes += itemBytes;
            }
            result->setProperty("items", items);
            return juce::var(result);
        }
        return error("stale_snapshot", "The snapshot expired; obtain another manifest");
    }
    size_t retainedBytes() const { return bytes; }
    static juce::var error(const juce::String& code, const juce::String& message)
    {
        auto* object = new juce::DynamicObject(); auto* detail = new juce::DynamicObject();
        object->setProperty("status", "error"); detail->setProperty("code", code); detail->setProperty("message", message);
        object->setProperty("error", juce::var(detail)); return juce::var(object);
    }
private:
    static size_t retainedSize(const juce::var& value)
    {
        size_t total = sizeof(juce::var) * 2;
        if (const auto* object = value.getDynamicObject())
        {
            total += 256;
            for (const auto& property : object->getProperties())
                total += 128 + property.name.toString().getNumBytesAsUTF8() * 4 + retainedSize(property.value);
        }
        else if (const auto* array = value.getArray())
        {
            total += 128;
            for (const auto& item : *array) total += retainedSize(item);
        }
        else if (value.isString()) total += 64 + value.toString().getNumBytesAsUTF8() * 4;
        return total;
    }
    struct Entry { juce::String id; juce::var value; size_t bytes; Clock::time_point created; };
    void pop() { bytes -= entries.front().bytes; entries.pop_front(); }
    void prune() { while (!entries.empty() && clock() - entries.front().created >= std::chrono::seconds(60)) pop(); }
    std::deque<Entry> entries;
    std::function<Clock::time_point()> clock;
    size_t bytes = 0, byteLimit, entryLimit;
};
}
