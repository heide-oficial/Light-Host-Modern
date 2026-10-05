#include "UiProcessLifetime.h"
#include <iostream>

using lightHostModern::ipc::Handle;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && std::wstring(argv[1]) == L"child")
    {
        Handle close(OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]));
        if (!close) return 90;
        SetEvent(close.get());
        return 0;
    }
    if (argc == 2) { Sleep(INFINITE); return 91; }
    try
    {
        wchar_t path[32768] {};
        GetModuleFileNameW(nullptr, path, 32768);
        unsigned sequence = 0;
        auto eventName = [&] { return L"Local\\LightHostModern-LifetimeTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence); };
        auto launch = [&](const std::wstring& arguments) {
            std::wstring command = L"\"" + std::wstring(path) + L"\" " + arguments;
            STARTUPINFOW startup { sizeof(startup) }; PROCESS_INFORMATION child {};
            require(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child), "launch child");
            CloseHandle(child.hThread);
            return child.hProcess;
        };
        {
            lightHostModern::UiProcessLifetime lifetime(eventName());
            Handle notified(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            auto process = launch(L"child \"" + lifetime.eventName() + L"\"");
            lifetime.monitor(process, [&] { SetEvent(notified.get()); });
            require(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0, "normal child exits");
            require(!lifetime.running() && !lifetime.endedUnexpectedly(), "normal close preserves host");
            require(WaitForSingleObject(notified.get(), 150) == WAIT_TIMEOUT, "normal close never invokes shutdown");
        }
        for (DWORD exitCode : {0UL, 1UL, 0xC0000005UL})
        {
            Handle notified(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            lightHostModern::UiProcessLifetime lifetime(eventName());
            auto process = launch(L"wait");
            lifetime.monitor(process, [&] { SetEvent(notified.get()); });
            require(lifetime.running(), "live UI is monitored");
            require(TerminateProcess(process, exitCode), "terminate owned child");
            require(WaitForSingleObject(notified.get(), 5000) == WAIT_OBJECT_0, "abrupt exit invokes shutdown regardless of exit code");
            require(lifetime.endedUnexpectedly(), "unexpected exit is retained until shutdown");
        }
        {
            Handle notified(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            Handle child(launch(L"wait"));
            {
                lightHostModern::UiProcessLifetime lifetime(eventName());
                HANDLE duplicate = nullptr;
                require(DuplicateHandle(GetCurrentProcess(), child.get(), GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS), "duplicate process handle");
                lifetime.monitor(duplicate, [&] { SetEvent(notified.get()); });
            }
            require(WaitForSingleObject(child.get(), 0) == WAIT_TIMEOUT, "destroy monitor does not kill UI");
            TerminateProcess(child.get(), 0);
            require(WaitForSingleObject(notified.get(), 150) == WAIT_TIMEOUT, "destroyed monitor cannot invoke callback");
        }
        {
            Handle child(launch(L"wait"));
            const auto pid = GetProcessId(child.get());
            for (DWORD access : {DWORD(SYNCHRONIZE), DWORD(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION)}) {
                lightHostModern::UiProcessLifetime lifetime(eventName());
                require(lifetime.processId() == DWORD{0}, "closed UI has known zero identity");
                lifetime.monitor(OpenProcess(access, FALSE, pid), [] {});
                const auto observed = lifetime.processId();
                require(access == SYNCHRONIZE ? !observed.has_value() : observed == pid,
                    "query failure must be unavailable; packaged UI handle must permit identity query");
            }
            TerminateProcess(child.get(), 0);
            require(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0, "metrics child exits");
        }
        std::cout << "UI process lifetime passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
