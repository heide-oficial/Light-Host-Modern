#include "StateEvents.h"
#include "StateSnapshots.h"
#include <future>
#include <iostream>

using namespace lightHostModern::ipc;
static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main()
{
    try
    {
        StateEvents events(3, 4096);
        StateRevisions revision{};
        require(events.read(0).sequence == 0 && !events.read(0).resyncRequired, "empty event cursor");
        revision[0] = 1;
        require(events.publish(revision, {{"chain", {"a", "b"}}}) == 1, "first event sequence");
        require(events.publish(revision) == 1, "unchanged revisions produced events");
        revision[0] = 2;
        events.publish(revision, {{"chain", {"b", "c"}}});
        const auto batch = events.read(0);
        require(batch.sequence == 2 && batch.changes.at("chain").size() == 3 && batch.revisions[0] == 2,
            "coalesced changes lost IDs or revision");
        auto pending = std::async(std::launch::async, [&] { return events.read(2, std::chrono::seconds(4)); });
        revision[4] = 1; events.publish(revision, {{"operations", {"op"}}});
        require(pending.get().sequence == 3, "long poll failed to wake on operation completion");
        for (int i = 0; i < 1000; ++i) { ++revision[0]; events.publish(revision, {{"chain", {"a"}}}); }
        require(events.retainedRecords() <= 3 && events.retainedBytes() <= 4096, "event retention grows without bounds");
        require(events.read(0).resyncRequired && events.read(UINT64_MAX).resyncRequired, "lost/future event cursor accepted");
        auto current = events.read(UINT64_MAX).sequence;
        auto closing = std::async(std::launch::async, [&] { return events.read(current, std::chrono::seconds(4)); });
        events.close();
        require(closing.get().stopped, "event shutdown did not drain a pending reader");
        StateEvents oversized;
        oversized.publish(revision, {{"chain", std::vector<std::string>(257, "instance")}});
        require(oversized.read(0).resyncRequired, "oversized change set did not request resynchronization");

        StateSnapshots::Clock::time_point now{};
        StateSnapshots snapshots([&] { return now; }, 4 * 1024 * 1024, 2);
        auto* object = new juce::DynamicObject();
        juce::var logical(object);
        object->setProperty("status", "online");
        juce::Array<juce::var> plugins;
        for (int i = 0; i < 1000; ++i)
        {
            auto* item = new juce::DynamicObject(); item->setProperty("instanceId", juce::String(i));
            item->setProperty("name", "Original"); plugins.add(juce::var(item));
        }
        object->setProperty("activePlugins", plugins); object->setProperty("knownPluginList", juce::Array<juce::var>{});
        auto manifest = snapshots.capture(juce::JSON::toString(logical), "host-a", 9, juce::var());
        const auto id = manifest["snapshotId"].toString();
        require(id.isNotEmpty() && !manifest.hasProperty("activePlugins") && int(manifest["collections"]["activePlugins"]) == 1000,
            "manifest must contain counts without inline collections");
        plugins.getReference(900).getDynamicObject()->setProperty("name", "Changed");
        object->setProperty("activePlugins", plugins);
        const auto newer = snapshots.capture(juce::JSON::toString(logical), "host-a", 10, juce::var());
        for (int offset = 0; offset < 1000; offset += 100)
        {
            const auto page = snapshots.page(id, "activePlugins", offset, 100);
            require(page["snapshotId"].toString() == id && page["hostSession"].toString() == "host-a", "page changed snapshot/session");
            const auto* items = page["items"].getArray();
            require(items && items->size() == 100, "pagination omitted items");
            for (int index = 0; index < items->size(); ++index)
                require((*items)[index]["instanceId"].toString() == juce::String(offset + index)
                    && (*items)[index]["name"].toString() == "Original", "immutable snapshot mixed revisions");
        }
        const auto changed = snapshots.page(newer["snapshotId"].toString(), "activePlugins", 900, 100);
        require(changed["items"][0]["name"].toString() == "Changed", "new snapshot did not capture changed data");
        require(snapshots.page(id, "activePlugins", 0, 101)["error"]["code"].toString() == "invalid_arguments", "unbounded page accepted");
        snapshots.capture(juce::JSON::toString(logical), "host-a", 11, juce::var());
        require(snapshots.page(id, "activePlugins", 0, 1)["error"]["code"].toString() == "stale_snapshot", "snapshot count retention exceeded");
        now += std::chrono::seconds(60);
        require(snapshots.page(newer["snapshotId"].toString(), "activePlugins", 0, 1)["error"]["code"].toString() == "stale_snapshot"
            && snapshots.retainedBytes() == 0, "snapshot expiration did not release storage");
        StateSnapshots tiny([&] { return now; }, 32);
        require(tiny.capture(juce::JSON::toString(logical), "host", 0, juce::var())["error"]["code"].toString() == "snapshot_capacity",
            "oversized logical snapshot accepted");
        // Larger than a frame, but still inside the retained-tree budget.
        StateSnapshots large;
        juce::Array<juce::var> catalog;
        for (int i = 0; i < 6000; ++i) {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("knownId", juce::String(i));
            entry->setProperty("name", juce::String::repeatedString("x", 700));
            catalog.add(juce::var(entry));
        }
        object->setProperty("activePlugins", juce::Array<juce::var>{});
        object->setProperty("knownPluginList", catalog);
        const auto largeJson = juce::JSON::toString(logical, true);
        require(largeJson.getNumBytesAsUTF8() > lightHostModern::maximumMessageJsonBytes, "large fixture must cross transport limit");
        const auto largeManifest = large.capture(largeJson, "large-host", 1, juce::var());
        require(largeManifest["snapshotId"].toString().isNotEmpty(), "large logical snapshot was rejected by the wire cap");
        int received = 0;
        while (received < catalog.size()) {
            const auto page = large.page(largeManifest["snapshotId"].toString(), "knownPluginList", received, 100);
            require(juce::JSON::toString(page, true).getNumBytesAsUTF8() < lightHostModern::maximumMessageJsonBytes, "page exceeds frame budget");
            auto* rows = page["items"].getArray();
            require(rows && !rows->isEmpty(), "large catalog lost a page");
            received += rows->size();
        }
        require(received == 6000 && large.retainedBytes() <= lightHostModern::maximumSnapshotJsonBytes, "large catalog capacity exceeded");
        StateSnapshots insufficient([] { return StateSnapshots::Clock::now(); }, 8 * 1024 * 1024);
        require(insufficient.capture(largeJson, "large-host", 1, juce::var())["error"]["code"].toString() == "snapshot_capacity",
            "retained-tree budget must remain independent of text length");
        std::cout << "State events and immutable snapshot regressions passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
