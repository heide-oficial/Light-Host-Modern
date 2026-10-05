#pragma once

#include "IpcPipe.h"
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace lightHostModern
{
// Each UI launch gets its own event. A normal window close acknowledges it just
// before exiting; TerminateProcess/End task cannot run that acknowledgement.
// Keep process handles rather than polling PIDs (which Windows can reuse).
class UiProcessLifetime
{
public:
    explicit UiProcessLifetime(std::wstring eventName)
        : name(std::move(eventName)), orderlyClose(CreateEventW(nullptr, TRUE, FALSE, name.c_str()))
    {
        if (!orderlyClose) throw std::runtime_error("Cannot create UI lifetime event");
    }

    ~UiProcessLifetime()
    {
        stop.signal();
        if (watcher.joinable()) watcher.join();
    }

    const std::wstring& eventName() const noexcept { return name; }
    bool running() const noexcept
    {
        return process && WaitForSingleObject(process->get(), 0) == WAIT_TIMEOUT;
    }
    std::optional<DWORD> processId() const noexcept
    {
        if (!process) return DWORD{0};
        const auto status = WaitForSingleObject(process->get(), 0);
        if (status == WAIT_OBJECT_0) return DWORD{0};
        if (status != WAIT_TIMEOUT) return std::nullopt;
        const auto id = GetProcessId(process->get());
        return id ? std::optional<DWORD>(id) : std::nullopt;
    }
    bool endedUnexpectedly() const noexcept
    {
        return process && WaitForSingleObject(process->get(), 0) == WAIT_OBJECT_0
            && WaitForSingleObject(orderlyClose.get(), 0) != WAIT_OBJECT_0;
    }

    // Takes ownership of the handle returned by the launcher, including on error.
    void monitor(HANDLE launchedProcess, std::function<void()> unexpectedExit)
    {
        process = std::make_unique<ipc::Handle>(launchedProcess);
        if (!*process) throw std::runtime_error("Cannot monitor the UI process");
        watcher = std::thread([this, callback = std::move(unexpectedExit)] {
            HANDLE waits[] { stop.get(), process->get() };
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1
                && WaitForSingleObject(orderlyClose.get(), 0) != WAIT_OBJECT_0)
                callback();
        });
    }

private:
    std::wstring name;
    ipc::Handle orderlyClose;
    ipc::StopEvent stop;
    std::unique_ptr<ipc::Handle> process;
    std::thread watcher;
};
}
