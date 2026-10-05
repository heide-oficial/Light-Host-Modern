#include "IsolatedPlugin.h"
#include "PluginBusLayout.h"
#include "PluginWorkerProtocol.h"
#include "RealtimeAudit.h"
#include "RealtimeHostProcessor.h"
#include <iostream>
#include <thread>

void lightHostModernLog(const juce::String&) {}
void setLightHostModernCrashContext(const juce::String&) {}
using namespace juce;
using namespace lightHostModern;
static void require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
template<class Fn> static void await(Fn fn, int ms = 20000) {
    const auto until = GetTickCount64() + ms;
    while (!fn()) { if (GetTickCount64() > until) throw std::runtime_error("Worker test timed out"); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
}
static PluginDescription description(const char* name) {
    PluginDescription d; d.name = name; d.pluginFormatName = "LightHostTest"; d.uniqueId = 1; d.fileOrIdentifier = "fixture"; d.numInputChannels = d.numOutputChannels = 2; return d;
}
static void verifyIsolatedCaptureDeadline()
{
    IsolatedCaptureBarrier barrier; using Status = IsolatedCaptureBarrier::Status;
    uint64_t now = 100; int starts = 0, polls = 0;
    const auto clock = [&] { return now; };
    const auto changing = [&](bool start) { starts += start ? 1 : 0; ++polls; return false; };
    require(barrier.poll(clock, changing) == Status::waiting, "Capture barrier did not start");
    for (int attempt = 1; attempt < 30; ++attempt) {
        now += 1000;
        require(barrier.poll(clock, changing) == Status::waiting, "Capture deadline expired early");
    }
    require(starts == 1 && polls == 30, "Recapture restarted the absolute deadline");
    now = 30100;
    require(barrier.poll(clock, [&](bool) { ++polls; return true; }) == Status::timedOut && polls == 30,
        "Expired capture executed work or accepted a late ready result");
    barrier.reset();
    require(barrier.poll(clock, [&](bool start) { require(start, "Next operation reused expired barrier"); return true; }) == Status::ready,
        "Capture timeout prevented the next operation");
    barrier.reset();
    require(barrier.poll(clock, [&](bool) { now += 30000; return true; }) == Status::timedOut,
        "Time spent preparing was excluded from the capture deadline");
}
int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    try {
        require(installRealtimeAllocationAudit(), "Cannot instrument isolated callback allocations");
        verifyIsolatedCaptureDeadline();
        require(argc == 2 || (argc == 3 && String(argv[2]) == "--capture-regressions"), "Pass the worker executable [--capture-regressions]");
        const File workerFile(String::fromUTF8(argv[1])); const bool captureOnly = argc == 3;
        if (!captureOnly) {
            IsolatedPluginSession session(description("gain"), {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); }); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            auto proxy = session.createProxy(); require(proxy && proxy->getTotalNumInputChannels() == 2, "Worker layout lost");
            proxy->setRateAndBufferSizeDetails(48000, 128); proxy->prepareToPlay(48000, 128);
            await([&] { return session.poll() || session.failure().isNotEmpty(); }); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            require(proxy->getLatencySamples() == 256, "Pipeline latency not reported");
            AudioBuffer<float> buffer(2, 128); MidiBuffer midi; midi.ensureSize(worker::midiBytes);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buffer.setSample(c, i, .25f);
            midi.addEvent(MidiMessage::noteOn(1, 60, uint8(100)), 7);
            { realtimeAudit::Scope scope(realtimeAudit::Origin::host); proxy->processBlock(buffer, midi); }
            require(buffer.getMagnitude(0, 128) == 0, "First pipeline block was not silent");
            Thread::sleep(20);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buffer.setSample(c, i, .5f);
            { realtimeAudit::Scope scope(realtimeAudit::Origin::host); proxy->processBlock(buffer, midi); }
            require(buffer.getMagnitude(0, 128) == 0, "Scheduling headroom changed the declared delay");
            Thread::sleep(20);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buffer.setSample(c, i, .5f);
            { realtimeAudit::Scope scope(realtimeAudit::Origin::host); proxy->processBlock(buffer, midi); }
            require(std::abs(buffer.getSample(0, 20) - .5f) < .0001f, "Isolated audio was not processed with the declared two-block delay");
            require(!midi.isEmpty() && (*midi.begin()).samplePosition == 7, "Isolated MIDI timestamp lost");
            const auto ticket = session.requestCapture(); await([&] { return session.captureFinished(ticket); });
            require(session.failure().isEmpty() && session.capturedState().isNotEmpty(), "Isolated capture failed");
            const auto previousState = session.capturedState();
            const auto limited = session.requestCapture(12); await([&] { return session.captureFinished(limited); });
            require(session.failure().isEmpty() && session.captureFailure().isNotEmpty() && session.capturedState() == previousState,
                "Oversized capture killed the worker or erased the prior state");
            proxy.reset();
        }
        {
            const auto prefix = "Local\\LightHostModernCaptureTest-" + Uuid().toString();
            ipc::Handle captured(CreateEventW(nullptr, TRUE, FALSE, (prefix + "-captured").toWideCharPointer()));
            ipc::Handle resume(CreateEventW(nullptr, TRUE, FALSE, (prefix + "-resume").toWideCharPointer()));
            require(captured && resume, "Cannot create capture test barriers");
            auto fixture = description("capture-barrier"); fixture.fileOrIdentifier = prefix;
            IsolatedPluginSession session(fixture, {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); });
            require(session.initialized(), session.failure().toRawUTF8());
            auto slot = std::make_shared<PluginSlot>(fixture, session.createProxy()); slot->prepare(48000, 128, 2);
            await([&] { return session.poll(); });
            slot->stateDirty.exchange(false);
            const auto captureRevision = slot->stateRevision.load();
            const auto ticket = session.requestCapture();
            require(WaitForSingleObject(captured.get(), 5000) == WAIT_OBJECT_0, "Capture did not reach persistence barrier");
            // Persistence is still held by the test. Audio must run and publish B.
            const auto before = static_cast<int64>(session.diagnostics()["processedBlocks"]);
            AudioBuffer<float> audio(2, 128); audio.clear(); MidiBuffer midi;
            { realtimeAudit::Scope scope(realtimeAudit::Origin::host); slot->processor->processBlock(audio, midi); }
            await([&] { return static_cast<int64>(session.diagnostics()["processedBlocks"]) > before; }, 3000);
            session.poll();
            require(slot->stateDirty.exchange(false), "State B notification did not reach autosave timer");
            require(!session.captureFinished(ticket), "Capture unexpectedly escaped its barrier");
            SetEvent(resume.get());
            await([&] { return session.captureFinished(ticket); });
            const auto first = session.captureResult();
            require(first.ticket == ticket && first.error.isEmpty(), "Capture A failed");
            require(slot->stateChangedSince(captureRevision) && first.revision < session.stateRevision(),
                "Capture A incorrectly acknowledged later state B after timer consumed its flag");
            MemoryBlock bytes; require(bytes.fromBase64Encoding(first.state) && bytes.getSize() == sizeof(float), "Invalid state A");
            float gain = 0; std::memcpy(&gain, bytes.getData(), sizeof(gain)); require(gain == 2, "Captured A included the later edit");
            const auto nextRevision = slot->stateRevision.load();
            const auto next = session.requestCapture(); await([&] { return session.captureFinished(next); });
            const auto second = session.captureResult();
            require(second.ticket == next && second.error.isEmpty() && second.revision == session.stateRevision()
                && !slot->stateChangedSince(nextRevision), "Capture B did not acknowledge the current revision");
            require(bytes.fromBase64Encoding(second.state) && bytes.getSize() == sizeof(float), "Invalid state B");
            std::memcpy(&gain, bytes.getData(), sizeof(gain)); require(gain == 3, "Follow-up capture lost state B");
            const auto limited = session.requestCapture(12); await([&] { return session.captureFinished(limited); });
            require(session.failure().isEmpty() && session.captureResult().ticket == limited
                && session.captureResult().error.isNotEmpty() && session.capturedState() == second.state,
                "Capture budget failure erased B or lost its ticket");
            session.requestCapture(); const auto forced = session.requestCapture();
            await([&] { return session.captureFinished(forced); });
            require(session.captureResult().ticket == forced && session.captureResult().error.isEmpty()
                && session.captureResult().state == second.state, "Forced capture returned an older ticket or stale error");
            IsolatedPluginSession restored(description("gain"), second.state, 48000, 128, workerFile, true);
            await([&] { return restored.initialized() || restored.failure().isNotEmpty(); });
            require(restored.initialized(), "Cannot restore captured B");
            const auto saved = restored.requestCapture(); await([&] { return restored.captureFinished(saved); });
            require(restored.capturedState() == second.state, "Restart did not preserve captured B");
        }
        {
            MemoryBlock bytes; const float gain = 2; bytes.append(&gain, sizeof(gain));
            IsolatedPluginSession session(description("capture-write-failure"), bytes.toBase64Encoding(), 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); });
            require(session.initialized(), "Write-failure fixture did not initialize");
            const auto ticket = session.requestCapture(); await([&] { return session.captureFinished(ticket); });
            require(session.failure().isEmpty() && session.captureResult().ticket == ticket
                && session.captureResult().error == "Cannot write captured state" && session.capturedState() == bytes.toBase64Encoding(),
                "Failed persistence erased the last valid state or killed the worker");
        }
        {
            IsolatedPluginSession session(description("large-state"), {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); });
            require(session.initialized(), "Large-state fixture did not initialize");
            const auto ticket = session.requestCapture(); await([&] { return session.captureFinished(ticket); });
            const auto result = session.captureResult(); MemoryBlock state;
            require(session.failure().isEmpty() && result.error.isEmpty() && state.fromBase64Encoding(result.state)
                && state.getSize() == 4u * 1024 * 1024, "Large state capture/hash verification failed");
            const auto* bytes = static_cast<const unsigned char*>(state.getData());
            require(std::all_of(bytes, bytes + state.getSize(), [](unsigned char value) { return value == 0x5a; }), "Large captured state was mutated");
        }
        if (captureOnly) {
            require(realtimeAudit::hostAllocations.load() == 0 && realtimeAudit::hostFrees.load() == 0, "Capture regression allocated in host audio callback");
            std::cout << "Isolated capture revision, persistence and recovery scenarios passed\n"; return 0;
        }
        {
            IsolatedPluginSession first(description("gain"), {}, 48000, 128, workerFile, true);
            IsolatedPluginSession second(description("gain"), {}, 48000, 128, workerFile, true);
            await([&] { return first.initialized() && second.initialized(); });
            auto a = first.createProxy(), b = second.createProxy();
            for (auto* proxy : {a.get(), b.get()}) { proxy->setRateAndBufferSizeDetails(48000, 128); proxy->prepareToPlay(48000, 128); }
            bool preparedA = false, preparedB = false;
            await([&] { preparedA = first.poll() || preparedA; preparedB = second.poll() || preparedB; return preparedA && preparedB; });
            require(a->getLatencySamples() + b->getLatencySamples() == 512, "Serial worker latency did not accumulate");
            AudioBuffer<float> audio(2, 128); MidiBuffer midi;
            for (int block = 0; block < 7; ++block) {
                audio.clear(); if (block == 0) audio.setSample(0, 7, .25f);
                { realtimeAudit::Scope scope(realtimeAudit::Origin::host); a->processBlock(audio, midi); b->processBlock(audio, midi); }
                if (block == 4) require(std::abs(audio.getSample(0, 7) - 1.0f) < .0001f, "Serial worker impulse lost amplitude or position");
                else require(audio.getMagnitude(0, 128) == 0, "Serial worker impulse arrived outside its declared latency");
                Thread::sleep(20);
            }
        }
        {
            IsolatedPluginSession session(description("gain"), {}, 48000, 64, workerFile, true);
            await([&] { return session.initialized(); }); auto proxy = session.createProxy();
            proxy->setRateAndBufferSizeDetails(48000, 64); proxy->prepareToPlay(48000, 64);
            await([&] { return session.poll(); });
            require(proxy->getLatencySamples() == 256, "Tiny callback scheduling margin was not reported");
            AudioBuffer<float> audio(2, 64); MidiBuffer midi;
            for (int block = 0; block < 8; ++block) {
                for (int c = 0; c < 2; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), .25f, 64);
                { realtimeAudit::Scope scope(realtimeAudit::Origin::host); proxy->processBlock(audio, midi); }
                if (block < 4) require(audio.getMagnitude(0, 64) == 0, "Tiny callback output arrived too early");
                else require(std::abs(audio.getSample(0, 20) - .5f) < .0001f, "Early completed block was discarded");
                Thread::sleep(20);
            }
        }
        {
            IsolatedPluginSession session(description("sidechain"),{},48000,128,workerFile,true);
            await([&]{return session.initialized()||session.failure().isNotEmpty();});require(session.initialized(),session.failure().toRawUTF8());
            auto proxy=session.createProxy();
            require(proxy->getBus(true,0)->getCurrentLayout()==AudioChannelSet::stereo(),"Worker lost speaker identities");
            proxy->setRateAndBufferSizeDetails(48000,128);proxy->prepareToPlay(48000,128);
            // Deliberately change layout before the initial mapping is acknowledged.
            auto inventory=session.busInventory();AudioProcessor::BusesLayout layout;
            require(pluginBuses::decode(inventory["layout"],layout),"Invalid worker layout inventory");
            layout.inputBuses.set(2,AudioChannelSet::stereo());
            require(session.configureBuses(pluginBuses::encode(layout),inventory["layout"]),"Cannot request worker buses");
            await([&]{return session.layoutChanged()||session.failure().isNotEmpty();});require(session.failure().isEmpty(),session.failure().toRawUTF8());
            proxy=session.createProxy();require(proxy->getTotalNumInputChannels()==5&&proxy->getBus(true,2)->isEnabled(),"Worker did not enable optional input");
            require(!session.layoutChanged(),"Worker metadata was not acknowledged");
            proxy->setRateAndBufferSizeDetails(48000,128);proxy->prepareToPlay(48000,128);await([&]{return session.poll()||session.failure().isNotEmpty();});
            require(session.failure().isEmpty(),session.failure().toRawUTF8());
            auto persisted=pluginBuses::encode(proxy->getBusesLayout());
            IsolatedPluginSession restored(description("sidechain"),{},48000,128,workerFile,true,persisted);
            await([&]{return restored.initialized()||restored.failure().isNotEmpty();});require(restored.initialized(),restored.failure().toRawUTF8());
            require(restored.createProxy()->getTotalNumInputChannels()==5,"Worker channel settings were lost after restart");
        }
        {
            IsolatedPluginSession session(description("dynamic-channels"),{},48000,128,workerFile,true);
            await([&]{return session.initialized()||session.failure().isNotEmpty();});require(session.initialized(),session.failure().toRawUTF8());
            auto proxy=session.createProxy();proxy->setRateAndBufferSizeDetails(48000,128);proxy->prepareToPlay(48000,128);await([&]{return session.poll();});
            AudioBuffer<float> audio(2,128);audio.clear();MidiBuffer midi;proxy->processBlock(audio,midi);
            await([&]{return session.layoutChanged()||session.failure().isNotEmpty();});require(session.failure().isEmpty(),session.failure().toRawUTF8());
            proxy=session.createProxy();require(proxy->getTotalNumInputChannels()==1&&proxy->getMainBusNumOutputChannels()==1,"Plugin-initiated layout change was not propagated");
            proxy->setRateAndBufferSizeDetails(48000,128);proxy->prepareToPlay(48000,128);await([&]{return session.poll()||session.failure().isNotEmpty();});
            require(session.failure().isEmpty(),session.failure().toRawUTF8());
        }
        for (const auto* layout : {"sidechain", "mono", "instrument"}) {
            auto d = description(layout); if (d.name == "mono") d.numInputChannels = d.numOutputChannels = 1;
            if (d.name == "instrument") d.numInputChannels = 0;
            IsolatedPluginSession session(d, {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); }); require(session.initialized(), session.failure().toRawUTF8());
            auto proxy = session.createProxy();
            if (d.name == "sidechain") require(proxy->getBusCount(true) == 3 && !proxy->getBus(true, 2)->isEnabled()
                && proxy->getTotalNumInputChannels() == 3 && proxy->getBusCount(false) == 2, "Auxiliary bus metadata was flattened");
            else require(proxy->getTotalNumInputChannels() == d.numInputChannels, "Mono/instrument layout changed");
            proxy->setRateAndBufferSizeDetails(48000, 128); proxy->prepareToPlay(48000, 128); await([&] { return session.poll(); });
            const int channels = jmax(1, jmax(proxy->getTotalNumInputChannels(), proxy->getTotalNumOutputChannels()));
            AudioBuffer<float> audio(channels, 32); MidiBuffer midi;
            for (int part = 0; part < 16; ++part) {
                for (int c = 0; c < channels; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), .25f, 32);
                { realtimeAudit::Scope scope(realtimeAudit::Origin::host); proxy->processBlock(audio, midi); }
                if (part < 8) require(audio.getMagnitude(0, 32) == 0, "Fragmented pipeline delay changed");
                else require(std::abs(audio.getSample(0, 16) - .5f) < .0001f, "Fragmented worker audio lost alignment");
                if (part % 4 == 3) Thread::sleep(20);
            }
        }
        {
            std::unique_ptr<ipc::Handle> descendant;
            {
                IsolatedPluginSession session(description("descendant"), {}, 48000, 128, workerFile, true);
                await([&] { return session.initialized() || session.failure().isNotEmpty(); }); require(session.initialized(), "Descendant fixture startup failed");
                const auto ticket = session.requestCapture(); await([&] { return session.captureFinished(ticket); });
                MemoryBlock data; require(data.fromBase64Encoding(session.capturedState()) && data.getSize() == sizeof(DWORD), "Missing descendant PID");
                DWORD pid = 0; std::memcpy(&pid, data.getData(), sizeof(pid)); descendant = std::make_unique<ipc::Handle>(OpenProcess(SYNCHRONIZE, FALSE, pid));
                require(*descendant && WaitForSingleObject(descendant->get(), 0) == WAIT_TIMEOUT, "Descendant not running");
            }
            require(WaitForSingleObject(descendant->get(), 5000) == WAIT_OBJECT_0, "Worker descendant survived session shutdown");
        }
        {
            IsolatedPluginSession session(description("dynamic-latency"), {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized(); }); auto proxy = session.createProxy();
            proxy->setRateAndBufferSizeDetails(48000, 128); proxy->prepareToPlay(48000, 128); await([&] { return session.poll(); });
            AudioBuffer<float> audio(2, 128); audio.clear(); MidiBuffer midi; proxy->processBlock(audio, midi);
            await([&] { session.poll(); return proxy->getLatencySamples() == 320; });
        }
        for (const auto* failure : {"crash-create", "hang-create", "crash-prepare", "hang-prepare", "crash-restore", "hang-restore"}) {
            MemoryBlock state; float gain = 3; state.append(&gain, sizeof(gain));
            const auto started = GetTickCount64();
            IsolatedPluginSession session(description(failure), state.toBase64Encoding(), 48000, 128, workerFile, true);
            require(GetTickCount64() - started < 500, "Plugin startup blocked the caller");
            await([&] { return session.failure().isNotEmpty(); }); require(!session.initialized(), "Faulty startup accepted");
            std::cout << failure << " contained\n";
        }
        for (const auto* failure : {"crash-process", "hang-process", "invalid-audio", "crash-editor", "hang-editor", "crash-capture", "hang-capture"}) {
            IsolatedPluginSession session(description(failure), {}, 48000, 128, workerFile, true);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); }); require(session.initialized(), "Fixture startup failed");
            auto proxy = session.createProxy(); proxy->setRateAndBufferSizeDetails(48000, 128); proxy->prepareToPlay(48000, 128);
            await([&] { return session.poll(); });
            if (String(failure).contains("editor")) session.showEditor();
            else if (String(failure).contains("capture")) session.requestCapture();
            else { AudioBuffer<float> audio(2, 128); audio.clear(); MidiBuffer midi; proxy->processBlock(audio, midi); }
            await([&] { return session.failure().isNotEmpty(); });
            AudioBuffer<float> audio(2, 128); audio.clear(); MidiBuffer midi;
            const auto start = GetTickCount64(); for (int i = 0; i < 100; ++i) proxy->processBlock(audio, midi);
            require(GetTickCount64() - start < 500, "Failed worker blocked the audio callback");
            std::cout << failure << " contained\n";
        }
        require(realtimeAudit::hostAllocations.load() == 0 && realtimeAudit::hostFrees.load() == 0, "Isolated callback allocated or freed memory");
        std::cout << "Isolated plugin scenarios passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
