#include "RealtimeHostProcessor.h"
#include "RoutingRuntime.h"

#include <algorithm>
#include <thread>
#include <stdexcept>

void lightHostModernLog(const String& message);
void setLightHostModernCrashContext(const String& context);

PluginSlot::PluginSlot(PluginDescription descriptionIn, std::unique_ptr<AudioPluginInstance> processorIn)
	: description(std::move(descriptionIn)),
	  processor(std::move(processorIn))
{
	if (processor != nullptr)
	{
		processor->addListener(this);
        refreshLayout();
		inputChannels = processor->getTotalNumInputChannels();
        mainInputChannels = processor->getMainBusNumInputChannels();
		outputChannels = processor->getTotalNumOutputChannels();
        mainOutputChannels = processor->getMainBusNumOutputChannels();
	}
}

PluginSlot::~PluginSlot()
{
	if (processor) processor->removeListener(this);
	try { release(); }
	catch (...) { lightHostModernLog("Plugin threw during release; processor destruction continues"); }
}

void PluginSlot::prepare(double sampleRateIn, int blockSizeIn, int hostChannels)
{
	if (processor == nullptr)
		return;

	const int inputs = inputChannels;
	const int outputs = outputChannels;

	const int channels = jmax(inputs, outputs);
	if (channels > RealtimeHostProcessor::maxScratchChannels)
		throw std::invalid_argument("Plugin layout exceeds 256 channels");
	if (prepared && preparedSampleRate == sampleRateIn && preparedBlockSize == blockSizeIn && preparedChannels == channels)
    {
        dryDelay.prepare(hostChannels, blockSizeIn, latencySamples, sampleRateIn);
        return;
    }

	if (prepared)
	{
		prepared = false;
		processor->releaseResources();
	}

	// Preserve enabled, disabled and auxiliary buses exactly as negotiated by the plugin.
    lightHostModern::audioLimits::format(hostChannels, blockSizeIn, sampleRateIn);
    processor->setRateAndBufferSizeDetails(sampleRateIn, blockSizeIn);
	processor->prepareToPlay(sampleRateIn, blockSizeIn);
    if (!layoutMatches()) { layoutPending.store(true); throw std::runtime_error("Plugin changed its channel layout during preparation"); }
    const auto preparedLatency = lightHostModern::audioLimits::latency(processor->getLatencySamples(), sampleRateIn);
    latencyPlanDirty = latencyPlanDirty || preparedLatency != latencySamples;
	latencySamples = preparedLatency;
	requestedLatency.store(latencySamples);
	transitionStep = (float) (1.0 / jmax(1.0, sampleRateIn * 0.005));
	dryDelay.prepare(hostChannels, blockSizeIn, latencySamples, sampleRateIn);
	prepared = true;
	preparedSampleRate = sampleRateIn;
	preparedBlockSize = blockSizeIn;
	preparedChannels = channels;
}

void PluginSlot::release()
{
	if (processor != nullptr && prepared)
	{
		prepared = false;
		preparedSampleRate = 0.0;
		preparedBlockSize = 0;
		preparedChannels = 0;
		processor->releaseResources();
	}
}

void PluginSlot::captureDry(const AudioBuffer<float>& buffer)
{
    dryDelay.capture(buffer);
}

void PluginSlot::mixDry(AudioBuffer<float>& buffer, bool useDry)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        bypassMix = useDry ? jmin(1.0f, bypassMix + transitionStep)
                           : jmax(0.0f, bypassMix - transitionStep);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            wet[i] = bypassMix == 1.0f ? dryDelay.output().getSample(ch, i)
                : wet[i] * (1.0f - bypassMix) + dryDelay.output().getSample(ch, i) * bypassMix;
        }
    }
}

void PluginSlot::processBypass(AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, dryDelay.output(), ch, 0, buffer.getNumSamples());
}

bool PluginSlot::refreshLatency()
{
    if (!prepared || processor == nullptr) return true;
    const int next = requestedLatency.load();
    if (next == latencySamples) { latencyPlanDirty = false; return true; }
    const bool compatible = dryDelay.prepare(dryDelay.channels(), preparedBlockSize, next, preparedSampleRate);
    latencySamples = next;
    latencyPlanDirty = false;
    return compatible;
}

RealtimeHostProcessor::ScopedSuspension::ScopedSuspension(RealtimeHostProcessor& processor, bool fadeOnResume)
    : owner(processor), lock(processor.controlMutex),
      wasSuspended(owner.processingSuspended.exchange(true)), fade(fadeOnResume)
{
    while (owner.callbacksInFlight.load() != 0) std::this_thread::yield();
}

RealtimeHostProcessor::ScopedSuspension::~ScopedSuspension()
{
    if (!wasSuspended)
    {
        if (fade) owner.resumeFade.store(true);
        owner.processingSuspended.store(false);
    }
}

void RealtimeHostProcessor::prepareBuffers()
{
    buffersReady.store(false);
    preparedHostChannels = jlimit(1, maxScratchChannels, jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
    preparedInputChannels = jlimit(0, maxScratchChannels, getTotalNumInputChannels());
    preparedOutputChannels = jlimit(0, maxScratchChannels, getTotalNumOutputChannels());
    lightHostModern::audioLimits::format(preparedHostChannels, currentBlockSize, currentSampleRate);
    if (static_cast<size_t>(maxScratchChannels) * currentBlockSize * sizeof(float) > lightHostModern::audioLimits::maximumBufferBytes)
        throw std::length_error("Scratch audio buffers exceed 64 MiB");
    buffersReady.store(false);
    lightHostModern::audioLimits::Reservation replacementMemory(static_cast<size_t>(maxScratchChannels+1)*currentBlockSize*sizeof(float)
        +3u*midiCapacity+2u*maxScratchChannels*(maxScratchChannels+1)*sizeof(float*)/2);
    std::vector<float> replacementGains(static_cast<size_t>(currentBlockSize));
    AudioBuffer<float> replacement(maxScratchChannels,currentBlockSize);
    monoGains=std::move(replacementGains);scratchBuffer=std::move(replacement);scratchMemory=std::move(replacementMemory);
    // JUCE reallocates channel-pointer storage when a view grows beyond its current
    // channel count. Keep one fixed-count view per layout, including 32+ channels.
    for (int channels = 1; channels <= maxScratchChannels; ++channels)
    {
        segmentViews[(size_t) channels].setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), channels, currentBlockSize);
        expandedViews[(size_t) channels].setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), channels, currentBlockSize);
    }
    segmentMidi.ensureSize(midiCapacity);
    filteredMidi.ensureSize(midiCapacity);
    outputMidi.ensureSize(midiCapacity);
    globalControls.prepare(preparedHostChannels, currentBlockSize, getLatencySamples(), currentSampleRate);
    inputMeters.prepare(currentSampleRate, getTotalNumInputChannels());
    outputMeters.prepare(currentSampleRate, getTotalNumOutputChannels());
    buffersReady.store(true);
}

void RealtimeHostProcessor::refreshLatencies(bool force)
{
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    const auto snapshot = activeSnapshot;
    if (!snapshot) return;
    bool changed = force;
    for (const auto& slot : snapshot->slots)
        if (slot && slot->processor && slot->prepared && slot->hasPendingLatency()) changed = true;
    if (!changed) return;
    ScopedSuspension suspension(*this, false);
    buffersReady.store(false);
    bool compatible = true;
    snapshot->totalLatencySamples = 0;
    for (const auto& slot : snapshot->slots)
        if (slot && slot->prepared) {
            try {
                if (!snapshot->graphMode && slot->hasPendingLatency())
                    lightHostModern::audioLimits::latency(snapshot->totalLatencySamples + lightHostModern::audioLimits::latency(slot->nextLatency(), currentSampleRate), currentSampleRate);
                if (!slot->processDisabled.load()) compatible = slot->refreshLatency() && compatible;
            } catch (const std::exception& error) {
                slot->processDisabled.store(true); slot->processFailed.store(true);
                processFailureCount.fetch_add(1, std::memory_order_relaxed);
                lightHostModernLog("Invalid plugin latency; previous dry route retained: " + String(error.what()));
            }
            if (!snapshot->graphMode) {
                const auto next = static_cast<int64>(snapshot->totalLatencySamples) + slot->getLatencySamples();
                if (next > lightHostModern::audioLimits::maximumLatency(currentSampleRate)) {
                    slot->processDisabled.store(true); slot->processFailed.store(true); slot->release();
                } else snapshot->totalLatencySamples = static_cast<int>(next);
            }
        }
    if (snapshot->graphMode)
    {
        try {
            snapshot->routing = RoutingRuntime::compile(*snapshot);
            snapshot->routingError.clear();
            snapshot->totalLatencySamples = snapshot->routing->latency;
        } catch (const std::exception& e) {
            lightHostModernLog("Routing latency update failed; output silenced: " + String(e.what()));
            snapshot->routing.reset(); snapshot->totalLatencySamples = 0;
            snapshot->routingError = String(e.what());
        }
    }
    setLatencySamples(snapshot->totalLatencySamples);
    compatible = globalControls.prepare(preparedHostChannels, currentBlockSize, snapshot->totalLatencySamples, currentSampleRate) && compatible;
    if (!compatible) resumeFade.store(true);
    buffersReady.store(true);
}

RealtimeHostProcessor::RealtimeHostProcessor()
	: AudioProcessor(BusesProperties()
		.withInput("Input", AudioChannelSet::stereo(), true)
		.withOutput("Output", AudioChannelSet::stereo(), true))
{
	prepareBuffers();
}

RealtimeHostProcessor::~RealtimeHostProcessor()
{
    ScopedSuspension suspension(*this);
    realtimeSnapshot.store(nullptr, std::memory_order_release);
    activeSnapshot.reset();
    retiredSnapshots.clear();
}

void RealtimeHostProcessor::publishSnapshot(std::shared_ptr<ChainSnapshot> snapshot)
{
	ScopedSuspension suspension(*this);
	if (snapshot) for (const auto& slot : snapshot->slots)
		if (slot && jmax(slot->inputChannels, slot->outputChannels) > maxScratchChannels)
			throw std::invalid_argument("Plugin layout exceeds 256 channels");
	if (snapshot != nullptr)
		prepareSnapshot(*snapshot);
    else { setLatencySamples(0); globalControls.prepare(preparedHostChannels, currentBlockSize, 0, currentSampleRate); }

	if (auto previous = activeSnapshot)
		retiredSnapshots.push_back(std::move(previous));

	activeSnapshot = std::move(snapshot);
    static_assert(std::atomic<ChainSnapshot*>::is_always_lock_free, "Audio snapshot publication must be lock-free");
    realtimeSnapshot.store(activeSnapshot.get(), std::memory_order_release);
	collectRetiredSnapshots();
}

std::shared_ptr<ChainSnapshot> RealtimeHostProcessor::getActiveSnapshot() const
{
	const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return activeSnapshot;
}

void RealtimeHostProcessor::updateRoutingControls(const lightHostModern::RoutingGraph& graph)
{
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (!activeSnapshot || !activeSnapshot->graphMode) return;
    activeSnapshot->graph = graph;
    if (activeSnapshot->routing)
        for (const auto& node : activeSnapshot->routing->nodes)
            if (const auto* saved = graph.find(node->layout.id); saved && saved->kind == "mixer")
                for (size_t lane = 0; lane < jmin(saved->gains.size(), node->layout.gains.size()); ++lane)
                    node->targetGains[lane].store(saved->muted[lane] ? 0.0f : saved->gains[lane], std::memory_order_relaxed);
}

juce::var RealtimeHostProcessor::getRoutingMeters() const
{
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    auto* result = new DynamicObject();
    const auto snapshot = getActiveSnapshot();
    if (snapshot && snapshot->routing)
    {
        snapshot->routing->telemetrySamples.store(static_cast<int>(currentSampleRate * .5), std::memory_order_relaxed);
        for (const auto& node : snapshot->routing->nodes)
            result->setProperty(Identifier(node->layout.id), node->peak.load(std::memory_order_relaxed));
        for (const auto& edge : snapshot->routing->edges)
        {
            float peak = 0;
            const auto& node = snapshot->routing->nodes[static_cast<size_t>(edge.from)];
            for (int c = edge.layout.output; c < edge.layout.output + edge.layout.sourceWidth; ++c)
                peak = jmax(peak, node->channelPeaks[static_cast<size_t>(c)].load(std::memory_order_relaxed));
            result->setProperty(Identifier(edge.layout.id), peak);
        }
    }
    return var(result);
}

RealtimeHostStats RealtimeHostProcessor::getStats() const
{
	RealtimeHostStats stats;
	stats.processFailures = processFailureCount.load(std::memory_order_relaxed);
    stats.midiOverflow = midiOverflowCount.load(std::memory_order_relaxed);
    stats.processedBlocks = processedBlocks.load(std::memory_order_relaxed);
    stats.processedSamples = processedSamples.load(std::memory_order_relaxed);
    stats.inputMidiEvents = inputMidiEvents.load(std::memory_order_relaxed);
    stats.outputMidiEvents = outputMidiEvents.load(std::memory_order_relaxed);
    stats.hostAllocations = lightHostModern::realtimeAudit::hostAllocations.load();
    stats.hostFrees = lightHostModern::realtimeAudit::hostFrees.load();
    stats.pluginAllocations = lightHostModern::realtimeAudit::pluginAllocations.load();
    stats.pluginFrees = lightHostModern::realtimeAudit::pluginFrees.load();

	if (auto snapshot = getActiveSnapshot())
	{
		stats.loadedSlots = (int) snapshot->slots.size();
		stats.chainLatencySamples = snapshot->totalLatencySamples;
		stats.reusedSlots = snapshot->reusedSlots;
		stats.rebuiltSlots = snapshot->rebuiltSlots;
	}

	stats.inputLevel = lastInputLevel.load(std::memory_order_relaxed);
	stats.outputLevel = lastOutputLevel.load(std::memory_order_relaxed);
	return stats;
}

void RealtimeHostProcessor::collectRetiredSnapshots()
{
	const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (activeSnapshot && activeSnapshot->routing)
        for (auto& node : activeSnapshot->routing->nodes)
            if (node->midiNeedsRepair.exchange(false)) {
                ScopedSuspension suspension(*this, false);
                node->midi.ensureSize(midiCapacity);
            }
    if (midiStorageNeedsRepair.exchange(false))
    {
        ScopedSuspension suspension(*this, false);
        segmentMidi.ensureSize(midiCapacity);
        filteredMidi.ensureSize(midiCapacity);
        outputMidi.ensureSize(midiCapacity);
    }
	retiredSnapshots.erase(std::remove_if(retiredSnapshots.begin(), retiredSnapshots.end(),
		[] (const std::shared_ptr<ChainSnapshot>& snapshot)
		{
			return snapshot == nullptr || snapshot.use_count() == 1;
		}),
		retiredSnapshots.end());
}

void RealtimeHostProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
	ScopedSuspension suspension(*this);
    buffersReady.store(false);
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
    inputPresentation.reset(); outputPresentation.reset();
	currentSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
	currentBlockSize = jmax(1, maximumExpectedSamplesPerBlock);
	prepareBuffers();

	if (auto snapshot = getActiveSnapshot())
    {
        try { prepareSnapshot(*snapshot); }
        catch (const std::exception& error) {
            if (!snapshot->graphMode) throw;
            snapshot->routing.reset(); snapshot->totalLatencySamples = 0;
            setLatencySamples(0);
            lightHostModernLog("Routing could not be prepared; output silenced: " + String(error.what()));
        }
    }
}

void RealtimeHostProcessor::releaseResources()
{
	ScopedSuspension suspension(*this);
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
    inputPresentation.reset(); outputPresentation.reset();
	if (auto snapshot = getActiveSnapshot())
		for (auto& slot : snapshot->slots)
			if (slot != nullptr)
				slot->release();
}

void RealtimeHostProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    lightHostModern::realtimeAudit::Scope audit(lightHostModern::realtimeAudit::Origin::host);
    ScopedNoDenormals noDenormals;
    // The second check closes the race with a controller suspending between the
    // first check and admission. Only the controller waits for in-flight work.
    if (processingSuspended.load()||!buffersReady.load()) { buffer.clear(); return; }
    callbacksInFlight.fetch_add(1);
    struct Exit { std::atomic<unsigned>& count; ~Exit() { count.fetch_sub(1); } } exit { callbacksInFlight };
    if (processingSuspended.load()) { buffer.clear(); return; }
    const int channels = buffer.getNumChannels();
    if (channels > preparedHostChannels || channels == 0) { buffer.clear(); return; }
    lightHostModern::audioLimits::sanitize(buffer);
    const bool collect = lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed);
    lastInputLevel.store(collect ? inputMeters.process(buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples())
                                : buffer.getMagnitude(0, buffer.getNumSamples()), std::memory_order_relaxed);
    inputPresentation.process(lastInputLevel.load(std::memory_order_relaxed), buffer.getNumSamples(), currentSampleRate);
    if (collect) inputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
    auto* const snapshot = realtimeSnapshot.load(std::memory_order_acquire);
    if (resumeFade.exchange(false)) resumeGain = 0.0f;
    const int destinationCapacity = &midiMessages == preparedMidiDestination ? midiCapacity : jmin(midiCapacity, midiMessages.data.size());
    uint64 dropped = 0;
    outputMidi.clear();
    for (int offset = 0; offset < buffer.getNumSamples(); offset += currentBlockSize)
    {
        const int count = jmin(currentBlockSize, buffer.getNumSamples() - offset);
        auto& segment = segmentViews[(size_t) channels];
        segment.setDataToReferTo(buffer.getArrayOfWritePointers(), channels, offset, count);
        // Morph the input matrix, retaining continuity and the same dry/wet route.
        const float target = monoInputs.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        const float step = static_cast<float>(1.0 / (currentSampleRate * 0.005));
        const int inputs = jmin(preparedInputChannels, channels);
        for (int sample = 0; sample < count; ++sample)
        {
            monoMix += jlimit(-step, step, target - monoMix);
            monoGains[static_cast<size_t>(sample)] = monoMix;
            if (monoMix == 0.0f) continue;
            float sum = 0.0f;
            for (int ch = 0; ch < inputs; ++ch) sum += segment.getSample(ch, sample);
            // Main stereo route only: never duplicate the mix into auxiliary channels.
            for (int ch = 0; ch < jmin(2, channels); ++ch)
            {
                auto& value = segment.getWritePointer(ch)[sample];
                value += (sum - value) * monoMix;
            }
        }
        globalControls.capture(segment);
        segmentMidi.clear();
        dropped += lightHostModern::copyBoundedMidi(segmentMidi, midiMessages, offset, count, -offset, midiCapacity);
        if (snapshot && snapshot->graphMode)
        {
            if (snapshot->routing) snapshot->routing->process(segment, preparedInputChannels, preparedOutputChannels, processFailureCount);
            else segment.clear();
        }
        else if (snapshot) for (auto& slot : snapshot->slots)
        {
            if (!slot || !slot->processor || !slot->prepared) continue;
            slot->captureDry(segment);
            processSlot(*slot, segment, segmentMidi);
            if (segmentMidi.data.getAllocatedCapacity() < midiCapacity) midiStorageNeedsRepair.store(true);
            filteredMidi.clear();
            dropped += lightHostModern::copyBoundedMidi(filteredMidi, segmentMidi, 0, count, 0, midiCapacity);
            segmentMidi.swapWith(filteredMidi);
            slot->mixDry(segment, slot->bypassed.load(std::memory_order_relaxed));
        }
        globalControls.mix(segment);
        // Physical outputs 1/2 remain the principal pair even when JUCE packs
        // a sparse output mask. Never mix an auxiliary output into this pair.
        const bool hasPair = mainOutputLeft >= 0 && mainOutputRight >= 0
            && mainOutputLeft < preparedOutputChannels && mainOutputRight < preparedOutputChannels;
        const float outputTarget = monoOutput.load(std::memory_order_relaxed) && hasPair ? 1.0f : 0.0f;
        for (int i = 0; i < count; ++i) {
            outputMonoMix += jlimit(-step, step, outputTarget - outputMonoMix);
            if (!hasPair || outputMonoMix == 0.0f) continue;
            auto& left = segment.getWritePointer(mainOutputLeft)[i];
            auto& right = segment.getWritePointer(mainOutputRight)[i];
            const float average = 0.5f * left + 0.5f * right;
            left += (average - left) * outputMonoMix;
            right += (average - right) * outputMonoMix;
        }
        dropped += lightHostModern::copyBoundedMidi(outputMidi, segmentMidi, 0, count, offset, midiCapacity);
        for (int i = 0; i < count && resumeGain < 1.0f; ++i)
        {
            resumeGain = jmin(1.0f, resumeGain + (float) (1.0 / (currentSampleRate * 0.005)));
            for (int ch = 0; ch < channels; ++ch) segment.getWritePointer(ch)[i] *= resumeGain;
        }
    }
    if (lightHostModern::audioLimits::sanitize(buffer)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
    midiMessages.clear();
    dropped += lightHostModern::copyBoundedMidi(midiMessages, outputMidi, 0, buffer.getNumSamples(), 0, destinationCapacity);
    float outputPeak = 0.0f;
    if (!collect)
        for (int ch = 0; ch < jmin(preparedOutputChannels, channels); ++ch)
            outputPeak = jmax(outputPeak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    lastOutputLevel.store(collect ? outputMeters.process(buffer.getArrayOfReadPointers(), jmin(preparedOutputChannels, channels), buffer.getNumSamples())
                                 : outputPeak, std::memory_order_relaxed);
    outputPresentation.process(lastOutputLevel.load(std::memory_order_relaxed), buffer.getNumSamples(), currentSampleRate);
    if (collect) {
        midiOverflowCount.fetch_add(dropped, std::memory_order_relaxed);
        outputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
        processedBlocks.fetch_add(1, std::memory_order_relaxed);
        processedSamples.fetch_add(static_cast<uint64>(buffer.getNumSamples()), std::memory_order_relaxed);
    }
}

void RealtimeHostProcessor::setDiagnosticsEnabled(bool enabled)
{
    ScopedSuspension suspension(*this, false);
    lightHostModern::diagnosticsCollectionEnabled.store(enabled, std::memory_order_relaxed);
    inputMeters.clearHistory(); outputMeters.clearHistory();
}

void RealtimeHostProcessor::configureOutputChannels(const BigInteger& physicalChannels)
{
    ScopedSuspension suspension(*this, false);
    mainOutputLeft = mainOutputRight = -1;
    int packed = 0;
    for (int physical = physicalChannels.findNextSetBit(0); physical >= 0; physical = physicalChannels.findNextSetBit(physical + 1), ++packed) {
        if (physical == 0) mainOutputLeft = packed;
        if (physical == 1) mainOutputRight = packed;
    }
    outputMonoMix = 0.0f;
}

void RealtimeHostProcessor::prepareMidiBuffer(MidiBuffer& buffer)
{
    ScopedSuspension suspension(*this, false);
    buffer.ensureSize(midiCapacity);
    preparedMidiDestination = &buffer;
}

void RealtimeHostProcessor::prepareSnapshot(ChainSnapshot& snapshot)
{
    buffersReady.store(false);
	lightHostModernLog("RealtimeHostProcessor prepareSnapshot begin slots=" + String((int) snapshot.slots.size()));
	snapshot.sampleRate = currentSampleRate;
	snapshot.blockSize = currentBlockSize;
	snapshot.maxPluginChannels = jmax(snapshot.inputChannels, snapshot.outputChannels);
	snapshot.totalLatencySamples = 0;

	for (auto& slot : snapshot.slots)
	{
		if (slot == nullptr)
			continue;

		snapshot.maxPluginChannels = jmax(snapshot.maxPluginChannels, jmax(slot->inputChannels, slot->outputChannels));

		try
		{
			setLightHostModernCrashContext("RealtimeHostProcessor::prepareSnapshot prepare '" + slot->description.name
				+ "' sampleRate=" + String(currentSampleRate)
				+ " blockSize=" + String(currentBlockSize)
				+ " inputs=" + String(slot->inputChannels)
				+ " outputs=" + String(slot->outputChannels));
			lightHostModernLog("RealtimeHostProcessor prepare slot begin '" + slot->description.name + "'");
			slot->prepare(currentSampleRate, currentBlockSize, snapshot.graphMode ? jmax(1, jmax(slot->inputChannels, slot->outputChannels)) : preparedHostChannels);
			lightHostModernLog("RealtimeHostProcessor prepare slot completed '" + slot->description.name + "'");
		}
		catch (const std::exception& e)
		{
			lightHostModernLog("RealtimeHostProcessor prepare slot C++ exception '" + slot->description.name + "': " + String(e.what()));
			slot->processDisabled.store(true, std::memory_order_release);
			slot->processFailed.store(true, std::memory_order_release);
			if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
		}
		catch (...)
		{
			lightHostModernLog("RealtimeHostProcessor prepare slot unknown exception '" + slot->description.name + "'");
			slot->processDisabled.store(true, std::memory_order_release);
			slot->processFailed.store(true, std::memory_order_release);
			if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
		}

        if (!snapshot.graphMode && slot->prepared) {
            const auto total = static_cast<int64>(snapshot.totalLatencySamples) + slot->getLatencySamples();
            if (total > lightHostModern::audioLimits::maximumLatency(currentSampleRate)) {
                slot->processDisabled.store(true); slot->processFailed.store(true); slot->release();
                processFailureCount.fetch_add(1, std::memory_order_relaxed);
            } else snapshot.totalLatencySamples = static_cast<int>(total);
        }
	}

    if (snapshot.graphMode)
    {
        try {
            snapshot.routing = RoutingRuntime::compile(snapshot);
            snapshot.totalLatencySamples = snapshot.routing->latency;
            snapshot.routingError.clear();
        } catch (const std::exception& error) {
            snapshot.routing.reset(); snapshot.totalLatencySamples = 0;
            snapshot.routingError = String(error.what());
            lightHostModernLog("Routing unavailable; output silenced: " + snapshot.routingError);
        }
    }
	setLatencySamples(snapshot.totalLatencySamples);
    globalControls.prepare(preparedHostChannels, currentBlockSize, snapshot.totalLatencySamples, currentSampleRate);
    for (const auto& slot : snapshot.slots) if (slot) slot->acknowledgeLatencyPlan();
    buffersReady.store(true);
	lightHostModernLog("RealtimeHostProcessor prepareSnapshot completed latencySamples=" + String(snapshot.totalLatencySamples));
}

void RealtimeHostProcessor::processSlot(PluginSlot& slot, AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    const int samples = buffer.getNumSamples();
    if (samples <= 0) return;
    const ScopedTryLock callbackLock(slot.processor->getCallbackLock());
    if (!callbackLock.isLocked()) { buffer.clear(); return; }
    if (!slot.layoutMatches()) { slot.layoutPending.store(true); buffer.clear(); return; }
    if (slot.processDisabled.load(std::memory_order_acquire)) { slot.processBypass(buffer); return; }
    const int pluginChannels = jmax(slot.inputChannels, slot.outputChannels);
    if (pluginChannels > maxScratchChannels)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
        slot.processBypass(buffer);
        return;
    }
    // Auxiliary buses keep their declared layout but receive silence until the
    // host has an explicit route. Output-only channels must start at zero.
    for (int channel = 0; channel < pluginChannels; ++channel)
    {
        if (channel < jmin(buffer.getNumChannels(), slot.mainInputChannels))
            scratchBuffer.copyFrom(channel, 0, buffer, channel, 0, samples);
        else scratchBuffer.clear(channel, 0, samples);
    }
    auto& pluginBuffer = expandedViews[static_cast<size_t>(pluginChannels)];
    pluginBuffer.setDataToReferTo(scratchBuffer.getArrayOfWritePointers(), pluginChannels, samples);
    try
    {
        // Keep the JUCE format bridge in the host audit. Only executable imports
        // are intercepted; allocations inside third-party DLLs are not observed.
        slot.processor->processBlock(pluginBuffer, midiMessages);
        if (!slot.layoutMatches()) { slot.layoutPending.store(true); buffer.clear(); return; }
        if (lightHostModern::audioLimits::sanitize(pluginBuffer)) {
            slot.processDisabled.store(true); slot.processFailed.store(true);
            processFailureCount.fetch_add(1, std::memory_order_relaxed);
            slot.processBypass(buffer); return;
        }
    }
    catch (...)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHostModern::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
        slot.processBypass(buffer);
        return;
    }
    // Main output is the serial route; asymmetric and disabled buses don't leak
    // stale auxiliary samples into subsequent processors.
    const int copied = jmin(buffer.getNumChannels(), slot.mainOutputChannels);
    for (int channel = 0; channel < copied; ++channel) buffer.copyFrom(channel, 0, scratchBuffer, channel, 0, samples);
    for (int channel = copied; channel < buffer.getNumChannels(); ++channel) buffer.clear(channel, 0, samples);
    if (slot.mainOutputChannels == 1 && buffer.getNumChannels() >= 2)
        for (int sample = 0; sample < samples; ++sample)
            buffer.setSample(1, sample, scratchBuffer.getSample(0, sample) * monoGains[static_cast<size_t>(sample)]);
}
