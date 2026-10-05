#pragma once
#include "BackgroundRelease.h"
#include "ScenarioRunner.h"
#include <cstring>
#include <memory>

namespace backgroundReleaseTests
{
namespace release = lightHostModern::backgroundRelease;
inline std::string metadata(const std::string& tag = "v2.0.1")
{
    return "{\"tag_name\":\"" + tag + "\",\"html_url\":\"https://github.com/heide-oficial/Light-Host-Modern/releases/tag/"
        + tag + "\",\"draft\":false,\"prerelease\":false}";
}
template<class Condition> void waitFor(Condition&& condition)
{
    const auto deadline = release::Clock::now() + std::chrono::seconds(2);
    while (!condition())
    {
        scenarios::require(release::Clock::now() < deadline, "Injected background worker did not finish within its test budget");
        Sleep(1);
    }
}
inline std::optional<std::wstring> finish(release::Checker& checker, release::Clock::time_point now)
{
    waitFor([&] { return !checker.running(); });
    return checker.poll(true, now);
}
inline void run(scenarios::Runner& runner)
{
    using namespace std::chrono;
    const auto start = release::Clock::time_point{} + hours(24);
    runner.run("Background release accepts only newer stable metadata from the exact repository", [] {
        const auto valid = release::newerStableVersion(metadata(), L"2.0.0");
        scenarios::require(valid && *valid == L"v2.0.1", "New stable release was not recognized");
        for (const auto* tag : {"v2.0.0", "v1.4.1", "v2.0.1-beta", "v2.00.1", "v2.0"})
            scenarios::require(!release::newerStableVersion(metadata(tag), L"2.0.0"), "Non-newer or invalid release was accepted");
        for (const auto* field : {"draft", "prerelease"})
        {
            auto body = metadata();
            const std::string key = std::string("\"") + field + "\":false";
            body.replace(body.find(key), key.size(), std::string("\"") + field + "\":true");
            scenarios::require(!release::newerStableVersion(body, L"2.0.0"), "Draft or prerelease produced a notice");
        }
        auto wrongRepository = metadata();
        wrongRepository.replace(wrongRepository.find("heide-oficial"), std::strlen("heide-oficial"), "unrelated-owner");
        scenarios::require(!release::newerStableVersion(wrongRepository, L"2.0.0"), "Foreign release URL accepted");
        for (const auto& body : {std::string{}, std::string("{}"), std::string("{"),
            std::string("{\"tag_name\":\"v2.0.1\",\"draft\":\"false\",\"prerelease\":false}"),
            std::string(release::maximumResponseBytes + 1, ' '), metadata() + std::string(1, '\0')})
            scenarios::require(!release::newerStableVersion(body, L"2.0.0"), "Malformed or oversized metadata accepted");
    });
    runner.run("Background schedule starts without GUI, repeats at six hours and retries failures after five minutes", [=] {
        release::Schedule schedule;
        scenarios::require(schedule.shouldCheck(true, false, false, start), "Startup check required a GUI");
        scenarios::require(!schedule.shouldCheck(true, false, false, start + hours(6) - milliseconds(1)), "Periodic check ran before six hours");
        scenarios::require(schedule.shouldCheck(true, false, false, start + hours(6)), "Six-hour check was omitted");
        schedule.failed(start + hours(6));
        scenarios::require(!schedule.shouldCheck(true, false, false, start + hours(6) + minutes(5) - milliseconds(1)), "HTTP failure caused an immediate retry");
        scenarios::require(schedule.shouldCheck(true, false, false, start + hours(6) + minutes(5)), "HTTP failure was not retried after five minutes");
    });
    runner.run("Background scheduling obeys live preferences and test profiles cannot use the network", [=] {
        release::Schedule schedule;
        scenarios::require(!schedule.shouldCheck(false, false, false, start), "Disabled Windows notices started a query");
        scenarios::require(schedule.shouldCheck(true, false, false, start + seconds(1)), "Live enable did not trigger a query");
        scenarios::require(!schedule.shouldCheck(false, false, false, start + seconds(2)), "Live disable was ignored");
        scenarios::require(schedule.shouldCheck(true, false, false, start + seconds(3)), "Re-enable incorrectly waited six hours");
        release::Schedule isolated;
        scenarios::require(!isolated.shouldCheck(true, true, false, start), "Test profile without fixture permitted network access");
        scenarios::require(!isolated.shouldCheck(true, true, false, start + hours(7)), "Periodic check bypassed test isolation");
        scenarios::require(isolated.shouldCheck(true, true, true, start + hours(7)), "Explicit fixture was refused");
        release::Checker offline({L"2.0.0", true, {}});
        scenarios::require(!offline.poll(true, start) && !offline.running(), "Offline test profile created a network worker");
        scenarios::require(!offline.poll(true, start + hours(7)) && !offline.running(), "Offline profile later created a network worker");
    });
    runner.run("Host and UI notification paths deduplicate normalized releases only within the current run", [] {
        for (const auto* first : {L"v2.0.1", L"2.0.1"})
        {
            release::NotificationHistory history;
            scenarios::require(history.shouldNotify(first), "First delivery was suppressed");
            // The common host delivery endpoint records both host-initiated and UI IPC notices.
            history.didNotify(first);
            for (const auto* equivalent : {L"v2.0.1", L"V2.0.1", L"2.0.1"})
                scenarios::require(!history.shouldNotify(equivalent), "Host/UI paths duplicated an equivalent release");
            scenarios::require(history.shouldNotify(L"2.0.2"), "A different release was suppressed");
            scenarios::require(!history.shouldNotify(L"invalid"), "Invalid version entered notification history");
            release::NotificationHistory restarted;
            scenarios::require(restarted.shouldNotify(first), "A new host run cannot announce the same release again");
        }
    });
    runner.run("Checker discovers a release without GUI and does not fetch again before the six-hour boundary", [=] {
        std::atomic<unsigned> calls{0};
        release::Checker checker({L"2.0.0", true, {}}, [&](HANDLE) { ++calls; return metadata(); });
        checker.poll(true, start);
        scenarios::require(finish(checker, start) == std::optional<std::wstring>(L"v2.0.1"), "Headless checker lost the release");
        scenarios::require(!checker.poll(true, start + hours(6) - milliseconds(1)) && calls == 1, "Checker repeated a completed result or fetched early");
        checker.poll(true, start + hours(6));
        scenarios::require(finish(checker, start + hours(6)).has_value() && calls == 2, "Checker did not repeat at six hours");
    });
    runner.run("No newer release is silent and an HTTP failure is retried without crashing the host", [=] {
        std::atomic<unsigned> calls{0};
        release::Checker checker({L"2.0.0", true, {}}, [&](HANDLE) {
            if (++calls == 1) throw std::runtime_error("fixture_http_503");
            return metadata("v2.0.0");
        });
        checker.poll(true, start);
        scenarios::require(!finish(checker, start), "HTTP failure became a notification");
        checker.poll(true, start + minutes(5) - milliseconds(1));
        scenarios::require(calls == 1 && !checker.running(), "HTTP failure caused a busy retry loop");
        checker.poll(true, start + minutes(5));
        scenarios::require(!finish(checker, start + minutes(5)) && calls == 2, "No-release response was not silent");
        checker.poll(true, start + minutes(6));
        scenarios::require(calls == 2 && !checker.running(), "No-release response was retried like a transport failure");
    });
    runner.run("Disabling an in-flight check cancels and discards its result; live enable starts a fresh check", [=] {
        std::atomic<unsigned> calls{0};
        std::atomic<bool> cancelled{false};
        release::Checker checker({L"2.0.0", true, {}}, [&](HANDLE cancel) {
            if (++calls == 1) cancelled = WaitForSingleObject(cancel, 5000) == WAIT_OBJECT_0;
            return metadata();
        });
        checker.poll(true, start);
        waitFor([&] { return calls.load() == 1; });
        scenarios::require(!checker.poll(false, start + seconds(1)), "Disabled preference delivered an in-flight result");
        waitFor([&] { return !checker.running(); });
        scenarios::require(cancelled && !checker.poll(false, start + seconds(1)), "Disabling failed to cancel and discard pending work");
        checker.poll(true, start + seconds(2));
        scenarios::require(finish(checker, start + seconds(2)).has_value() && calls == 2, "Live enable did not replace cancelled work");
    });
    runner.run("Checker destruction cancels and joins an outstanding request without a late callback", [=] {
        std::atomic<bool> entered{false}, exited{false}, cancelled{false};
        auto checker = std::make_unique<release::Checker>(release::Checker::Config{L"2.0.0", true, {}}, [&](HANDLE cancel) {
            entered = true;
            cancelled = WaitForSingleObject(cancel, 5000) == WAIT_OBJECT_0;
            exited = true;
            return metadata();
        });
        checker->poll(true, start);
        waitFor([&] { return entered.load(); });
        const auto began = release::Clock::now();
        checker.reset();
        scenarios::require(cancelled && exited, "Destructor returned before its worker had cancelled and exited");
        scenarios::require(release::Clock::now() - began < seconds(2), "Shutdown waited on the outstanding transport");
    });
}
}
