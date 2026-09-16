#include "RealtimeHostProcessor.h"

#include <algorithm>
#include <thread>
#include <stdexcept>

void lightHostLog(const String& message);
void setLightHostCrashContext(const String& context);

PluginSlot::PluginSlot(PluginDescription descriptionIn, std::unique_ptr<AudioPluginInstance> processorIn)
	: description(std::move(descriptionIn)),
	  processor(std::move(processorIn))
{
	if (processor != nullptr)
	{
		processor->addListener(this);
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
	catch (...) { lightHostLog("Plugin threw during release; processor destruction continues"); }
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
    processor->setRateAndBufferSizeDetails(sampleRateIn, blockSizeIn);
	processor->prepareToPlay(sampleRateIn, blockSizeIn);
	latencySamples = jmax(0, processor->getLatencySamples());
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
            wet[i] = wet[i] * (1.0f - bypassMix) + dryDelay.output().getSample(ch, i) * bypassMix;
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
    if (next == latencySamples) return true;
    const bool compatible = dryDelay.prepare(dryDelay.channels(), preparedBlockSize, next, preparedSampleRate);
    latencySamples = next;
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
    preparedHostChannels = jlimit(1, maxScratchChannels, jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
    preparedInputChannels = jlimit(0, preparedHostChannels, getTotalNumInputChannels());
    scratchBuffer.setSize(maxScratchChannels, currentBlockSize, false, false, true);
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
}

void RealtimeHostProcessor::refreshLatencies()
{
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    const auto snapshot = activeSnapshot;
    if (!snapshot) return;
    bool changed = false;
    for (const auto& slot : snapshot->slots)
        if (slot && slot->processor && slot->prepared && slot->hasPendingLatency()) changed = true;
    if (!changed) return;
    ScopedSuspension suspension(*this, false);
    bool compatible = true;
    snapshot->totalLatencySamples = 0;
    for (const auto& slot : snapshot->slots)
        if (slot) { compatible = slot->refreshLatency() && compatible; snapshot->totalLatencySamples += slot->getLatencySamples(); }
    setLatencySamples(snapshot->totalLatencySamples);
    compatible = globalControls.prepare(preparedHostChannels, currentBlockSize, snapshot->totalLatencySamples, currentSampleRate) && compatible;
    if (!compatible) resumeFade.store(true);
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

RealtimeHostStats RealtimeHostProcessor::getStats() const
{
	RealtimeHostStats stats;
	stats.processFailures = processFailureCount.load(std::memory_order_relaxed);
    stats.midiOverflow = midiOverflowCount.load(std::memory_order_relaxed);
    stats.processedBlocks = processedBlocks.load(std::memory_order_relaxed);
    stats.processedSamples = processedSamples.load(std::memory_order_relaxed);
    stats.inputMidiEvents = inputMidiEvents.load(std::memory_order_relaxed);
    stats.outputMidiEvents = outputMidiEvents.load(std::memory_order_relaxed);
    stats.hostAllocations = lightHost::realtimeAudit::hostAllocations.load();
    stats.hostFrees = lightHost::realtimeAudit::hostFrees.load();
    stats.pluginAllocations = lightHost::realtimeAudit::pluginAllocations.load();
    stats.pluginFrees = lightHost::realtimeAudit::pluginFrees.load();

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
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
	currentSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
	currentBlockSize = jmax(1, maximumExpectedSamplesPerBlock);
	prepareBuffers();

	if (auto snapshot = getActiveSnapshot())
		prepareSnapshot(*snapshot);
}

void RealtimeHostProcessor::releaseResources()
{
	ScopedSuspension suspension(*this);
	lastInputLevel.store(0.0f); lastOutputLevel.store(0.0f);
	if (auto snapshot = getActiveSnapshot())
		for (auto& slot : snapshot->slots)
			if (slot != nullptr)
				slot->release();
}

void RealtimeHostProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    lightHost::realtimeAudit::Scope audit(lightHost::realtimeAudit::Origin::host);
    ScopedNoDenormals noDenormals;
    // The second check closes the race with a controller suspending between the
    // first check and admission. Only the controller waits for in-flight work.
    if (processingSuspended.load()) { buffer.clear(); return; }
    callbacksInFlight.fetch_add(1);
    struct Exit { std::atomic<unsigned>& count; ~Exit() { count.fetch_sub(1); } } exit { callbacksInFlight };
    if (processingSuspended.load()) { buffer.clear(); return; }
    const int channels = buffer.getNumChannels();
    if (channels > preparedHostChannels || channels == 0) { buffer.clear(); return; }
    const bool collect = lightHost::diagnosticsCollectionEnabled.load(std::memory_order_relaxed);
    lastInputLevel.store(collect ? inputMeters.process(buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples())
                                : buffer.getMagnitude(0, buffer.getNumSamples()), std::memory_order_relaxed);
    if (collect) inputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
    // Input meters above keep per-channel levels; the chain and its dry paths see the fold.
    if (monoInputs.load(std::memory_order_relaxed)) foldInputsToMono(buffer);
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
        globalControls.capture(segment);
        segmentMidi.clear();
        dropped += lightHost::copyBoundedMidi(segmentMidi, midiMessages, offset, count, -offset, midiCapacity);
        if (snapshot) for (auto& slot : snapshot->slots)
        {
            if (!slot || !slot->processor || !slot->prepared) continue;
            slot->captureDry(segment);
            processSlot(*slot, segment, segmentMidi);
            if (segmentMidi.data.getAllocatedCapacity() < midiCapacity) midiStorageNeedsRepair.store(true);
            filteredMidi.clear();
            dropped += lightHost::copyBoundedMidi(filteredMidi, segmentMidi, 0, count, 0, midiCapacity);
            segmentMidi.swapWith(filteredMidi);
            slot->mixDry(segment, slot->bypassed.load(std::memory_order_relaxed));
        }
        globalControls.mix(segment);
        dropped += lightHost::copyBoundedMidi(outputMidi, segmentMidi, 0, count, offset, midiCapacity);
        for (int i = 0; i < count && resumeGain < 1.0f; ++i)
        {
            resumeGain = jmin(1.0f, resumeGain + (float) (1.0 / (currentSampleRate * 0.005)));
            for (int ch = 0; ch < channels; ++ch) segment.getWritePointer(ch)[i] *= resumeGain;
        }
    }
    midiMessages.clear();
    dropped += lightHost::copyBoundedMidi(midiMessages, outputMidi, 0, buffer.getNumSamples(), 0, destinationCapacity);
    lastOutputLevel.store(collect ? outputMeters.process(buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples())
                                 : buffer.getMagnitude(0, buffer.getNumSamples()), std::memory_order_relaxed);
    if (collect) {
        midiOverflowCount.fetch_add(dropped, std::memory_order_relaxed);
        outputMidiEvents.fetch_add(static_cast<uint64>(midiMessages.getNumEvents()), std::memory_order_relaxed);
        processedBlocks.fetch_add(1, std::memory_order_relaxed);
        processedSamples.fetch_add(static_cast<uint64>(buffer.getNumSamples()), std::memory_order_relaxed);
    }
}

void RealtimeHostProcessor::foldInputsToMono(AudioBuffer<float>& buffer) const noexcept
{
    // Device inputs occupy the leading channels; the remainder is silent until
    // processed. Sum at unity so a single source keeps its level on every side.
    const int channels = buffer.getNumChannels(), samples = buffer.getNumSamples();
    const int inputs = jmin(preparedInputChannels, channels);
    if (inputs < 1 || channels < 2 || samples <= 0) return;
    auto* const mono = buffer.getWritePointer(0);
    for (int channel = 1; channel < inputs; ++channel)
        FloatVectorOperations::add(mono, buffer.getReadPointer(channel), samples);
    for (int channel = 1; channel < channels; ++channel)
        FloatVectorOperations::copy(buffer.getWritePointer(channel), mono, samples);
}

void RealtimeHostProcessor::setDiagnosticsEnabled(bool enabled)
{
    ScopedSuspension suspension(*this, false);
    lightHost::diagnosticsCollectionEnabled.store(enabled, std::memory_order_relaxed);
    inputMeters.clearHistory(); outputMeters.clearHistory();
}

void RealtimeHostProcessor::prepareMidiBuffer(MidiBuffer& buffer)
{
    ScopedSuspension suspension(*this, false);
    buffer.ensureSize(midiCapacity);
    preparedMidiDestination = &buffer;
}

void RealtimeHostProcessor::prepareSnapshot(ChainSnapshot& snapshot)
{
	lightHostLog("RealtimeHostProcessor prepareSnapshot begin slots=" + String((int) snapshot.slots.size()));
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
			setLightHostCrashContext("RealtimeHostProcessor::prepareSnapshot prepare '" + slot->description.name
				+ "' sampleRate=" + String(currentSampleRate)
				+ " blockSize=" + String(currentBlockSize)
				+ " inputs=" + String(slot->inputChannels)
				+ " outputs=" + String(slot->outputChannels));
			lightHostLog("RealtimeHostProcessor prepare slot begin '" + slot->description.name + "'");
			slot->prepare(currentSampleRate, currentBlockSize, preparedHostChannels);
			lightHostLog("RealtimeHostProcessor prepare slot completed '" + slot->description.name + "'");
		}
		catch (const std::exception& e)
		{
			lightHostLog("RealtimeHostProcessor prepare slot C++ exception '" + slot->description.name + "': " + String(e.what()));
			slot->processDisabled.store(true, std::memory_order_release);
			slot->processFailed.store(true, std::memory_order_release);
			if (lightHost::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
		}
		catch (...)
		{
			lightHostLog("RealtimeHostProcessor prepare slot unknown exception '" + slot->description.name + "'");
			slot->processDisabled.store(true, std::memory_order_release);
			slot->processFailed.store(true, std::memory_order_release);
			if (lightHost::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1, std::memory_order_relaxed);
		}

		snapshot.totalLatencySamples += slot->getLatencySamples();
	}

	setLatencySamples(snapshot.totalLatencySamples);
    globalControls.prepare(preparedHostChannels, currentBlockSize, snapshot.totalLatencySamples, currentSampleRate);
	lightHostLog("RealtimeHostProcessor prepareSnapshot completed latencySamples=" + String(snapshot.totalLatencySamples));
}

void RealtimeHostProcessor::processSlot(PluginSlot& slot, AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    const int samples = buffer.getNumSamples();
    if (samples <= 0) return;
    if (slot.processDisabled.load(std::memory_order_acquire)) { slot.processBypass(buffer); return; }
    const int pluginChannels = jmax(slot.inputChannels, slot.outputChannels);
    if (pluginChannels > maxScratchChannels)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHost::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
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
    }
    catch (...)
    {
        slot.processDisabled.store(true);
        slot.processFailed.store(true);
        if (lightHost::diagnosticsCollectionEnabled.load(std::memory_order_relaxed)) processFailureCount.fetch_add(1);
        slot.processBypass(buffer);
        return;
    }
    // Main output is the serial route; asymmetric and disabled buses don't leak
    // stale auxiliary samples into subsequent processors.
    const int copied = jmin(buffer.getNumChannels(), slot.mainOutputChannels);
    for (int channel = 0; channel < copied; ++channel) buffer.copyFrom(channel, 0, scratchBuffer, channel, 0, samples);
    for (int channel = copied; channel < buffer.getNumChannels(); ++channel) buffer.clear(channel, 0, samples);
}
