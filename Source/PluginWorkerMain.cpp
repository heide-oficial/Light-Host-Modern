#include "PluginBusLayout.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginWorkerProtocol.h"
#include "PluginInstances.h"
#include "PluginIdentity.h"
#include <shellapi.h>
#include <avrt.h>
#include <thread>
#include <mutex>
#if LIGHTHOST_WORKER_TEST_FIXTURES
#include "../Tests/WorkerFixture.h"
#endif

namespace
{
using namespace juce;
using namespace lightHostModern;
var object() { return var(new DynamicObject()); }
void put(var& value, const char* key, const var& field) { value.getDynamicObject()->setProperty(key, field); }
class AudioScheduling final
{
public:
    AudioScheduling()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        DWORD index = 0;
        task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        if (task) AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL);
    }
    ~AudioScheduling() { if (task) AvRevertMmThreadCharacteristics(task); }
    AudioScheduling(const AudioScheduling&) = delete;
    AudioScheduling& operator=(const AudioScheduling&) = delete;
private:
    HANDLE task = nullptr;
};
class EditorWindow final : public DocumentWindow
{
public:
    explicit EditorWindow(AudioPluginInstance& processor) : DocumentWindow(processor.getName(), Colours::darkgrey, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* content = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;
        if (!content) content = new GenericAudioProcessorEditor(processor);
        setContentOwned(content, true);
        centreWithSize(getWidth(), getHeight()); setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
class Worker final : private AudioProcessorListener
{
public:
    Worker(File root, bool fixture) : directory(std::move(root)), allowFixture(fixture) { juce::addDefaultFormatsToManager(formats); }
    ~Worker() { stopAudio(); editor.reset(); if (plugin) { plugin->removeListener(this); plugin->releaseResources(); } }
    var execute(const var& request)
    {
        auto result = object(); put(result, "protocol", worker::protocol);
        try {
            const auto action = request["action"].toString();
            if (action == "create") {
                PluginDescription description;
                const auto xml = parseBoundedXml(request["description"].toString());
                if (!xml || !description.loadFromXml(*xml)) throw std::runtime_error("Invalid plugin description");
                const auto rate = static_cast<double>(request["rate"]); const int block = static_cast<int>(request["block"]);
                audioLimits::format(1, block, rate);
                String error;
#if LIGHTHOST_WORKER_TEST_FIXTURES
                if (allowFixture && description.pluginFormatName == "LightHostTest") plugin = std::make_unique<WorkerFixture>(description);
                else
#endif
                    plugin = formats.createPluginInstance(description, rate, block, error);
                if (!plugin) throw std::runtime_error(error.isNotEmpty() ? error.toStdString() : "Cannot create isolated plugin");
                PluginDescription actual; plugin->fillInPluginDescription(actual);
                if (!samePluginClass(description, actual)) throw std::runtime_error("plugin_identity_mismatch");
                plugin->addListener(this);
                const auto stateBytes = static_cast<int64>(request["stateBytes"]);
                if (stateBytes < 0 || static_cast<uint64_t>(stateBytes) > maximumPluginStateBytes) throw std::runtime_error("Invalid restore state size");
                if (request.hasProperty("stateBytes")) {
                    const auto file = directory.getChildFile("restore.bin"); MemoryBlock state;
                    if (file.getSize() != stateBytes || !file.loadFileAsData(state) || state.getSize() != static_cast<size_t>(stateBytes)) throw std::runtime_error("Cannot read restore state");
                    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                }
                if (request["busLayout"].isObject()) {
                    AudioProcessor::BusesLayout layout;
                    if (!pluginBuses::decode(request["busLayout"], layout) || !plugin->setBusesLayout(layout)) throw std::runtime_error("Saved plugin channel configuration is no longer supported");
                }
                plugin->setRateAndBufferSizeDetails(rate, block); plugin->prepareToPlay(rate, block);
                preparedLayout = plugin->getBusesLayout();
                result = metadata();
            } else if (!plugin) throw std::runtime_error("Isolated plugin is not initialized");
            else if (action == "prepare") {
                stopAudio(); const std::lock_guard<std::mutex> lock(audioMutex);
                const auto rate = static_cast<double>(request["rate"]); const int frames = static_cast<int>(request["block"]), channels = static_cast<int>(request["channels"]);
                audioLimits::format(channels, frames, rate);
                const auto configurationChanged = [&] {
                    return channels != jmax(1, jmax(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()))
                        || JSON::toString(pluginBuses::encode(plugin->getBusesLayout()), true) != JSON::toString(request["layout"], true);
                };
                if (configurationChanged()) {
                    preparedLayout = plugin->getBusesLayout(); layoutPending.store(false);
                    result = metadata(); put(result, "layoutChanged", true); return result;
                }
                plugin->releaseResources(); plugin->setRateAndBufferSizeDetails(rate, frames); plugin->prepareToPlay(rate, frames);
                if (configurationChanged()) {
                    preparedLayout = plugin->getBusesLayout(); layoutPending.store(false);
                    result = metadata(); put(result, "layoutChanged", true); return result;
                }
                preparedLayout = plugin->getBusesLayout();
                audioLimits::latency(plugin->getLatencySamples(), rate);
                const auto name = request["mapping"].toString();
                if (!name.startsWith("Local\\LightHostModernAudio-") || name.length() > 128) throw std::runtime_error("Invalid audio mapping name");
                mapping = std::make_unique<worker::Mapping>(name, channels, frames, false);
                signal = std::make_unique<ipc::Handle>(OpenEventW(SYNCHRONIZE, FALSE, (name + "-ready").toWideCharPointer()));
                if (!*signal) throw std::runtime_error("Audio wake event unavailable");
                midi.ensureSize(worker::midiBytes); buffer.setSize(channels, frames);
                put(result, "latency", plugin->getLatencySamples()); audioStop.store(false); audioThread = std::thread([this] { process(); });
            } else if (action == "buses") {
                stopAudio(); const std::lock_guard<std::mutex> lock(audioMutex);
                plugin->releaseResources();
                const auto old = plugin->getBusesLayout(); AudioProcessor::BusesLayout requested;
                String error;
                if (JSON::toString(pluginBuses::encode(old), true) != JSON::toString(request["previousLayout"], true)) error = "The plugin channels changed. Reopen the channel settings.";
                else if (!pluginBuses::decode(request["layout"], requested) || !plugin->checkBusesLayoutSupported(requested)) error = "This plugin does not support that channel configuration.";
                else {
                    plugin->releaseResources();
                    if (!plugin->setBusesLayout(requested)) { plugin->setBusesLayout(old); error = "This plugin rejected the channel configuration."; }
                }
                preparedLayout = plugin->getBusesLayout(); result = metadata(); put(result, "layoutError", error);
            } else if (action == "ping") {
                if (layoutPending.load() || !pluginBuses::matches(*plugin, preparedLayout)) {
                    stopAudio(); const std::lock_guard<std::mutex> lock(audioMutex);
                    preparedLayout = plugin->getBusesLayout(); layoutPending.store(false);
                    result = metadata(); put(result, "layoutChanged", true);
                }
            } else if (action == "capture") {
                MemoryBlock state;
                uint64_t capturedRevision = 0;
                double captureMs = 0;
                {
                    const std::lock_guard<std::mutex> lock(audioMutex);
                    const auto captureStart = Time::getMillisecondCounterHiRes();
                    // A notification during getStateInformation is conservatively
                    // left pending; disk/hash must not acknowledge later edits.
                    capturedRevision = revision.load();
                    plugin->getStateInformation(state);
                    captureMs = Time::getMillisecondCounterHiRes() - captureStart;
                }
                const auto capturedAt = Time::getMillisecondCounterHiRes();
#if LIGHTHOST_WORKER_TEST_FIXTURES
                if (auto* fixture = dynamic_cast<WorkerFixture*>(plugin.get())) fixture->afterStateCapture(directory);
#endif
                const auto maximum = static_cast<int64>(request["maxBytes"]);
                if (maximum < 0 || static_cast<uint64_t>(maximum) > maximumPluginStateBytes || state.getSize() > static_cast<uint64_t>(maximum))
                    throw std::runtime_error("Isolated plugin state exceeds the available session budget; previous state retained");
                if (!directory.getChildFile("capture.bin").replaceWithData(state.getData(), state.getSize())) throw std::runtime_error("Cannot write captured state");
                put(result, "bytes", static_cast<int64>(state.getSize())); put(result, "sha256", SHA256(state).toHexString());
                put(result, "capturedRevision", static_cast<int64>(capturedRevision));
                put(result, "captureMs", captureMs);
                put(result, "persistMs", Time::getMillisecondCounterHiRes() - capturedAt);
            } else if (action == "editor") {
                if (!editor) editor = std::make_unique<EditorWindow>(*plugin);
                if (editor) { editor->setVisible(true); editor->setMinimised(false); editor->toFront(true); }
            } else if (action != "ping") throw std::runtime_error("Unknown isolated plugin command");
            put(result, "stateRevision", static_cast<int64>(revision.load()));
        } catch (const std::exception& e) { put(result, "error", String::fromUTF8(e.what())); }
        catch (...) { put(result, "error", "Plugin threw in isolated worker"); }
        return result;
    }
private:
    var metadata()
    {
        // Every metadata call runs with audio stopped, and the parent will
        // prepare a fresh mapping before resuming. VST3 support queries require
        // an inactive processor; active instances otherwise accept too much.
        plugin->releaseResources();
        auto result = pluginBuses::inventory(*plugin); put(result, "protocol", worker::protocol);
        for (bool input : {true, false}) {
            Array<var> buses; int channels = 0;
            for (int i = 0; i < plugin->getBusCount(input); ++i) {
                const auto* bus = plugin->getBus(input, i); auto value = result[input ? "inputs" : "outputs"][i];
                put(value, "name", bus->getName()); put(value, "channels", bus->getLastEnabledLayout().size()); put(value, "enabled", bus->isEnabled());
                channels += bus->getNumberOfChannels(); buses.add(value);
            }
            if (channels > 256) throw std::runtime_error("Plugin exceeds 256 audio channels");
            put(result, input ? "inputs" : "outputs", buses);
        }
        put(result, "latency", audioLimits::latency(plugin->getLatencySamples(), plugin->getSampleRate()));
        put(result, "acceptsMidi", plugin->acceptsMidi()); put(result, "producesMidi", plugin->producesMidi()); put(result, "midiEffect", plugin->isMidiEffect());
        return result;
    }
    void stopAudio() { audioStop.store(true); if (audioThread.joinable()) audioThread.join(); }
    void process()
    {
        // Register this thread, not the controller/editor or the entire process.
        // A normal high-priority thread can miss 64-sample ASIO deadlines even
        // when DSP is tiny. MMCSS cooperates with Windows' audio scheduling.
        AudioScheduling scheduling;
        auto& header = mapping->header(); const int channels = buffer.getNumChannels(), frames = buffer.getNumSamples();
        while (!audioStop.load()) {
            bool worked = false;
            for (int iteration = 0; iteration < worker::slots && !audioStop.load(); ++iteration) {
                int i = -1; LONG64 oldest = std::numeric_limits<LONG64>::max();
                for (int candidate = 0; candidate < worker::slots; ++candidate) {
                    auto& pending = header.blocks[static_cast<size_t>(candidate)];
                    if (worker::read(pending.owner) == worker::pending && pending.sequence < oldest) { i = candidate; oldest = pending.sequence; }
                }
                if (i < 0) break;
                auto& b = header.blocks[static_cast<size_t>(i)];
                if (InterlockedCompareExchange(&b.owner, worker::processing, worker::pending) != worker::pending) continue;
                worked = true;
                try {
                    const std::lock_guard<std::mutex> lock(audioMutex);
                    if (b.frames != frames || b.channels != channels || b.sequence <= 0) throw std::runtime_error("Invalid shared audio block");
                    auto* samples = worker::audio(header, i, channels, frames);
                    for (int c = 0; c < channels; ++c) buffer.copyFrom(c, 0, samples + static_cast<size_t>(c) * frames, frames);
                    midi.clear(); if (!worker::unpackMidi(b, midi, worker::midiBytes)) throw std::runtime_error("Invalid shared MIDI");
                    const ScopedTryLock callbackLock(plugin->getCallbackLock());
                    if (!callbackLock.isLocked()) { buffer.clear(); midi.clear(); }
                    else if (pluginBuses::matches(*plugin, preparedLayout)) plugin->processBlock(buffer, midi);
                    else { layoutPending.store(true); buffer.clear(); midi.clear(); }
                    if (callbackLock.isLocked() && !pluginBuses::matches(*plugin, preparedLayout)) { layoutPending.store(true); buffer.clear(); midi.clear(); }
                    if (audioLimits::sanitize(buffer)) throw std::runtime_error("Plugin produced invalid audio");
                    for (int c = 0; c < channels; ++c) std::memcpy(samples + static_cast<size_t>(c) * frames, buffer.getReadPointer(c), static_cast<size_t>(frames) * sizeof(float));
                    InterlockedAdd64(&header.droppedMidi, static_cast<LONG64>(worker::packMidi(b, midi))); InterlockedExchange(&header.latency, plugin->getLatencySamples());
                    InterlockedExchange64(&header.stateRevision, static_cast<LONG64>(revision.load()));
                    InterlockedIncrement64(&header.processed);
                } catch (...) { InterlockedExchange(&header.fault, 1); }
                InterlockedExchange(&b.owner, worker::complete);
            }
            if (!worked) WaitForSingleObject(signal->get(), 25);
        }
    }
    void audioProcessorParameterChanged(AudioProcessor*, int, float) override { ++revision; }
    void audioProcessorChanged(AudioProcessor*, const ChangeDetails&) override { ++revision; }
    File directory;
    bool allowFixture;
    AudioPluginFormatManager formats;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<EditorWindow> editor;
    std::unique_ptr<worker::Mapping> mapping;
    std::unique_ptr<ipc::Handle> signal;
    AudioBuffer<float> buffer;
    MidiBuffer midi;
    std::mutex audioMutex;
    std::thread audioThread;
    std::atomic<bool> audioStop{false}, layoutPending{false};
    AudioProcessor::BusesLayout preparedLayout;
    std::atomic<uint64_t> revision{0};
};
}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    juce::ScopedJuceInitialiser_GUI initialization;
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#if LIGHTHOST_WORKER_TEST_FIXTURES
    if (argc == 2 && juce::String(argv[1]) == "--fixture-child") { LocalFree(argv); Sleep(INFINITE); return 0; }
#endif
    juce::String pipeName, directory; bool fixture = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String argument(argv[i]);
        if (argument == "--pipe" && i + 1 < argc) pipeName = argv[++i];
        else if (argument == "--directory" && i + 1 < argc) directory = argv[++i];
        else if (argument == "--test-fixture") fixture = true;
    }
    LocalFree(argv);
    if (!pipeName.startsWith("\\\\.\\pipe\\LightHostModernWorker-") || !juce::File::isAbsolutePath(directory)) return 2;
    lightHostModern::ipc::Handle pipe(CreateFileW(pipeName.toWideCharPointer(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!pipe) return 3;
    DWORD mode = PIPE_READMODE_MESSAGE; if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) return 4;
    Worker worker(juce::File(directory), fixture);
    lightHostModern::ipc::StopEvent stop;
    std::thread controls([&] {
        for (;;) {
            std::string message;
            // Parent performs the bounded wait and owns the kill-on-close job.
            if (lightHostModern::ipc::PipeIo(pipe.get(), stop.get(), 60000).read(message) != ERROR_SUCCESS) break;
            const auto request = lightHostModern::parseBoundedJson(juce::String::fromUTF8(message.data(), static_cast<int>(message.size())));
            juce::WaitableEvent done; juce::var response;
            if (!juce::MessageManager::callAsync([&] { response = worker.execute(request); done.signal(); })) break;
            done.wait();
            if (lightHostModern::ipc::PipeIo(pipe.get(), stop.get(), 15000).write(juce::JSON::toString(response, true).toStdString()) != ERROR_SUCCESS) break;
        }
        juce::MessageManager::callAsync([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });
    juce::MessageManager::getInstance()->runDispatchLoop(); stop.signal(); controls.join();
    return 0;
}
