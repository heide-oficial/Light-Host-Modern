#pragma once
#include "HostTransport.h"
#include <set>
#include <cmath>

namespace lightHostModern::ui
{
// Shared transport/state service. Its public state is consumed on the UI
// apartment; overlapped I/O and operation reconciliation stay on workers.
class HostConnection : public std::enable_shared_from_this<HostConnection>
{
public:
    std::shared_ptr<ipc::ClientState> commands = std::make_shared<ipc::ClientState>();
    std::shared_ptr<ipc::ClientState> events = std::make_shared<ipc::ClientState>();
    std::shared_ptr<ipc::ClientState> meters = std::make_shared<ipc::ClientState>();
    std::wstring pipeName;
    std::string snapshotJson, session;
    uint64_t sequence = 0;
    bool snapshotNeeded = true, connected = false, closed = false;

    void close() { closed = true; commands->close(); events->close(); meters->close(); }
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> meterLevelsAsync()
    {
        auto lifetime = shared_from_this();
        // One short-lived read, independent of commands, snapshot pages and
        // event long-polls. Old readings are never queued behind operations.
        co_return co_await ipc::requestAsync(meters, pipeName + L"-meters", "meter-levels", 150);
    }
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> requestAsync(std::string command, DWORD timeoutMs = 120000)
    {
        auto lifetime = shared_from_this();
        co_return co_await ipc::requestAsync(commands, pipeName, std::move(command), timeoutMs);
    }
    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> snapshotAsync(uint64_t deadline = 0)
    {
        using namespace ipc;
        auto lifetime = shared_from_this();
        snapshotNeeded = true;
        connected = false;
        try
        {
        // An expired read restarts with a new manifest. Partial collections
        // never replace the current displayed snapshot.
        for (int attempt = 0; attempt < 3 && !closed; ++attempt)
        {
            const auto manifestNow = GetTickCount64();
            if (deadline && manifestNow >= deadline) co_return L"";
            auto wire = winrt::to_string(co_await requestAsync("snapshot-manifest", deadline
                ? static_cast<DWORD>((std::min)(deadline - manifestNow, static_cast<uint64_t>(120000))) : 120000));
            auto manifest = parseObject(wire);
            if (wire.empty() || extractString(wire, "status") == "error")
            { connected = false; snapshotNeeded = true; co_return winrt::to_hstring(wire); }
            const auto snapshotId = manifest.GetNamedString(L"snapshotId", L"");
            const auto hostSession = manifest.GetNamedString(L"hostSession", L"");
            const auto cursor = manifest.GetNamedNumber(L"eventSequence", -1);
            const auto counts = manifest.GetNamedObject(L"collections", JsonObject{});
            if (snapshotId.empty() || hostSession.empty() || !std::isfinite(cursor)
                || cursor < 0 || cursor > 9007199254740991.0 || std::floor(cursor) != cursor)
                co_return winrt::to_hstring(transportError("invalid_snapshot", "Host returned an invalid snapshot manifest"));
            bool stale = false;
            size_t receivedBytes = wire.size();
            for (const auto* collection : {L"activePlugins", L"knownPluginList"})
            {
                const auto count = counts.GetNamedNumber(collection, -1);
                if (!std::isfinite(count) || count < 0 || count > 1000000 || std::floor(count) != count)
                    co_return winrt::to_hstring(transportError("invalid_snapshot", "Host returned an invalid collection count"));
                JsonArray items;
                std::set<std::wstring> ids;
                while (items.Size() < count && !closed)
                {
                    JsonObject options;
                    options.SetNamedValue(L"snapshotId", JsonValue::CreateStringValue(snapshotId));
                    options.SetNamedValue(L"collection", JsonValue::CreateStringValue(collection));
                    options.SetNamedValue(L"offset", JsonValue::CreateNumberValue(items.Size()));
                    options.SetNamedValue(L"limit", JsonValue::CreateNumberValue(100));
                    const auto now = GetTickCount64();
                    if (deadline && now >= deadline) co_return L"";
                    auto pageWire = winrt::to_string(co_await requestAsync("snapshot-page:" + winrt::to_string(options.Stringify()), deadline
                        ? static_cast<DWORD>((std::min)(deadline - now, static_cast<uint64_t>(120000))) : 120000));
                    receivedBytes += pageWire.size();
                    if (receivedBytes > maximumSnapshotJsonBytes)
                        co_return winrt::to_hstring(transportError("message_too_large", "The complete snapshot exceeds the client capacity"));
                    if (extractString(pageWire, "code") == "stale_snapshot") { stale = true; break; }
                    if (pageWire.empty() || extractString(pageWire, "status") == "error")
                    { connected = false; snapshotNeeded = true; co_return winrt::to_hstring(pageWire); }
                    const auto page = parseObject(pageWire);
                    const auto rows = page.GetNamedArray(L"items", JsonArray{});
                    if (page.GetNamedString(L"snapshotId", L"") != snapshotId || page.GetNamedString(L"hostSession", L"") != hostSession
                        || page.GetNamedString(L"collection", L"") != collection
                        || page.GetNamedNumber(L"total", -1) != count || page.GetNamedNumber(L"offset", -1) != items.Size()
                        || rows.Size() == 0 || rows.Size() > 100 || items.Size() + rows.Size() > count)
                        co_return winrt::to_hstring(transportError("invalid_snapshot", "Snapshot pages did not match their manifest"));
                    for (const auto& row : rows)
                    {
                        const auto id = row.GetObject().GetNamedString(std::wstring(collection) == L"activePlugins" ? L"instanceId" : L"knownId", L"");
                        if (id.empty() || !ids.insert(std::wstring(id)).second)
                            co_return winrt::to_hstring(transportError("invalid_snapshot", "Snapshot contains missing or repeated item identities"));
                        items.Append(row);
                    }
                }
                if (stale) break;
                manifest.SetNamedValue(collection, items);
            }
            if (stale) continue;
            if (closed) co_return L"";
            auto complete = winrt::to_string(manifest.Stringify());
            // Validate the whole document before replacing the last good state.
            if (!parseSnapshotObject(complete).HasKey(L"snapshotId"))
                co_return winrt::to_hstring(transportError("snapshot_capacity", "The complete snapshot exceeds the client capacity"));
            if (deadline && GetTickCount64() >= deadline) co_return L"";
            snapshotJson = std::move(complete);
            session = winrt::to_string(hostSession); sequence = static_cast<uint64_t>(cursor);
            snapshotNeeded = false; connected = true;
            co_return winrt::to_hstring(snapshotJson);
        }
        co_return winrt::to_hstring(ipc::transportError("stale_snapshot", "The state changed before a complete snapshot could be read"));
        }
        catch (...)
        { co_return winrt::to_hstring(transportError("invalid_snapshot", "The host snapshot contains invalid data")); }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> pollEventsAsync()
    {
        using namespace ipc;
        auto lifetime = shared_from_this();
        const auto requestedSession = session;
        JsonObject options;
        options.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(winrt::to_hstring(requestedSession)));
        options.SetNamedValue(L"afterSequence", JsonValue::CreateNumberValue(static_cast<double>(sequence)));
        options.SetNamedValue(L"waitMs", JsonValue::CreateNumberValue(4000));
        const auto wire = co_await ipc::requestAsync(events, pipeName + L"-events", "events:" + winrt::to_string(options.Stringify()), 5000);
        if (closed || requestedSession != session) co_return L"";
        const auto text = winrt::to_string(wire);
        if (text.empty() || extractString(text, "status") == "error")
        { connected = false; snapshotNeeded = true; co_return wire; }
        const auto result = parseObject(text);
        if (result.GetNamedString(L"hostSession", L"") != winrt::to_hstring(session))
        { connected = false; snapshotNeeded = true; co_return wire; }
        const auto number = result.GetNamedNumber(L"sequence", -1);
        if (!std::isfinite(number) || number < 0 || number > 9007199254740991.0 || std::floor(number) != number)
        { connected = false; snapshotNeeded = true; co_return winrt::to_hstring(transportError("invalid_event", "Invalid host event sequence")); }
        const auto cursor = static_cast<uint64_t>(number);
        if (result.GetNamedBoolean(L"resyncRequired", false)) snapshotNeeded = true;
        if (cursor > sequence)
        {
            const auto changes = result.GetNamedObject(L"changes", JsonObject{});
            if (changes.HasKey(L"chain") || changes.HasKey(L"database") || changes.HasKey(L"devices") || changes.HasKey(L"operations"))
                snapshotNeeded = true;
            sequence = cursor;
        }
        connected = true;
        co_return wire;
    }
};
}
