#include "PluginBusLayout.h"
#include "IsolatedPlugin.h"
#include "PluginWorkerProtocol.h"
#include "PluginInstances.h"
#include "DryDelay.h"
#include "RuntimeProfile.h"
#include "ScanProcess.h"
#include <condition_variable>
#include <thread>

namespace lightHostModern
{
namespace
{
juce::var object() { return juce::var(new juce::DynamicObject()); }
void put(juce::var& value, const char* key, const juce::var& field) { value.getDynamicObject()->setProperty(key, field); }
int boundedInteger(const juce::var& value, int minimum, int maximum) {
    if ((!value.isInt() && !value.isInt64()) || static_cast<juce::int64>(value) < minimum || static_cast<juce::int64>(value) > maximum)
        throw std::runtime_error("Invalid isolated plugin numeric metadata");
    return static_cast<int>(value);
}
int pipelineLatency(int plugin, int block, double rate) {
    audioLimits::latency(plugin, rate);
    if (block < 1 || block > (audioLimits::maximumLatency(rate) - plugin) / 2) throw std::length_error("Isolated latency exceeds ten seconds");
    return plugin + block * 2;
}
struct RemoteAudio
{
    RemoteAudio(int channels, int frames, double rate, int latency)
        : name("Local\\LightHostModernAudio-" + juce::Uuid().toString()),
          reservation(worker::mappingSize(channels, frames) + static_cast<size_t>(channels) * frames * sizeof(float) * 2 + worker::midiBytes * 5),
          mapping(name, channels, frames, true), input(channels, frames), output(channels, frames)
    {
        input.clear(); output.clear(); inMidi.ensureSize(worker::midiBytes); outMidi.ensureSize(worker::midiBytes); returnedMidi.ensureSize(worker::midiBytes); dryMidi.ensureSize(worker::midiBytes); nextDryMidi.ensureSize(worker::midiBytes);
        dry.prepare(channels, frames, latency, rate);
    }
    juce::String name;
    juce::var layout;
    audioLimits::Reservation reservation;
    worker::Mapping mapping;
    ipc::Handle signal{CreateEventW(nullptr, FALSE, FALSE, (name + "-ready").toWideCharPointer())};
    juce::AudioBuffer<float> input, output;
    juce::MidiBuffer inMidi, outMidi, returnedMidi, dryMidi, nextDryMidi;
    DryDelay dry;
    int offset = 0;
    LONG64 sequence = 1;
    bool wet = false;
    std::atomic<bool> configured{false}, ready{false};
};
}

struct IsolatedPluginSession::Impl
{
    Impl(const juce::PluginDescription& desc, juce::String state, double initialRate, int initialBlock,
         const juce::File& executable, bool fixture, const juce::var& buses)
        : description(desc), cached(std::move(state)), rate(initialRate), block(initialBlock), testFixture(fixture),
          workerFile(executable == juce::File{} ? juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("LightHostModernWorker.exe") : executable),
          directory((fixture ? workerFile.getParentDirectory().getChildFile("worker-test-runtime")
              : juce::File(juce::String((RuntimeProfile::current().test ? RuntimeProfile::current().directory
                   : std::filesystem::path(RuntimeProfile::environment(L"LOCALAPPDATA")) / L"LightHostModern").wstring().c_str())).getChildFile("worker-runtime"))
              .getChildFile(juce::Uuid().toString()))
    {
        initialBuses = buses;
        thread = std::thread([this] { run(); });
    }
    ~Impl() { stop.signal(); wake.notify_all(); if (thread.joinable()) thread.join(); }
    void fail(const juce::String& error)
    {
        { const std::lock_guard<std::mutex> lock(mutex); errorText = error; }
        failed.store(true); initialized.store(false);
    }
    juce::var exchange(HANDLE pipe, const juce::var& command, DWORD deadline = 15000, bool acceptCaptureError = false)
    {
        const auto started = GetTickCount64();
        const auto action = command["action"].toString().toStdString();
        if (action != "ping") verbose::log("worker.control.start", "pid=" + std::to_string(pid.load()) + " action=" + action);
        ipc::PipeIo io(pipe, stop.get(), deadline); std::string response;
        const auto write = io.write(juce::JSON::toString(command, true).toStdString());
        const auto read = write == ERROR_SUCCESS ? io.read(response) : write;
        if (read != ERROR_SUCCESS) {
            verbose::log("worker.control.failed", "pid=" + std::to_string(pid.load()) + " action=" + action + " windowsError=" + std::to_string(read));
            throw std::runtime_error("Isolated plugin stopped responding during " + action + ". Use Retry to start it again.");
        }
        auto reply = parseBoundedJson(juce::String::fromUTF8(response.data(), static_cast<int>(response.size())));
        if (!reply.isObject() || boundedInteger(reply["protocol"], worker::protocol, worker::protocol) != worker::protocol) throw std::runtime_error("Invalid isolated plugin response");
        if (command["action"].toString() != "ping") verbose::log("worker.control", "pid=" + std::to_string(pid.load())
            + " action=" + command["action"].toString().toStdString() + " milliseconds=" + std::to_string(GetTickCount64() - started)
            + " error=" + reply["error"].toString().toStdString());
        if (!acceptCaptureError && reply["error"].toString().isNotEmpty()) throw std::runtime_error(reply["error"].toString().toStdString());
        return reply;
    }
    void run()
    {
        try {
            if (!workerFile.existsAsFile() || directory.createDirectory().failed()) throw std::runtime_error("Isolated plugin worker is unavailable");
            ipc::Handle job(CreateJobObjectW(nullptr, nullptr));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Cannot contain isolated plugin process");
            const auto pipeName = "\\\\.\\pipe\\LightHostModernWorker-" + juce::Uuid().toString();
            ipc::Handle pipe(CreateNamedPipeW(pipeName.toWideCharPointer(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr));
            if (!pipe) throw std::runtime_error("Cannot create isolated plugin transport");
            auto command = scan::quoteArgument(std::wstring(workerFile.getFullPathName().toWideCharPointer())) + L" --pipe " + scan::quoteArgument(std::wstring(pipeName.toWideCharPointer()))
                + L" --directory " + scan::quoteArgument(std::wstring(directory.getFullPathName().toWideCharPointer())) + (testFixture ? L" --test-fixture" : L"");
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
            if (!CreateProcessW(workerFile.getFullPathName().toWideCharPointer(), command.data(), nullptr, nullptr, FALSE,
                CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, workerFile.getParentDirectory().getFullPathName().toWideCharPointer(), &startup, &process))
                throw std::runtime_error("Cannot launch isolated plugin worker");
            ipc::Handle child(process.hProcess), childThread(process.hThread);
            if (!AssignProcessToJobObject(job.get(), child.get())) { TerminateProcess(child.get(), 1); throw std::runtime_error("Cannot assign isolated plugin process to its job"); }
            pid.store(process.dwProcessId);
            if (ResumeThread(childThread.get()) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot start isolated plugin worker");
            if (ipc::PipeIo(pipe.get(), stop.get(), 15000).connect() != ERROR_SUCCESS) throw std::runtime_error("Isolated plugin startup timed out");
            ULONG connected = 0;
            if (!GetNamedPipeClientProcessId(pipe.get(), &connected) || connected != process.dwProcessId) throw std::runtime_error("Unexpected isolated plugin transport peer");
            auto request = object(); put(request, "action", "create"); put(request, "description", description.createXml()->toString());
            put(request, "rate", rate); put(request, "block", block);
            if (initialBuses.isObject()) put(request, "busLayout", initialBuses);
            if (cached.isNotEmpty()) {
                juce::MemoryBlock state; if (!decodePluginState(cached, state)) throw std::runtime_error("Invalid isolated plugin state");
                if (!directory.getChildFile("restore.bin").replaceWithData(state.getData(), state.getSize())) throw std::runtime_error("Cannot stage isolated plugin state");
                put(request, "stateBytes", static_cast<juce::int64>(state.getSize()));
            }
            auto created = exchange(pipe.get(), request);
            { const std::lock_guard<std::mutex> lock(mutex); metadata = created; }
            initialized.store(true);
            uint64_t completedCapture = 0, completedConfig = 0;
            ULONGLONG heartbeat = 0, lastProgressTime = GetTickCount64(); LONG64 lastProgress = 0;
            while (WaitForSingleObject(stop.get(), 0) != WAIT_OBJECT_0) {
                if (WaitForSingleObject(child.get(), 0) == WAIT_OBJECT_0) throw std::runtime_error("Isolated plugin process exited. Use Retry to start it again.");
                std::shared_ptr<RemoteAudio> next; uint64_t config = 0, capture = 0; size_t budget = 0; double nextRate = 0; bool editor = false;
                { const std::lock_guard<std::mutex> lock(mutex); next = audio; config = configRevision; capture = requestedCapture; budget = captureBudget; nextRate = rate; editor = std::exchange(editorRequested, false); }
                juce::var change;
                { const std::lock_guard<std::mutex> lock(mutex); change = requestedBuses; requestedBuses = juce::var{}; }
                if (change.isObject()) {
                    auto reply = exchange(pipe.get(), change);
                    { const std::lock_guard<std::mutex> lock(mutex); metadata = reply; busChangePending = false; ++layoutRevision; }
                    if (next) next->ready.store(false);
                    completedConfig = config; // The old proxy must not prepare buffers for the new layout.
                }
                if (next && completedConfig != config) {
                    request = object(); put(request, "action", "prepare"); put(request, "mapping", next->name); put(request, "rate", nextRate);
                    put(request, "channels", next->input.getNumChannels()); put(request, "block", next->input.getNumSamples()); put(request, "layout", next->layout);
                    auto reply = exchange(pipe.get(), request);
                    if (reply["layoutChanged"].isBool() && static_cast<bool>(reply["layoutChanged"])) {
                        const std::lock_guard<std::mutex> lock(mutex); metadata = reply; ++layoutRevision; completedConfig = config;
                        next->ready.store(false); continue;
                    }
                    const int latency = boundedInteger(reply["latency"], 0, audioLimits::maximumLatency(nextRate));
                    InterlockedExchange(&next->mapping.header().latency, latency);
                    next->configured.store(true); completedConfig = config; lastProgressTime = GetTickCount64();
                }
                if (editor) { request = object(); put(request, "action", "editor"); exchange(pipe.get(), request, 5000); }
                if (capture != completedCapture) {
                    const size_t allowed = budget < 12 ? 0 : juce::jmin(maximumPluginStateBytes, (budget - 12) * 6 / 8);
                    request = object(); put(request, "action", "capture"); put(request, "maxBytes", static_cast<juce::int64>(allowed)); auto reply = exchange(pipe.get(), request, 15000, true);
                    if (reply["error"].toString().isNotEmpty()) {
                        const std::lock_guard<std::mutex> lock(mutex); captureError = reply["error"].toString();
                        captured = {capture, 0, {}, captureError}; finishedCapture = capture; completedCapture = capture; continue;
                    }
                    const auto file = directory.getChildFile("capture.bin");
                    const auto size = boundedInteger(reply["bytes"], 0, static_cast<int>(allowed));
                    if (size < 0 || static_cast<uint64_t>(size) > allowed || file.getSize() != size)
                        throw std::runtime_error("Invalid isolated plugin state size");
                    juce::MemoryBlock binary;
                    if (!file.loadFileAsData(binary) || binary.getSize() != static_cast<size_t>(size)
                        || juce::SHA256(binary).toHexString() != reply["sha256"].toString()) throw std::runtime_error("Invalid isolated plugin state data");
                    auto encoded = binary.toBase64Encoding();
                    const auto covered = readRevision(reply["capturedRevision"]);
                    const auto observed = readRevision(reply["stateRevision"]);
                    if (covered > observed) throw std::runtime_error("Invalid isolated capture revision");
                    const auto captureMs = static_cast<double>(reply["captureMs"]), persistMs = static_cast<double>(reply["persistMs"]);
                    if (!std::isfinite(captureMs) || !std::isfinite(persistMs) || captureMs < 0 || persistMs < 0)
                        throw std::runtime_error("Invalid isolated capture timing");
                    stateRevision.store(observed);
                    { const std::lock_guard<std::mutex> lock(mutex); cached = std::move(encoded); captureError.clear();
                      captured = {capture, covered, cached, {}, captureMs, persistMs}; finishedCapture = capture; }
                    completedCapture = capture;
                }
                const auto now = GetTickCount64();
                if (next && next->ready.load()) {
                    auto& header = next->mapping.header(); const auto progress = InterlockedCompareExchange64(&header.processed, 0, 0);
                    bool busy = false; for (auto& item : header.blocks) busy |= worker::read(item.owner) == worker::processing;
                    if (!busy || progress != lastProgress) { lastProgressTime = now; lastProgress = progress; }
                    if (worker::read(header.fault)) throw std::runtime_error("Isolated plugin returned invalid audio or MIDI");
                    if (now - lastProgressTime > 2000) throw std::runtime_error("Isolated plugin audio processing stalled. Use Retry to start it again.");
                }
                if (now - heartbeat >= 1000) {
                    request = object(); put(request, "action", "ping"); const auto pong = exchange(pipe.get(), request, 3000); heartbeat = now;
                    stateRevision.store(readRevision(pong["stateRevision"]));
                    if (pong["layoutChanged"].isBool() && static_cast<bool>(pong["layoutChanged"])) {
                        const std::lock_guard<std::mutex> lock(mutex); metadata = pong; ++layoutRevision;
                        if (next) next->ready.store(false);
                    }
                    const auto memory = processMemory(child.get()); resident.store(memory.resident.value_or(0)); committed.store(memory.committed.value_or(0));
                    cpuTicks.store(processCpuTicks(child.get()).value_or(0));
                    cpuPercent.store(cpuSampler.sample(cpuTicks.load(), now, processorCount()).value_or(-1));
                }
                std::unique_lock<std::mutex> lock(mutex); wake.wait_for(lock, std::chrono::milliseconds(100));
            }
            // Closing the job terminates this worker and all of its descendants.
        } catch (const std::exception& e) { if (WaitForSingleObject(stop.get(), 0) != WAIT_OBJECT_0) fail(juce::String::fromUTF8(e.what())); }
        catch (...) { fail("Isolated plugin worker failed"); }
        pid.store(0); resident.store(0); committed.store(0);
        // Never follow a plugin-created directory junction during cleanup.
        const auto path = std::wstring(directory.getFullPathName().toWideCharPointer());
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            for (const auto* name : {L"restore.bin", L"capture.bin"}) DeleteFileW((path + L"\\" + name).c_str());
            RemoveDirectoryW(path.c_str());
        }
    }
    juce::PluginDescription description;
    static uint64_t readRevision(const juce::var& value) {
        if ((!value.isInt() && !value.isInt64()) || static_cast<juce::int64>(value) < 0)
            throw std::runtime_error("Invalid isolated state revision");
        return static_cast<uint64_t>(static_cast<juce::int64>(value));
    }
    uint64_t observedStateRevision() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return juce::jmax(stateRevision.load(), audio ? static_cast<uint64_t>(worker::read(audio->mapping.header().stateRevision)) : uint64_t(0));
    }
    mutable std::mutex mutex;
    std::condition_variable wake;
    juce::String cached, errorText, captureError;
    juce::var metadata, initialBuses, requestedBuses;
    bool busChangePending = false;
    std::atomic<uint64_t> layoutRevision{0};
    double rate;
    int block;
    bool testFixture, editorRequested = false;
    juce::File workerFile, directory;
    ipc::StopEvent stop;
    std::shared_ptr<RemoteAudio> audio;
    uint64_t configRevision = 0, requestedCapture = 0, finishedCapture = 0;
    CaptureResult captured;
    size_t captureBudget = maximumSessionStateBytes;
    std::atomic<bool> initialized{false}, failed{false};
    std::atomic<uint64_t> underruns{0}, resident{0}, committed{0}, cpuTicks{0};
    std::atomic<DWORD> pid{0};
    std::atomic<uint64_t> stateRevision{0};
    CpuUsageSampler cpuSampler;
    std::atomic<double> cpuPercent{-1};
    Proxy* proxy = nullptr; // message thread only
    std::thread thread;
};

class IsolatedPluginSession::Proxy final : public juce::AudioPluginInstance
{
public:
    static BusesProperties buses(const juce::var& metadata)
    {
        BusesProperties result;
        for (bool input : {true, false}) {
            const auto value = metadata[input ? "inputs" : "outputs"]; const auto* items = value.getArray();
            if (!items || items->size() > 256) throw std::runtime_error("Invalid isolated bus inventory");
            int total = 0;
            for (const auto& bus : *items) {
                if (!bus.isObject() || !bus["enabled"].isBool() || !bus["name"].isString()) throw std::runtime_error("Invalid isolated bus metadata");
                juce::AudioChannelSet types;
                if (!pluginBuses::readChannels(bus["lastTypes"], types)) throw std::runtime_error("Invalid isolated channel types");
                const int channels = types.size(); const bool enabled = static_cast<bool>(bus["enabled"]);
                if (channels < 0 || channels > 256 || (total += enabled ? channels : 0) > 256) throw std::runtime_error("Invalid isolated bus layout");
                const auto name = bus["name"].toString().substring(0, 128);
                result = input ? result.withInput(name, types, enabled)
                               : result.withOutput(name, types, enabled);
            }
        }
        return result;
    }
    Proxy(std::shared_ptr<Impl> state, juce::var metadata, uint64_t revision)
        : AudioPluginInstance(buses(metadata)), owner(std::move(state)), meta(std::move(metadata)) { owner->proxy = this; metadataRevision = revision; }
    ~Proxy() override { if (owner->proxy == this) owner->proxy = nullptr; }
    uint64_t metadataRevision = 0;
    void fillInPluginDescription(juce::PluginDescription& result) const override { result = owner->description; }
    const juce::String getName() const override { return owner->description.name; }
    bool acceptsMidi() const override { return static_cast<bool>(meta["acceptsMidi"]); }
    bool producesMidi() const override { return static_cast<bool>(meta["producesMidi"]); }
    bool isMidiEffect() const override { return static_cast<bool>(meta["midiEffect"]); }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return false; } juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock& result) override
    { const std::lock_guard<std::mutex> lock(owner->mutex); if (!decodePluginState(owner->cached, result)) throw std::runtime_error("Invalid isolated state cache"); }
    void setStateInformation(const void*, int) override { throw std::runtime_error("Isolated state restore requires a fresh worker"); }
    void prepareToPlay(double rate, int frames) override
    {
        audioLimits::format(juce::jmax(1, juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels())), frames, rate);
        // Tiny device callbacks may arrive in bursts. Accumulate at least 2 ms
        // (rounded up to a power of two) and retain an extra completed block,
        // so the worker has scheduling headroom without waiting in the host.
        frames = juce::jmax(frames, juce::nextPowerOfTwo(static_cast<int>(std::ceil(rate * .002))));
        pluginLatency = boundedInteger(meta["latency"], 0, audioLimits::maximumLatency(rate));
        const int totalLatency = pipelineLatency(pluginLatency, frames, rate);
        auto next = std::make_shared<RemoteAudio>(juce::jmax(1, juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels())), frames, rate, totalLatency);
        next->layout = pluginBuses::encode(getBusesLayout());
        current = next; sampleRate = rate; setLatencySamples(totalLatency);
        { const std::lock_guard<std::mutex> lock(owner->mutex); owner->rate = rate; owner->block = frames; owner->audio = std::move(next); ++owner->configRevision; }
        owner->wake.notify_all();
    }
    void releaseResources() override { if (current) current->ready.store(false); }
    bool poll()
    {
        if (!current || !current->configured.load()) return false;
        const auto revision = owner->observedStateRevision();
        if (revision != lastStateRevision) { lastStateRevision = revision; updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true)); }
        const int reported = worker::read(current->mapping.header().latency);
        if (reported != pluginLatency) {
            try {
                const auto latency = pipelineLatency(reported, current->input.getNumSamples(), sampleRate);
                // Engine calls this with its callback suspended. No live buffers
                // are replaced by the controller or the worker process.
                current->dry.prepare(current->input.getNumChannels(), current->input.getNumSamples(), latency, sampleRate);
                pluginLatency = reported; setLatencySamples(latency); updateHostDisplay(ChangeDetails().withLatencyChanged(true));
            } catch (const std::exception& e) { owner->fail(e.what()); }
        }
        const bool first = !current->ready.exchange(true); return first;
    }
    bool needsPoll() const { return current && current->configured.load() && (!current->ready.load() || worker::read(current->mapping.header().latency) != pluginLatency || lastStateRevision != owner->observedStateRevision()); }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        if (!current) { audio.clear(); midi.clear(); return; }
        auto& r = *current; auto& header = r.mapping.header(); const int frames = r.input.getNumSamples(), channels = r.input.getNumChannels();
        r.dry.capture(audio); r.returnedMidi.clear();
        int consumed = 0;
        while (consumed < audio.getNumSamples()) {
            if (r.offset == 0) {
                r.wet = false; r.outMidi.clear();
                for (int i = 0; i < worker::slots; ++i) {
                    auto& b = header.blocks[static_cast<size_t>(i)];
                    if (InterlockedCompareExchange(&b.owner, worker::reading, worker::complete) != worker::complete) continue;
                    if (b.sequence > r.sequence - 2 && b.sequence < r.sequence) {
                        InterlockedExchange(&b.owner, worker::complete);
                        continue; // Completed early: retain it until its declared output time.
                    }
                    if (b.sequence == r.sequence - 2 && r.sequence > 2 && b.frames == frames && b.channels == channels
                        && r.ready.load() && !owner->failed.load() && !worker::read(header.fault)) {
                        const auto* source = worker::audio(header, i, channels, frames); bool valid = worker::unpackMidi(b, r.outMidi, worker::midiBytes);
                        for (int c = 0; c < channels; ++c) { r.output.copyFrom(c, 0, source + static_cast<size_t>(c) * frames, frames); }
                        if (audioLimits::sanitize(r.output)) valid = false;
                        r.wet = valid; if (!valid) InterlockedExchange(&header.fault, 1);
                    }
                    InterlockedExchange(&b.owner, worker::empty);
                }
                if (!r.wet && r.sequence > 2 && r.ready.load()) owner->underruns.fetch_add(1, std::memory_order_relaxed);
                r.inMidi.clear();
            }
            const int count = juce::jmin(frames - r.offset, audio.getNumSamples() - consumed);
            copyBoundedMidi(r.inMidi, midi, consumed, count, r.offset - consumed, worker::midiBytes);
            if (r.wet) copyBoundedMidi(r.returnedMidi, r.outMidi, r.offset, count, consumed - r.offset, worker::midiBytes);
            else if (getTotalNumInputChannels() > 0) copyBoundedMidi(r.returnedMidi, r.dryMidi, r.offset, count, consumed - r.offset, worker::midiBytes);
            for (int c = 0; c < audio.getNumChannels(); ++c) {
                if (c < r.input.getNumChannels()) r.input.copyFrom(c, r.offset, audio, c, consumed, count);
                if (r.wet && c < r.output.getNumChannels()) audio.copyFrom(c, consumed, r.output, c, r.offset, count);
                else if (c < getTotalNumInputChannels()) audio.copyFrom(c, consumed, r.dry.output(), c, consumed, count);
                else audio.clear(c, consumed, count);
            }
            r.offset += count; consumed += count;
            if (r.offset == frames) {
                if (r.ready.load() && !owner->failed.load()) for (int i = 0; i < worker::slots; ++i) {
                    auto& b = header.blocks[static_cast<size_t>(i)];
                    if (InterlockedCompareExchange(&b.owner, worker::writing, worker::empty) != worker::empty) continue;
                    b.frames = frames; b.channels = channels; b.sequence = r.sequence;
                    InterlockedAdd64(&header.droppedMidi, static_cast<LONG64>(worker::packMidi(b, r.inMidi)));
                    auto* destination = worker::audio(header, i, channels, frames);
                    for (int c = 0; c < channels; ++c) std::memcpy(destination + static_cast<size_t>(c) * frames, r.input.getReadPointer(c), static_cast<size_t>(frames) * sizeof(float));
                    InterlockedExchange(&b.owner, worker::pending); SetEvent(r.signal.get()); break;
                }
                r.dryMidi.swapWith(r.nextDryMidi);
                r.nextDryMidi.clear(); copyBoundedMidi(r.nextDryMidi, r.inMidi, 0, -1, 0, worker::midiBytes);
                ++r.sequence; r.offset = 0;
            }
        }
        midi.clear(); copyBoundedMidi(midi, r.returnedMidi, 0, -1, 0, midi.data.getAllocatedCapacity());
    }
private:
    std::shared_ptr<Impl> owner;
    juce::var meta;
    std::shared_ptr<RemoteAudio> current;
    double sampleRate = 48000;
    int pluginLatency = 0;
    uint64_t lastStateRevision = 0;
};

IsolatedPluginSession::IsolatedPluginSession(const juce::PluginDescription& description, const juce::String& state, double rate, int block,
    const juce::File& executable, bool fixture, const juce::var& buses) : impl(std::make_shared<Impl>(description, state, rate, block, executable, fixture, buses)) {}
IsolatedPluginSession::~IsolatedPluginSession() = default;
juce::var IsolatedPluginSession::busInventory() const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->metadata.clone(); }
bool IsolatedPluginSession::layoutChanged() const { return impl->proxy && impl->proxy->metadataRevision != impl->layoutRevision.load(); }
bool IsolatedPluginSession::configureBuses(const juce::var& layout, const juce::var& previous) {
    const std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->busChangePending || !impl->initialized.load()) return false;
    auto request = object(); put(request, "action", "buses"); put(request, "layout", layout); put(request, "previousLayout", previous);
    impl->requestedBuses = request; impl->busChangePending = true; impl->wake.notify_all(); return true;
}
bool IsolatedPluginSession::initialized() const { return impl->initialized.load(); }
juce::String IsolatedPluginSession::failure() const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->errorText; }
std::unique_ptr<juce::AudioPluginInstance> IsolatedPluginSession::createProxy()
{
    if (!initialized()) return {};
    juce::var metadata; uint64_t revision; { const std::lock_guard<std::mutex> lock(impl->mutex); metadata = impl->metadata; revision = impl->layoutRevision.load(); }
    return std::make_unique<Proxy>(impl, std::move(metadata), revision);
}
void IsolatedPluginSession::showEditor() { { const std::lock_guard<std::mutex> lock(impl->mutex); impl->editorRequested = true; } impl->wake.notify_all(); }
uint64_t IsolatedPluginSession::requestCapture(size_t budget) { const std::lock_guard<std::mutex> lock(impl->mutex); impl->captureBudget = juce::jmin(budget, maximumSessionStateBytes); const auto ticket = ++impl->requestedCapture; impl->wake.notify_all(); return ticket; }
bool IsolatedPluginSession::captureFinished(uint64_t ticket) const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->finishedCapture >= ticket || impl->failed.load(); }
IsolatedPluginSession::CaptureResult IsolatedPluginSession::captureResult() const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->captured; }
uint64_t IsolatedPluginSession::stateRevision() const { return impl->observedStateRevision(); }
juce::String IsolatedPluginSession::captureFailure() const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->captureError; }
juce::String IsolatedPluginSession::capturedState() const { const std::lock_guard<std::mutex> lock(impl->mutex); return impl->cached; }
bool IsolatedPluginSession::poll() { return impl->proxy && impl->proxy->poll(); }
bool IsolatedPluginSession::needsPoll() const { return impl->proxy && impl->proxy->needsPoll(); }
bool IsolatedPluginSession::isProxy(const juce::AudioPluginInstance* value) { return dynamic_cast<const Proxy*>(value) != nullptr; }
juce::var IsolatedPluginSession::diagnostics() const
{
    auto result = object(); put(result, "pid", static_cast<juce::int64>(impl->pid.load())); put(result, "underruns", static_cast<juce::int64>(impl->underruns.load()));
    put(result, "residentBytes", static_cast<juce::int64>(impl->resident.load())); put(result, "committedBytes", static_cast<juce::int64>(impl->committed.load()));
    put(result, "cpuTicks", static_cast<juce::int64>(impl->cpuTicks.load())); put(result, "error", failure());
    const auto cpu = impl->cpuPercent.load(); put(result, "cpuPercent", cpu < 0 ? juce::var() : juce::var(cpu));
    put(result, "latencySamples", impl->proxy ? impl->proxy->getLatencySamples() : 0);
    { const std::lock_guard<std::mutex> lock(impl->mutex);
      put(result, "blockSize", impl->audio ? impl->audio->input.getNumSamples() : 0);
      put(result, "droppedMidi", impl->audio ? static_cast<juce::int64>(InterlockedCompareExchange64(&impl->audio->mapping.header().droppedMidi, 0, 0)) : 0);
      put(result, "processedBlocks", impl->audio ? static_cast<juce::int64>(worker::read(impl->audio->mapping.header().processed)) : 0);
      put(result, "captureMs", impl->captured.captureMs); put(result, "capturePersistMs", impl->captured.persistMs);
      put(result, "captureError", impl->captureError); }
    return result;
}
}
