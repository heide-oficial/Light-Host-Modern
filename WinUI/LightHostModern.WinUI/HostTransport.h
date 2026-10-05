#pragma once

#include "../../Source/IpcPipe.h"
#include "../../Source/IpcSchema.h"
#include "HostJson.h"
#include <condition_variable>
#include <memory>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <deque>
#include <functional>
#include <set>
#include <winrt/Windows.Foundation.h>

namespace lightHostModern::ipc
{
// One FIFO for commands and snapshots. The coroutine copies all inputs before
// suspension and owns transport state until cancellation has been drained.
class ClientState
{
public:
    struct Pending { std::string id, session; };
    inline static std::atomic<uint64_t> clients{0};
    const std::string clientId = std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(GetTickCount64()) + "-" + std::to_string(++clients);
    std::string hostSession;
    std::deque<Pending> pending;
    std::deque<std::string> lateResults;
    static constexpr size_t maximumQueuedRequests = 64;
    static constexpr size_t maximumQueuedBytes = 8 * 1024 * 1024;
    static constexpr size_t maximumLateBytes = 32 * 1024 * 1024;
    size_t queuedRequests = 0, queuedBytes = 0, lateBytes = 0;
    // Tests substitute a deterministic peer without changing queue semantics.
    std::function<std::string(const std::wstring&, const std::string&, DWORD)> transport;
    std::deque<std::string> takeLateResults()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<std::string> result;
        result.swap(lateResults);
        lateBytes = 0;
        return result;
    }
    void close()
    {
        stop.signal();
        changed.notify_all();
    }
    StopEvent stop;
    std::mutex mutex;
    std::condition_variable changed;
    uint64_t next = 0;
    uint64_t serving = 0;
    std::set<uint64_t> expiredTickets;
    void advance() // mutex is held by the caller
    {
        ++serving;
        while (expiredTickets.erase(serving)) ++serving;
        changed.notify_all();
    }
};

inline std::string encodeRequest(const std::string& request, const winrt::hstring& id)
{
    // UI action tags are local strings; the wire contains only typed JSON.
    const auto separator = request.find(':');
    const auto command = request.substr(0, separator);
    const auto payload = separator == std::string::npos ? std::string() : request.substr(separator + 1);
    const auto schema = argumentsFor(command);
    JsonArray args;
    const auto integer = [](const std::string& text) {
        size_t used = 0;
        const auto value = std::stoi(text, &used);
        if (used != text.size()) throw std::invalid_argument("Invalid integer argument");
        return JsonValue::CreateNumberValue(value);
    };
    const auto boolean = [](const std::string& text) {
        if (text != "0" && text != "1") throw std::invalid_argument("Invalid boolean argument");
        return JsonValue::CreateBooleanValue(text == "1");
    };
    switch (schema)
    {
        case Arguments::none: break;
        case Arguments::integer: args.Append(integer(payload)); break;
        case Arguments::number: {
            size_t used = 0;
            const auto number = std::stod(payload, &used);
            if (used != payload.size()) throw std::invalid_argument("Invalid numeric argument");
            args.Append(JsonValue::CreateNumberValue(number)); break;
        }
        case Arguments::boolean: args.Append(boolean(payload)); break;
        case Arguments::text: args.Append(JsonValue::CreateStringValue(winrt::to_hstring(payload))); break;
        case Arguments::object: args.Append(JsonObject::Parse(winrt::to_hstring(payload))); break;
        case Arguments::twoTexts: {
            const auto split = payload.find(':');
            if (split == std::string::npos) throw std::invalid_argument("Missing second instance ID");
            args.Append(JsonValue::CreateStringValue(winrt::to_hstring(payload.substr(0, split))));
            args.Append(JsonValue::CreateStringValue(winrt::to_hstring(payload.substr(split + 1))));
            break;
        }
        case Arguments::twoIntegers:
        case Arguments::integerBoolean: {
            const auto split = payload.find(':');
            if (split == std::string::npos) throw std::invalid_argument("Missing second argument");
            args.Append(integer(payload.substr(0, split)));
            args.Append(schema == Arguments::twoIntegers ? integer(payload.substr(split + 1)) : boolean(payload.substr(split + 1)));
            break;
        }
        default: throw std::invalid_argument("Unknown host command");
    }
    JsonObject object;
    object.SetNamedValue(L"version", JsonValue::CreateNumberValue(protocolVersion));
    object.SetNamedValue(L"id", JsonValue::CreateStringValue(id));
    object.SetNamedValue(L"command", JsonValue::CreateStringValue(winrt::to_hstring(command)));
    object.SetNamedValue(L"args", args);
    return winrt::to_string(object.Stringify());
}

inline std::string exchangeRequest(ClientState& state, const std::wstring& pipeName,
                                   const std::string& request, DWORD timeoutMs)
{
    if (state.transport) return state.transport(pipeName, request, timeoutMs);
    const auto started = GetTickCount64();
    const auto deadline = started + timeoutMs;
    const auto openingDeadline = started + (timeoutMs < 5000 ? timeoutMs : 5000);
    HANDLE raw = INVALID_HANDLE_VALUE;
    while (WaitForSingleObject(state.stop.get(), 0) != WAIT_OBJECT_0)
    {
        if (GetTickCount64() >= openingDeadline) return {};
        raw = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (raw != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return {};
        const auto now = GetTickCount64();
        if (now >= openingDeadline) return {};
        WaitForSingleObject(state.stop.get(), static_cast<DWORD>((std::min)(openingDeadline - now, 20ull)));
    }
    Handle pipe(raw);
    if (!pipe) return {};
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) return {};
    const auto now = GetTickCount64();
    if (now >= deadline) return {};
    PipeIo io(pipe.get(), state.stop.get(), static_cast<DWORD>(deadline - now));
    if (io.write(request) != ERROR_SUCCESS) return {};
    std::string response;
    if (io.read(response) != ERROR_SUCCESS) return {};
    io.write("received");
    return response;
}

inline std::string transportError(const char* code, const char* message)
{
    JsonObject result;
    result.SetNamedValue(L"status", JsonValue::CreateStringValue(L"error"));
    result.SetNamedValue(L"code", JsonValue::CreateStringValue(winrt::to_hstring(code)));
    result.SetNamedValue(L"message", JsonValue::CreateStringValue(winrt::to_hstring(message)));
    return winrt::to_string(result.Stringify());
}

inline winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> requestAsync(
    std::shared_ptr<ClientState> state, std::wstring pipeName, std::string request, DWORD timeoutMs = 120000)
{
    // The budget starts before admission, not after the FIFO becomes available.
    const auto deadline = GetTickCount64() + timeoutMs;
    uint64_t ticket;
    const auto requestBytes = request.size() + pipeName.size() * sizeof(wchar_t);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (WaitForSingleObject(state->stop.get(), 0) == WAIT_OBJECT_0) co_return L"";
        if (state->queuedRequests >= ClientState::maximumQueuedRequests
            || state->next - state->serving >= ClientState::maximumQueuedRequests
            || requestBytes > ClientState::maximumQueuedBytes - state->queuedBytes)
            co_return winrt::to_hstring(transportError("transport_capacity", "The host request queue is full"));
        ++state->queuedRequests;
        state->queuedBytes += requestBytes;
        ticket = state->next++;
    }
    struct Reservation
    {
        std::shared_ptr<ClientState> state;
        size_t bytes;
        ~Reservation()
        { std::lock_guard<std::mutex> guard(state->mutex); --state->queuedRequests; state->queuedBytes -= bytes; }
    } reservation{state, requestBytes};
    co_await winrt::resume_background();
    std::unique_lock<std::mutex> lock(state->mutex);
    while (state->serving != ticket && WaitForSingleObject(state->stop.get(), 0) != WAIT_OBJECT_0)
    {
        const auto now = GetTickCount64();
        if (now >= deadline)
        {
            state->expiredTickets.insert(ticket);
            co_return winrt::to_hstring(transportError("queue_timeout", "The request expired before it was sent"));
        }
        state->changed.wait_for(lock, std::chrono::milliseconds(deadline - now));
    }
    if (WaitForSingleObject(state->stop.get(), 0) == WAIT_OBJECT_0) co_return L"";
    lock.unlock();
    struct Finish
    {
        std::shared_ptr<ClientState> state;
        ~Finish()
        {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->advance();
        }
    } finish{state};

    const auto idText = state->clientId + "-" + std::to_string(ticket);
    const auto id = winrt::to_hstring(idText);
    const auto command = request.substr(0, request.find(':'));
    const bool mutation = !isReadOnly(command);
    if (GetTickCount64() >= deadline)
        co_return winrt::to_hstring(transportError("queue_timeout", "The request expired before it was sent"));
    try
    {
        auto exchange = [&](const std::string& action, const std::string& actionId) {
            auto wire = parseObject(encodeRequest(action, winrt::to_hstring(actionId)));
            wire.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(winrt::to_hstring(state->hostSession)));
            const auto now = GetTickCount64();
            if (now >= deadline) return std::string{};
            auto response = exchangeRequest(*state, pipeName, winrt::to_string(wire.Stringify()),
                static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(5000))));
            if (!response.empty() && (extractNumber(response, "version", -1) != protocolVersion
                || extractString(response, "id") != actionId))
                return transportError("incompatible_version", "Host and UI protocol versions do not match. Restart both with the same version.");
            return response;
        };
        auto outcome = [](const std::string& response) -> std::string {
            if (extractString(response, "status") != "operation") return response;
            const auto phase = extractString(response, "operationState");
            if (phase == "queued" || phase == "running") return {};
            auto value = field(response, "result");
            if (value.ValueType() == JsonValueType::Object) return winrt::to_string(value.Stringify());
            return transportError("internal_error", "Operation completed without a result");
        };

        // Reconnection queries use the original session and ID. Never submit
        // the mutation a second time, even if a restarted host has forgotten it.
        for (auto it = state->pending.begin(); it != state->pending.end();)
        {
            auto response = exchange("operation-status:" + it->id + ":" + it->session, idText + "-reconcile");
            auto result = outcome(response);
            if (result.empty()) { ++it; continue; }
            {
                std::lock_guard<std::mutex> guard(state->mutex);
                while (!state->lateResults.empty() && (state->lateResults.size() >= 256
                       || result.size() > ClientState::maximumLateBytes - state->lateBytes))
                { state->lateBytes -= state->lateResults.front().size(); state->lateResults.pop_front(); }
                if (result.size() > ClientState::maximumLateBytes)
                    result = transportError("message_too_large", "A late operation result exceeded the retention capacity");
                state->lateBytes += result.size();
                state->lateResults.push_back(std::move(result));
            }
            it = state->pending.erase(it);
        }
        if (mutation && state->pending.size() >= 256)
            co_return winrt::to_hstring(transportError("operation_capacity", "Too many operations are awaiting reconciliation"));
        if (mutation && state->hostSession.empty())
        {
            auto hello = exchange("hello", idText + "-hello");
            state->hostSession = extractString(hello, "hostSession");
            if (state->hostSession.empty()) co_return hello.empty() ? L"" : winrt::to_hstring(
                transportError("incompatible_version", "Host did not identify its session"));
        }
        // Validate before recording an operation that may reach the host.
        encodeRequest(request, id);
        if (GetTickCount64() >= deadline) co_return winrt::to_hstring(
            transportError("queue_timeout", "The request expired before it was sent"));
        if (mutation) state->pending.push_back({idText, state->hostSession});
        auto response = exchange(request, idText);
        if (response.empty()) co_return L"";
        const auto session = extractString(response, "hostSession");
        if (!session.empty()) state->hostSession = session;
        if (!mutation) co_return winrt::to_hstring(response);
        while (true)
        {
            if (auto result = outcome(response); !result.empty())
            {
                state->pending.pop_back();
                co_return winrt::to_hstring(result);
            }
            if (GetTickCount64() >= deadline || WaitForSingleObject(state->stop.get(), 50) == WAIT_OBJECT_0) co_return L"";
            response = exchange("operation-status:" + idText + ":" + state->pending.back().session, idText + "-result");
            if (response.empty()) co_return L"";
        }
    }
    catch (winrt::hresult_error const&) { co_return L""; }
    catch (std::exception const&) { co_return winrt::to_hstring(transportError("invalid_arguments", "Invalid host command arguments")); }
}
}
