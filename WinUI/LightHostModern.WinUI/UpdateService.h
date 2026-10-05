#pragma once
#include "../../Source/RuntimeProfile.h"
#include "../../Source/UpdateWindows.h"
#include "../../Source/UpdateCache.h"
#include "../../Source/UpdateSignature.h"
#include "../../Source/PortablePaths.h"
#include "HostJson.h"
#include <winrt/Windows.Data.Json.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace lightHostModern::ui
{
struct AvailableUpdate
{
    std::wstring releaseUrl, version, artifactUrl;
    update::Artifact artifact;
    bool available = false;
    std::string signedManifest, signature;
};

class UpdateService : public std::enable_shared_from_this<UpdateService>
{
public:
    AvailableUpdate latest;
    std::filesystem::path validatedPackage;
    std::string lastError;
    std::string applicationState;
    std::filesystem::path applicationLog;
    winrt::Windows::Foundation::IAsyncAction loadApplicationResultAsync()
    {
        auto lifetime = shared_from_this(); winrt::apartment_context ui;
        co_await winrt::resume_background();
        std::string state; std::filesystem::path resultPath;
        std::filesystem::file_time_type newest = (std::filesystem::file_time_type::min)();
        const auto& profile = RuntimeProfile::current();
        const auto parent = profile.test ? profile.directory / L"Temp" / L"Updates"
            : profile.uiSettings().parent_path() / L"Temp" / L"Updates";
        update::pruneUpdateCache(parent);
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(parent, error))
        {
            // Directory enumeration order is unspecified. Inspect each bounded
            // report so old operation folders cannot hide the newest result.
            const auto file = entry.path() / L"update-result.json";
            if (!std::filesystem::is_regular_file(file, error) || std::filesystem::file_size(file, error) > 65536) continue;
            const auto changed = std::filesystem::last_write_time(file, error);
            if (error || changed <= newest) continue;
            std::ifstream stream(file, std::ios::binary);
            const auto result = ipc::extractString(std::string(std::istreambuf_iterator<char>(stream), {}), "state");
            if (result != "completed" && result != "restart_required" && result != "cancelled" && result != "shutdown_timeout"
                && result != "installer_start_failed" && result != "install_failed" && result != "installing"
                && result != "update_apply_failed" && result != "rollback_failed" && result != "restart_launch_failed") continue;
            newest = changed; state = result; resultPath = file;
        }
        co_await ui;
        applicationState = std::move(state); applicationLog = std::move(resultPath);
    }
    bool portable() const { return latest.artifact.distribution == update::Distribution::portable; }
    void cancel()
    {
        cancelled.store(true, std::memory_order_relaxed);
        const std::lock_guard<std::mutex> guard(eventMutex);
        if (cancelEvent && !committed) SetEvent(cancelEvent.value);
    }
    winrt::Windows::Foundation::IAsyncAction cancelAndWaitAsync()
    {
        auto lifetime = shared_from_this(); cancel();
        while (running.load(std::memory_order_acquire)) co_await winrt::resume_after(std::chrono::milliseconds(100));
    }
    winrt::Windows::Foundation::IAsyncAction checkAsync(std::wstring currentVersion, uint32_t hostPid)
    {
        auto lifetime = shared_from_this(); winrt::apartment_context ui;
        co_await winrt::resume_background();
        const auto host = update::processHandle(hostPid);
        const auto executable = update::processPath(host.value);
        update::require(executable.filename() == L"LightHostModern.exe", "process_mismatch");
        const auto distribution = RuntimeProfile::current().test
            ? update::Distribution::portable : update::detectDistribution(executable);
        std::unique_ptr<update::Input> source;
        const auto& profile = RuntimeProfile::current();
        const auto testMetadata = profile.directory / L"Temp" / L"update-fixture.json";
        const bool fixture = profile.test && std::filesystem::is_regular_file(testMetadata);
        if (fixture) source = std::make_unique<update::FileInput>(testMetadata);
        else source = std::make_unique<update::HttpInput>(L"https://api.github.com/repos/heide-oficial/Light-Host-Modern/releases/latest");
        std::string body; std::array<char, 16384> buffer{};
        for (;;)
        {
            update::checkCancelled(cancelled);
            const auto count = source->read(buffer.data(), buffer.size()); if (!count) break;
            update::require(count <= 4 * 1024 * 1024 - body.size(), "metadata_invalid"); body.append(buffer.data(), count);
        }
        if (body.rfind("\xef\xbb\xbf", 0) == 0) body.erase(0, 3);
        const auto json = ipc::parseObject(body);
        std::filesystem::path fixtureSource;
        unsigned fixtureDelay = 0;
        if (fixture)
        {
            currentVersion = json.GetNamedString(L"reportedCurrentVersion", winrt::hstring(currentVersion));
            const auto candidatePath = std::filesystem::path(std::wstring(json.GetNamedString(L"localPackage", L"")));
            update::require(candidatePath.is_absolute(), "metadata_invalid");
            fixtureSource = std::filesystem::weakly_canonical(candidatePath);
            const auto relative = fixtureSource.lexically_relative(std::filesystem::weakly_canonical(profile.directory / L"Temp"));
            update::require(!relative.empty() && *relative.begin() != L".." && !relative.is_absolute(), "metadata_invalid");
            const auto delay = json.GetNamedNumber(L"chunkDelayMs", 0);
            update::require(delay >= 0 && delay <= 50 && std::floor(delay) == delay, "metadata_invalid");
            fixtureDelay = static_cast<unsigned>(delay);
        }
        AvailableUpdate candidate;
        candidate.version = json.GetNamedString(L"tag_name", L"");
        candidate.releaseUrl = json.GetNamedString(L"html_url", L"");
        const auto releaseVersion = update::parseVersion(candidate.version), current = update::parseVersion(currentVersion);
        const auto releaseBase = std::wstring(L"https://github.com/heide-oficial/Light-Host-Modern/releases/tag/");
        candidate.available = releaseVersion && current && *releaseVersion > *current
            && candidate.releaseUrl == releaseBase + candidate.version && !json.GetNamedBoolean(L"draft", false)
            && !json.GetNamedBoolean(L"prerelease", false);
        candidate.artifact.distribution = distribution;
        if (candidate.available)
        {
            for (const auto& value : json.GetNamedArray(L"assets", ipc::JsonArray{}))
            {
                if (value.ValueType() != ipc::JsonValueType::Object) continue;
                const auto asset = value.GetObject();
                if (!update::artifactNameAllowed(distribution, candidate.version, std::wstring(asset.GetNamedString(L"name", L"")))) continue;
                try {
                auto& artifact = candidate.artifact;
                artifact.name = asset.GetNamedString(L"name"); artifact.version = candidate.version;
                artifact.url = asset.GetNamedString(L"browser_download_url", L"");
                const auto size = asset.GetNamedNumber(L"size", 0);
                update::require(std::isfinite(size) && size > 0 && size <= update::maximumPackageBytes && std::floor(size) == size, "size_mismatch");
                artifact.bytes = static_cast<uint64_t>(size);
                const auto digest = asset.GetNamedValue(L"digest", ipc::JsonValue::CreateNullValue());
                if (digest.ValueType() == ipc::JsonValueType::String) artifact.digest = digest.GetString();
                artifact.validate(); candidate.artifactUrl = artifact.url;
                } catch (...) { candidate.artifactUrl.clear(); }
                break;
            }
        }
        // GitHub availability is independent of eligibility for unattended file
        // replacement. Authenticate exact manifest bytes using embedded roots.
        if(!candidate.artifactUrl.empty()) {
            try {
                update::require(!update::trustedUpdateKeys.empty(),"signature_untrusted");
                update::require(distribution!=update::Distribution::portable||!update::portableRoot(executable).empty(),"portable_migration_required");
                update::require(distribution!=update::Distribution::portable||update::portableDurabilitySupported(update::portableRoot(executable)),"filesystem_unsupported");
                auto fetch=[&](const wchar_t* name,size_t limit) {
                    std::string data;const auto base=L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/"+candidate.version+L"/";
                    std::unique_ptr<update::Input> input;
                    if(fixture)input=std::make_unique<update::FileInput>(fixtureSource.parent_path()/name);
                    else input=std::make_unique<update::HttpInput>(base+name);
                    std::array<char,16384> chunk{};while(auto count=input->read(chunk.data(),chunk.size())) {
                        update::require(count<=limit-data.size(),"metadata_invalid");data.append(chunk.data(),count);
                    }return data;
                };
                candidate.signedManifest=fetch(L"update-manifest.json",4*1024*1024);candidate.signature=fetch(L"update-manifest.sig",16384);
                const auto manifest=ipc::parseObject(candidate.signedManifest);
                update::verifyManifestSignature(candidate.signedManifest,candidate.signature,winrt::to_string(manifest.GetNamedString(L"keyId")));
                update::require(manifest.GetNamedNumber(L"formatVersion")==1&&manifest.GetNamedString(L"architecture")==L"x64"
                    &&manifest.GetNamedNumber(L"minimumLauncher")==1
                    &&update::parseVersion(std::wstring(manifest.GetNamedString(L"version")))==releaseVersion,"metadata_invalid");
                bool found=false;
                for(const auto& value:manifest.GetNamedArray(L"artifacts")) {
                    const auto entry=value.GetObject();if(entry.GetNamedString(L"name")!=candidate.artifact.name)continue;
                    update::require(!found,"metadata_invalid");found=true;
                    update::require(entry.GetNamedNumber(L"size")==double(candidate.artifact.bytes)
                        &&update::normalizedDigest(std::wstring(entry.GetNamedString(L"digest")))==update::normalizedDigest(candidate.artifact.digest),"checksum_mismatch");
                }
                update::require(found,"artifact_mismatch");
            }catch(...) {candidate.artifactUrl.clear();candidate.signedManifest.clear();candidate.signature.clear();}
        }
        const auto created = update::processCreation(host.value);
        co_await ui;
        hostProcessId = hostPid; hostCreated = created; hostExecutable = executable;
        localFixture = fixtureSource; localFixtureDelay = fixtureDelay;
        latest = std::move(candidate);
    }

    winrt::Windows::Foundation::IAsyncAction downloadAsync(update::ProgressCallback progress)
    {
        auto lifetime = shared_from_this(); const auto release = latest; winrt::apartment_context ui;
        update::require(!running.exchange(true), "operation_conflict");
        cancelled.store(false); lastError.clear(); validatedPackage.clear(); pendingPackage.clear();
        co_await winrt::resume_background();
        struct Finish { std::atomic<bool>& value; ~Finish() { value.store(false, std::memory_order_release); } } finish{running};
        std::exception_ptr failure; std::string failureCode;
        try
        {
            release.artifact.validate();
            const auto& profile = RuntimeProfile::current();
            const auto parent = profile.test ? profile.directory / L"Temp" / L"Updates"
                : profile.uiSettings().parent_path() / L"Temp" / L"Updates";
            std::filesystem::create_directories(parent);
            GUID id{}; winrt::check_hresult(CoCreateGuid(&id));
            wchar_t identifier[40]{}; StringFromGUID2(id, identifier, 40);
            operation = parent / identifier;
            update::require(std::filesystem::create_directory(operation), "storage_failed");
            cacheLease=update::updateCacheLease(operation);update::markUpdateCache(operation);
            update::require(!release.signedManifest.empty()&&!release.signature.empty(),"signature_untrusted");
            {update::FileOutput file(operation/L"update-manifest.json");file.write(release.signedManifest.data(),release.signedManifest.size());file.flush();}
            {update::FileOutput file(operation/L"update-manifest.sig");file.write(release.signature.data(),release.signature.size());file.flush();}
            std::unique_ptr<update::Input> injected;
            if (!localFixture.empty()) injected = std::make_unique<FixtureInput>(localFixture, localFixtureDelay);
            pendingPackage = update::download(release.artifact, operation, cancelled, progress, injected.get());
            update::checkCancelled(cancelled);
            const auto process = startHelper(false);
            const auto started = GetTickCount64();
            while (WaitForSingleObject(process.value, 100) == WAIT_TIMEOUT)
            {
                if (GetTickCount64() - started > 180000)
                { TerminateProcess(process.value, ERROR_TIMEOUT); WaitForSingleObject(process.value, 5000); throw update::Error("validation_timeout"); }
            }
            update::checkCancelled(cancelled);
            const auto result = readResult();
            update::require(ipc::extractString(result, "state") == "validated", resultCode(result).c_str());
        }
        catch (const update::Error& error) { failureCode = error.code; failure = std::current_exception(); discardPackage(); }
        catch (...) { failureCode = "storage_failed"; failure = std::current_exception(); discardPackage(); }
        co_await ui;
        lastError = std::move(failureCode);
        if (failure) std::rethrow_exception(failure);
        validatedPackage = pendingPackage;
    }

    winrt::Windows::Foundation::IAsyncAction prepareUpdateAsync()
    {
        auto lifetime = shared_from_this(); winrt::apartment_context ui;
        update::require(!running.exchange(true), "operation_conflict");
        lastError.clear();
        co_await winrt::resume_background();
        struct Finish { std::atomic<bool>& value; ~Finish() { value.store(false, std::memory_order_release); } } finish{running};
        std::exception_ptr failure; std::string failureCode;
        try
        {
            update::require((!RuntimeProfile::current().test || portable()) && !validatedPackage.empty(), "distribution_mismatch");
            update::checkCancelled(cancelled);
            const auto host = update::processHandle(hostProcessId, hostCreated);
            update::require(update::detectDistribution(hostExecutable) == latest.artifact.distribution, "distribution_mismatch");
            eventBase = L"Local\\LightHostModernUpdate-" + operation.filename().wstring();
            {
                const std::lock_guard<std::mutex> guard(eventMutex);
                armEvent = update::Handle(CreateEventW(nullptr, TRUE, FALSE, (eventBase + L"-arm").c_str()));
                cancelEvent = update::Handle(CreateEventW(nullptr, TRUE, FALSE, (eventBase + L"-cancel").c_str()));
                update::windowsCheck(bool(armEvent) && bool(cancelEvent), "helper_start_failed");
            }
            update::Handle ready(CreateEventW(nullptr, TRUE, FALSE, (eventBase + L"-ready").c_str()));
            update::windowsCheck(bool(ready), "helper_start_failed");
            update::checkCancelled(cancelled);
            helper = startHelper(true);
            HANDLE signals[] = {ready.value, helper.value};
            const auto started = GetTickCount64();
            for (;;)
            {
                update::checkCancelled(cancelled);
                const auto status = WaitForMultipleObjects(2, signals, FALSE, 100);
                if (status == WAIT_OBJECT_0) break;
                if (status != WAIT_TIMEOUT || GetTickCount64() - started > 180000)
                    throw update::Error(resultCode(readResult(), "helper_start_failed"));
            }
        }
        catch (const update::Error& error) { failureCode = error.code; failure = std::current_exception(); cancel(); }
        catch (...) { failureCode = "helper_start_failed"; failure = std::current_exception(); cancel(); }
        co_await ui;
        lastError = std::move(failureCode);
        if (failure) std::rethrow_exception(failure);
    }
    void armUpdate()
    {
        const std::lock_guard<std::mutex> guard(eventMutex);
        update::windowsCheck(bool(armEvent) && SetEvent(armEvent.value), "helper_start_failed"); committed = true;
    }
    void abortUpdate()
    {
        const std::lock_guard<std::mutex> guard(eventMutex);
        committed = false;
        if(cancelEvent) SetEvent(cancelEvent.value);
    }
    void showPortablePackage() const
    {
        update::require(portable() && !validatedPackage.empty(), "package_unavailable");
        const auto arguments = L"/select," + update::quoteArgument(validatedPackage.wstring());
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
        update::require(result > 32, "package_unavailable");
    }
private:
    struct FixtureInput final : update::Input
    {
        update::FileInput input; unsigned delay;
        FixtureInput(const std::filesystem::path& path, unsigned milliseconds) : input(path), delay(milliseconds) {}
        size_t read(void* bytes, size_t capacity) override
        {
            if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            return input.read(bytes, (std::min)(capacity, size_t(65536)));
        }
    };
    std::atomic<bool> cancelled{false}, running{false};
    std::filesystem::path hostExecutable, operation, pendingPackage;
    std::filesystem::path localFixture;
    unsigned localFixtureDelay = 0;
    uint32_t hostProcessId = 0; uint64_t hostCreated = 0;
    update::Handle helper, armEvent, cancelEvent, cacheLease;
    std::wstring eventBase;
    std::mutex eventMutex;
    bool committed = false;
    std::string readResult() const
    {
        const auto file = operation / L"update-result.json";
        if (!std::filesystem::exists(file) || std::filesystem::file_size(file) > 65536) return {};
        std::ifstream input(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }
    static std::string resultCode(const std::string& result, const char* fallback = "package_invalid")
    { const auto state = ipc::extractString(result, "state"); return state.empty() || state == "validated" || state == "ready" ? fallback : state; }
    void discardPackage()
    { if (!pendingPackage.empty()) DeleteFileW(pendingPackage.c_str()); pendingPackage.clear(); }
    update::Handle startHelper(bool apply)
    {
        const auto source = hostExecutable.parent_path() / L"LightHostModernUpdateHelper.exe";
        const auto executable = apply ? operation / L"LightHostModernUpdateHelper.exe" : source;
        if (apply) update::windowsCheck(CopyFileW(source.c_str(), executable.c_str(), TRUE), "helper_start_failed");
        std::vector<std::wstring> arguments{L"--mode", apply ? L"apply" : L"validate-signed", L"--operation", operation.wstring(),
            L"--package", pendingPackage.wstring(), L"--version", latest.artifact.version, L"--size", std::to_wstring(latest.artifact.bytes),
            L"--sha256", latest.artifact.digest, L"--distribution", portable() ? L"portable" : L"installed"};
        if (apply)
            arguments.insert(arguments.end(), {L"--host-pid", std::to_wstring(hostProcessId), L"--host-created", std::to_wstring(hostCreated),
                L"--ui-pid", std::to_wstring(GetCurrentProcessId()), L"--ui-created", std::to_wstring(update::processCreation(GetCurrentProcess())),
                L"--events", eventBase});
        const auto& profile = RuntimeProfile::current();
        if (profile.test) arguments.insert(arguments.end(), {L"--test-profile", profile.name, L"--profile-root", profile.directory.parent_path().wstring()});
        auto line = update::quoteArgument(executable.wstring());
        for (const auto& argument : arguments) line += L" " + update::quoteArgument(argument);
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        update::windowsCheck(CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, operation.c_str(), &startup, &process), "helper_start_failed");
        CloseHandle(process.hThread); return update::Handle(process.hProcess);
    }
};
}
