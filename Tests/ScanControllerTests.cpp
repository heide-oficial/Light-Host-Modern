#include "PluginScanController.h"
#include "ScanProcess.h"
#include "ScannerProtocol.h"
#include "PluginInstances.h"
#include "PluginIdentity.h"
#include "ScanArchitecture.h"
#include <shellapi.h>
#include <iostream>
#include <set>
#include <array>
#include <winioctl.h>
#include <aclapi.h>

using namespace juce;
static void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static void waitIdle(PluginScanController& controller, unsigned timeoutMs = 10000)
{
    const auto deadline = GetTickCount64() + timeoutMs;
    while (controller.status().active && GetTickCount64() < deadline) Sleep(10);
    require(!controller.status().active, "scan did not complete");
}

static bool createTestJunction(const File& link, const File& target)
{
    if (!link.isAChildOf(target) || link.createDirectory().failed()) return false;
    const std::wstring substitute = L"\\??\\" + std::wstring(target.getFullPathName().toWideCharPointer());
    const std::wstring print(target.getFullPathName().toWideCharPointer());
    struct Header { DWORD tag; WORD length, reserved, substituteOffset, substituteLength, printOffset, printLength; };
    const auto payloadBytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
    std::vector<uint8_t> data(sizeof(Header) + payloadBytes, 0);
    auto& header = *reinterpret_cast<Header*>(data.data());
    header.tag = IO_REPARSE_TAG_MOUNT_POINT;
    header.length = static_cast<WORD>(payloadBytes + 8);
    header.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    header.printOffset = static_cast<WORD>(header.substituteLength + sizeof(wchar_t));
    header.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    std::memcpy(data.data() + sizeof(Header), substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(data.data() + sizeof(Header) + header.printOffset, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    lightHostModern::ipc::Handle directory(CreateFileW(link.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    DWORD returned = 0;
    return directory && DeviceIoControl(directory.get(), FSCTL_SET_REPARSE_POINT, data.data(), static_cast<DWORD>(data.size()), nullptr, 0, &returned, nullptr);
}

// Opt-in integration mode. Only the scanner child loads modules; this process
// never creates audio devices, application properties or an active host chain.
static int testRealScan(const File& scanner, const File& modules, const String& format, int expected)
{
    try
    {
        const auto cache = modules.getSiblingFile("ScannerMetadataCache-" + format);
        PluginScanController controller(scanner, 60000, cache);
        require(controller.begin(), "begin real scan");
        controller.enqueue(FileSearchPath(modules.getFullPathName()), format, {}, true);
        waitIdle(controller, 300000);
        const auto status = controller.status();
        for (const auto& failure : status.failures)
            std::cerr << failure.path << ": " << failure.reason << '\n';
        require(status.failureCount == 0 && status.completed == expected, "real module discovery failed");
        const auto plugins = controller.takeResults();
        require((int) plugins.size() == expected, "unexpected real plugin count");
        Array<PluginDescription> restored;
        std::set<String> identities;
        for (const auto& plugin : plugins)
        {
            require(plugin.pluginFormatName == format && plugin.numInputChannels == 2
                && plugin.numOutputChannels == 2, "real format and stereo channel metadata");
            require(identities.insert(plugin.createIdentifierString()).second, "distinct real plugin identities");
            const auto xml = XmlDocument::parse(plugin.createXml()->toString());
            PluginDescription copy;
            require(xml && copy.loadFromXml(*xml), "real description XML round trip");
            require(copy.createIdentifierString() == plugin.createIdentifierString(), "stable cached identity");
            restored.add(copy);
            std::cout << plugin.name << " | " << plugin.manufacturerName << " | " << plugin.version
                      << " | " << plugin.createIdentifierString() << '\n';
        }
        // A fresh controller still enumerates in isolation, but cached modules
        // must not be instantiated again.
        PluginScanController cached(scanner, 60000, cache);
        require(cached.begin(), "begin restored real cache");
        cached.enqueue(FileSearchPath(modules.getFullPathName()), format, restored);
        waitIdle(cached);
        const auto cacheStatus = cached.status();
        require(cacheStatus.cached == expected && cacheStatus.failureCount == 0 && cacheStatus.examined == 0
            && cached.takeResults().empty(), "real cache must survive serialized restart without worker launch");
        std::cout << "PASS: " << expected << ' ' << format << " real modules and restored cache\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

int main()
{
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments && count == 6 && String(arguments[1]) == "--real-scan")
    {
        const File scanner { String(arguments[2]) }, modules { String(arguments[3]) };
        const String format(arguments[4]);
        const auto expected = String(arguments[5]).getIntValue();
        LocalFree(arguments);
        return testRealScan(scanner, modules, format, expected);
    }
    if (count == 3)
    {
        auto request = XmlDocument::parse(File(String(arguments[1])));
        const File output { String(arguments[2]) };
        LocalFree(arguments);
        if (!request) return 2;
        if (request->getStringAttribute("mode") == "enumerate") return lightHostModern::scan::enumerate(*request, output, [](const String& root) {
            if (root.contains("enum-hang")) Sleep(INFINITE);
            return root.contains("denied-fixture") ? String("access_denied") : String();
        });
        const auto path = request->getStringAttribute("path");
        const auto mode=request->getStringAttribute("mode");
        if(mode=="verify")return 0;
        int onlyClass=-1;
        if(path.contains("isolated")&&mode=="class") {
            const auto* plugin=request->getChildByName("PLUGIN");if(!plugin)return 3;
            onlyClass=plugin->getStringAttribute("uniqueId").getHexValue32()-1;
            const auto calls=File(path+".calls"+String(onlyClass));
            calls.replaceWithText(String(calls.loadFileAsString().getIntValue()+1));
            if(!File(path+".allow").existsAsFile()) {
                if(onlyClass==1){TerminateProcess(GetCurrentProcess(),44);return 44;}
                if(onlyClass==2)Sleep(INFINITE);
            }
        }
        if (path.contains("hang")) { Sleep(INFINITE); return 3; }
        if (path.contains("crash")) { TerminateProcess(GetCurrentProcess(), 44); return 44; }
        XmlElement response("SCAN");
        response.setAttribute("version", lightHostModern::scan::scannerProtocolVersion);
        response.setAttribute("mode", "probe");
        response.setAttribute("fingerprint", request->getStringAttribute("fingerprint"));
        response.setAttribute("id", path.contains("bad") ? "incorrect" : request->getStringAttribute("id"));
        response.setAttribute("path", path);
        const auto format = request->getStringAttribute("format");
        response.setAttribute("format", format);
        if(path.contains("isolated")&&mode!="class")response.setAttribute("catalog",true);
        for (int i = 0; i < (path.contains("isolated")?4:path.contains("multi") ? 2 : 1); ++i)
        {
            if(onlyClass>=0&&i!=onlyClass)continue;
            PluginDescription plugin;
            plugin.name = "Simulated plugin " + String(i);
            plugin.pluginFormatName = format;
            plugin.fileOrIdentifier = path;
            if (format == "VST3")
                plugin.fileOrIdentifier = File(path).getChildFile("Contents/x86_64-win/module.vst3").getFullPathName();
            if (path.contains("outside"))
                plugin.fileOrIdentifier = File(path).getSiblingFile("test-bundle.vst3/Contents/x86_64-win/module.vst3").getFullPathName();
            plugin.uniqueId = i + 1;
            plugin.deprecatedUid = i + 1;
            plugin.numInputChannels = plugin.numOutputChannels = 2;
            auto* entry = response.createNewChildElement("ENTRY");
            entry->setAttribute("knownId", lightHostModern::knownPluginId(plugin));
            entry->setAttribute("verifiedMetadata", "verified");
            entry->setAttribute("declaredMetadata", "unavailable");
            if (path.contains("partial") && i == 1) entry->setAttribute("error", "identity_changed");
            entry->addChildElement(plugin.createXml().release());
            for (const auto* direction : {"input", "output"})
            {
                auto* bus = entry->createNewChildElement("BUS");
                bus->setAttribute("direction", direction); bus->setAttribute("name", "Main");
                bus->setAttribute("channels", 2); bus->setAttribute("defaultChannels", 2);
                bus->setAttribute("main", true); bus->setAttribute("enabled", true); bus->setAttribute("layout", "Stereo");
            }
        }
        return response.writeTo(output) ? 0 : 4;
    }
    if (arguments) LocalFree(arguments);
    const auto root = File::getSpecialLocation(File::tempDirectory).getChildFile("LightHostModernScanTest-" + Uuid().toString());
    const StringArray names { "a-multi.dll", "b-one.dll", "c-bad.dll", "d-crash.dll", "z-hang.dll" };
    struct Cleanup {
        File root; StringArray names;
        ~Cleanup() {
            for (const auto& name : names) root.getChildFile(name).deleteFile();
            for (const auto& bundle : { "ignored.lv2", "ignored.vst3" }) {
                root.getChildFile(bundle).getChildFile("internal.dll").deleteFile();
                root.getChildFile(bundle).deleteFile();
            }
            root.getChildFile("test-bundle.vst3/Contents/x86_64-win/module.vst3").deleteFile();
            root.getChildFile("test-bundle.vst3/Contents/x86_64-win").deleteFile();
            root.getChildFile("test-bundle.vst3/Contents").deleteFile();
            root.getChildFile("test-bundle.vst3").deleteFile();
            root.getChildFile("outside.vst3").deleteFile();
            const auto cache = root.getChildFile("Cache");
            for (const auto& file : cache.findChildFiles(File::findFiles, false, "*.xml")) file.deleteFile();
            cache.deleteFile();
            root.deleteFile();
        }
    } cleanup { root, names };
    try
    {
        require(root.createDirectory().wasOk(), "test directory");
        {
            const auto pe=root.getChildFile("architecture-fixture.dll");
            MemoryOutputStream image;image.writeShort(IMAGE_DOS_SIGNATURE);image.writeRepeatedByte(0,0x3c-2);image.writeInt(0x40);image.writeInt(IMAGE_NT_SIGNATURE);image.writeShort(IMAGE_FILE_MACHINE_I386);
            require(pe.replaceWithData(image.getData(),image.getDataSize()),"x86 fixture");
            require(lightHostModern::scan::architectureError(pe)=="incompatible_architecture","PE architecture must be detected without relying on directory names");
            pe.deleteFile();
        }
        {
            PluginDescription declared,actual;
            declared.pluginFormatName=actual.pluginFormatName="VST3";declared.uniqueId=actual.uniqueId=17;
            declared.fileOrIdentifier=root.getChildFile("identity.vst3").getFullPathName();
            actual.fileOrIdentifier=File(declared.fileOrIdentifier).getChildFile("Contents/x86_64-win/identity.vst3").getFullPathName();
            declared.vst3ClassId=actual.vst3ClassId="0123456789abcdef0123456789abcdef";
            require(lightHostModern::samePluginClass(declared,actual),"same class bundle and inner binary");
            actual.vst3ClassId="0123456789abcdef0123456789abcdee";
            require(!lightHostModern::samePluginClass(declared,actual),"CID mismatch cannot pass through a matching 32-bit hash");
            actual.vst3ClassId=declared.vst3ClassId;actual.fileOrIdentifier=root.getChildFile("other.vst3").getFullPathName();
            require(!lightHostModern::samePluginClass(declared,actual),"another module must not compare equal");
        }
        for (const auto& name : names) require(root.getChildFile(name).replaceWithText("simulated module"), "test module");
        for (const auto& bundle : { "ignored.lv2", "ignored.vst3" }) {
            require(root.getChildFile(bundle).createDirectory().wasOk(), "foreign format bundle");
            require(root.getChildFile(bundle).getChildFile("internal.dll").replaceWithText("not a VST2 module"), "foreign format DLL");
        }
        const auto cacheDirectory=root.getChildFile("Cache"); require(cacheDirectory.createDirectory().wasOk(),"Create scanner cache fixture");
        require(cacheDirectory.getChildFile("invalid-restart.xml").replaceWithText(String::repeatedString("<a>",100000)+String::repeatedString("</a>",100000)),"Write deep cache before scanner restart");
        PluginScanController controller(File::getSpecialLocation(File::currentExecutableFile), 1000, cacheDirectory);
        require(controller.begin(), "begin batch");
        controller.enqueue(FileSearchPath(root.getFullPathName()), "VST", {}, true);
        waitIdle(controller);
        const auto status = controller.status();
        require(status.completed == 5 && status.failures.size() == 3, "per-file crash/hang/malformed errors");
        auto plugins = controller.takeResults();
        require(plugins.size() == 3, "validate multiple plugins from a single module");
        const auto busMetadata = XmlDocument::parse(controller.metadata(lightHostModern::knownPluginId(plugins.front())));
        require(busMetadata && busMetadata->getChildByName("BUS") && busMetadata->getStringAttribute("verifiedMetadata") == "verified",
            "verified bus metadata unavailable");
        Array<PluginDescription> known;
        for (const auto& plugin : plugins) known.add(plugin);
        require(controller.begin(), "begin cache batch");
        controller.enqueue(FileSearchPath(root.getChildFile("a-multi.dll").getFullPathName()), "VST", known);
        waitIdle(controller);
        require(controller.status().cached == 1 && controller.status().failures.empty(), "incremental known-list cache");
        const auto changedModule = root.getChildFile("a-multi.dll");
        const auto unchangedStamp = changedModule.getLastModificationTime();
        require(changedModule.replaceWithText("modified  module") && changedModule.setLastModificationTime(unchangedStamp), "changed-content fixture");
        require(controller.begin(), "begin same-size same-time changed module");
        controller.enqueue(FileSearchPath(changedModule.getFullPathName()), "VST", known);
        waitIdle(controller);
        require(controller.status().cached == 0 && controller.status().examined == 1 && controller.takeResults().size() == 2,
            "module content change reused stale metadata despite identical size and timestamp");
        require(controller.begin(), "begin cancellation batch");
        controller.enqueue(FileSearchPath(root.getFullPathName()), "VST", {}, true);
        const auto deadline = GetTickCount64() + 8000;
        while (!controller.status().currentFile.contains("z-hang") && GetTickCount64() < deadline) Sleep(10);
        controller.cancel();
        waitIdle(controller);
        require(controller.status().cancelled && controller.takeResults().size() == 3, "cancellation preserves validated results");
        controller.retryFailures();
        waitIdle(controller);
        require(controller.status().failures.size() == 2, "explicit retry retains individual failures");
        require(controller.begin(), "begin unavailable paths batch");
        FileSearchPath unavailable;
        for (int i = 0; i < 150; ++i)
            unavailable.add(root.getChildFile("missing-" + String(i)));
        // A non-module regular file cannot be traversed as a directory either.
        unavailable.add(root.getChildFile("b-one.dll"));
        controller.enqueue(unavailable, "VST3", {});
        waitIdle(controller);
        const auto unavailableStatus = controller.status();
        require(unavailableStatus.failureCount == 151 && unavailableStatus.failures.size() == 100,
            "unavailable roots must be reported with bounded status and complete failure count");
        const auto secondPage = controller.failures(unavailableStatus.scanId, unavailableStatus.revision, 100, 1000);
        require(!secondPage.stale && secondPage.total == 151 && secondPage.failures.size() == 51, "failure pagination omitted records");
        require(controller.failures(unavailableStatus.scanId, unavailableStatus.revision - 1, 0).stale, "stale failure revision accepted");
        std::set<String> failureIds;
        for (const auto& failure : unavailableStatus.failures) require(failureIds.insert(failure.id).second, "failure IDs repeated");
        for (const auto& failure : secondPage.failures) require(failureIds.insert(failure.id).second, "failure IDs repeated across pages");
        require(unavailableStatus.total == 0 && unavailableStatus.completed == 0,
            "enumeration failures must not pretend to scan modules");
        require(unavailableStatus.failures.front().reason == "missing", "missing paths must have a specific failure reason");
        controller.retryFailures();
        waitIdle(controller);
        require(controller.status().failureCount == 151, "retry must include failures beyond display snapshot");
        require(controller.retryFailures({secondPage.failures.back().id}), "individual failure retry rejected");
        waitIdle(controller);
        const auto retried = controller.status();
        const auto finalPage = controller.failures(retried.scanId, retried.revision, 100);
        require(finalPage.failures.back().attempt == 3 && finalPage.failures.front().attempt == 2, "selected retry changed unrelated attempts");
        require(controller.takeResults().empty(), "unavailable paths must not synthesize plugin descriptions");
        const auto bundle = root.getChildFile("test-bundle.vst3");
        const auto binary = bundle.getChildFile("Contents/x86_64-win/module.vst3");
        require(binary.getParentDirectory().createDirectory().wasOk() && binary.replaceWithText("test binary"), "VST3 fixture");
        require(controller.begin(), "begin inner-binary VST3 scan");
        controller.enqueue(FileSearchPath(bundle.getFullPathName()), "VST3", {}, true);
        waitIdle(controller);
        auto bundlePlugins = controller.takeResults();
        require(bundlePlugins.size() == 1 && controller.status().failureCount == 0,
            "VST3 descriptions may identify an existing inner bundle binary");
        require(bundlePlugins.front().fileOrIdentifier == binary.getFullPathName(),
            ("preserve JUCE original binary identifier: expected " + binary.getFullPathName() + " got " + bundlePlugins.front().fileOrIdentifier).toRawUTF8());
        require(controller.begin(), "begin VST3 bundle cache scan");
        controller.enqueue(FileSearchPath(bundle.getFullPathName()), "VST3", { bundlePlugins.front() });
        waitIdle(controller);
        require(controller.status().cached == 1, "match inner binary description to outer bundle cache");
        {
            const auto info = bundle.getChildFile("moduleinfo.json");
            struct CleanupInfo { File file; ~CleanupInfo() { file.deleteFile(); } } clean {info};
            require(info.replaceWithText("{\"Version\":\"new\"}") && controller.begin(), "changed static metadata fixture");
            controller.enqueue(FileSearchPath(bundle.getFullPathName()), "VST3", {bundlePlugins.front()});
            waitIdle(controller);
            require(controller.status().cached == 0 && controller.status().examined == 1 && controller.takeResults().size() == 1,
                "changed VST3 module metadata reused stale cache");
        }
        {
            const auto partial = root.getChildFile("partial-multi.dll");
            struct CleanupPartial { File file; ~CleanupPartial() { file.deleteFile(); } } clean {partial};
            require(partial.replaceWithText("fixture") && controller.begin(), "partial class failure fixture");
            controller.enqueue(FileSearchPath(partial.getFullPathName()), "VST", {}, true);
            waitIdle(controller);
            require(controller.status().failureCount == 1 && controller.takeResults().size() == 1,
                "invalid class discarded a valid sibling class");
        }
        const auto outside = root.getChildFile("outside.vst3");
        require(outside.createDirectory().wasOk() && controller.begin(), "begin mismatched bundle scan");
        controller.enqueue(FileSearchPath(outside.getFullPathName()), "VST3", {}, true);
        waitIdle(controller);
        require(controller.status().failureCount == 1 && controller.takeResults().empty(),
            "reject a worker description pointing to another existing bundle");
        require(controller.begin(), "begin isolated enumeration fault scenario");
        FileSearchPath faultRoots;
        faultRoots.add(root.getChildFile("a-multi.dll"));
        faultRoots.add(root.getChildFile("enum-hang"));
        faultRoots.add(root.getChildFile("denied-fixture"));
        faultRoots.add(root.getChildFile("b-one.dll"));
        controller.enqueue(faultRoots, "VST", {}, true);
        waitIdle(controller);
        const auto faultStatus = controller.status();
        require(faultStatus.completed == 2 && faultStatus.enumerations == 2 && faultStatus.failureCount == 2 && controller.takeResults().size() == 3,
            "blocked enumeration lost prior candidates or prevented subsequent roots");
        require(faultStatus.failures[0].reason == "timeout" && faultStatus.failures[1].reason == "access_denied", "enumeration failures were not distinguished");
        {
            // A nonexistent share on this machine exercises the actual UNC
            // filesystem path without contacting another machine or changing
            // network configuration. A blocked redirector remains job-bounded.
            const File missingShare(String("\\\\localhost\\LightHostModernScanAbsent-") + Uuid().toString());
            require(controller.begin(), "begin unavailable UNC scenario");
            FileSearchPath paths; paths.add(root.getChildFile("a-multi.dll")); paths.add(missingShare);
            paths.add(root.getChildFile("b-one.dll"));
            const auto started = GetTickCount64();
            controller.enqueue(paths, "VST", {}, true);
            waitIdle(controller);
            const auto state = controller.status();
            require(GetTickCount64() - started < 7000 && state.failureCount == 1 && state.completed == 2
                && controller.takeResults().size() == 3, "unavailable UNC lost local results or exceeded the worker deadline");
            const auto& failure = state.failures.front();
            require(failure.path == missingShare.getFullPathName() && (failure.reason == "network_unavailable"
                || failure.reason == "missing" || failure.reason == "access_denied" || failure.reason == "timeout"),
                "unavailable UNC did not retain a specific failure reason");
            std::cout << "Actual unavailable UNC: " << failure.reason << '\n';
        }
        {
            // Real NTFS denial on a directory created by this test. Save and
            // restore its DACL even when an assertion fails; no user path or
            // parent ACL is changed, and no privilege/elevation is requested.
            const auto directory = root.getChildFile("acl-unreadable");
            require(directory.isAChildOf(root) && directory.createDirectory().wasOk(), "private ACL fixture");
            struct CleanupDirectory { File value; ~CleanupDirectory() { value.deleteFile(); } } directoryCleanup {directory};
            std::wstring path(directory.getFullPathName().toWideCharPointer());
            PACL previous = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
            require(GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                &previous, nullptr, &descriptor) == ERROR_SUCCESS, "read fixture ACL");
            const std::unique_ptr<void, decltype(&LocalFree)> saved(descriptor, &LocalFree);
            std::array<BYTE, SECURITY_MAX_SID_SIZE> everyone{};
            DWORD sidSize = static_cast<DWORD>(everyone.size());
            require(CreateWellKnownSid(WinWorldSid, nullptr, everyone.data(), &sidSize) != FALSE, "create fixture trustee");
            EXPLICIT_ACCESSW denied{};
            denied.grfAccessPermissions = FILE_LIST_DIRECTORY; denied.grfAccessMode = DENY_ACCESS;
            denied.grfInheritance = NO_INHERITANCE; denied.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            denied.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
            denied.Trustee.ptstrName = reinterpret_cast<LPWSTR>(everyone.data());
            PACL replacement = nullptr;
            require(SetEntriesInAclW(1, &denied, previous, &replacement) == ERROR_SUCCESS, "prepare fixture denial");
            const std::unique_ptr<ACL, decltype(&LocalFree)> allocated(replacement, &LocalFree);
            require(SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                replacement, nullptr) == ERROR_SUCCESS, "apply fixture denial");
            DWORD restoreError = ERROR_SUCCESS;
            {
                struct Restore {
                    std::wstring& path; PACL acl; DWORD& error;
                    ~Restore() { error = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, acl, nullptr); }
                } restore {path, previous, restoreError};
                std::error_code error;
                std::filesystem::directory_iterator iterator(std::filesystem::path(path), error);
                require(error && lightHostModern::scan::filesystemFailure(error) == "access_denied", "NTFS must actually reject enumeration");
                require(controller.begin(), "begin actual access-denied scan");
                FileSearchPath paths; paths.add(directory); paths.add(root.getChildFile("b-one.dll"));
                controller.enqueue(paths, "VST", {}, true);
                waitIdle(controller);
                const auto state = controller.status();
                require(state.failureCount == 1 && state.failures.front().reason == "access_denied"
                    && state.completed == 1 && controller.takeResults().size() == 1,
                    "real access denial lost the following allowed module or its reason");
            }
            require(restoreError == ERROR_SUCCESS, "restore private fixture ACL");
            std::error_code error;
            std::filesystem::directory_iterator readable(std::filesystem::path(path), error);
            require(!error, "fixture permissions were not restored");
            require(lightHostModern::scan::filesystemFailure(std::make_error_code(std::errc::io_error)) == "enumeration",
                "generic I/O error was mistaken for a Win32 access-denied code");
            require(lightHostModern::scan::filesystemFailure(std::error_code(ERROR_BAD_NETPATH, std::system_category())) == "network_unavailable",
                "Win32 network failure was not distinguished");
        }
        {
            const auto directory = root.getChildFile("junction-fixture");
            const auto link = directory.getChildFile("loop");
            const auto binaryFile = directory.getChildFile("plain.dll");
            struct CleanupJunction {
                File directory, link, binary;
                ~CleanupJunction() { RemoveDirectoryW(link.getFullPathName().toWideCharPointer()); binary.deleteFile(); directory.deleteFile(); }
            } clean {directory, link, binaryFile};
            require(directory.createDirectory().wasOk() && binaryFile.replaceWithText("fixture"), "junction fixture files");
            require(createTestJunction(link, directory), "could not create actual junction fixture");
            require(controller.begin(), "begin junction and overlap scenario");
            FileSearchPath overlap; overlap.add(directory); overlap.add(link); overlap.add(binaryFile);
            controller.enqueue(overlap, "VST", {}, true);
            waitIdle(controller);
            require(controller.status().completed == 1 && controller.status().failureCount == 0 && controller.takeResults().size() == 1,
                "junction caused recursion or overlapping roots examined a module more than once");
        }
        {
            const File extendedRoot(String("\\\\?\\") + root.getFullPathName());
            const auto first = extendedRoot.getChildFile(String::repeatedString("a", 100));
            const auto second = first.getChildFile(String::repeatedString("b", 100));
            const auto longModule = second.getChildFile("long.dll");
            struct CleanupLongPath {
                File first, second, module;
                ~CleanupLongPath() { module.deleteFile(); second.deleteFile(); first.deleteFile(); }
            } clean {first, second, longModule};
            require(longModule.getFullPathName().length() > 260 && second.createDirectory().wasOk() && longModule.replaceWithText("fixture"),
                "could not create actual long-path fixture");
            require(controller.begin(), "begin long-path enumeration");
            controller.enqueue(FileSearchPath(second.getFullPathName()), "VST", {}, true);
            waitIdle(controller);
            require(controller.status().completed == 1 && controller.status().failureCount == 0 && controller.takeResults().size() == 1,
                "long paths were omitted or failed enumeration");
        }
        {
            const auto directory=root.getChildFile("initially-missing");
            const auto module=directory.getChildFile("restored.dll");
            struct CleanupRestored {File directory,module;~CleanupRestored(){module.deleteFile();directory.deleteFile();}}clean{directory,module};
            require(controller.begin(),"begin missing root");controller.enqueue(FileSearchPath(directory.getFullPathName()),"VST",{});waitIdle(controller);
            require(controller.status().failureCount==1&&controller.status().incomplete,"missing root failure");
            require(directory.createDirectory().wasOk()&&module.replaceWithText("fixture"),"restore missing root");
            require(controller.retryFailures(),"retry restored root");waitIdle(controller);
            require(controller.status().failureCount==0&&!controller.status().incomplete&&controller.status().recognized==1,"completed root resolves its previous failure");
            controller.takeResults();
            require(controller.begin(),"begin optional root");controller.enqueue(FileSearchPath(root.getChildFile("absent-optional").getFullPathName()),"VST",{},false,true);waitIdle(controller);
            require(controller.status().failureCount==0&&controller.status().ignored==1,"optional missing directory is not a scan failure");
        }
        {
            const auto module=root.getChildFile("isolated.dll");
            struct CleanupIsolated {File module;~CleanupIsolated(){module.deleteFile();File(module.getFullPathName()+".allow").deleteFile();for(int i=0;i<4;++i)File(module.getFullPathName()+".calls"+String(i)).deleteFile();}}clean{module};
            require(module.replaceWithText("fixture")&&controller.begin(),"isolated class fixture");
            controller.enqueue(FileSearchPath(module.getFullPathName()),"VST",{},true);waitIdle(controller);
            const auto isolatedStatus=controller.status();const auto isolatedResults=controller.takeResults();
            if(isolatedStatus.failureCount!=2||isolatedResults.size()!=2){std::cerr<<"isolated failures="<<isolatedStatus.failureCount<<" results="<<isolatedResults.size()<<'\n';for(const auto& f:isolatedStatus.failures)std::cerr<<f.reason<<'\n';}
            require(isolatedStatus.failureCount==2&&isolatedResults.size()==2,"class crash and timeout preserve earlier and later successes");
            require(File(module.getFullPathName()+".allow").replaceWithText("ready")&&controller.retryFailures(),"retry only failed classes");waitIdle(controller);
            require(controller.status().failureCount==0&&controller.status().recognized==4,"failed classes recover without duplicate counters");
            for(int i:{0,3})require(File(module.getFullPathName()+".calls"+String(i)).loadFileAsString().getIntValue()==1,"successful classes were unnecessarily instantiated on retry");
        }
        std::cout << "Scanner controller regressions passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
