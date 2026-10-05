#ifndef RealtimeHostProcessor_h
#define RealtimeHostProcessor_h

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <vector>
#include <array>
#include <mutex>
#include "PluginInstanceId.h"
#include "GlobalAudioControls.h"
#include "BoundedMidi.h"
#include "RealtimeAudit.h"
#include "AudioMeters.h"
#include "RoutingGraph.h"
#include "PluginBusLayout.h"

using namespace juce;

struct RealtimeHostStats
{
	int loadedSlots = 0;
	int chainLatencySamples = 0;
	uint64 processFailures = 0;
	uint64 reusedSlots = 0;
	uint64 rebuiltSlots = 0;
	uint64 midiOverflow = 0;
	uint64 processedBlocks = 0, processedSamples = 0, inputMidiEvents = 0, outputMidiEvents = 0;
	uint64 hostAllocations = 0, hostFrees = 0, pluginAllocations = 0, pluginFrees = 0;
	float inputLevel = 0.0f;
	float outputLevel = 0.0f;
};

struct PluginSlot : private AudioProcessorListener
{
	explicit PluginSlot(PluginDescription descriptionIn, std::unique_ptr<AudioPluginInstance> processorIn);
	~PluginSlot();

	void prepare(double sampleRateIn, int blockSizeIn, int hostChannels = 2);
	void release();
	void processBypass(AudioBuffer<float>& buffer);
	void captureDry(const AudioBuffer<float>& buffer);
	void mixDry(AudioBuffer<float>& buffer, bool useDry);
	bool refreshLatency();
	int getLatencySamples() const noexcept { return latencySamples; }
    size_t dryBytes() const noexcept { return dryDelay.allocatedSamples() * sizeof(float); }
	bool hasPendingLatency() const noexcept { return !processDisabled.load() && (latencyPlanDirty || requestedLatency.load() != latencySamples); }
    void acknowledgeLatencyPlan() noexcept { latencyPlanDirty = false; }
    int nextLatency() const noexcept { return requestedLatency.load(); }

	PluginDescription description;
	PluginInstanceId instanceId;
	std::unique_ptr<AudioPluginInstance> processor;
	NamedValueSet windowProperties;
	std::atomic<bool> bypassed { false };
	std::atomic<bool> processDisabled { false };
	std::atomic<bool> processFailed { false };
	std::atomic<bool> stateDirty { true };
    // Notifications may be consumed by the autosave timer while a capture is
    // still running. The revision survives that consumption.
    std::atomic<uint64_t> stateRevision { 1 };
    bool stateChangedSince(uint64_t revision) const noexcept { return stateRevision.load() != revision; }
	int inputChannels = 0;
	int outputChannels = 0;
	int mainInputChannels = 0, mainOutputChannels = 0;
    juce::AudioProcessor::BusesLayout busLayout;
    std::atomic<bool> layoutPending{false};
    bool layoutMatches() const { return processor && lightHostModern::pluginBuses::matches(*processor, busLayout); }
    void refreshLayout() {
        busLayout = processor->getBusesLayout();
        inputChannels = processor->getTotalNumInputChannels(); outputChannels = processor->getTotalNumOutputChannels();
        mainInputChannels = processor->getMainBusNumInputChannels(); mainOutputChannels = processor->getMainBusNumOutputChannels();
        layoutPending.store(false);
    }
	bool prepared = false;
	double preparedSampleRate = 0.0;
	int preparedBlockSize = 0;
	int preparedChannels = 0;

private:
	void markStateDirty() noexcept { stateRevision.fetch_add(1); stateDirty.store(true, std::memory_order_relaxed); }
	void audioProcessorParameterChanged(AudioProcessor*, int, float) override { markStateDirty(); }
	void audioProcessorChanged(AudioProcessor* source, const ChangeDetails& details) override
	{
		markStateDirty();
		if (details.latencyChanged) requestedLatency.store(source->getLatencySamples());
	}
	std::atomic<int> requestedLatency { 0 };
	int latencySamples = 0;
    bool latencyPlanDirty = false; // control thread, under host suspension
	DryDelay dryDelay;
	float bypassMix = 0.0f;
	float transitionStep = 1.0f;
};

struct RoutingRuntime;
struct ChainSnapshot
{
	double sampleRate = 44100.0;
	int blockSize = 512;
	int inputChannels = 2;
	int outputChannels = 2;
	int maxPluginChannels = 2;
	int totalLatencySamples = 0;
	uint64 reusedSlots = 0;
	uint64 rebuiltSlots = 0;
	std::vector<std::shared_ptr<PluginSlot>> slots;
    bool graphMode = false;
    lightHostModern::RoutingGraph graph;
    std::vector<int> physicalInputs, physicalOutputs;
    std::shared_ptr<RoutingRuntime> routing;
    String routingError;
};

class RealtimeHostProcessor final : public AudioProcessor
{
public:
	static constexpr int maxScratchChannels = 256;
	class ScopedSuspension
	{
	public:
		explicit ScopedSuspension(RealtimeHostProcessor&, bool fadeOnResume = true);
		~ScopedSuspension();
	private:
		RealtimeHostProcessor& owner;
		std::unique_lock<std::recursive_mutex> lock;
		bool wasSuspended;
		bool fade;
	};
	RealtimeHostProcessor();
	~RealtimeHostProcessor() override;

	void publishSnapshot(std::shared_ptr<ChainSnapshot> snapshot);
	std::shared_ptr<ChainSnapshot> getActiveSnapshot() const;
	void collectRetiredSnapshots();
	void refreshLatencies(bool force = false);
	RealtimeHostStats getStats() const;
    juce::var getRoutingMeters() const;
    void updateRoutingControls(const lightHostModern::RoutingGraph& graph);
	void setDiagnosticsEnabled(bool enabled);
	bool setGlobalMuted(bool value) { return globalControls.setMuted(value); }
	bool setGlobalBypassed(bool value) { return globalControls.setBypassed(value); }
	void setMonoInputs(bool value) noexcept { monoInputs.store(value, std::memory_order_relaxed); }
    bool isMonoInputs() const noexcept { return monoInputs.load(std::memory_order_relaxed); }
    void setMonoOutput(bool value) noexcept { monoOutput.store(value, std::memory_order_relaxed); }
    bool isMonoOutput() const noexcept { return monoOutput.load(std::memory_order_relaxed); }
    void configureOutputChannels(const BigInteger& physicalChannels);
    bool isGlobalMuted() const { return globalControls.isMuted(); }
	bool isGlobalBypassed() const { return globalControls.isBypassed(); }
	lightHostModern::MeterSnapshot getInputMeters() const noexcept { return inputMeters.snapshot(); }
	lightHostModern::MeterSnapshot getOutputMeters() const noexcept { return outputMeters.snapshot(); }
	// Read by the meter transport without touching the driver or controller.
	std::pair<float, float> getMeterPeaks() const noexcept
	{
		if (processingSuspended.load(std::memory_order_acquire)) return {0.0f, 0.0f};
		return {inputPresentation.read(), outputPresentation.read()};
	}
	void resetClipping(bool input, bool output, int channel = -1) noexcept
	{ if (input) inputMeters.resetClipping(channel); if (output) outputMeters.resetClipping(channel); }

	double getCurrentSampleRateForPlugins() const { std::lock_guard<std::recursive_mutex> lock(controlMutex); return currentSampleRate; }
	int getCurrentBlockSizeForPlugins() const { std::lock_guard<std::recursive_mutex> lock(controlMutex); return currentBlockSize; }

	const String getName() const override { return "LightHostModern Serial Chain"; }
	void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
	void releaseResources() override;
	bool isBusesLayoutSupported(const BusesLayout& layout) const override
	{
		int inputs = 0, outputs = 0;
		for (const auto& bus : layout.inputBuses) inputs += bus.size();
		for (const auto& bus : layout.outputBuses) outputs += bus.size();
		return inputs <= maxScratchChannels && outputs <= maxScratchChannels;
	}
	void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;
    // Call outside processing for a destination that should receive generated
    // MIDI up to the declared host capacity. Other destinations keep at most
    // their incoming byte count, which is a proven lower bound on their storage.
    void prepareMidiBuffer(MidiBuffer& buffer);

	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return true; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 0.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override { }
	const String getProgramName(int) override { return {}; }
	void changeProgramName(int, const String&) override { }

	bool hasEditor() const override { return false; }
	AudioProcessorEditor* createEditor() override { return nullptr; }

	void getStateInformation(MemoryBlock&) override { }
	void setStateInformation(const void*, int) override { }

private:
	void prepareSnapshot(ChainSnapshot& snapshot);
	void prepareBuffers();
	void processSlot(PluginSlot& slot, AudioBuffer<float>& buffer, MidiBuffer& midiMessages);

	mutable std::shared_ptr<ChainSnapshot> activeSnapshot;
	std::atomic<ChainSnapshot*> realtimeSnapshot { nullptr };
	std::vector<std::shared_ptr<ChainSnapshot>> retiredSnapshots;
	std::atomic<uint64> processFailureCount { 0 };
	std::atomic<uint64> midiOverflowCount { 0 };
	std::atomic<uint64> processedBlocks { 0 }, processedSamples { 0 }, inputMidiEvents { 0 }, outputMidiEvents { 0 };
	lightHostModern::AudioMeters inputMeters, outputMeters;
	std::atomic<bool> midiStorageNeedsRepair { false };
	std::atomic<float> lastInputLevel { 0.0f };
	std::atomic<float> lastOutputLevel { 0.0f };
	double currentSampleRate = 44100.0;
	int currentBlockSize = 512;
	AudioBuffer<float> scratchBuffer;
    lightHostModern::audioLimits::Reservation scratchMemory;
    std::atomic<bool> buffersReady{false};
	std::array<AudioBuffer<float>, maxScratchChannels + 1> segmentViews;
	std::array<AudioBuffer<float>, maxScratchChannels + 1> expandedViews;
	MidiBuffer segmentMidi, filteredMidi, outputMidi;
	static constexpr int midiCapacity = lightHostModern::midiCapacityBytes;
	MidiBuffer* preparedMidiDestination = nullptr;
	int preparedHostChannels = 2, preparedInputChannels = 2;
    int preparedOutputChannels = 2, mainOutputLeft = 0, mainOutputRight = 1;
    std::atomic<bool> monoOutput{false};
    float outputMonoMix = 0.0f;
    std::atomic<bool> monoInputs{false};
    float monoMix = 0.0f;
    std::vector<float> monoGains;
    lightHostModern::PresentationPeak inputPresentation, outputPresentation;
	mutable std::recursive_mutex controlMutex;
	std::atomic<bool> processingSuspended { false };
	std::atomic<unsigned> callbacksInFlight { 0 };
	std::atomic<bool> resumeFade { false };
	float resumeGain = 1.0f;
	GlobalAudioControls globalControls;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealtimeHostProcessor)
};

#endif /* RealtimeHostProcessor_h */
