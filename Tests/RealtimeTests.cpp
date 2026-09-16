#include "RealtimeHostProcessor.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

void lightHostLog(const String&) {}
void setLightHostCrashContext(const String&) {}
bool installRealtimeAllocationAudit();

static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class GainPlugin : public AudioPluginInstance
{
public:
    explicit GainPlugin(int channels)
        : AudioPluginInstance(BusesProperties().withInput("In", AudioChannelSet::discreteChannels(channels), true)
                                               .withOutput("Out", AudioChannelSet::discreteChannels(channels), true)) {}
    explicit GainPlugin(const BusesProperties& buses) : AudioPluginInstance(buses) {}
    int samples = 0;
    int largestBlock = 0;
    std::atomic<bool> hold { false }, entered { false };
    std::atomic<int> preparations { 0 };
    bool failPreparation = false;
    void fillInPluginDescription(PluginDescription&) const override {}
    const String getName() const override { return "Simulated gain"; }
    void prepareToPlay(double, int) override
    {
        preparations.fetch_add(1);
        if (failPreparation) throw std::runtime_error("Simulated prepare failure");
    }
    void releaseResources() override {}
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        entered.store(true);
        while (hold.load()) std::this_thread::yield();
        samples += buffer.getNumSamples();
        largestBlock = jmax(largestBlock, buffer.getNumSamples());
        buffer.applyGain(2.0f);
    }
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

class AsymmetricPlugin final : public GainPlugin
{
public:
    AsymmetricPlugin() : GainPlugin(BusesProperties().withInput("Main", AudioChannelSet::stereo(), true)
        .withInput("Sidechain", AudioChannelSet::mono(), true).withInput("Disabled", AudioChannelSet::stereo(), false)
        .withOutput("Main", AudioChannelSet::mono(), true).withOutput("Auxiliary", AudioChannelSet::stereo(), true)) {}
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override
    {
        require(audio.getNumChannels() == 3 && getBusCount(true) == 3 && !getBus(true, 2)->isEnabled(), "Bus layout flattened");
        require(audio.getMagnitude(2, 0, audio.getNumSamples()) == 0, "Sidechain received unrelated host channels");
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            audio.setSample(0, i, audio.getSample(0, i) * 2);
            audio.setSample(1, i, 100);
            audio.setSample(2, i, 100);
        }
    }
};

class MidiProducer final : public GainPlugin
{
public:
    MidiProducer() : GainPlugin(2), payload(60000, 0x01) { payload.front() = 0xf0; payload.back() = 0xf7; }
    void processBlock(AudioBuffer<float>&, MidiBuffer& midi) override
    {
        lightHost::realtimeAudit::Scope simulatedPlugin(lightHost::realtimeAudit::Origin::plugin);
        midi.clear();
        for (int i = 0; i < 40; ++i) midi.addEvent(payload.data(), static_cast<int>(payload.size()), i);
    }
    std::vector<uint8> payload;
};

class MidiStorageShrinker final : public GainPlugin
{
public:
    MidiStorageShrinker() : GainPlugin(2) {}
    void processBlock(AudioBuffer<float>&, MidiBuffer& midi) override
    {
        lightHost::realtimeAudit::Scope simulatedPlugin(lightHost::realtimeAudit::Origin::plugin);
        midi.clear();
        midi.data.minimiseStorageOverheads();
    }
};

int main()
{
    try
    {
        ScopedJuceInitialiser_GUI juce;
        require(installRealtimeAllocationAudit(), "Release CRT allocation interception was not installed");
        for (int pluginChannels : { 2, 32, 64, 256 })
        for (int hostChannels : { 2, 32, 64, 256 })
        {
            auto host = std::make_unique<RealtimeHostProcessor>();
            require(!host->isGlobalMuted() && !host->isGlobalBypassed(), "Global controls must start off");
            host->setPlayConfigDetails(hostChannels, hostChannels, 48000, 64);
            host->prepareToPlay(48000, 64);
            auto plugin = std::make_unique<GainPlugin>(pluginChannels);
            auto* gain = plugin.get();
            auto slot = std::make_shared<PluginSlot>(PluginDescription(), std::move(plugin));
            auto chain = std::make_shared<ChainSnapshot>();
            chain->inputChannels = chain->outputChannels = hostChannels;
            chain->slots.push_back(slot);
            host->publishSnapshot(chain);
            AudioBuffer<float> audio(hostChannels, 1001);
            MidiBuffer midi;
            midi.ensureSize(1024 * 1024);
            auto fill = [&] { for (int c = 0; c < hostChannels; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), 0.25f, 1001); };
            fill(); host->processBlock(audio, midi); // Complete the resume ramp.
            fill();
            for (int offset : { 0, 63, 64, 511, 1000 }) midi.addEvent(MidiMessage::noteOn(1, 60, (uint8) 100), offset);
            host->processBlock(audio, midi);
            require(gain->samples == 2002 && gain->largestBlock == 64, "Segment dropped or oversized");
            require(midi.getNumEvents() == 5, "MIDI lost");
            int index = 0;
            const int expected[] { 0, 63, 64, 511, 1000 };
            for (const auto event : midi) require(event.samplePosition == expected[index++], "MIDI offset changed");
            for (int c = 0; c < hostChannels; ++c)
                require(std::abs(audio.getSample(c, 1000) - (c < pluginChannels ? 0.5f : 0.0f)) < 0.0001f, "Wrong channel output");
            slot->bypassed.store(true);
            fill(); host->processBlock(audio, midi);
            require(gain->samples == 3003, "Bypassed plugin stopped processing");
            require(std::abs(audio.getSample(0, 1000) - 0.25f) < 0.0001f, "Bypass dry output wrong");
            {
                RealtimeHostProcessor::ScopedSuspension suspension(*host);
                fill(); host->processBlock(audio, midi);
                require(audio.getMagnitude(0, 1001) == 0.0f && gain->samples == 3003, "Suspension allowed processing");
            }
            slot->bypassed.store(false);
            host->setGlobalBypassed(true);
            fill(); host->processBlock(audio, midi);
            for (int c = 0; c < hostChannels; ++c)
                require(std::abs(audio.getSample(c, 1000) - 0.25f) < 0.0001f, "Global bypass lost dry channel");
            host->setGlobalMuted(true);
            fill(); host->processBlock(audio, midi);
            require(audio.getMagnitude(256, 745) == 0.0f && gain->samples == 5005, "Mute must silence dry output without stopping plugins");
            host->setGlobalBypassed(false);
            fill(); host->processBlock(audio, midi);
            require(audio.getMagnitude(0, 1001) == 0.0f && gain->samples == 6006, "Mute must also silence wet output");
            host->setGlobalMuted(false);
            fill(); host->processBlock(audio, midi);
            require(std::abs(audio.getSample(0, 1000) - 0.5f) < 0.0001f && gain->samples == 7007, "Unmute must restore wet processing");
        }
        {
            auto measured = std::make_unique<RealtimeHostProcessor>();
            measured->prepareToPlay(48000, 64);
            auto measuredChain = std::make_shared<ChainSnapshot>();
            measuredChain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(2)));
            measured->publishSnapshot(measuredChain);
            AudioBuffer<float> audio(2, 14400);
            MidiBuffer events;
            const auto fill = [&] { FloatVectorOperations::fill(audio.getWritePointer(0), .75f, 14400); FloatVectorOperations::fill(audio.getWritePointer(1), .25f, 14400); };
            fill(); measured->processBlock(audio, events);
            fill(); measured->processBlock(audio, events);
            require(measured->getInputMeters().aggregate.rms == .75f && measured->getOutputMeters().aggregate.rms == 1.5f,
                "Input RMS must precede processing; output RMS must measure wet audio without clamping");
            require(measured->getMeterPeaks() == std::make_pair(.75f, 1.5f),
                "Live meter peaks must reflect the latest block without RMS averaging or clipping");
            require(!measured->getInputMeters().aggregate.clipped && measured->getOutputMeters().channels[0].clipped
                && !measured->getOutputMeters().channels[1].clipped, "Clipping direction and channel isolation");
            measured->setGlobalBypassed(true);
            fill(); measured->processBlock(audio, events); fill(); measured->processBlock(audio, events);
            require(measured->getOutputMeters().aggregate.rms == .75f, "Output meter did not follow global dry selection");
            measured->setGlobalMuted(true);
            fill(); measured->processBlock(audio, events); fill(); measured->processBlock(audio, events);
            require(measured->getOutputMeters().aggregate.rms < 1e-6f && measured->getOutputMeters().aggregate.peak == 0
                && measured->getInputMeters().aggregate.rms == .75f,
                "Mute must silence only output measurements while processing continues");
            require(measured->getOutputMeters().aggregate.clipped, "Mute erased persistent clipping");
            measured->resetClipping(false, true, 0);
            require(!measured->getOutputMeters().aggregate.clipped, "Channel clipping reset requires no callback");
            const auto stats = measured->getStats();
            require(stats.processedBlocks == 6 && stats.processedSamples == 86400, "Processing counters lost large segmented blocks");
            require(measured->getMeterPeaks() == std::make_pair(.75f, 0.0f), "Live peaks must follow output mute");
            measured->releaseResources();
            require(measured->getMeterPeaks() == std::make_pair(0.0f, 0.0f), "Stopping audio must not leave stale peaks");
        }
        auto host = std::make_unique<RealtimeHostProcessor>();
        auto chain = std::make_shared<ChainSnapshot>();
        auto slot = std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(2));
        slot->outputChannels = 257;
        chain->slots.push_back(slot);
        bool rejected = false;
        try { host->publishSnapshot(chain); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !host->getActiveSnapshot(), "Oversized layout activated");
        slot->outputChannels = 2;
        host->prepareToPlay(48000, 64);
        host->publishSnapshot(chain);
        auto* gain = static_cast<GainPlugin*>(slot->processor.get());
        gain->setLatencySamples(17);
        host->refreshLatencies();
        require(host->getStats().chainLatencySamples == 17, "Dynamic latency not applied");
        AudioBuffer<float> impulse(2, 64);
        impulse.clear(); impulse.setSample(0, 63, 1.0f);
        slot->captureDry(impulse);
        impulse.clear();
        slot->captureDry(impulse);
        slot->processBypass(impulse);
        require(impulse.getSample(0, 16) == 1.0f && impulse.getMagnitude(1, 0, 64) == 0.0f, "Dry history lost across blocks");
        MidiBuffer midi;
        host->setGlobalBypassed(true);
        for (int i = 0; i < 8; ++i) { impulse.clear(); host->processBlock(impulse, midi); }
        impulse.clear(); impulse.setSample(0, 63, 1.0f);
        host->processBlock(impulse, midi);
        impulse.clear(); host->processBlock(impulse, midi);
        require(std::abs(impulse.getSample(0, 16) - 1.0f) < 0.0001f, "Global bypass must retain chain latency across blocks");
        host->setGlobalMuted(true);
        gain->hold.store(true);
        gain->entered.store(false);
        std::thread callback([&] { host->processBlock(impulse, midi); });
        while (!gain->entered.load()) std::this_thread::yield();
        const int before = gain->preparations.load();
        std::atomic<bool> started { false };
        std::thread control([&] { started.store(true); host->prepareToPlay(96000, 128); });
        while (!started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const bool preparedDuringCallback = gain->preparations.load() != before;
        gain->hold.store(false);
        callback.join(); control.join();
        require(!preparedDuringCallback && gain->preparations.load() == before + 1, "Reconfiguration raced callback");
        require(host->isGlobalMuted() && host->isGlobalBypassed(), "Reconfiguration must preserve runtime controls");
        gain->failPreparation = true;
        host->prepareToPlay(48000, 1024);
        require(!slot->prepared && slot->processFailed.load(), "Failed preparation left stale buffers active");
        const int processedBeforeFailure = gain->samples;
        host->processBlock(impulse, midi);
        require(gain->samples == processedBeforeFailure, "Unprepared plugin was processed");
        {
            auto asymmetricHost = std::make_unique<RealtimeHostProcessor>();
            asymmetricHost->prepareToPlay(48000, 64);
            auto buses = std::make_shared<ChainSnapshot>();
            buses->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<AsymmetricPlugin>()));
            asymmetricHost->publishSnapshot(buses);
            AudioBuffer<float> audio(2, 512);
            MidiBuffer events;
            for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), 0.25f, 512);
            asymmetricHost->processBlock(audio, events);
            require(audio.getSample(0, 511) == 0.5f && audio.getMagnitude(1, 0, 512) == 0, "Auxiliary output escaped into main output");
        }
        {
            DryDelay history;
            history.prepare(2, 64, 17, 48000);
            AudioBuffer<float> constant(2, 64);
            for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(constant.getWritePointer(ch), 0.25f, 64);
            for (int i = 0; i < 6; ++i) history.capture(constant);
            require(history.prepare(2, 64, 9, 48000), "Available history was not reused");
            for (int i = 0; i < 6; ++i)
            {
                history.capture(constant);
                for (int sample = 0; sample < 64; ++sample) require(std::abs(history.output().getSample(0, sample) - 0.25f) < 0.00001f, "Latency transition zeroed valid history");
            }
            require(history.allocatedSamples() < 2000, "Stereo delay reserved 256 channels");
        }
        {
            auto midiHost = std::make_unique<RealtimeHostProcessor>();
            midiHost->prepareToPlay(48000, 64);
            auto producers = std::make_shared<ChainSnapshot>();
            producers->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<MidiProducer>()));
            midiHost->publishSnapshot(producers);
            AudioBuffer<float> audio(2, 128);
            audio.clear();
            MidiBuffer events;
            midiHost->prepareMidiBuffer(events);
            midiHost->processBlock(audio, events);
            const int retained = lightHost::midiCapacityBytes / 60006;
            require(events.getNumEvents() == retained, "MIDI capacity did not preserve complete prefix");
            for (const auto event : events) require(event.numBytes == 60000 && event.data[0] == 0xf0 && event.data[59999] == 0xf7, "Partial MIDI event emitted");
            const auto stats = midiHost->getStats();
            require(stats.midiOverflow == 80 - static_cast<uint64>(retained), "MIDI overflow count incorrect");
            require(stats.pluginAllocations > 0, "Third-party MIDI growth was not distinguished");
        }
        require(lightHost::realtimeAudit::hostAllocations.load() == 0 && lightHost::realtimeAudit::hostFrees.load() == 0,
            "Host allocated or freed memory in a prepared callback");
        {
            auto shrinkHost = std::make_unique<RealtimeHostProcessor>();
            shrinkHost->prepareToPlay(48000, 64);
            auto shrinking = std::make_shared<ChainSnapshot>();
            shrinking->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<MidiStorageShrinker>()));
            shrinkHost->publishSnapshot(shrinking);
            AudioBuffer<float> audio(2, 512);
            audio.clear();
            MidiBuffer events;
            shrinkHost->prepareMidiBuffer(events);
            for (int offset = 0; offset < 512; offset += 64) events.addEvent(MidiMessage::noteOn(1, 60, uint8(100)), offset);
            shrinkHost->processBlock(audio, events);
            require(lightHost::realtimeAudit::hostAllocations.load() == 0 && lightHost::realtimeAudit::hostFrees.load() == 0,
                "Plugin shrinking its MIDI buffer caused a host allocation");
            shrinkHost->collectRetiredSnapshots(); // Storage repair happens on the controller.
        }
        {
            auto diagnosticsHost = std::make_unique<RealtimeHostProcessor>();
            diagnosticsHost->prepareToPlay(48000, 64);
            auto chain = std::make_shared<ChainSnapshot>();
            auto processor = std::make_unique<GainPlugin>(2);
            auto* processed = processor.get();
            chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::move(processor)));
            diagnosticsHost->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 64); MidiBuffer events;
            diagnosticsHost->prepareMidiBuffer(events);
            const auto process = [&] {
                for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), 0.25f, 64);
                diagnosticsHost->processBlock(audio, events);
            };
            process(); const auto before = diagnosticsHost->getStats();
            diagnosticsHost->setDiagnosticsEnabled(false);
            for (int i = 0; i < 10; ++i) process();
            const auto paused = diagnosticsHost->getStats();
            require(paused.processedBlocks == before.processedBlocks && paused.processedSamples == before.processedSamples,
                    "disabled diagnostics still collected callback counters");
            require(processed->samples == 11 * 64 && audio.getMagnitude(0, 64) == 0.5f,
                    "disabling diagnostics stopped or changed plugin processing");
            require(diagnosticsHost->getMeterPeaks() == std::pair<float,float>{0.25f,0.5f}, "dashboard peaks stopped with diagnostics");
            diagnosticsHost->setDiagnosticsEnabled(true); process();
            require(diagnosticsHost->getStats().processedBlocks == before.processedBlocks + 1, "diagnostics did not resume");
        }
        {
            for (int inputs : { 1, 2 })
            {
                auto monoHost = std::make_unique<RealtimeHostProcessor>();
                require(!monoHost->isMonoInputs(), "Mono inputs must start off");
                monoHost->setPlayConfigDetails(inputs, 2, 48000, 64);
                monoHost->prepareToPlay(48000, 64);
                auto monoChain = std::make_shared<ChainSnapshot>();
                monoChain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(2)));
                monoHost->publishSnapshot(monoChain);
                AudioBuffer<float> audio(2, 512); MidiBuffer events;
                monoHost->prepareMidiBuffer(events);
                // Input 1 carries 0.25; input 2 (when opened) carries 0.125. Unopened channels are silent.
                const auto process = [&] {
                    audio.clear();
                    FloatVectorOperations::fill(audio.getWritePointer(0), 0.25f, 512);
                    if (inputs == 2) FloatVectorOperations::fill(audio.getWritePointer(1), 0.125f, 512);
                    monoHost->processBlock(audio, events);
                };
                process(); process();
                require(std::abs(audio.getSample(0, 511) - 0.5f) < 0.0001f
                    && std::abs(audio.getSample(1, 511) - (inputs == 2 ? 0.25f : 0.0f)) < 0.0001f, "Stereo input routing changed");
                require(monoHost->setMonoInputs(true) && !monoHost->setMonoInputs(true) && monoHost->isMonoInputs(), "Mono inputs toggle");
                process(); process();
                const float expected = inputs == 2 ? 0.75f : 0.5f;
                for (int ch = 0; ch < 2; ++ch)
                    require(std::abs(audio.getSample(ch, 511) - expected) < 0.0001f, "Mono inputs must be summed and centered on every channel");
                const auto meters = monoHost->getInputMeters();
                require(meters.channels[0].peak == 0.25f && (inputs == 1 || meters.channels[1].peak == 0.125f),
                    "Input meters must report the device channels before the mono fold");
                monoHost->setMonoInputs(false);
                process(); process();
                require(std::abs(audio.getSample(1, 511) - (inputs == 2 ? 0.25f : 0.0f)) < 0.0001f, "Disabling mono inputs must restore stereo routing");
            }
            require(lightHost::realtimeAudit::hostAllocations.load() == 0 && lightHost::realtimeAudit::hostFrees.load() == 0,
                "Mono input fold allocated in the callback");
        }
        std::cout << "Channels, asymmetric buses, bounded MIDI, preserved delay history, lifecycle, diagnostics opt-out, mono inputs and Release allocation audit passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
