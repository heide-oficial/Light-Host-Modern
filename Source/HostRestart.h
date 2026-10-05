#pragma once
#include "UpdateWindows.h"
#include "RuntimeProfile.h"
#include "PortablePaths.h"
#include <map>

namespace lightHostModern::restart
{
// Separate helper mode: no package download, installation, or elevation.
inline void prepare(DWORD uiPid, uint64_t uiCreated)
{
    using namespace update;
    auto ui = processHandle(uiPid, uiCreated);
    require(processPath(ui.value).filename() == L"LightHostModernWinUI.exe", "process_mismatch");
    const auto host = processPath(GetCurrentProcess());
    const auto helper = host.parent_path() / L"LightHostModernUpdateHelper.exe";
    const auto eventName = L"Local\\LightHostModernRestart-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
    windowsCheck(bool(ready), "restart_prepare_failed");
    auto command = quoteArgument(helper.wstring()) + L" --mode restart --host-pid " + std::to_wstring(GetCurrentProcessId())
        + L" --host-created " + std::to_wstring(processCreation(GetCurrentProcess()))
        + L" --ui-pid " + std::to_wstring(uiPid) + L" --ui-created " + std::to_wstring(uiCreated)
        + L" --ready " + quoteArgument(eventName);
    const auto& profile = RuntimeProfile::current();
    if (profile.test) command += L" --test-profile " + quoteArgument(profile.name) + L" --profile-root " + quoteArgument(profile.directory.parent_path().wstring());
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    windowsCheck(CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, helper.parent_path().c_str(), &startup, &process), "restart_prepare_failed");
    Handle child(process.hProcess), thread(process.hThread);
    HANDLE waits[]{ready.value, child.value};
    require(WaitForMultipleObjects(2, waits, FALSE, 5000) == WAIT_OBJECT_0, "restart_prepare_failed");
}
inline int run(const std::map<std::wstring, std::wstring>& args)
{
    using namespace update;
    const auto get = [&](const wchar_t* key) { return args.at(key); };
    auto host = processHandle(static_cast<DWORD>(std::stoul(get(L"--host-pid"))), std::stoull(get(L"--host-created")));
    auto ui = processHandle(static_cast<DWORD>(std::stoul(get(L"--ui-pid"))), std::stoull(get(L"--ui-created")));
    const auto executable = processPath(host.value);
    require(executable.filename() == L"LightHostModern.exe" && executable.parent_path() == processPath(GetCurrentProcess()).parent_path()
        && processPath(ui.value).filename() == L"LightHostModernWinUI.exe", "process_mismatch");
    const auto eventName = get(L"--ready");
    require(eventName.rfind(L"Local\\LightHostModernRestart-", 0) == 0 && eventName.size() < 120, "invalid_arguments");
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str()));
    windowsCheck(bool(ready) && SetEvent(ready.value), "restart_prepare_failed");
    HANDLE waits[]{host.value, ui.value};
    // Never kill a host that is still saving state or force-close the user's UI.
    require(WaitForMultipleObjects(2, waits, TRUE, 60000) == WAIT_OBJECT_0, "restart_shutdown_timeout");
    const auto entry=launchEntry(executable);
    auto command = quoteArgument(entry.wstring()) + L" --show-ui" + RuntimeProfile::current().arguments();
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    windowsCheck(CreateProcessW(entry.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, entry.parent_path().c_str(), &startup, &process), "restart_launch_failed");
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0;
}
}
