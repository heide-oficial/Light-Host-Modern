#include "UpdateCache.h"
#include "UpdatePackage.h"
#include "PortableUpdate.h"
#include "UpdateApply.h"
#include "LegacyInstallMigration.h"
#include "HostRestart.h"
#include "InstallerLocation.h"
#include <shellapi.h>

using namespace lightHostModern::update;
namespace
{
void writeResult(const std::filesystem::path& directory, const std::string& state, uint32_t code = 0,
                 const std::string& detail = {})
{
    juce::DynamicObject::Ptr object = new juce::DynamicObject;
    object->setProperty("state", juce::String(state)); object->setProperty("exitCode", (juce::int64)code);
    object->setProperty("detail", juce::String(detail)); object->setProperty("timestampUtc", juce::Time::getCurrentTime().toISO8601(true));
    const auto bytes = juce::JSON::toString(juce::var(object.get()), true).toStdString();
    const auto temporary = directory / L"update-result.pending", target = directory / L"update-result.json";
    DeleteFileW(temporary.c_str());
    { FileOutput output(temporary); output.write(bytes.data(), bytes.size()); output.flush(); }
    windowsCheck(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "storage_failed");
}
class WindowsApply final : public ApplyEnvironment
{
public:
    Handle host, ui, armEvent, cancelEvent;
    std::filesystem::path package, operation, hostExecutable;
    std::wstring installedProduct;
    bool portable = false, installationStarted = false;
    PortableUpdate portableUpdate;
    bool cancelled() override { return WaitForSingleObject(cancelEvent.value, 0) == WAIT_OBJECT_0; }
    bool armed() override { return WaitForSingleObject(armEvent.value, 0) == WAIT_OBJECT_0; }
    bool processesExited() override
    { return WaitForSingleObject(host.value, 0) == WAIT_OBJECT_0 && WaitForSingleObject(ui.value, 0) == WAIT_OBJECT_0; }
    uint64_t milliseconds() override { return GetTickCount64(); }
    void wait() override { WaitForSingleObject(cancelEvent.value, 100); }
    uint32_t install() override
    {
        installationStarted = true;
        if(portable) { portableUpdate.apply(); return 0; }
        wchar_t system[32768]{}; require(GetSystemDirectoryW(system, 32768) != 0, "installer_start_failed");
        const auto executable = std::filesystem::path(system) / L"msiexec.exe";
        require(lightHostModern::installation::validRoot(hostExecutable.parent_path()), "installation_path_invalid");
        const auto arguments = L"/i " + quoteArgument(package.wstring())
            + L" APPLICATIONFOLDER=" + quoteArgument(hostExecutable.parent_path().wstring()) + L" /quiet /norestart /L*V "
            + quoteArgument((operation / L"installer.log").wstring());
        SHELLEXECUTEINFOW start{sizeof(start)};
        start.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        start.lpVerb = L"runas"; start.lpFile = executable.c_str(); start.lpParameters = arguments.c_str(); start.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&start))
        {
            if (GetLastError() == ERROR_CANCELLED) return 1602;
            throw Error("installer_start_failed");
        }
        Handle process(start.hProcess); require(bool(process), "installer_start_failed");
        require(WaitForSingleObject(process.value, INFINITE) == WAIT_OBJECT_0, "install_failed");
        DWORD code = 0; windowsCheck(GetExitCodeProcess(process.value, &code), "install_failed"); return code;
    }
    void report(const std::string& state, uint32_t code) override {
        writeResult(operation, state, code);
        if(!installationStarted || state=="installing" || state=="rollback_failed" || !processesExited())return;
        portableUpdate.releaseLock();
        auto entry=launchEntry(hostExecutable);
        if (!portable && installationCompleted(state, code)) {
            const auto root = lightHostModern::installation::productRoot(installedProduct.c_str());
            if (root.empty()) { writeResult(operation,"restart_location_unknown",code); return; }
            entry = root / L"LightHostModern.exe";
        }
        auto command=quoteArgument(entry.wstring())+L" --show-ui"+lightHostModern::RuntimeProfile::current().arguments();
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if(CreateProcessW(entry.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,entry.parent_path().c_str(),&startup,&process)) {
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
        } else writeResult(operation,"restart_launch_failed",GetLastError(),state);
    }
};
}
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    int count = 0; auto* command = CommandLineToArgvW(GetCommandLineW(), &count);
    if (command && count == 2 && std::wstring(command[1]) == L"--migrate-legacy-install") {
        LocalFree(command);
        try { return lightHostModern::migration::run(processPath(GetCurrentProcess()).parent_path()); }
        catch (...) { return 1; }
    }
    std::map<std::wstring, std::wstring> options;
    for (int index = 1; command && index + 1 < count; index += 2) options.emplace(command[index], command[index + 1]);
    if (command) LocalFree(command);
    if(options[L"--mode"]==L"restart") {
        try{return lightHostModern::restart::run(options);}
        catch(const std::exception& error){OutputDebugStringA(error.what());return 1;}
    }
    const auto operation = std::filesystem::path(options[L"--operation"]);
    try
    {
        require(operation.is_absolute() && std::filesystem::is_directory(operation), "invalid_arguments");
        auto cacheLease=updateCacheLease(operation,true);
        const auto package = std::filesystem::path(options[L"--package"]);
        require(package.is_absolute() && package.parent_path() == operation, "invalid_arguments");
        Artifact artifact;
        require(options[L"--distribution"] == L"installed" || options[L"--distribution"] == L"portable", "invalid_arguments");
        artifact.distribution = options[L"--distribution"] == L"installed" ? Distribution::installed : Distribution::portable;
        artifact.name = std::filesystem::path(options[L"--package"]).filename().wstring(); artifact.version = options[L"--version"];
        artifact.digest = options[L"--sha256"]; artifact.bytes = std::stoull(options[L"--size"]);
        artifact.url = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/" + artifact.version + L"/" + artifact.name;
        FileInput lock(package); // Keep this handle until Windows Installer has completed.
        validatePackage(package, artifact);
        if (options[L"--mode"] == L"validate-signed") { verifySignedArtifact(operation,artifact);writeResult(operation,"validated");return 0; }
        if (options[L"--mode"] == L"validate") { writeResult(operation, "validated"); return 0; }
        require(options[L"--mode"] == L"apply", "invalid_arguments");
        verifySignedArtifact(operation,artifact);
        require(parseVersion(artifact.version)>parseVersion(juce::String(LIGHTHOST_APP_VERSION).toWideCharPointer()),"downgrade_refused");
        WindowsApply environment;
        environment.operation = operation; environment.package = package;
        environment.host = processHandle(static_cast<DWORD>(std::stoul(options[L"--host-pid"])), std::stoull(options[L"--host-created"]));
        environment.ui = processHandle(static_cast<DWORD>(std::stoul(options[L"--ui-pid"])), std::stoull(options[L"--ui-created"]));
        require(processPath(environment.host.value).filename() == L"LightHostModern.exe"
            && processPath(environment.ui.value).filename() == L"LightHostModernWinUI.exe", "process_mismatch");
        environment.hostExecutable=processPath(environment.host.value);
        environment.portable=artifact.distribution==Distribution::portable;
        if (!environment.portable) {
            MsiHandle database;
            require(MsiOpenDatabaseW(package.c_str(), nullptr /* read-only */, &database.value) == ERROR_SUCCESS, "package_invalid");
            environment.installedProduct = msiProperty(database.value, L"ProductCode");
            require(!environment.installedProduct.empty(), "package_invalid");
        }
        require(!lightHostModern::RuntimeProfile::current().test || environment.portable,"distribution_mismatch");
        require(detectDistribution(environment.hostExecutable) == artifact.distribution, "distribution_mismatch");
        const auto uiRelative=processPath(environment.ui.value).lexically_relative(environment.hostExecutable.parent_path());
        require(!uiRelative.empty() && !uiRelative.is_absolute() && *uiRelative.begin()==L"WinUI", "process_mismatch");
        const auto events = options[L"--events"];
        require(events.rfind(L"Local\\LightHostModernUpdate-", 0) == 0 && events.size() <= 100, "invalid_arguments");
        environment.armEvent = Handle(OpenEventW(SYNCHRONIZE, FALSE, (events + L"-arm").c_str()));
        environment.cancelEvent = Handle(OpenEventW(SYNCHRONIZE, FALSE, (events + L"-cancel").c_str()));
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (events + L"-ready").c_str()));
        windowsCheck(bool(environment.armEvent) && bool(environment.cancelEvent) && bool(ready), "helper_start_failed");
        if(environment.portable) {
            const auto root=portableRoot(environment.hostExecutable);require(!root.empty(),"portable_migration_required");
            environment.portableUpdate.prepare(package,artifact,root,[&] {return environment.cancelled();});
        }
        writeResult(operation, "ready"); windowsCheck(SetEvent(ready.value), "helper_start_failed");
        applyWhenReady(environment);
        return 0;
    }
    catch (const Error& error)
    { try { writeResult(operation, error.code, 0, error.what()); } catch (...) {} return 1; }
    catch (const std::filesystem::filesystem_error& error)
    { try { writeResult(operation, "storage_failed", 0, error.what()); } catch (...) {} return 1; }
    catch (const std::exception& error)
    { try { writeResult(operation, "package_invalid", 0, error.what()); } catch (...) {} return 1; }
}
