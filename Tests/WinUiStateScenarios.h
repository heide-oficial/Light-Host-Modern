#pragma once
#include "HostConnection.h"
#include <future>

static void runStateScenarios()
{
    using namespace lightHostModern::ipc;
    struct Peer
    {
        int count = 100, manifests = 0, pages = 0, nameLength = 0;
        bool expireOnce = false, duplicate = false, malformed = false;
        std::string session = "session-one";
        std::string reply(const std::string& request)
        {
            auto result = JsonObject::Parse(winrt::to_hstring(request));
            auto command = winrt::to_string(result.GetNamedString(L"command"));
            result.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(winrt::to_hstring(session)));
            result.SetNamedValue(L"status", JsonValue::CreateStringValue(L"ok"));
            if (command == "snapshot-manifest")
            {
                ++manifests;
                result.SetNamedValue(L"snapshotId", JsonValue::CreateStringValue(winrt::to_hstring(manifests)));
                result.SetNamedValue(L"eventSequence", JsonValue::CreateNumberValue(manifests));
                JsonObject counts;
                counts.SetNamedValue(L"activePlugins", JsonValue::CreateNumberValue(count));
                counts.SetNamedValue(L"knownPluginList", JsonValue::CreateNumberValue(count));
                result.SetNamedValue(L"collections", counts);
            }
            else if (command == "snapshot-page")
            {
                ++pages;
                auto options = result.GetNamedArray(L"args").GetObjectAt(0);
                int offset = static_cast<int>(options.GetNamedNumber(L"offset"));
                if (expireOnce && offset == 100)
                {
                    expireOnce = false;
                    result.SetNamedValue(L"status", JsonValue::CreateStringValue(L"error"));
                    result.SetNamedValue(L"code", JsonValue::CreateStringValue(L"stale_snapshot"));
                }
                else
                {
                    result.SetNamedValue(L"snapshotId", options.GetNamedValue(L"snapshotId"));
                    result.SetNamedValue(L"collection", options.GetNamedValue(L"collection"));
                    result.SetNamedValue(L"offset", JsonValue::CreateNumberValue(offset + (malformed ? 1 : 0)));
                    result.SetNamedValue(L"total", JsonValue::CreateNumberValue(count));
                    JsonArray rows;
                    for (int i = offset; i < (std::min)(count, offset + 100); ++i)
                    {
                        JsonObject row;
                        auto id = winrt::to_hstring(duplicate ? 1 : i);
                        row.SetNamedValue(L"instanceId", JsonValue::CreateStringValue(id));
                        row.SetNamedValue(L"knownId", JsonValue::CreateStringValue(id));
                        row.SetNamedValue(L"name", JsonValue::CreateStringValue(nameLength ? std::wstring(nameLength, L'x') : L"Réverbération 日本語"));
                        rows.Append(row);
                    }
                    result.SetNamedValue(L"items", rows);
                }
            }
            else if (command == "events")
            {
                result.SetNamedValue(L"sequence", JsonValue::CreateNumberValue(manifests + 1));
                JsonObject changes;
                changes.SetNamedValue(L"chain", JsonArray());
                result.SetNamedValue(L"changes", changes);
            }
            return winrt::to_string(result.Stringify());
        }
    } peer;
    auto connection = std::make_shared<lightHostModern::ui::HostConnection>();
    auto transport = [&](const std::wstring&, const std::string& request, DWORD) { return peer.reply(request); };
    connection->commands->transport = transport;
    connection->events->transport = transport;
    for (int count : {100, 500, 1000})
    {
        peer.count = count;
        auto snapshot = winrt::to_string(connection->snapshotAsync().get());
        require(connection->connected && !connection->snapshotNeeded, "Complete snapshot is adopted");
        require(extractArray(snapshot, "activePlugins").Size() == count
            && extractArray(snapshot, "knownPluginList").Size() == count, "All 100/500/1000 rows are fetched");
        require(extractArray(snapshot, "activePlugins").GetObjectAt(count - 1).GetNamedString(L"instanceId") == winrt::to_hstring(count - 1), "Page order is preserved");
    }
    peer.count = 3000; peer.nameLength = 768;
    const auto large = winrt::to_string(connection->snapshotAsync().get());
    require(large.size() > lightHostModern::maximumMessageJsonBytes && connection->connected,
        "Paginated state larger than one transport frame must be adopted");
    require(parseSnapshotObject(large).HasKey(L"snapshotId") && extractArray(large, "knownPluginList").Size() == 3000,
        "Presenters must read the complete large catalog");
    require(!parseObject(large).HasKey(L"snapshotId"), "Snapshot cache cannot relax the wire parser limit");
    peer.count = 1000; peer.nameLength = 0;
    auto before = peer.manifests;
    peer.expireOnce = true;
    connection->snapshotAsync().get();
    require(peer.manifests == before + 2 && connection->connected, "Expired page restarts with a fresh manifest");
    const auto valid = connection->snapshotJson;
    peer.duplicate = true;
    auto error = winrt::to_string(connection->snapshotAsync().get());
    require(extractString(error, "code") == "invalid_snapshot" && connection->snapshotJson == valid
        && !connection->connected && connection->snapshotNeeded, "Repeated IDs cannot replace the previous valid snapshot");
    peer.duplicate = false; peer.malformed = true;
    require(extractString(winrt::to_string(connection->snapshotAsync().get()), "code") == "invalid_snapshot"
        && connection->snapshotJson == valid, "Mismatched page offset does not adopt partial state");
    peer.malformed = false;
    connection->snapshotAsync().get();
    connection->pollEventsAsync().get();
    require(connection->snapshotNeeded, "Structural events require a consistent snapshot");
    peer.session = "session-two";
    connection->pollEventsAsync().get();
    require(!connection->connected && connection->snapshotNeeded, "Host restart invalidates the old event session");
    connection->snapshotAsync().get();
    require(connection->session == peer.session && connection->connected, "Reconnect adopts the restarted host snapshot");
    connection->close();

    // Slow control traffic must not delay the next visible meter frame.
    auto live = std::make_shared<lightHostModern::ui::HostConnection>();
    live->pipeName = L"isolated";
    std::promise<void> commandEntered, releaseCommand;
    auto commandRelease = releaseCommand.get_future().share();
    live->commands->transport = [&](const auto&, const auto&, DWORD) {
        commandEntered.set_value(); commandRelease.wait(); return std::string{};
    };
    live->meters->transport = [](const std::wstring& pipe, const std::string& request, DWORD timeout) {
        require(pipe == L"isolated-meters" && timeout <= 150, "Meters require their independent, short-deadline endpoint");
        auto response = parseObject(request);
        require(response.GetNamedString(L"command") == L"meter-levels", "Meters must not request full diagnostics");
        response.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(L"meter-test"));
        response.SetNamedValue(L"status", JsonValue::CreateStringValue(L"ok"));
        response.SetNamedValue(L"inputPeak", JsonValue::CreateNumberValue(.75));
        return winrt::to_string(response.Stringify());
    };
    auto blockedCommand = live->requestAsync("telemetry");
    commandEntered.get_future().wait();
    auto meterRead = std::async(std::launch::async, [live] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        return winrt::to_string(live->meterLevelsAsync().get());
    });
    const bool independent = meterRead.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    releaseCommand.set_value(); blockedCommand.get();
    require(independent && extractNumber(meterRead.get(), "inputPeak") == .75, "Meter frame waited behind a blocked command");
    live->close();
    require(WaitForSingleObject(live->meters->stop.get(), 0) == WAIT_OBJECT_0, "Window shutdown did not stop meter reads");

    auto bounded = std::make_shared<ClientState>();
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    bounded->transport = [&](const std::wstring&, const std::string&, DWORD) { entered.set_value(); released.wait(); return std::string{}; };
    auto first = requestAsync(bounded, L"fake", "snapshot");
    entered.get_future().wait();
    std::vector<winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>> queued;
    for (size_t i = 1; i < ClientState::maximumQueuedRequests; ++i)
        queued.push_back(requestAsync(bounded, L"fake", "snapshot"));
    require(extractString(winrt::to_string(requestAsync(bounded, L"fake", "snapshot").get()), "code") == "transport_capacity", "Command queue is bounded while a request is blocked");
    bounded->close(); release.set_value();
    first.get();
    for (auto& item : queued) item.get();
    require(bounded->queuedRequests == 0 && bounded->queuedBytes == 0, "Shutdown releases every queued request reservation");
}
