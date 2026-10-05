#include "PluginScanController.h"
#include "ScanProcess.h"
#include "ScannerProtocol.h"
#include "RuntimeProfile.h"
#include "PluginInstances.h"
#include "PluginIdentity.h"
#include "ScanCacheJournal.h"
#include <algorithm>

using namespace juce;
namespace
{
File defaultCacheDirectory()
{
    const auto& profile = lightHostModern::RuntimeProfile::current();
    return profile.test ? File((profile.directory / L"Cache" / L"Plugins").wstring().c_str())
        : File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("LightHostModern/Cache/Plugins");
}
struct ScanFiles
{
    File folder, request, response;
    int batches = 0;
    ScanFiles()
    {
        const auto& profile = lightHostModern::RuntimeProfile::current();
        const auto root = profile.test ? File((profile.directory / L"Temp").wstring().c_str())
            : File::getSpecialLocation(File::tempDirectory);
        folder = root.getChildFile("LightHostModernScan-" + Uuid().toString());
        request = folder.getChildFile("request.xml");
        response = folder.getChildFile("response.xml");
    }
    ~ScanFiles()
    {
        request.deleteFile(); response.deleteFile();
        for(const auto& file:folder.findChildFiles(File::findFiles,false,"response.xml.batch-*.xml"))file.deleteFile();
        folder.deleteFile(); // Never recursively remove worker-created paths.
    }
};
String workerFailure(lightHostModern::scan::Result result)
{
    using Exit = lightHostModern::scan::Exit;
    if (result.outcome == Exit::timeout) return "timeout";
    if (result.outcome == Exit::totalTimeout) return "total_timeout";
    if (result.outcome == Exit::launchFailed) return "launch_failed";
    if (result.outcome == Exit::cancelled) return "cancelled";
    if (result.outcome == Exit::success) return {};
    if (result.code == 7) return "changed";
    if (result.code == 8) return "missing";
    if (result.code == 9) return "metadata_unavailable";
    if (result.code == 10) return "incompatible_architecture";
    if (result.code == 3) return "protocol_mismatch";
    if (result.code == 4) return "format_unavailable";
    if (result.code == 5) return "write_failed";
    return "crash";
}
std::wstring workerArguments(const ScanFiles& files)
{
    return lightHostModern::scan::quoteArgument(files.request.getFullPathName().toWideCharPointer()) + L" "
        + lightHostModern::scan::quoteArgument(files.response.getFullPathName().toWideCharPointer());
}
}

PluginScanController::PluginScanController(File executable, unsigned timeout, File cache)
    : scannerExecutable(executable == File() ? File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("LightHostModernScanner.exe") : executable),
      cacheDirectory(cache == File() ? defaultCacheDirectory() : cache), timeoutMs(timeout), worker([this] { run(); }) {}
PluginScanController::~PluginScanController()
{
    stopping.store(true);
    cancel(); wake.notify_all();
    if (worker.joinable()) worker.join();
}

void PluginScanController::enqueue(FileSearchPath paths, String format, Array<PluginDescription> known, bool force, bool optional)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (progress.scanId.isEmpty()) progress.scanId = Uuid().toString();
    progress.cancelled = false;
    queue.push_back({std::move(paths), std::move(format), std::move(known), force, generation.load(), 1,optional});
    progress.active = true; ++progress.revision;
    wake.notify_one();
}
void PluginScanController::cancel()
{
    std::lock_guard<std::mutex> lock(mutex);
    ++generation;
    progress.cancelled = true; queue.clear(); progress.active = working; ++progress.revision;
    wake.notify_all();
}
bool PluginScanController::begin()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (working || !queue.empty()) return false;
    ++generation;
    progress = {}; progress.scanId = Uuid().toString(); progress.revision = 1;
    seenModules.clear();
    countedModules.clear();completedModules.clear();cachedModules.clear();acceptedClasses.clear();
    return true;
}
PluginScanController::Status PluginScanController::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    Status snapshot;
    snapshot.active = progress.active; snapshot.cancelled = progress.cancelled;
    snapshot.completed = progress.completed; snapshot.total = progress.total; snapshot.cached = progress.cached;
    snapshot.currentFile = progress.currentFile; snapshot.scanId = progress.scanId; snapshot.revision = progress.revision;
    snapshot.enumerations = progress.enumerations; snapshot.examined = progress.examined;
    snapshot.recognized=(int)acceptedClasses.size();snapshot.ignored=progress.ignored;snapshot.enumerating=progress.enumerating;
    snapshot.incomplete=progress.cancelled;
    for (const auto& failure : progress.failures) if (!failure.resolved)
    {
        ++snapshot.failureCount;
        if(failure.kind=="enumeration")snapshot.incomplete=true;
        if (snapshot.failures.size() < 100) snapshot.failures.push_back(failure);
    }
    return snapshot;
}
PluginScanController::FailurePage PluginScanController::failures(const String& id, uint64_t revision, size_t offset, size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex);
    FailurePage page;
    page.stale = id != progress.scanId || revision != progress.revision;
    size_t index = 0;
    size_t bytes = 0;
    bool full = false;
    for (const auto& failure : progress.failures) if (!failure.resolved)
    {
        ++page.total;
        if (!page.stale && index++ >= offset && !full && page.failures.size() < jmin(size_t(100), limit))
        {
            // Conservative JSON escaping allowance keeps every page below the
            // command/event message limit even for long Unicode paths.
            const auto size = 512 + 6 * (failure.path.getNumBytesAsUTF8() + failure.reason.getNumBytesAsUTF8() + failure.format.getNumBytesAsUTF8());
            if (!page.failures.empty() && bytes + size > 3 * 1024 * 1024) full = true;
            else { page.failures.push_back(failure); bytes += size; }
        }
    }
    return page;
}
String PluginScanController::metadata(const String& id) const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = pluginMetadata.find(id);
    return found == pluginMetadata.end() ? String() : found->second;
}
std::vector<PluginDescription> PluginScanController::takeResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PluginDescription> batch; batch.swap(results); return batch;
}
bool PluginScanController::retryFailures(const StringArray& ids)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (working || !queue.empty()) return false;
    for (const auto& id : ids)
        if (std::none_of(progress.failures.begin(), progress.failures.end(), [&](const auto& failure) { return failure.id == id && !failure.resolved; })) return false;
    std::map<std::pair<String, int>, FileSearchPath> groups;
    for (const auto& failure : progress.failures)
        if (!failure.resolved && (ids.isEmpty() || ids.contains(failure.id))) groups[{failure.format, failure.attempt + 1}].add(File(failure.path));
    if (groups.empty()) return false;
    ++generation; seenModules.clear();
    // Cached successes are retained. Failed classes are retried from partial module records.
    for (auto& group : groups) queue.push_back({std::move(group.second), group.first.first, {}, false, generation.load(), group.first.second});
    progress.cancelled = false; progress.active = true; ++progress.revision;
    wake.notify_one(); return true;
}
void PluginScanController::addFailure(const Work& work, const String& path, const String& reason, const String& kind)
{
    lightHostModern::verbose::log("scan.failure","path="+path.toStdString()+" format="+work.format.toStdString()+" stage="+kind.toStdString()+" attempt="+std::to_string(work.attempt)+" reason="+reason.toStdString());
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& failure : progress.failures)
        if (failure.path == path && failure.format == work.format && failure.kind == kind)
        {
            failure.reason = reason; failure.attempt = work.attempt; failure.resolved = false; ++progress.revision; return;
        }
    progress.failures.push_back({path, work.format, reason, Uuid().toString(), kind, work.attempt, false});
    ++progress.revision;
}
void PluginScanController::run()
{
    // Restore persisted metadata on this worker, never on a snapshot/UI call.
    for (const auto& file : cacheDirectory.findChildFiles(File::findFiles, false, "*.xml"))
    {
        if (stopping.load()) return;
        const auto cached = lightHostModern::scan::parseScannerXml(file);
        if (!cached || cached->getIntAttribute("cacheVersion") != lightHostModern::scan::metadataCacheVersion) continue;
        for (const auto* item : cached->getChildIterator())
            if (item->hasTagName("ENTRY") && item->getStringAttribute("verifiedMetadata") == "verified")
            {
                const auto id = item->getStringAttribute("knownId");
                if (id.length() != 64 || !id.containsOnly("0123456789abcdef")) continue;
                std::lock_guard<std::mutex> lock(mutex); pluginMetadata[id] = item->toString();
            }
    }
    for (;;)
    {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [this] { return stopping.load() || !queue.empty(); });
        if (stopping.load()) return;
        auto work = std::move(queue.front()); queue.pop_front(); working = true;
        lock.unlock();
        try { scan(work); }
        catch (...) { addFailure(work, work.paths.toString(), "internal_error", "enumeration"); }
        lock.lock(); working = false; progress.enumerating=false; progress.active = !queue.empty(); progress.currentFile.clear(); ++progress.revision;
    }
}

void PluginScanController::scan(const Work& work)
{
    using namespace lightHostModern::scan;
    StageTiming scanTiming("scan", "format=" + work.format.toStdString());
    std::multimap<int, const PluginDescription*> knownByClass;
    std::set<String> knownIds;
    for (const auto& known : work.known) {
        knownByClass.emplace(known.uniqueId != 0 ? known.uniqueId : known.deprecatedUid, &known);
        knownIds.insert(lightHostModern::knownPluginId(known));
    }
    const auto cancelled = [&] { return stopping.load() || generation.load() != work.generation; };
    struct Candidate { String path, fingerprint; Time stamp; };
    std::deque<Candidate> candidates;
    std::mutex candidateMutex;
    std::condition_variable candidateReady;
    std::atomic<bool> enumerationDone{false}, pipelineStop{false};
    int currentRootIndex = -1;
    uint64_t enumerationProgress=0;
    ScanFiles enumeration;
    XmlElement request("SCAN");
    const auto enumerationId = Uuid().toString();
    request.setAttribute("version", scannerProtocolVersion); request.setAttribute("id", enumerationId);
    request.setAttribute("mode", "enumerate"); request.setAttribute("format", work.format);
    request.setAttribute("attempt",work.attempt);request.setAttribute("generation",String(work.generation));
    request.setAttribute("logRoot",String(lightHostModern::verbose::root().wstring().c_str()));
    for (int index = 0; index < work.paths.getNumPaths(); ++index)
    {
        auto* root = request.createNewChildElement("ROOT");
        root->setAttribute("path", work.paths[index].getFullPathName()); root->setAttribute("index", index);
        root->setAttribute("optional",work.optional);
    }
    if (enumeration.folder.createDirectory().failed() || !request.writeTo(enumeration.request))
    { addFailure(work, work.paths.toString(), "write_failed", "enumeration"); return; }
    { std::lock_guard<std::mutex> lock(mutex); ++progress.enumerations; progress.enumerating=true; progress.currentFile = work.paths.toString(); ++progress.revision; }
    const auto consume = [&] {
        for (;;)
        {
            const auto file = batchFile(enumeration.response, enumeration.batches);
            if (!file.existsAsFile()) break; // Only the local per-operation directory is read by the host.
            auto batch = parseScannerXml(file);
            if (!batch || !batch->hasTagName("BATCH") || batch->getStringAttribute("id") != enumerationId
                || batch->getIntAttribute("version") != scannerProtocolVersion || batch->getIntAttribute("sequence") != enumeration.batches
                || batch->getNumChildElements() > batchItems) throw std::runtime_error("invalid_enumeration_result");
            for (const auto* item : batch->getChildIterator())
            {
                const auto path = item->getStringAttribute("path");
                ++enumerationProgress;
                if(item->hasTagName("PROGRESS"))continue;
                if(item->hasTagName("IGNORED")){std::lock_guard<std::mutex> lock(mutex);++progress.ignored;continue;}
                if(item->hasTagName("ROOT_DONE")){
                    const auto index=item->getIntAttribute("index",-1);
                    if(index<0||index>=work.paths.getNumPaths()||path!=work.paths[index].getFullPathName())throw std::runtime_error("invalid_root_completion");
                    std::lock_guard<std::mutex> lock(mutex);
                    for(auto& failure:progress.failures)if(failure.kind=="enumeration"&&(failure.path==path||File(failure.path).isAChildOf(File(path)))&&failure.format==work.format&&failure.attempt<work.attempt)failure.resolved=true;
                    ++progress.revision;continue;
                }
                if (item->hasTagName("ROOT"))
                {
                    const int index = item->getIntAttribute("index", -1);
                    if (index <= currentRootIndex || index >= work.paths.getNumPaths() || work.paths[index].getFullPathName() != path)
                        throw std::runtime_error("invalid_root_progress");
                    currentRootIndex = index;
                    std::lock_guard<std::mutex> lock(mutex); progress.currentFile = path; ++progress.revision;
                    continue;
                }
                if (item->hasTagName("FAILURE")) { addFailure(work, path, item->getStringAttribute("reason"), "enumeration"); continue; }
                if (!item->hasTagName("CANDIDATE") || !File::isAbsolutePath(path)
                    || !File(path).hasFileExtension(work.format == "VST3" ? "vst3" : "dll")
                    || item->getStringAttribute("fingerprint").length() != 64) throw std::runtime_error("invalid_candidate");
                const auto key = work.format + "\n" + item->getStringAttribute("canonicalPath", path).toLowerCase() + "\n" + item->getStringAttribute("fingerprint");
                bool accepted = false;
                { std::lock_guard<std::mutex> lock(mutex); accepted = seenModules.insert(key).second; if (accepted) { countedModules.insert(work.format+"\n"+path.toLowerCase());progress.total=(int)countedModules.size(); ++progress.revision; } }
                if (accepted) {
                    std::unique_lock<std::mutex> lock(candidateMutex);
                    while(candidates.size() >= 128 && !cancelled() && !pipelineStop)
                        candidateReady.wait_for(lock,std::chrono::milliseconds(50));
                    if(cancelled() || pipelineStop)return;
                    candidates.push_back({path, item->getStringAttribute("fingerprint"), Time(item->getStringAttribute("stamp").getLargeIntValue())});
                    candidateReady.notify_all();
                }
            }
            file.deleteFile(); ++enumeration.batches;
        }
    };
    // Enumeration and validation overlap, with one isolated validator and a
    // bounded queue. Backpressure never increases host memory with library size.
    std::thread producer([&] {
        try {
    const auto enumerated = lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(), workerArguments(enumeration), [&] { return cancelled() || pipelineStop.load(); }, timeoutMs, consume,
        [&] { return enumerationProgress; });
    consume();
    {std::lock_guard<std::mutex> lock(mutex);progress.enumerating=false;}
    auto enumerationError = workerFailure(enumerated);
    if (enumerationError.isEmpty())
    {
        const auto final = parseScannerXml(enumeration.response);
        if (!final || !final->hasTagName("SCAN") || final->getStringAttribute("id") != enumerationId
            || final->getIntAttribute("version") != scannerProtocolVersion || final->getStringAttribute("mode") != "enumerate"
            || final->getIntAttribute("batches", -1) != enumeration.batches) enumerationError = "invalid_result";
    }
    if (enumerationError.isNotEmpty() && enumerationError != "cancelled")
    {
        if (currentRootIndex < 0)
            for (int index = 0; index < work.paths.getNumPaths(); ++index) addFailure(work, work.paths[index].getFullPathName(), enumerationError, "enumeration");
        else
        {
            addFailure(work, work.paths[currentRootIndex].getFullPathName(), enumerationError, "enumeration");
            // A blocked root must not prevent later roots from being evaluated.
            FileSearchPath remaining;
            for (int index = currentRootIndex + 1; index < work.paths.getNumPaths(); ++index) remaining.add(work.paths[index]);
            if (remaining.getNumPaths() > 0 && !cancelled())
            {
                std::lock_guard<std::mutex> lock(mutex);
                queue.push_front({remaining, work.format, work.known, work.force, work.generation, work.attempt, work.optional});
            }
        }
    }

        } catch(const std::exception& error) {
            if(!cancelled()&&!pipelineStop)addFailure(work,work.paths.toString(),error.what(),"enumeration");
        }
        enumerationDone=true;candidateReady.notify_all();
    });
    struct JoinEnumeration {
        std::thread& thread;std::atomic<bool>& stop;std::condition_variable& ready;
        ~JoinEnumeration(){stop=true;ready.notify_all();if(thread.joinable())thread.join();}
    } joinEnumeration{producer,pipelineStop,candidateReady};
    for (;;) {
        Candidate candidate;
        {
            std::unique_lock<std::mutex> lock(candidateMutex);
            candidateReady.wait_for(lock,std::chrono::milliseconds(50),[&]{return enumerationDone||!candidates.empty()||cancelled();});
            if(cancelled())break;
            if(candidates.empty()){if(enumerationDone)break;continue;}
            candidate=std::move(candidates.front());candidates.pop_front();candidateReady.notify_all();
        }
        if (cancelled()) break;
        StageTiming moduleTiming("module", "path=" + candidate.path.toStdString());
        { std::lock_guard<std::mutex> lock(mutex); progress.currentFile = candidate.path; ++progress.revision; }
        const auto moduleKey = work.format + "\n" + candidate.path.toLowerCase();
        const auto cacheFile = cacheDirectory.getChildFile(SHA256(moduleKey.toRawUTF8(), moduleKey.getNumBytesAsUTF8()).toHexString() + ".xml");
        CacheJournal checkpoint(cacheFile);
        auto response = !work.force ? CacheJournal::load(cacheFile) : nullptr;
        const auto validResponse = [&](const XmlElement* xml) {
            return xml && xml->hasTagName("SCAN") && xml->getIntAttribute("version") == scannerProtocolVersion
                && xml->getStringAttribute("mode") == "probe" && xml->getStringAttribute("path") == candidate.path
                && xml->getStringAttribute("format") == work.format && xml->getStringAttribute("fingerprint") == candidate.fingerprint;
        };
        const bool reusable = validResponse(response.get()) && response->getIntAttribute("cacheVersion") == metadataCacheVersion;
        const bool cached = reusable && response->getBoolAttribute("complete",true);
        lightHostModern::verbose::log("scan.cache","module="+candidate.path.toStdString()+" fingerprint="+candidate.fingerprint.toStdString()+" reuse="+(cached?"complete":reusable?"partial":"none")+" attempt="+std::to_string(work.attempt));
        auto previous=reusable?std::make_unique<XmlElement>(*response):nullptr;
        std::map<String, const XmlElement*> previousClasses;
        if (previous) for (const auto* saved : previous->getChildIterator())
            if (saved->getStringAttribute("verifiedMetadata") == "verified" && saved->getStringAttribute("error").isEmpty())
                previousClasses.emplace(saved->getStringAttribute("knownId"), saved);
        String error;
        if (!cached)
        {
            response.reset();
            ScanFiles files;
            XmlElement probe("SCAN");
            const auto id = Uuid().toString();
            probe.setAttribute("version", scannerProtocolVersion); probe.setAttribute("id", id); probe.setAttribute("mode", "probe");
            probe.setAttribute("path", candidate.path); probe.setAttribute("format", work.format); probe.setAttribute("fingerprint", candidate.fingerprint);
            probe.setAttribute("attempt",work.attempt);probe.setAttribute("generation",String(work.generation));
            probe.setAttribute("validateSingleClass", true);
            probe.setAttribute("logRoot",String(lightHostModern::verbose::root().wstring().c_str()));
            if (files.folder.createDirectory().failed() || !probe.writeTo(files.request)) error = "write_failed";
            else
            {
                { std::lock_guard<std::mutex> lock(mutex); ++progress.examined; ++progress.revision; }
                const auto result = lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(), workerArguments(files), cancelled, timeoutMs);
                error = workerFailure(result);
                lightHostModern::verbose::log("scan.worker","path="+candidate.path.toStdString()+" exit="+std::to_string(result.code)+" outcome="+error.toStdString());
                if (error == "cancelled") break;
                if (error.isEmpty())
                {
                    response = parseScannerXml(files.response);
                    if (!validResponse(response.get()) || response->getStringAttribute("id") != id) { error = "invalid_result"; response.reset(); }
                }
            }
        }
        if(response&&response->getBoolAttribute("catalog")&&!cached){
            bool fingerprintVerified=false;
            auto catalog=std::move(response);response=std::make_unique<XmlElement>(*catalog);response->deleteAllChildElements();response->setAttribute("catalog",false);
            bool checkpointReady = checkpoint.begin(previous ? *previous : *response);
            if (!checkpointReady) lightHostModern::verbose::log("scan.cache.failure", "checkpoint_begin_failed path=" + candidate.path.toStdString());
            for(const auto* entry:catalog->getChildIterator()){
                if(cancelled())break;
                const auto* description=entry->getChildByName("PLUGIN");if(!description)continue;
                const auto key=entry->getStringAttribute("knownId");
                const auto saved = previousClasses.find(key);
                if (saved != previousClasses.end()) { response->addChildElement(new XmlElement(*saved->second)); continue; }
                ScanFiles child;XmlElement job("SCAN");const auto id=Uuid().toString();
                job.setAttribute("version",scannerProtocolVersion);job.setAttribute("id",id);job.setAttribute("mode","class");job.setAttribute("path",candidate.path);job.setAttribute("format",work.format);job.setAttribute("fingerprint",candidate.fingerprint);
                job.setAttribute("verifyAtEnd",entry->getNextElement()==nullptr);
                job.setAttribute("logRoot",String(lightHostModern::verbose::root().wstring().c_str()));job.addChildElement(new XmlElement(*description));
                auto resultEntry=std::make_unique<XmlElement>(*entry);String classError;
                if(child.folder.createDirectory().failed()||!job.writeTo(child.request))classError="write_failed";
                else {
                    const auto result=lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(),workerArguments(child),cancelled,timeoutMs);
                    classError=workerFailure(result);
                    lightHostModern::verbose::log("scan.class","path="+candidate.path.toStdString()+" class="+key.toStdString()+" exit="+std::to_string(result.code)+" error="+classError.toStdString());
                    if(classError=="cancelled")break;
                    if(classError.isEmpty()){
                        auto answer=parseScannerXml(child.response);
                        if(answer&&validResponse(answer.get())&&answer->getStringAttribute("id")==id&&answer->getNumChildElements()==1
                            &&answer->getFirstChildElement()->getStringAttribute("knownId")==key){resultEntry=std::make_unique<XmlElement>(*answer->getFirstChildElement());fingerprintVerified=answer->getBoolAttribute("fingerprintVerified");}
                        else classError="invalid_result";
                    }
                }
                if(classError.isNotEmpty())resultEntry->setAttribute("error",classError);
                if (checkpointReady && !checkpoint.append(*resultEntry)) {
                    checkpointReady = false;
                    lightHostModern::verbose::log("scan.cache.failure", "checkpoint_write_failed path=" + candidate.path.toStdString());
                }
                response->addChildElement(resultEntry.release());
                response->setAttribute("cacheVersion",metadataCacheVersion);response->setAttribute("complete",false);
            }
            if(!cancelled()&&!fingerprintVerified){
                ScanFiles check;XmlElement job("SCAN");job.setAttribute("version",scannerProtocolVersion);job.setAttribute("mode","verify");job.setAttribute("format",work.format);job.setAttribute("path",candidate.path);job.setAttribute("fingerprint",candidate.fingerprint);
                if(check.folder.createDirectory().failed()||!job.writeTo(check.request))error="write_failed";
                else error=workerFailure(lightHostModern::scan::run(scannerExecutable.getFullPathName().toWideCharPointer(),workerArguments(check),cancelled,timeoutMs));
                if(error.isNotEmpty()){response.reset();checkpoint.invalidate();}
            }
        }
        if (cancelled()) break;
        std::vector<PluginDescription> validated;
        bool classFailures=false;
        std::map<String, String> metadata;
        if (response)
        {
            if(response->hasAttribute("error"))error=response->getStringAttribute("error");
            std::set<String> identities;
            for (const auto* entry : response->getChildIterator())
            {
                const auto* description = entry->getChildByName("PLUGIN");
                PluginDescription plugin;
                if (!entry->hasTagName("ENTRY") || !description || !plugin.loadFromXml(*description)
                    || plugin.name.isEmpty() || plugin.pluginFormatName != work.format
                    || !belongsToModule(plugin.fileOrIdentifier, candidate.path, work.format)
                    || plugin.numInputChannels < 0 || plugin.numOutputChannels < 0
                    || entry->getStringAttribute("knownId") != lightHostModern::knownPluginId(plugin)
                    || !identities.insert(lightHostModern::knownPluginId(plugin)).second)
                { error = "invalid_result"; continue; }
                if (entry->getStringAttribute("error").isNotEmpty()) { error = entry->getStringAttribute("error"); classFailures=true;addFailure(work,candidate.path,error,"class:"+entry->getStringAttribute("knownId"));continue; }
                if (entry->getStringAttribute("verifiedMetadata") != "verified") { error = "unverified_metadata"; continue; }
                bool busesValid = true;
                for (const auto* bus : entry->getChildIterator()) if (bus->hasTagName("BUS"))
                    if ((bus->getStringAttribute("direction") != "input" && bus->getStringAttribute("direction") != "output")
                        || bus->getIntAttribute("channels", -1) < 0 || bus->getIntAttribute("defaultChannels", -1) < 0) busesValid = false;
                if (!busesValid) { error = "invalid_buses"; continue; }
                plugin.lastFileModTime = candidate.stamp;
                const auto matches = knownByClass.equal_range(plugin.uniqueId != 0 ? plugin.uniqueId : plugin.deprecatedUid);
                for (auto match = matches.first; match != matches.second; ++match)
                    if (lightHostModern::samePluginClass(*match->second, plugin)) { plugin.fileOrIdentifier = match->second->fileOrIdentifier; break; }
                XmlElement retained(*entry);retained.setAttribute("knownId",lightHostModern::knownPluginId(plugin));
                retained.removeChildElement(retained.getChildByName("PLUGIN"),true);retained.addChildElement(plugin.createXml().release());
                metadata[lightHostModern::knownPluginId(plugin)] = retained.toString();
                validated.push_back(std::move(plugin));
            }
            if (validated.empty() && error.isEmpty()) error = "no_plugins";
        }
        if (response && !cached && cacheDirectory.createDirectory().wasOk())
        {
            response->setAttribute("cacheVersion", metadataCacheVersion);
            response->setAttribute("complete",error.isEmpty()&&!cancelled());
            if (!checkpoint.compact(*response))
                lightHostModern::verbose::log("scan.cache.failure", "checkpoint_compact_failed path=" + candidate.path.toStdString());
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            completedModules.insert(moduleKey);progress.completed=(int)completedModules.size();if(cached)cachedModules.insert(moduleKey);progress.cached=(int)cachedModules.size();++progress.revision;
            for(const auto& plugin:validated){const auto id=lightHostModern::knownPluginId(plugin);acceptedClasses.insert(id);for(auto& failure:progress.failures)if(failure.path==candidate.path&&failure.format==work.format&&failure.kind=="class:"+id)failure.resolved=true;}
            for (auto& item : metadata) pluginMetadata[item.first] = std::move(item.second);
            // Cache also restores entries removed from memory after a controller restart.
            if (!cached || std::any_of(validated.begin(), validated.end(), [&](const auto& plugin) {
                return knownIds.count(lightHostModern::knownPluginId(plugin)) == 0;
            })) results.insert(results.end(), validated.begin(), validated.end());
            if (error.isEmpty()) for (auto& failure : progress.failures)
                if (failure.path == candidate.path && failure.format == work.format) failure.resolved = true;
        }
        if (error.isNotEmpty()&&!classFailures) addFailure(work, candidate.path, error, "probe");
    }
}
