#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <set>
#include <thread>
#include "BoundedInput.h"
#include "UpdateContract.h"

#pragma comment(lib, "winhttp.lib")

namespace lightHostModern::backgroundRelease
{
inline constexpr size_t maximumResponseBytes = 4u * 1024 * 1024;
inline constexpr auto checkInterval = std::chrono::hours(6);
inline constexpr auto failureRetryInterval = std::chrono::minutes(5);
using Clock = std::chrono::steady_clock;

inline bool windowsNotificationsEnabled(const std::filesystem::path& settings)
{
    const auto legacy = GetPrivateProfileIntW(L"Updates", L"NotifyNewReleases", 1, settings.c_str());
    return GetPrivateProfileIntW(L"Updates", L"NotifyReleasesOnWindows", legacy, settings.c_str()) != 0;
}

inline std::optional<std::wstring> newerStableVersion(std::string_view bytes, const std::wstring& currentVersion)
{
    if (bytes.empty() || bytes.size() > maximumResponseBytes || bytes.find('\0') != std::string_view::npos
        || !juce::CharPointer_UTF8::isValidString(bytes.data(), static_cast<int>(bytes.size()))) return {};
    const auto json = parseBoundedJson(juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size())), maximumResponseBytes);
    if (!json.isObject() || !json["tag_name"].isString() || !json["html_url"].isString()
        || !json["draft"].isBool() || !json["prerelease"].isBool()
        || static_cast<bool>(json["draft"]) || static_cast<bool>(json["prerelease"])) return {};
    const std::wstring tag(json["tag_name"].toString().toWideCharPointer());
    const auto version = update::parseVersion(tag), current = update::parseVersion(currentVersion);
    if (!version || !current || *version <= *current
        || json["html_url"].toString() != juce::String((L"https://github.com/heide-oficial/Light-Host-Modern/releases/tag/" + tag).c_str())) return {};
    return tag;
}

// Only the caller's monotonic clock affects scheduling. A preference change is
// observed on the next short host timer tick; it does not wait for six hours.
class Schedule
{
public:
    bool shouldCheck(bool enabled, bool testProfile, bool hasFixture, Clock::time_point now)
    {
        if (!enabled || (testProfile && !hasFixture)) { active = false; return false; }
        if (active && now < next) return false;
        active = true;
        next = now + checkInterval;
        return true;
    }
    void failed(Clock::time_point now) { if (active) next = now + failureRetryInterval; }
private:
    bool active = false;
    Clock::time_point next{};
};

// Shared by the UI's IPC notification and the host's background result. This is
// deliberately process-local: a new host session may notify the same version.
class NotificationHistory
{
public:
    bool shouldNotify(const std::wstring& version) const
    {
        const auto parsed = update::parseVersion(version);
        return parsed && delivered.find(*parsed) == delivered.end();
    }
    void didNotify(const std::wstring& version)
    {
        if (const auto parsed = update::parseVersion(version)) delivered.insert(*parsed);
    }
private:
    std::set<std::array<unsigned, 3>> delivered;
};

namespace detail
{
struct Event
{
    HANDLE value = nullptr;
    explicit Event(bool manual) : value(CreateEventW(nullptr, manual ? TRUE : FALSE, FALSE, nullptr)) {}
    ~Event() { if (value) CloseHandle(value); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};
struct Internet
{
    HINTERNET value = nullptr;
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
    Internet() = default;
};

inline bool cancelled(HANDLE event) { return WaitForSingleObject(event, 0) == WAIT_OBJECT_0; }
inline void require(bool condition) { if (!condition) throw std::runtime_error("background_release_fetch_failed"); }

// Async WinHTTP is used even though the sequencing lives in one owned worker:
// cancelling that worker closes its async request rather than waiting for a
// synchronous DNS/connect/read call. The callback touches only this context.
class Request
{
public:
    HINTERNET handle = nullptr;
    Event completed{false}, closed{true};
    std::atomic<DWORD> status{0}, bytesRead{0};
    std::array<char, 16384> buffer{};

    ~Request()
    {
        if (!handle) return;
        WinHttpCloseHandle(handle);
        // HANDLE_CLOSING is the final callback. Keep context, events and read
        // buffer alive until it arrives, including on cancellation/error.
        if (callbackArmed) WaitForSingleObject(closed.value, INFINITE);
    }
    void arm()
    {
        require(handle && completed.value && closed.value);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        require(WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) != FALSE);
        require(WinHttpSetStatusCallback(handle, callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK);
        callbackArmed = true;
    }
    void wait(DWORD expected, HANDLE cancel, Clock::time_point deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        require(remaining > 0);
        const HANDLE events[] { cancel, completed.value };
        require(WaitForMultipleObjects(2, events, FALSE, static_cast<DWORD>(remaining)) == WAIT_OBJECT_0 + 1);
        require(status.load(std::memory_order_acquire) == expected);
    }
private:
    bool callbackArmed = false;
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD event, LPVOID, DWORD length) noexcept
    {
        if (!context) return;
        auto& self = *reinterpret_cast<Request*>(context);
        if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(self.closed.value); return; }
        if (event != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
            && event != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
            && event != WINHTTP_CALLBACK_STATUS_READ_COMPLETE
            && event != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
        self.bytesRead.store(length, std::memory_order_relaxed);
        self.status.store(event, std::memory_order_release);
        SetEvent(self.completed.value);
    }
};

inline std::string fetchLatest(HANDLE cancel)
{
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    const auto ready = [&] { require(!cancelled(cancel) && Clock::now() < deadline); };
    ready();
    Internet session, connection;
    session.value = WinHttpOpen(L"LightHostModern/2.0.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    require(session.value != nullptr);
    require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000) != FALSE);
    connection.value = WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    require(connection.value != nullptr);
    Request request;
    request.handle = WinHttpOpenRequest(connection.value, L"GET", L"/repos/heide-oficial/Light-Host-Modern/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    require(request.handle != nullptr);
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    require(WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) != FALSE);
    request.arm();
    ready();
    const auto started = [](BOOL success) { require(success || GetLastError() == ERROR_IO_PENDING); };
    started(WinHttpSendRequest(request.handle, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
        DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)));
    request.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancel, deadline);
    ready();
    started(WinHttpReceiveResponse(request.handle, nullptr));
    request.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancel, deadline);
    DWORD status = 0, size = sizeof(status);
    require(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE && status == 200);
    DWORD length = 0;
    size = sizeof(length);
    if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) require(length <= maximumResponseBytes);
    std::string response;
    for (;;)
    {
        ready();
        started(WinHttpReadData(request.handle, request.buffer.data(), static_cast<DWORD>(request.buffer.size()), nullptr));
        request.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancel, deadline);
        const auto count = request.bytesRead.load(std::memory_order_relaxed);
        if (!count) return response;
        require(count <= request.buffer.size() && count <= maximumResponseBytes - response.size());
        response.append(request.buffer.data(), count);
    }
}
}

class Checker
{
public:
    struct Config
    {
        std::wstring currentVersion;
        bool testProfile = false;
        std::filesystem::path fixturePath;
    };
    using Fetch = std::function<std::string(HANDLE cancel)>;
    explicit Checker(Config value, Fetch transport = {}) : config(std::move(value)), fetch(std::move(transport)) {}
    ~Checker() { stop(); }
    Checker(const Checker&) = delete;
    Checker& operator=(const Checker&) = delete;

    bool running() const { return worker.joinable() && !done.load(std::memory_order_acquire); }
    std::optional<std::wstring> poll(bool enabled, Clock::time_point now = Clock::now())
    {
        if (stopped) return {};
        if (!enabled)
        {
            schedule.shouldCheck(false, config.testProfile, false, now);
            discardResult = true;
            if (cancel.value) SetEvent(cancel.value);
        }
        std::optional<std::wstring> result;
        if (worker.joinable())
        {
            if (!done.load(std::memory_order_acquire)) return {};
            worker.join();
            if (enabled && !discardResult)
            {
                result = std::move(latest);
                if (failed) schedule.failed(now);
            }
            latest.reset();
        }
        std::error_code error;
        const bool hasFixture = static_cast<bool>(fetch) || (!config.fixturePath.empty()
            && std::filesystem::is_regular_file(config.fixturePath, error));
        if (!schedule.shouldCheck(enabled, config.testProfile, hasFixture, now)) return result;
        if (!cancel.value) { schedule.failed(now); return result; }
        ResetEvent(cancel.value);
        discardResult = false;
        failed = false;
        done.store(false, std::memory_order_release);
        try
        {
            worker = std::thread([this]
            {
                try
                {
                    std::string bytes;
                    if (fetch) bytes = fetch(cancel.value);
                    else if (config.testProfile)
                    {
                        juce::String text;
                        detail::require(readBoundedText(juce::File(config.fixturePath.wstring().c_str()), text, maximumResponseBytes));
                        bytes = text.toStdString();
                    }
                    else bytes = detail::fetchLatest(cancel.value);
                    if (!detail::cancelled(cancel.value)) latest = newerStableVersion(bytes, config.currentVersion);
                }
                catch (...) { failed = true; }
                done.store(true, std::memory_order_release);
            });
        }
        catch (...) { done.store(true, std::memory_order_release); schedule.failed(now); }
        return result;
    }
    void stop()
    {
        stopped = true;
        if (cancel.value) SetEvent(cancel.value);
        if (worker.joinable()) worker.join();
        latest.reset();
    }
private:
    Config config;
    Fetch fetch;
    Schedule schedule;
    detail::Event cancel{true};
    std::thread worker;
    std::atomic<bool> done{true};
    std::optional<std::wstring> latest;
    bool failed = false, discardResult = false, stopped = false;
};
}
