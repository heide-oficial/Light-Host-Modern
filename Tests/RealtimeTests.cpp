#include "RealtimeHostProcessor.h"
#include "RoutingRuntime.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

void lightHostModernLog(const String&) {}
void setLightHostModernCrashContext(const String&) {}
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
    bool failPreparation = false, invalidOutput = false;
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
        if (invalidOutput) buffer.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
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
        lightHostModern::realtimeAudit::Scope simulatedPlugin(lightHostModern::realtimeAudit::Origin::plugin);
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
        lightHostModern::realtimeAudit::Scope simulatedPlugin(lightHostModern::realtimeAudit::Origin::plugin);
        midi.clear();
        midi.data.minimiseStorageOverheads();
    }
};

#include "PluginBusScenarios.h"

class LatencyQueryPlugin final : public GainPlugin
{
public:
    LatencyQueryPlugin() : GainPlugin(2) { setLatencySamples(17); }
    int latencyOnPrepare = -1;
    void prepareToPlay(double rate, int block) override {
        GainPlugin::prepareToPlay(rate, block);
        if (latencyOnPrepare >= 0) setLatencySamples(latencyOnPrepare);
        delay.prepare(2, block, getLatencySamples(), rate);
    }
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override {
        delay.capture(audio);
        for (int channel = 0; channel < 2; ++channel)
            audio.copyFrom(channel, 0, delay.output(), channel, 0, audio.getNumSamples());
    }
private:
    DryDelay delay;
};

static void verifyBusQueryLatency()
{
    for (const bool graph : {false, true}) for (const bool duringPrepare : {false, true}) {
        RealtimeHostProcessor host; host.setPlayConfigDetails(2, 2, 48000, 64); host.prepareToPlay(48000, 64);
        auto snapshot = std::make_shared<ChainSnapshot>();
        auto slot = std::make_shared<PluginSlot>(PluginDescription{}, std::make_unique<LatencyQueryPlugin>());
        slot->instanceId = "latency"; snapshot->slots.push_back(slot); snapshot->graphMode = graph;
        if (graph) {
            snapshot->graph = lightHostModern::RoutingGraph::empty(2, 2);
            lightHostModern::RouteNode node; node.id = slot->instanceId; node.kind = "plugin"; node.inputs = node.outputs = 2;
            snapshot->graph.nodes.push_back(node);
            snapshot->graph.edges = {{"in", "audio-in", "latency", 0, 0, 2, 2},
                {"wet", "latency", "audio-out", 0, 0, 1, 1}, {"parallel", "audio-in", "audio-out", 1, 1, 1, 1}};
        }
        host.publishSnapshot(snapshot);
        auto* plugin = static_cast<LatencyQueryPlugin*>(slot->processor.get());
        if (duringPrepare) plugin->latencyOnPrepare = 97; else plugin->setLatencySamples(97);
        const auto inspect = [&] {
            RealtimeHostProcessor::ScopedSuspension pause(host);
            slot->release(); const auto inventory = lightHostModern::pluginBuses::inventory(*plugin);
            require(inventory.isObject(), "Bus query failed"); slot->prepare(48000, 64, 2);
            host.refreshLatencies();
        };
        inspect();
        require(slot->getLatencySamples() == 97 && host.getStats().chainLatencySamples == 97 && !slot->hasPendingLatency(),
            "Bus inventory consumed latency invalidation before List/Chain plan update");
        require(plugin->preparations.load() == 2, "Latency reconciliation reprepared the plugin");
        const auto routing = snapshot->routing;
        inspect(); require(snapshot->routing == routing, "Unchanged inventory discarded the routing delay history");
        AudioBuffer<float> audio(2, 64); MidiBuffer midi;
        for (const int bypass : {0, 1, 2}) {
            slot->bypassed.store(bypass == 1); host.setGlobalBypassed(bypass == 2);
            for (int block = 0; block < 12; ++block) { audio.clear(); host.processBlock(audio, midi); }
            for (int block = 0; block < 3; ++block) {
                audio.clear(); if (block == 0) { audio.setSample(0, 0, 1); audio.setSample(1, 0, 1); }
                { lightHostModern::realtimeAudit::Scope audit(lightHostModern::realtimeAudit::Origin::host); host.processBlock(audio, midi); }
                for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < 64; ++sample)
                    require(std::abs(audio.getSample(channel, sample) - (block * 64 + sample == 97 ? 1.f : 0.f)) < .0001f,
                        "Bus query misaligned parallel audio or bypass impulse");
            }
        }
    }
}

int main()
{
    try
    {
        ScopedJuceInitialiser_GUI juce;
        require(installRealtimeAllocationAudit(), "Release CRT allocation interception was not installed");
        verifyBusQueryLatency();
        // Malicious latency and invalid samples must never reach buffers or the device.
        for (int latency : {-1, std::numeric_limits<int>::max(), 480001}) {
            DryDelay dry; bool rejected = false;
            try { dry.prepare(2, 64, latency, 48000); } catch (const std::exception&) { rejected = true; }
            require(rejected && dry.allocatedSamples() == 0, "Invalid latency allocated a dry buffer");
        }
        for (bool graphMode : {false, true}) {
            auto host = std::make_unique<RealtimeHostProcessor>();
            host->setPlayConfigDetails(2, 2, 48000, 64); host->prepareToPlay(48000, 64);
            auto chain = std::make_shared<ChainSnapshot>();
            auto plugin = std::make_unique<GainPlugin>(2); plugin->invalidOutput = true;
            auto slot = std::make_shared<PluginSlot>(PluginDescription{}, std::move(plugin)); slot->instanceId = "invalid"; slot->bypassed.store(true);
            chain->slots.push_back(slot); chain->graphMode = graphMode;
            if (graphMode) {
                chain->graph = lightHostModern::RoutingGraph::empty(2, 2);
                lightHostModern::RouteNode node; node.id = "invalid"; node.kind = "plugin"; node.inputs = node.outputs = 2;
                chain->graph.nodes.push_back(node);
                chain->graph.edges = {{"first", "audio-in", "invalid", 0, 0, 2, 2}, {"last", "invalid", "audio-out", 0, 0, 2, 2}};
            }
            host->publishSnapshot(chain); AudioBuffer<float> audio(2,64); MidiBuffer midi;
            for (int block = 0; block < 10; ++block) {
                for (int c = 0; c < 2; ++c) FloatVectorOperations::fill(audio.getWritePointer(c), .25f, 64);
                host->processBlock(audio, midi);
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 64; ++n)
                    require(std::isfinite(audio.getSample(c,n)), "Invalid plugin sample escaped containment");
            }
            require(slot->processFailed.load() && host->getStats().processFailures > 0, "Invalid output must be diagnosed");
        }
        {
            ChainSnapshot snapshot; snapshot.sampleRate = 48000; snapshot.blockSize = 64;
            snapshot.graph = lightHostModern::RoutingGraph::empty(2,2);
            for (int n = 0; n < 126; ++n) { lightHostModern::RouteNode node; node.id = "mix" + String(n); node.kind = "mixer"; node.inputs = node.outputs = 2; node.gains = {1}; node.muted = {false}; snapshot.graph.nodes.push_back(node); }
            const auto runtime = RoutingRuntime::compile(snapshot); size_t midiBytes = 0;
            for (const auto& node : runtime->nodes) midiBytes += node->midi.data.getAllocatedCapacity();
            require(midiBytes == 0, "Mixer-only graph allocated unused MIDI buffers");
        }
        // Variable mixer inputs sum once; every output pair receives that stereo mix.
        for(int pairs:{1,5,128}) {
            ChainSnapshot snapshot;snapshot.sampleRate=48000;snapshot.blockSize=64;snapshot.graph=lightHostModern::RoutingGraph::empty(2,4);
            lightHostModern::RouteNode mix;mix.id="mix";mix.kind="mixer";mix.inputs=pairs*2;mix.outputs=4;mix.gains.assign(pairs,.5f);mix.muted.assign(pairs,false);snapshot.graph.nodes.push_back(mix);
            for(int i=0;i<pairs;++i)snapshot.graph.edges.push_back({"in"+String(i),"audio-in","mix",0,i*2,2,2});
            snapshot.graph.edges.push_back({"out0","mix","audio-out",0,0,2,2});snapshot.graph.edges.push_back({"out1","mix","audio-out",2,2,2,2});
            auto routing=RoutingRuntime::compile(snapshot);AudioBuffer<float> audio(4,64);audio.clear();
            FloatVectorOperations::fill(audio.getWritePointer(0),.1f,64);FloatVectorOperations::fill(audio.getWritePointer(1),.2f,64);std::atomic<uint64> failures{0};
            { lightHostModern::realtimeAudit::Scope audit(lightHostModern::realtimeAudit::Origin::host);routing->process(audio,2,4,failures); }
            for(int c=0;c<4;++c)for(int sample=0;sample<64;++sample)require(std::abs(audio.getSample(c,sample)-pairs*(c%2?.1f:.05f))<.0001f,"Dynamic mixer output mismatch");
        }
        // Regression tests for the reviewed mono PR: unity sum, smooth toggle,
        // real stereo, mono plugin routing and dry paths use the same matrix.
        for (int pluginChannels : {0, 1, 2})
        {
            auto mono = std::make_unique<RealtimeHostProcessor>();
            mono->setPlayConfigDetails(2, 2, 48000, 64); mono->prepareToPlay(48000, 64);
            auto chain = std::make_shared<ChainSnapshot>();
            if (pluginChannels) chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription{}, std::make_unique<GainPlugin>(pluginChannels)));
            mono->publishSnapshot(chain);
            AudioBuffer<float> audio(2, 64); MidiBuffer midi; mono->prepareMidiBuffer(midi);
            const auto fill = [&](float l, float r) { for (int i=0;i<64;++i) { audio.setSample(0,i,l); audio.setSample(1,i,r); } };
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            const auto previous = audio.getSample(0,63);
            mono->setMonoInputs(true); fill(.25f,.5f); mono->processBlock(audio,midi);
            require(std::abs(audio.getSample(0,0)-previous)<.01f,"Mono toggle dropped output abruptly");
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            const auto expected = pluginChannels ? 1.5f : .75f;
            require(std::abs(audio.getSample(0,63)-expected)<.0001f && std::abs(audio.getSample(1,63)-expected)<.0001f,"Mono unity sum or plugin centering failed");
            mono->setGlobalBypassed(true);
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            require(std::abs(audio.getSample(0,63)-.75f)<.0001f && std::abs(audio.getSample(1,63)-.75f)<.0001f,"Global dry route lost mono matrix");
            mono->setGlobalBypassed(false); mono->setMonoInputs(false);
            for (int i=0;i<10;++i) { fill(.25f,.5f); mono->processBlock(audio,midi); }
            require(std::abs(audio.getSample(0,63)-(pluginChannels ? .5f:.25f))<.0001f,"Stereo left not restored");
            if (pluginChannels != 1) require(std::abs(audio.getSample(1,63)-(pluginChannels ? 1.f:.5f))<.0001f,"True stereo not preserved");
            mono->setMonoInputs(true);
            for (int i=0;i<10;++i) { fill(.5f,-.5f); mono->processBlock(audio,midi); }
            require(audio.getMagnitude(0,64)<.0001f,"Mono phase cancellation changed");
        }
        // Physical main outputs must never be inferred from two packed channels.
        for (const auto mask : {0, 1, 2, 3, 5, 10, 12, 15})
        for (const auto rate : {48000.0, 96000.0})
        {
            auto output = std::make_unique<RealtimeHostProcessor>();
            BigInteger physical(mask);
            const int outputs = physical.countNumberOfSetBits();
            output->setPlayConfigDetails(4, outputs, rate, 64);
            output->configureOutputChannels(physical);
            output->prepareToPlay(rate, 64);
            AudioBuffer<float> audio(4, 513); MidiBuffer events;
            output->prepareMidiBuffer(events);
            const auto process = [&] {
                for (int ch = 0; ch < 4; ++ch) FloatVectorOperations::fill(audio.getWritePointer(ch), .2f * (ch + 1), 513);
                output->processBlock(audio, events);
            };
            process(); process();
            output->setMonoOutput(true);
            process();
            require(std::abs(audio.getSample(0, 0) - .2f) < .002f, "Output mono toggle caused a discontinuity");
            process();
            for (int ch = 0; ch < 4; ++ch) {
                const auto expected = (mask & 3) == 3 && ch < 2 ? .3f : .2f * (ch + 1);
                require(std::abs(audio.getSample(ch, 512) - expected) < .0001f, "Output mono changed an auxiliary/single output or failed averaging");
            }
            output->setDiagnosticsEnabled(false);
            for (int i = 0; i < 20; ++i) process(); // Expire presentation peak retention.
            const auto expectedPeak = outputs == 0 ? 0.0f : (mask & 3) == 3 && outputs == 2 ? .3f : .2f * outputs;
            require(std::abs(output->getMeterPeaks().second - expectedPeak) < .0001f, "Output meter included an unrouted input or missed final mono");
            output->setDiagnosticsEnabled(true);
            output->setMonoOutput(false); process(); process();
            require(std::abs(audio.getSample(0, 512) - .2f) < .0001f && std::abs(audio.getSample(1, 512) - .4f) < .0001f, "Output stereo was not restored");
            output->setMonoOutput(true); output->setGlobalMuted(true); process(); process();
            require(audio.getMagnitude(0, 513) == 0, "Output mono bypassed mute");
        }
        for (int pluginChannels : {0, 1, 2})
        for (bool inputMono : {false, true})
        for (bool outputMono : {false, true})
        {
            auto output = std::make_unique<RealtimeHostProcessor>();
            output->prepareToPlay(48000, 32);
            auto chain = std::make_shared<ChainSnapshot>();
            if (pluginChannels) chain->slots.push_back(std::make_shared<PluginSlot>(PluginDescription(), std::make_unique<GainPlugin>(pluginChannels)));
            output->publishSnapshot(chain);
            output->setMonoInputs(inputMono); output->setMonoOutput(outputMono);
            AudioBuffer<float> audio(2, 512); MidiBuffer events; output->prepareMidiBuffer(events);
            const auto process = [&](float left, float right) {
                FloatVectorOperations::fill(audio.getWritePointer(0), left, 512);
                FloatVectorOperations::fill(audio.getWritePointer(1), right, 512);
                output->processBlock(audio, events);
            };
            process(.2f,.6f); process(.2f,.6f);
            const float left = (inputMono ? .8f : .2f) * (pluginChannels ? 2.f : 1.f);
            const float right = pluginChannels == 1 && !inputMono ? 0.f : (inputMono ? .8f : .6f) * (pluginChannels ? 2.f : 1.f);
            require(std::abs(audio.getSample(0,511) - (outputMono ? (left+right)*.5f : left)) < .0001f
                && std::abs(audio.getSample(1,511) - (outputMono ? (left+right)*.5f : right)) < .0001f, "Independent input/output mono combination failed");
            output->setGlobalBypassed(true); process(.2f,.6f); process(.2f,.6f);
            require(std::abs(audio.getSample(0,511) - (inputMono ? .8f : outputMono ? .4f : .2f)) < .0001f, "Global bypass lost output mono");
            output->setMonoInputs(false); output->setMonoOutput(true); process(.5f,-.5f); process(.5f,-.5f);
            require(audio.getMagnitude(0,512) < .0001f, "Output mono phase cancellation failed");
            process(.5f,.5f); process(.5f,.5f);
            require(std::abs(audio.getSample(0,511)-.5f)<.0001f, "Output mono doubled equal signals");
        }
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
            const int retained = lightHostModern::midiCapacityBytes / 60006;
            require(events.getNumEvents() == retained, "MIDI capacity did not preserve complete prefix");
            for (const auto event : events) require(event.numBytes == 60000 && event.data[0] == 0xf0 && event.data[59999] == 0xf7, "Partial MIDI event emitted");
            const auto stats = midiHost->getStats();
            require(stats.midiOverflow == 80 - static_cast<uint64>(retained), "MIDI overflow count incorrect");
            require(stats.pluginAllocations > 0, "Third-party MIDI growth was not distinguished");
        }
        require(lightHostModern::realtimeAudit::hostAllocations.load() == 0 && lightHostModern::realtimeAudit::hostFrees.load() == 0,
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
            require(lightHostModern::realtimeAudit::hostAllocations.load() == 0 && lightHostModern::realtimeAudit::hostFrees.load() == 0,
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
        verifyPluginBusChanges();
        std::cout << "Channels, asymmetric buses, bounded MIDI, preserved delay history, lifecycle, diagnostics opt-out and Release allocation audit passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
