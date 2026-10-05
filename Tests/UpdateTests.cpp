#include "UpdatePackage.h"
#include "UpdateApply.h"
#include "PortableUpdate.h"
#include "UpdateCache.h"
#include "InstallerLocation.h"
#include "ScenarioRunner.h"
#include <cstring>
#include <iostream>

using namespace lightHostModern::update;
namespace
{
struct Bytes final : Input
{
    std::string data; size_t at = 0, chunk = 731, maxRequest = 0;
    size_t failAt = SIZE_MAX;
    std::function<void()> beforeRead;
    explicit Bytes(std::string bytes) : data(std::move(bytes)) {}
    size_t read(void* target, size_t capacity) override
    {
        if (beforeRead) beforeRead();
        if (at >= failAt) throw Error("network_failed");
        maxRequest = std::max(maxRequest, capacity);
        auto count = std::min({capacity, chunk, data.size() - at});
        std::memcpy(target, data.data() + at, count); at += count; return count;
    }
};
struct Sink final : Output
{
    std::string data; bool flushed = false, failWrite = false, failFlush = false;
    void write(const void* bytes, size_t count) override { if (failWrite) throw Error("storage_failed"); data.append((const char*)bytes, count); }
    void flush() override { if (failFlush) throw Error("storage_failed"); flushed = true; }
};
Artifact fixture(const std::string& bytes)
{
    Artifact result; result.version = L"v1.2.2"; result.name = artifactName(result.distribution);
    result.url = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/" + result.version + L"/" + result.name;
    Sha256 hash; hash.append(bytes.data(), bytes.size()); result.digest = L"sha256:" + hash.finish(); result.bytes = bytes.size(); return result;
}
template<class Work> void expect(const std::string& code, Work&& work)
{
    try { work(); } catch (const Error& error) { scenarios::require(error.code == code, error.what()); return; }
    throw std::runtime_error("Expected error: " + code);
}
std::filesystem::path directory()
{
    const auto path = std::filesystem::current_path() / (L"update-test-" + std::wstring(juce::Uuid().toString().toWideCharPointer()));
    std::filesystem::create_directory(path); return path;
}
struct Apply final : ApplyEnvironment
{
    uint64_t time = 0, armAt = 100, hostExit = 200, uiExit = 300, cancelAt = UINT64_MAX;
    unsigned installs = 0; uint32_t exitCode = 0; bool failStart = false;
    std::string state; uint32_t reportedCode = 0;
    bool cancelled() override { return time >= cancelAt; }
    bool armed() override { return time >= armAt; }
    bool processesExited() override { return time >= hostExit && time >= uiExit; }
    uint64_t milliseconds() override { return time; }
    void wait() override { time += 100; }
    uint32_t install() override { ++installs; if (failStart) throw Error("installer_start_failed"); return exitCode; }
    void report(const std::string& value, uint32_t code) override { state = value; reportedCode = code; }
};
std::string pe(bool x64 = true)
{
    std::string bytes(512, '\0'); bytes[0] = 'M'; bytes[1] = 'Z'; bytes[60] = 128;
    bytes[128] = 'P'; bytes[129] = 'E'; bytes[132] = x64 ? 0x64 : 0x4c; bytes[133] = x64 ? 0x86 : 0x01; return bytes;
}
std::filesystem::path zipFixture(const std::filesystem::path& parent, const char* version = "1.2.2", const char* platform = "x64",
                                bool omitHelper = false, bool wrongPe = false, bool traversal = false)
{
    const auto file = parent / L"LightHostModern-Portable.zip";
    juce::ZipFile::Builder builder;
    const auto add = [&](const juce::String& name, const std::string& contents) {
        builder.addEntry(new juce::MemoryInputStream(contents.data(), contents.size(), true), 6, name, juce::Time::getCurrentTime());
    };
    add("release-info.json", std::string("{\"name\":\"LightHostModern\",\"version\":\"") + version + "\",\"platform\":\"" + platform + "\"}");
    add("legacy-payload-files.json", "[]");
    add("LightHostModern.exe", pe(!wrongPe)); add("LightHostModernScanner.exe", pe());
    if (!omitHelper) add("LightHostModernUpdateHelper.exe", pe());
    add("WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe", pe());
    if (traversal) add("../escape.txt", "escape");
    juce::FileOutputStream output(juce::File(file.c_str()));
    scenarios::require(output.openedOk() && builder.writeToStream(output, nullptr), "Could not create ZIP fixture"); output.flush(); return file;
}
void msiFixture(const std::filesystem::path& path, const wchar_t* version = L"1.2.2", const wchar_t* architecture = L"x64;1033")
{
    MsiHandle db, summary;
    scenarios::require(MsiOpenDatabaseW(path.c_str(), reinterpret_cast<LPCWSTR>(3) /*CREATE*/, &db.value) == ERROR_SUCCESS, "MSI fixture create failed");
    const auto sql = [&](const std::wstring& query) {
        MsiHandle view;
        scenarios::require(MsiDatabaseOpenViewW(db.value, query.c_str(), &view.value) == ERROR_SUCCESS && MsiViewExecute(view.value, 0) == ERROR_SUCCESS, "MSI fixture SQL failed");
    };
    sql(L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) LOCALIZABLE PRIMARY KEY `Property`)");
    for (const auto& pair : {std::pair{L"ProductName", L"LightHostModern"}, {L"ProductVersion", version}, {L"UpgradeCode", upgradeCode}})
        sql(std::wstring(L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('") + pair.first + L"','" + pair.second + L"')");
    sql(L"CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL PRIMARY KEY `File`)");
    unsigned index = 0;
    for (const auto* name : {L"LightHostModern.exe", L"LightHostModernScanner.exe", L"LightHostModernWinUI.exe", L"LightHostModernUpdateHelper.exe"})
        sql(L"INSERT INTO `File` (`File`, `FileName`) VALUES ('f" + std::to_wstring(++index) + L"','" + name + L"')");
    scenarios::require(MsiGetSummaryInformationW(db.value, nullptr, 1, &summary.value) == ERROR_SUCCESS
        && MsiSummaryInfoSetPropertyW(summary.value, 7, VT_LPSTR, 0, nullptr, architecture) == ERROR_SUCCESS
        && MsiSummaryInfoPersist(summary.value) == ERROR_SUCCESS && MsiDatabaseCommit(db.value) == ERROR_SUCCESS, "MSI fixture summary failed");
}
Artifact forFile(const std::filesystem::path& file, Distribution distribution = Distribution::portable)
{
    Artifact artifact; artifact.distribution = distribution; artifact.version = L"v1.2.2"; artifact.name = artifactName(distribution);
    artifact.bytes = std::filesystem::file_size(file); artifact.digest = fileDigest(file);
    artifact.url = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/" + artifact.version + L"/" + artifact.name; return artifact;
}
}
#include "PortableUpdateScenarios.h"
#include "BackgroundReleaseScenarios.h"
int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--verify-configured-manifest")
    {
        // This entry point deliberately uses the compiled production roots.
        // No TestSigningKey, trust override, package preparation or installation.
        try
        {
            require(argc == 3, "invalid_arguments");
            const auto operation = std::filesystem::absolute(juce::String::fromUTF8(argv[2]).toWideCharPointer());
            const auto body = readSmallFile(operation / L"update-manifest.json", 4 * 1024 * 1024);
            const auto signature = readSmallFile(operation / L"update-manifest.sig", 16384);
            verifySignedManifest(body, signature);
            std::cout << "configured_manifest_validated\n";
            return 0;
        }
        catch (const Error& error) { std::cerr << error.code << '\n'; return 1; }
        catch (const std::exception&) { std::cerr << "manifest_read_failed\n"; return 1; }
    }
    scenarios::Runner runner;
    if (argc == 2 && std::string(argv[1]) == "--background-release") { backgroundReleaseTests::run(runner); return runner.result(); }
    if (argc == 2) { realPortablePackageScenario(runner, std::filesystem::absolute(juce::String::fromUTF8(argv[1]).toWideCharPointer())); return runner.result(); }
    runner.run("Strict release identity, architecture, artifact, size and checksum metadata", [] {
        scenarios::require(parseVersion(L"v1.2.2") == parseVersion(L"1.2.2"), "Version normalization failed");
        for (const auto* value : {L"1.2", L"1.2.2.4", L"1.2.2-beta", L"1x2x3", L"1.02.3", L"1.2.3 ", L"999999999999.2.3"})
            scenarios::require(!parseVersion(value), "Invalid version accepted");
        const auto good = fixture("audio"); good.validate();
        auto invalid = good; invalid.architecture = L"arm64"; expect("architecture_mismatch", [&] { invalid.validate(); });
        invalid = good; invalid.name = L"../LightHostModern-Setup.msi"; expect("artifact_mismatch", [&] { invalid.validate(); });
        invalid = good; invalid.url += L"?other"; expect("artifact_mismatch", [&] { invalid.validate(); });
        invalid = good; invalid.bytes = maximumPackageBytes + 1; expect("size_mismatch", [&] { invalid.validate(); });
        invalid = good; invalid.digest.clear(); expect("checksum_unavailable", [&] { invalid.validate(); });
    });
    runner.run("Versioned installer and exact compatibility alias", [] {
        auto artifact = fixture("package"); artifact.distribution = Distribution::installed; artifact.version = L"v1.4.0";
        for (const auto& name : {versionedArtifactName(artifact.distribution, artifact.version), artifactName(artifact.distribution)}) {
            artifact.name = name;
            artifact.url = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/v1.4.0/" + name;
            artifact.validate();
        }
        artifact.name = L"LightHostModern-9.9.9-Setup.msi";
        expect("artifact_mismatch", [&] { artifact.validate(); });
    });
    runner.run("Bounded streaming SHA-256 and exact byte progress", [] {
        const std::string bytes(3 * transferCapacity + 43, 'x'); auto artifact = fixture(bytes);
        Bytes source(bytes); Sink sink; Sha256 digest; std::atomic<bool> cancel{false}; Progress latest;
        transfer(source, sink, digest, artifact, cancel, [&](Progress value) {
            scenarios::require(value.received >= latest.received && value.expected == bytes.size(), "Progress regressed"); latest = value;
        });
        scenarios::require(sink.flushed && sink.data == bytes && latest.received == bytes.size() && source.maxRequest == transferCapacity, "Streaming lost bytes or grew capacity");
        Sha256 standard; standard.append("a", 1); standard.append("bc", 2);
        scenarios::require(standard.finish() == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "Incremental SHA-256 vector failed");
    });
    runner.run("Short, excess and corrupt transfers never flush a usable package", [] {
        const auto original = fixture("correct"); std::atomic<bool> cancel{false};
        for (const auto& data : {std::string("short"), std::string("too long"), std::string("corrupt")}) {
            Bytes source(data); Sink sink; Sha256 digest;
            expect(data.size() == original.bytes ? "checksum_mismatch" : "size_mismatch", [&] { transfer(source, sink, digest, original, cancel); });
            scenarios::require(!sink.flushed, "Invalid package was committed");
        }
    });
    runner.run("Interrupted transfer and cancellation delete only their own partial", [] {
        const std::string bytes(3000, 's'); const auto artifact = fixture(bytes); std::atomic<bool> cancel{false};
        const auto parent = directory(); const auto unrelated = parent / L"unrelated.partial";
        { FileOutput file(unrelated); file.write("keep", 4); file.flush(); }
        Bytes broken(bytes); broken.failAt = 1000;
        expect("network_failed", [&] { download(artifact, parent, cancel, {}, &broken); });
        scenarios::require(!std::filesystem::exists(parent / (artifact.name + L".partial")) && std::filesystem::file_size(unrelated) == 4, "Partial cleanup crossed operation ownership");
        Bytes cancelled(bytes);
        expect("cancelled", [&] { download(artifact, parent, cancel, [&](Progress p) { if (p.received) cancel = true; }, &cancelled); });
        scenarios::require(!std::filesystem::exists(parent / artifact.name) && !std::filesystem::exists(parent / (artifact.name + L".partial")), "Cancelled package was retained");
        cancel = false; Bytes valid(bytes); const auto result = download(artifact, parent, cancel, {}, &valid);
        scenarios::require(fileDigest(result) == normalizedDigest(artifact.digest), "Flushed package mismatch");
        Bytes duplicate(bytes); expect("operation_conflict", [&] { download(artifact, parent, cancel, {}, &duplicate); });
        scenarios::require(fileDigest(result) == normalizedDigest(artifact.digest), "Conflict removed existing package");
    });
    runner.run("Storage failures and cancellation after the final byte prevent flush", [] {
        for (int failure = 0; failure < 3; ++failure) {
            Bytes source("payload"); Sink sink; Sha256 digest; std::atomic<bool> cancel{false};
            sink.failWrite = failure == 0; sink.failFlush = failure == 1;
            expect(failure == 2 ? "cancelled" : "storage_failed", [&] {
                transfer(source, sink, digest, fixture(source.data), cancel, [&](Progress p) { if (failure == 2 && p.received == p.expected) cancel = true; });
            });
            scenarios::require(!sink.flushed, "Failed transfer became usable");
        }
    });
    runner.run("Portable internal version, platform, binaries and safe archive paths", [] {
        auto valid = zipFixture(directory()); validatePackage(valid, forFile(valid));
        auto wrongVersion = zipFixture(directory(), "1.2.1"); expect("version_mismatch", [&] { validatePackage(wrongVersion, forFile(wrongVersion)); });
        auto wrongPlatform = zipFixture(directory(), "1.2.2", "arm64"); expect("architecture_mismatch", [&] { validatePackage(wrongPlatform, forFile(wrongPlatform)); });
        auto missing = zipFixture(directory(), "1.2.2", "x64", true); expect("package_incomplete", [&] { validatePackage(missing, forFile(missing)); });
        auto wrongPe = zipFixture(directory(), "1.2.2", "x64", false, true); expect("architecture_mismatch", [&] { validatePackage(wrongPe, forFile(wrongPe)); });
        auto escape = zipFixture(directory(), "1.2.2", "x64", false, false, true); expect("package_invalid", [&] { validatePackage(escape, forFile(escape)); });
    });
    runner.run("MSI inspection is read-only and rejects version or architecture mismatch", [] {
        const auto valid = directory() / L"LightHostModern-Setup.msi"; msiFixture(valid);
        const auto artifact = forFile(valid, Distribution::installed); validatePackage(valid, artifact);
        scenarios::require(fileDigest(valid) == normalizedDigest(artifact.digest), "MSI inspection modified database");
        const auto wrongVersion = directory() / L"LightHostModern-Setup.msi"; msiFixture(wrongVersion, L"1.2.1");
        expect("version_mismatch", [&] { validatePackage(wrongVersion, forFile(wrongVersion, Distribution::installed)); });
        const auto wrongArch = directory() / L"LightHostModern-Setup.msi"; msiFixture(wrongArch, L"1.2.2", L"Intel;1033");
        expect("architecture_mismatch", [&] { validatePackage(wrongArch, forFile(wrongArch, Distribution::installed)); });
    });
    runner.run("Installer requires arm plus both exits; timeouts and cancellation never install", [] {
        Apply normal; applyWhenReady(normal); scenarios::require(normal.installs == 1 && normal.time == 300 && normal.state == "completed", "Installer ran before all exits");
        for (int blocked = 0; blocked < 3; ++blocked) {
            Apply pending; if (blocked == 0) pending.armAt = UINT64_MAX; if (blocked == 1) pending.hostExit = UINT64_MAX; if (blocked == 2) pending.uiExit = UINT64_MAX;
            applyWhenReady(pending, 1000); scenarios::require(pending.installs == 0 && pending.state == "shutdown_timeout", "Missing condition allowed install");
        }
        Apply cancelled; cancelled.cancelAt = 300; applyWhenReady(cancelled);
        scenarios::require(cancelled.installs == 0 && cancelled.state == "cancelled", "Cancellation raced installation");
    });
    runner.run("Installer start failure, user cancellation, errors and restart results stay distinct", [] {
        for (auto code : {0u, 1602u, 1603u, 1641u, 3010u}) {
            Apply environment; environment.exitCode = code; applyWhenReady(environment);
            scenarios::require(environment.state == installerOutcome(code) && environment.reportedCode == code, "Installer result was lost");
            scenarios::require(installationCompleted(environment.state, environment.reportedCode)
                == (code == 0 || code == 3010 || code == 1641), "Only successful installation can change the restart location");
        }
        Apply failure; failure.failStart = true; applyWhenReady(failure);
        scenarios::require(failure.state == "installer_start_failed", "Start failure was hidden");
        scenarios::require(!installationCompleted(failure.state, failure.reportedCode), "A start failure with code zero must preserve the old executable path");
    });
    runner.run("Process creation identity and installation path prevent reuse or portable misclassification", [] {
        const auto process = processHandle(GetCurrentProcessId()); const auto created = processCreation(process.value);
        expect("process_mismatch", [&] { (void)processHandle(GetCurrentProcessId(), created + 1); });
        const auto path = processPath(process.value);
        scenarios::require(detectDistribution(path) == Distribution::portable, "Test executable was treated as an installed application");
        const auto parent = directory(); std::filesystem::create_directory(parent / L"elsewhere");
        scenarios::require(sameDirectory(parent / L"host.exe", parent.wstring()) && !sameDirectory(parent / L"host.exe", (parent / L"elsewhere").wstring()), "Installation location was approximated");
    });
    runner.run("MSI upgrades select a unique registered directory and preserve custom paths", [] {
        using namespace lightHostModern::installation;
        scenarios::require(uniqueRoot({}).empty(), "Fresh install must use the MSI default directory");
        const std::filesystem::path custom = LR"(D:\Audio Apps\LightHostModern)";
        scenarios::require(samePath(uniqueRoot({custom, LR"(d:\Audio Apps\LightHostModern\)"}), custom), "Equivalent MSI registrations changed the install directory");
        bool rejected = false;
        try { (void)uniqueRoot({custom, LR"(C:\Program Files\LightHostModern)"}); } catch (...) { rejected = true; }
        scenarios::require(rejected, "Ambiguous installation must not silently choose a different directory");
        for (const auto* invalid : {L"", L"C:\\", L"relative\\app", L"C:\\bad\"name"}) {
            rejected = false;
            try { (void)uniqueRoot({invalid}); } catch (...) { rejected = true; }
            scenarios::require(rejected, "Invalid MSI installation path accepted");
        }
    });
    runner.run("Update cache retains leased operations and bounds completed history", [] {
        const auto parent=directory();std::vector<std::filesystem::path> operations;
        Handle lease;
        for(int n=0;n<7;++n){const auto path=parent/(L"{"+std::wstring(juce::Uuid().toDashedString().toWideCharPointer())+L"}");std::filesystem::create_directory(path);markUpdateCache(path);operations.push_back(path);std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*(n+1)));}
        lease=updateCacheLease(operations.back());
        std::filesystem::last_write_time(operations.back(),std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*8));
        const auto unrelated=parent/L"user-files";std::filesystem::create_directory(unrelated);
        pruneUpdateCache(parent);
        scenarios::require(std::filesystem::exists(operations.back())&&std::filesystem::exists(unrelated),"Active operation or unowned directory deleted");
        scenarios::require(!std::filesystem::exists(operations[5]),"Old inactive cache not pruned");
        lease.reset();pruneUpdateCache(parent);scenarios::require(!std::filesystem::exists(operations.back()),"Expired released lease retained forever");
    });
    backgroundReleaseTests::run(runner);
    portableScenarios(runner);
    return runner.result();
}
