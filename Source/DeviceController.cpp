#include "DeviceController.h"
#include <algorithm>
#include <cmath>
#include "PluginInstances.h"
void lightHostModernLog(const String& message);

namespace
{
	String audioChannelStateKey(const String& backendName, const String& inputDeviceName, const String& outputDeviceName)
	{
		const String identity = backendName.trim() + "|" + inputDeviceName.trim() + "|" + outputDeviceName.trim();
		return "audioChannelState_" + String::toHexString(identity.hashCode64());
	}

	BigInteger parseChannelMask(const String& mask)
	{
		BigInteger bits;
		const String trimmed = mask.trim();
		for (int i = 0; i < trimmed.length(); ++i)
		{
			const int bitIndex = trimmed.length() - 1 - i;
			if (trimmed[i] == '1')
				bits.setBit(bitIndex, true);
		}

		return bits;
	}

	void applyStoredChannelMask(BigInteger& target, const String& mask, int channelCount)
	{
		if (mask.isEmpty() || channelCount <= 0)
			return;

		const auto stored = parseChannelMask(mask);
		target.clear();
		for (int i = 0; i < channelCount; ++i)
			target.setBit(i, stored[i]);
	}

	String normaliseAudioPersistenceMode(String mode)
	{
		mode = mode.trim().toLowerCase();
		if (mode == "last" || mode == "last-selected" || mode == "lastselected")
			return "lastSelected";
		if (mode == "custom")
			return "custom";
		return "disabled";
	}

	int clampRecoveryRetrySeconds(int value)
	{
		return jlimit(1, 60, value);
	}

	int clampRecoveryRetryAttempts(int value)
	{
		return jlimit(1, 100, value);
	}

	String quotedTarget(const String& backend, const String& input, const String& output)
	{
		if (backend.equalsIgnoreCase("ASIO"))
			return backend + " / " + (output.isNotEmpty() ? output : input);

		return backend + " / input: " + (input.isNotEmpty() ? input : "none")
			+ " / output: " + (output.isNotEmpty() ? output : "none");
	}

	StringArray readSettingLines(PropertySet& preferences, const String& key)
	{
		StringArray values;
		values.addLines(preferences.getValue(key));
		values.trim();
		values.removeEmptyStrings();
		return values;
	}

	void writeSettingLines(PropertySet& preferences, const String& key, const StringArray& values)
	{
		preferences.setValue(key, values.joinIntoString("\n"));
	}

	bool stringArrayContainsIgnoreCase(const StringArray& values, const String& value)
	{
		for (const auto& existing : values)
		{
			if (existing.equalsIgnoreCase(value))
				return true;
		}

		return false;
	}

	bool removeStringIgnoreCase(StringArray& values, const String& value)
	{
		for (int i = values.size(); --i >= 0;)
		{
			if (values[i].equalsIgnoreCase(value))
			{
				values.remove(i);
				return true;
			}
		}

		return false;
	}

	String makeBlockedDeviceEntry(const String& backend, const String& role, const String& device)
	{
		return backend.trim() + "|" + role.trim().toLowerCase() + "|" + device.trim();
	}

	BlockedAudioDeviceChoice parseBlockedDeviceEntry(const String& entry)
	{
		StringArray parts;
		parts.addTokens(entry, "|", {});
		parts.trim();

		BlockedAudioDeviceChoice choice;
		if (parts.size() >= 3)
		{
			choice.backendName = parts[0];
			choice.role = parts[1].toLowerCase();
			choice.deviceName = parts[2];
		}

		return choice;
	}

	String normaliseBlockedDeviceRole(const String& role)
	{
		if (role.equalsIgnoreCase("input") || role.equalsIgnoreCase("output") || role.equalsIgnoreCase("device"))
			return role.toLowerCase();

		return "device";
	}

	bool backendCanUsePracticalBufferChoices(const String& backendName)
	{
		return backendName.startsWithIgnoreCase("Windows Audio")
			|| backendName.equalsIgnoreCase("DirectSound");
	}

	void addUniqueSampleRate(std::vector<double>& sampleRates, double sampleRate)
	{
		if (sampleRate <= 0.0)
			return;

		const int roundedRate = roundToInt(sampleRate);
		const auto alreadyExists = std::any_of(sampleRates.begin(), sampleRates.end(), [roundedRate](double existing)
		{
			return roundToInt(existing) == roundedRate;
		});

		if (!alreadyExists)
			sampleRates.push_back(sampleRate);
	}

	void addUniqueBufferSize(std::vector<int>& bufferSizes, int bufferSize)
	{
		if (bufferSize <= 0)
			return;

		if (std::find(bufferSizes.begin(), bufferSizes.end(), bufferSize) == bufferSizes.end())
			bufferSizes.push_back(bufferSize);
	}

	void addPracticalSharedAudioBufferChoices(std::vector<int>& bufferSizes)
	{
		static constexpr int practicalSizes[] =
		{
			32, 64, 96, 128, 160, 192, 224, 256, 320, 384, 448, 512,
			640, 768, 896, 1024, 1536, 2048, 4096
		};

		for (const auto size : practicalSizes)
			addUniqueBufferSize(bufferSizes, size);
	}
}

String DeviceController::apply(AudioDeviceManager& manager, const String& backend,
    const AudioDeviceManager::AudioDeviceSetup& setup)
{
    const ScopedValueSetter<String> openingType(openingBackend, backend),
        openingIn(openingInput, setup.inputDeviceName), openingOut(openingOutput, setup.outputDeviceName);
    const auto applyingGeneration = generation;
    if (!isAudioDeviceChoiceAllowed(backend, setup.inputDeviceName, setup.outputDeviceName))
        return "Audio device is blocked by settings";
    if (setup.inputDeviceName.isEmpty() && setup.outputDeviceName.isEmpty())
        return "No audio device was selected";
    bool exists = false;
    for (auto* type : manager.getAvailableDeviceTypes())
        exists = exists || type->getTypeName() == backend;
    if (!exists) return "Audio backend is unavailable: " + backend;
    auto verify = [&](const String& error) -> String {
        if (applyingGeneration != generation)
        {
            manager.closeAudioDevice();
            return "Audio configuration was superseded";
        }
        if (error.isNotEmpty()) return error;
        auto* actual = manager.getCurrentAudioDevice();
        manager.getAudioDeviceSetup(effectiveSetup);
        if (!actual || !actual->isOpen() || actual->getTypeName() != backend
            || effectiveSetup.inputDeviceName != setup.inputDeviceName
            || effectiveSetup.outputDeviceName != setup.outputDeviceName)
            return "The opened audio device does not match the requested configuration";
        if (!isAudioDeviceChoiceAllowed(backend, effectiveSetup.inputDeviceName, effectiveSetup.outputDeviceName))
        {
            manager.closeAudioDevice();
            return "Audio device is blocked by settings";
        }
        configuredBackend = backend;
        configuredSetup = setup;
        return {};
    };
    if (manager.getCurrentAudioDeviceType() == backend && manager.getCurrentAudioDevice() != nullptr)
        return verify(manager.setAudioDeviceSetup(setup, true));

    // initialise(XML) selects a backend and the complete setup in one operation.
    // setCurrentAudioDeviceType would first open that backend's default device.
    auto state = manager.createStateXml();
    if (!state) state = std::make_unique<XmlElement>("DEVICESETUP");
    state->setAttribute("deviceType", backend);
    state->removeAttribute("audioDeviceName");
    state->setAttribute("audioInputDeviceName", setup.inputDeviceName);
    state->setAttribute("audioOutputDeviceName", setup.outputDeviceName);
    state->setAttribute("audioDeviceRate", setup.sampleRate);
    state->setAttribute("audioDeviceBufferSize", setup.bufferSize);
    if (setup.useDefaultInputChannels) state->removeAttribute("audioDeviceInChans");
    else state->setAttribute("audioDeviceInChans", setup.inputChannels.toString(2));
    if (setup.useDefaultOutputChannels) state->removeAttribute("audioDeviceOutChans");
    else state->setAttribute("audioDeviceOutChans", setup.outputChannels.toString(2));
    return verify(manager.initialise(256, 256, state.get(), false));
}

DeviceController::DeviceController(AudioDeviceManager& manager, PropertySet& settings,
    std::function<void()> dirtyCallback, std::function<void()> reconfigureCallback,
    std::function<Clock::time_point()> clockCallback)
    : deviceManager(manager), preferences(settings), dirty(std::move(dirtyCallback)),
      reconfigure(std::move(reconfigureCallback)), clock(std::move(clockCallback))
{
}

var DeviceController::selectionState() const
{
    auto* result = new DynamicObject;
    result->setProperty("generation", String(generation));
    result->setProperty("configured", lightHostModern::audioSelection::setupJson(configuredBackend, configuredSetup));
    AudioDeviceManager::AudioDeviceSetup current;
    auto* device = deviceManager.getCurrentAudioDevice();
    const bool available = device && device->isOpen();
    if (available)
    {
        deviceManager.getAudioDeviceSetup(current);
        current.sampleRate = device->getCurrentSampleRate(); current.bufferSize = device->getCurrentBufferSizeSamples();
        current.inputChannels = device->getActiveInputChannels(); current.outputChannels = device->getActiveOutputChannels();
        current.useDefaultInputChannels = current.useDefaultOutputChannels = false;
    }
    result->setProperty("effective", lightHostModern::audioSelection::setupJson(available ? device->getTypeName() : String(), current));
    const bool attempted = attemptedGeneration == generation && attemptedBackend.isNotEmpty();
    result->setProperty("attempted", attempted ? lightHostModern::audioSelection::setupJson(attemptedBackend, attemptedSetup) : var());
    result->setProperty("editable", available ? lightHostModern::audioSelection::setupJson(device->getTypeName(), current)
        : lightHostModern::audioSelection::setupJson(attempted ? attemptedBackend : configuredBackend, attempted ? attemptedSetup : configuredSetup));
    result->setProperty("driverAvailable", available); result->setProperty("error", lastAudioConfigurationError);
    result->setProperty("processingAvailable", available && device->isPlaying());
    result->setProperty("suspended", audioStartSuspended);
    result->setProperty("preferenceKey", monoInputsKey());
    result->setProperty("mainOutputPairActive", available && current.outputChannels[0] && current.outputChannels[1]);
    result->setProperty("recoveryState", audioRecoveryState); result->setProperty("attempt", failedAudioRecoveryAttempts);
    return var(result);
}

var DeviceController::optionsForBackend(const String& backend)
{
    auto* result = new DynamicObject;
    result->setProperty("status", "ok"); result->setProperty("backend", backend);
    result->setProperty("generation", String(generation));
    result->setProperty("available", false); result->setProperty("separateInputsAndOutputs", true);
    Array<var> inputs, outputs;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (!type || type->getTypeName() != backend || isAudioBackendBlocked(backend)) continue;
        type->scanForDevices();
        const bool separate = type->hasSeparateInputsAndOutputs();
        result->setProperty("available", true); result->setProperty("separateInputsAndOutputs", separate);
        for (const auto& name : type->getDeviceNames(true))
            if (!isAudioDeviceBlocked(backend, separate ? "input" : "device", name)) inputs.add(name);
        for (const auto& name : type->getDeviceNames(false))
            if (!isAudioDeviceBlocked(backend, separate ? "output" : "device", name)) outputs.add(name);
        const auto suggested = [&](bool input, const Array<var>& allowed) -> String {
            const auto names = type->getDeviceNames(input);
            const auto index = type->getDefaultDeviceIndex(input);
            const auto preferred = isPositiveAndBelow(index, names.size()) ? names[index] : String();
            for (const auto& name : allowed) if (name.toString() == preferred) return preferred;
            return allowed.isEmpty() ? String() : allowed.getFirst().toString();
        };
        const auto output = suggested(false, outputs);
        result->setProperty("suggestedOutput", output);
        result->setProperty("suggestedInput", separate ? suggested(true, inputs) : output);
        break;
    }
    result->setProperty("inputs", inputs); result->setProperty("outputs", outputs);
    return var(result);
}

bool DeviceController::restoreProfileConfiguration(const AudioDeviceSelection& request)
{
    if (selectConfiguration(request)) return true;
    // A profile must never keep playing through the previous profile's device
    // when its own target cannot be opened. Keep the requested identity for retry.
    deviceManager.closeAudioDevice();
    configuredBackend = request.backend; configuredSetup = request.setup; effectiveSetup = {};
    attemptedBackend = request.backend; attemptedSetup = request.setup; attemptedGeneration = generation;
    audioStartSuspended = false; applicationsSuspended = false;
    audioRecoveryState = "failed"; audioRecoveryMessage = lastAudioConfigurationError;
    preferences.setValue("audioSelectionSuspended", false);
    for (const auto* prefix : {"audioPersistenceLast", "audioPersistenceCustom"})
    {
        preferences.setValue(String(prefix) + "Backend", request.backend);
        preferences.setValue(String(prefix) + "InputDevice", request.setup.inputDeviceName);
        preferences.setValue(String(prefix) + "OutputDevice", request.setup.outputDeviceName);
    }
    XmlElement saved("DEVICESETUP"); saved.setAttribute("deviceType", request.backend);
    saved.setAttribute("audioInputDeviceName", request.setup.inputDeviceName); saved.setAttribute("audioOutputDeviceName", request.setup.outputDeviceName);
    saved.setAttribute("audioDeviceRate", request.setup.sampleRate); saved.setAttribute("audioDeviceBufferSize", request.setup.bufferSize);
    saved.setAttribute("audioDeviceInChans", request.setup.inputChannels.toString(2)); saved.setAttribute("audioDeviceOutChans", request.setup.outputChannels.toString(2));
    preferences.setValue("audioDeviceState", &saved); ++audioConfigVersion; markSettingsDirty(); scheduleRetry();
    return false;
}

bool DeviceController::selectConfiguration(const AudioDeviceSelection& request)
{
    if (request.expectedGeneration != generation)
    { lastAudioConfigurationError = "Audio configuration was superseded"; return false; }
    if (!isAudioDeviceChoiceAllowed(request.backend, request.setup.inputDeviceName, request.setup.outputDeviceName))
    { lastAudioConfigurationError = "Audio device is blocked by settings"; return false; }
    ScopedValueSetter<bool> manual(manualAudioSelectionInProgress, true);
    AudioDeviceManager::AudioDeviceSetup previous;
    deviceManager.getAudioDeviceSetup(previous);
    const auto* previousDevice = deviceManager.getCurrentAudioDevice();
    const auto previousBackend = previousDevice ? previousDevice->getTypeName() : String();
    const auto previousTarget = configuredSetup; const auto previousTargetBackend = configuredBackend;
    invalidateConfiguration(); const auto selectedGeneration = generation;
    attemptedGeneration = generation; attemptedBackend = request.backend; attemptedSetup = request.setup;
    lastAudioConfigurationError.clear();
    const bool none = request.setup.inputDeviceName.isEmpty() && request.setup.outputDeviceName.isEmpty();
    if (!none)
    {
        const auto options = optionsForBackend(request.backend);
        const auto contains = [&](const char* role, const String& name) {
            if (name.isEmpty()) return true;
            if (const auto* list = options[role].getArray())
                for (const auto& value : *list) if (value.toString() == name) return true;
            return false;
        };
        if (!static_cast<bool>(options["available"]) || !contains("inputs", request.setup.inputDeviceName)
            || !contains("outputs", request.setup.outputDeviceName))
        { lastAudioConfigurationError = "The selected audio device is unavailable"; return false; }
        if (!static_cast<bool>(options["separateInputsAndOutputs"]) && request.setup.inputDeviceName != request.setup.outputDeviceName)
        { lastAudioConfigurationError = "This backend requires the same input and output device"; return false; }
    }
    if (none)
    {
        deviceManager.closeAudioDevice();
        configuredBackend = request.backend; configuredSetup = request.setup; effectiveSetup = {};
        // Explicit None remains suspended until another explicit selection/retry.
        audioStartSuspended = true; applicationsSuspended = true; audioRecoveryState = "suspended";
        audioRecoveryMessage.clear();
        preferences.setValue("audioSelectionSuspended", true); markSettingsDirty();
        loadActivePlugins(); return true;
    }
    auto desired = request.setup;
    // A default-channel selection reuses the saved mask for that exact device
    // pair. Explicit masks always take precedence, including an empty mask.
    auto saved = desired;
    applySavedAudioChannelState(saved, request.backend, desired.inputDeviceName, desired.outputDeviceName);
    if (desired.useDefaultInputChannels) { desired.inputChannels = saved.inputChannels; desired.useDefaultInputChannels = saved.useDefaultInputChannels; }
    if (desired.useDefaultOutputChannels) { desired.outputChannels = saved.outputChannels; desired.useDefaultOutputChannels = saved.useDefaultOutputChannels; }
    const auto error = apply(deviceManager, request.backend, desired);
    if (selectedGeneration != generation) return false;
    if (error.isNotEmpty())
    {
        if (previousBackend.isNotEmpty() && isAudioDeviceChoiceAllowed(previousBackend, previous.inputDeviceName, previous.outputDeviceName))
            (void) apply(deviceManager, previousBackend, previous);
        else deviceManager.closeAudioDevice();
        if (selectedGeneration == generation) { configuredSetup = previousTarget; configuredBackend = previousTargetBackend; lastAudioConfigurationError = error; }
        return false;
    }
    saveAudioDeviceState(); rememberManualSelectedAudioDevice();
    failedAudioRecoveryAttempts = 0; audioRecoveryState = "running"; audioRecoveryMessage.clear();
    loadActivePlugins(); return true;
}

bool DeviceController::setPreferredDevice(const String& backend, const String& input, const String& output, uint64 expectedGeneration)
{
    if (expectedGeneration != generation) { lastAudioConfigurationError = "Audio configuration was superseded"; return false; }
    if (!isAudioDeviceChoiceAllowed(backend, input, output)) { lastAudioConfigurationError = "Audio device is blocked by settings"; return false; }
    // Missing names are preserved as recovery targets; only exact identities are saved.
    invalidateConfiguration(); lastAudioConfigurationError.clear();
    preferences.setValue("audioPersistenceCustomBackend", backend);
    preferences.setValue("audioPersistenceCustomInputDevice", input);
    preferences.setValue("audioPersistenceCustomOutputDevice", output);
    markSettingsDirty(); return true;
}

void DeviceController::start(bool safeMode, bool suspended)
{
    if (const auto saved = lightHostModern::parseBoundedXml(preferences.getValue("audioDeviceState"), 4 * 1024 * 1024))
    {
        configuredBackend = saved->getStringAttribute("deviceType");
        configuredSetup.inputDeviceName = saved->getStringAttribute("audioInputDeviceName", saved->getStringAttribute("audioDeviceName"));
        configuredSetup.outputDeviceName = saved->getStringAttribute("audioOutputDeviceName", saved->getStringAttribute("audioDeviceName"));
        configuredSetup.sampleRate = saved->getDoubleAttribute("audioDeviceRate");
        configuredSetup.bufferSize = saved->getIntAttribute("audioDeviceBufferSize");
        configuredSetup.inputChannels.parseString(saved->getStringAttribute("audioDeviceInChans"), 2);
        configuredSetup.outputChannels.parseString(saved->getStringAttribute("audioDeviceOutChans"), 2);
        configuredSetup.useDefaultInputChannels = !saved->hasAttribute("audioDeviceInChans");
        configuredSetup.useDefaultOutputChannels = !saved->hasAttribute("audioDeviceOutChans");
    }
    audioStartSuspended = suspended || preferences.getBoolValue("audioSelectionSuspended", false);
    if (safeMode) audioStartSuspended = true;
    if (audioStartSuspended) { audioRecoveryState = "suspended"; return; }
    const auto config = getAudioRecoveryConfiguration();
    const auto mode = normaliseAudioPersistenceMode(config.mode);
    if (!hasPermittedCandidate())
    {
        applicationsSuspended = true;
        audioRecoveryState = "blocked";
        audioRecoveryMessage = "No permitted audio device is available.";
    }
    else if (safeMode || mode == "disabled")
    {
        if (configuredBackend.isEmpty()) { audioRecoveryState = "unconfigured"; applicationsSuspended = true; }
        else
        {
            lastAudioConfigurationError = apply(deviceManager, configuredBackend, configuredSetup);
            if (lastAudioConfigurationError.isNotEmpty()) { audioRecoveryState = "failed"; deviceManager.closeAudioDevice(); }
        }
    }
    else
    {
        audioRecoveryState = "retrying";
        applyPreferredAudioDevice(config, false);
    }
    inventory = deviceInventory();
    scheduleRetry();
}

void DeviceController::scheduleRetry()
{
    scheduledGeneration = generation;
    nextRetry = clock() + std::chrono::seconds(getAudioRecoveryConfiguration().retrySeconds);
}

void DeviceController::invalidateConfiguration()
{
    ++generation;
    ++audioConfigVersion;
    scheduledGeneration = generation;
    nextRetry = clock();
    failedAudioRecoveryAttempts = 0;
    applicationsSuspended = false;
}

String DeviceController::deviceInventory() const
{
    String result;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        result << type->getTypeName() << ":" << type->getDeviceNames(true).joinIntoString("|")
               << ":" << type->getDeviceNames(false).joinIntoString("|") << "\n";
    return result;
}

bool DeviceController::hasPermittedCandidate()
{
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (isAudioBackendBlocked(type->getTypeName())) continue;
        type->scanForDevices();
        const auto inputs = type->getDeviceNames(true), outputs = type->getDeviceNames(false);
        if (!type->hasSeparateInputsAndOutputs())
        {
            for (const auto& name : outputs)
                if (isAudioDeviceChoiceAllowed(type->getTypeName(), name, name)) return true;
            continue;
        }
        for (const auto& name : outputs)
            if (isAudioDeviceChoiceAllowed(type->getTypeName(), {}, name)) return true;
        for (const auto& name : inputs)
            if (isAudioDeviceChoiceAllowed(type->getTypeName(), name, {})) return true;
    }
    return false;
}

void DeviceController::tick()
{
    if (audioStartSuspended || manualAudioSelectionInProgress || applicationsSuspended
        || scheduledGeneration != generation || clock() < nextRetry) return;
    scheduleRetry();
    closeCurrentAudioDeviceIfBlocked("recovery");
    if (!hasPermittedCandidate())
    {
        deviceManager.closeAudioDevice();
        applicationsSuspended = true;
        audioRecoveryState = "blocked";
        audioRecoveryMessage = "No permitted audio device is available.";
        ++audioConfigVersion;
        return;
    }
    const auto config = getAudioRecoveryConfiguration();
    const auto mode = normaliseAudioPersistenceMode(config.mode);
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device && device->isOpen() && device->isPlaying()
        && (mode == "disabled" || currentAudioDeviceMatchesPreferred(config)))
    {
        failedAudioRecoveryAttempts = 0;
        if (audioRecoveryState != "running") ++audioConfigVersion;
        audioRecoveryState = "running";
        audioRecoveryMessage.clear();
        return;
    }
    if (mode == "disabled")
    {
        const auto recoveringGeneration = generation;
        recoverIfNeeded(deviceManager, config, failedAudioRecoveryAttempts, audioRecoveryState, audioRecoveryMessage);
        if (generation == recoveringGeneration) saveAudioDeviceState();
        ++audioConfigVersion;
        return;
    }
    if (failedAudioRecoveryAttempts >= config.retryAttempts)
    {
        audioRecoveryState = "failed";
        audioRecoveryMessage = "The preferred audio device did not reconnect. Choose a device or retry manually.";
        return;
    }
    ++failedAudioRecoveryAttempts;
    audioRecoveryState = "retrying";
    applyPreferredAudioDevice(config, false);
    ++audioConfigVersion;
}

void DeviceController::devicesChanged()
{
    if (manualAudioSelectionInProgress) return;
    const auto updatedInventory = deviceInventory();
    if (inventory != updatedInventory)
    {
        inventory = updatedInventory;
        invalidateConfiguration();
    }
    if (audioStartSuspended) return;
    closeCurrentAudioDeviceIfBlocked("device change");
    const auto config = getAudioRecoveryConfiguration();
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device || !device->isOpen())
    {
        if (!applicationsSuspended) audioRecoveryState = "retrying";
        ++audioConfigVersion;
        return;
    }
    if (config.mode == "disabled" || currentAudioDeviceMatchesPreferred(config)) saveAudioDeviceState();
    ++audioConfigVersion;
}

String DeviceController::initialise(AudioDeviceManager& deviceManager, const XmlElement* savedAudioState, bool)
{
    if (!savedAudioState) return "No audio device was selected";
    const auto error = deviceManager.initialise(256, 256, savedAudioState, false);
    if (error.isNotEmpty()) deviceManager.closeAudioDevice();
    return error;
}

void DeviceController::recoverIfNeeded(AudioDeviceManager& deviceManager,
	AudioRecoveryConfiguration const& recoveryConfig,
	int& failedAudioRecoveryAttempts,
	String& recoveryState,
	String& recoveryMessage)
{
	const auto recoveringGeneration = generation;
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device != nullptr && device->isOpen() && device->isPlaying())
	{
		failedAudioRecoveryAttempts = 0;
		recoveryState = "running";
		recoveryMessage.clear();
		return;
	}

	if (normaliseAudioPersistenceMode(recoveryConfig.mode) != "disabled")
		return;

	if (failedAudioRecoveryAttempts >= 1)
		return;

	failedAudioRecoveryAttempts++;
	recoveryState = "retrying";
	recoveryMessage = "Audio device stopped; restarting the last audio device.";
	Logger::writeToLog("LightHostModern: audio device is not running; attempting restart");
    const auto error = configuredBackend.isEmpty() ? String("No audio device was selected")
        : apply(deviceManager, configuredBackend, configuredSetup);
	if (recoveringGeneration != generation) { deviceManager.closeAudioDevice(); return; }

	if (deviceManager.getCurrentAudioDevice() == nullptr
		|| !deviceManager.getCurrentAudioDevice()->isOpen()
		|| !deviceManager.getCurrentAudioDevice()->isPlaying())
	{
        deviceManager.closeAudioDevice();
        recoveryState = "failed";
        recoveryMessage = "The selected audio device is unavailable. Audio processing has stopped.";
        lastAudioConfigurationError = error;
	}
}

DiagnosticsSnapshot DeviceController::createDiagnosticsSnapshot(AudioDeviceManager& deviceManager, bool collect) const
{
	DiagnosticsSnapshot snapshot;
	const auto recoveryConfig = getAudioRecoveryConfiguration();
	const auto custom = recoveryConfig.mode == "custom";
	snapshot.recoveryState = audioRecoveryState;
	snapshot.recoveryMessage = audioRecoveryMessage;
	snapshot.recoveryAttempt = failedAudioRecoveryAttempts;
	snapshot.recoveryMaxAttempts = recoveryConfig.retryAttempts;
	snapshot.recoveryTargetBackend = custom ? recoveryConfig.customBackend : recoveryConfig.lastBackend;
	snapshot.recoveryTargetInputDevice = custom ? recoveryConfig.customInputDevice : recoveryConfig.lastInputDevice;
	snapshot.recoveryTargetOutputDevice = custom ? recoveryConfig.customOutputDevice : recoveryConfig.lastOutputDevice;
	snapshot.configurationGeneration = generation;
	snapshot.requestedSampleRate = configuredSetup.sampleRate;
	snapshot.requestedBufferSize = configuredSetup.bufferSize;
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();

	snapshot.backend = device != nullptr ? device->getTypeName() : "none";
	snapshot.driverAvailable = device != nullptr;
	snapshot.deviceName = device != nullptr ? device->getName() : "none";
	snapshot.cpuUsagePercent = collect ? deviceManager.getCpuUsage() * 100.0 : 0.0;
	snapshot.xRunCount = collect ? deviceManager.getXRunCount() : 0;
	snapshot.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
	snapshot.bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
	snapshot.inputLatency = collect && device != nullptr ? device->getInputLatencyInSamples() : -1;
	snapshot.outputLatency = collect && device != nullptr ? device->getOutputLatencyInSamples() : -1;
	snapshot.inputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
	snapshot.outputChannels = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 0;

	return snapshot;
}

AudioDeviceConfiguration DeviceController::getAudioDeviceConfiguration()
{
	AudioDeviceConfiguration config;
    if (auto* type = customBackendType())
    {
        const auto backend = type->getTypeName();
        const bool asio = backend.equalsIgnoreCase("ASIO");
        for (const auto& name : type->getDeviceNames(true))
            if (!isAudioDeviceBlocked(backend, asio ? "device" : "input", name)) config.customInputDeviceNames.push_back(name);
        for (const auto& name : type->getDeviceNames(false))
            if (!isAudioDeviceBlocked(backend, asio ? "device" : "output", name)) config.customOutputDeviceNames.push_back(name);
    }
	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
	const auto displayedBackend = currentDevice && currentDevice->isOpen() ? currentDevice->getTypeName()
        : attemptedGeneration == generation && attemptedBackend.isNotEmpty() ? attemptedBackend : configuredBackend;
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);

	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	for (int i = 0; i < deviceTypes.size(); ++i)
	{
		auto* type = deviceTypes[i];
		if (type == nullptr)
			continue;

		if (isAudioBackendBlocked(type->getTypeName()))
			continue;

		config.backendNames.push_back(type->getTypeName());
		if (displayedBackend == type->getTypeName())
			config.currentBackendIndex = (int) config.backendNames.size() - 1;
	}

	AudioIODeviceType* displayedType = nullptr;
    for (auto* type : deviceTypes) if (type && type->getTypeName() == displayedBackend && !isAudioBackendBlocked(displayedBackend)) displayedType = type;
	if (auto* currentType = displayedType)
	{
		const String backendName = currentType->getTypeName();
		const bool isAsioBackend = backendName.equalsIgnoreCase("ASIO");
		const auto inputs = currentType->getDeviceNames(true);
		const auto outputs = currentType->getDeviceNames(false);

		for (int i = 0; i < inputs.size(); ++i)
		{
			if (isAudioDeviceBlocked(backendName, isAsioBackend ? "device" : "input", inputs[i]))
				continue;

			config.inputDeviceNames.push_back(inputs[i]);
			if (currentDevice && currentDevice->isOpen() && inputs[i] == setup.inputDeviceName)
				config.currentInputDeviceIndex = (int) config.inputDeviceNames.size() - 1;
		}

		for (int i = 0; i < outputs.size(); ++i)
		{
			if (isAudioDeviceBlocked(backendName, isAsioBackend ? "device" : "output", outputs[i]))
				continue;

			config.outputDeviceNames.push_back(outputs[i]);
			if (currentDevice && currentDevice->isOpen() && outputs[i] == setup.outputDeviceName)
				config.currentOutputDeviceIndex = (int) config.outputDeviceNames.size() - 1;
		}
	}

	if (currentDevice != nullptr)
	{
		const auto rates = currentDevice->getAvailableSampleRates();
		const auto sizes = currentDevice->getAvailableBufferSizes();
		const auto inputNames = currentDevice->getInputChannelNames();
		const auto outputNames = currentDevice->getOutputChannelNames();
		const auto activeInputs = currentDevice->getActiveInputChannels();
		const auto activeOutputs = currentDevice->getActiveOutputChannels();

		addUniqueSampleRate(config.sampleRates, currentDevice->getCurrentSampleRate());
		for (auto rate : rates)
			addUniqueSampleRate(config.sampleRates, rate);

		addUniqueBufferSize(config.bufferSizes, currentDevice->getCurrentBufferSizeSamples());
		for (auto size : sizes)
			addUniqueBufferSize(config.bufferSizes, size);

		if (backendCanUsePracticalBufferChoices(currentDevice->getTypeName()))
			addPracticalSharedAudioBufferChoices(config.bufferSizes);

		std::sort(config.sampleRates.begin(), config.sampleRates.end());
		std::sort(config.bufferSizes.begin(), config.bufferSizes.end());

		for (int i = 0; i < inputNames.size(); ++i)
		{
			config.inputChannelNames.push_back(inputNames[i]);
			config.activeInputChannels.push_back(activeInputs[i]);
		}
		for (int i = 0; i < outputNames.size(); ++i)
		{
			config.outputChannelNames.push_back(outputNames[i]);
			config.activeOutputChannels.push_back(activeOutputs[i]);
		}

		config.currentInputChannels = currentDevice->getActiveInputChannels().countNumberOfSetBits();
		config.currentOutputChannels = currentDevice->getActiveOutputChannels().countNumberOfSetBits();
		config.maxInputChannels = currentDevice->getInputChannelNames().size();
		config.maxOutputChannels = currentDevice->getOutputChannelNames().size();
	}

	return config;
}

AudioRecoveryConfiguration DeviceController::getAudioRecoveryConfiguration() const
{
	auto* settings = preferencesPtr();
	AudioRecoveryConfiguration config;
	config.mode = normaliseAudioPersistenceMode(settings->getValue("audioPersistenceMode", "disabled"));
	config.retrySeconds = clampRecoveryRetrySeconds(settings->getIntValue("audioPersistenceRetrySeconds", 5));
	config.retryAttempts = clampRecoveryRetryAttempts(settings->getIntValue("audioPersistenceRetryAttempts", 10));
	config.customBackend = settings->getValue("audioPersistenceCustomBackend");
	config.customInputDevice = settings->getValue("audioPersistenceCustomInputDevice");
	config.customOutputDevice = settings->getValue("audioPersistenceCustomOutputDevice");
	config.lastBackend = settings->getValue("audioPersistenceLastBackend");
	config.lastInputDevice = settings->getValue("audioPersistenceLastInputDevice");
	config.lastOutputDevice = settings->getValue("audioPersistenceLastOutputDevice");
	return config;
}

AudioBlocklistConfiguration DeviceController::getAudioBlocklistConfiguration() const
{
	AudioBlocklistConfiguration config;

	for (const auto& backend : readSettingLines(preferences, "blockedAudioBackends"))
		config.blockedBackends.push_back(backend);

	for (const auto& entry : readSettingLines(preferences, "blockedAudioDevices"))
	{
		auto choice = parseBlockedDeviceEntry(entry);
		if (choice.backendName.isNotEmpty() && choice.role.isNotEmpty() && choice.deviceName.isNotEmpty())
			config.blockedDevices.push_back(choice);
	}

	return config;
}

AvailableAudioChoicesConfiguration DeviceController::getAvailableAudioChoicesConfiguration()
{
	AvailableAudioChoicesConfiguration config;

	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	for (int i = 0; i < deviceTypes.size(); ++i)
	{
		auto* type = deviceTypes[i];
		if (type == nullptr)
			continue;

		const String backendName = type->getTypeName();
		config.backendNames.push_back(backendName);
		config.backendEnabled.push_back(!isAudioBackendBlocked(backendName));

		type->scanForDevices();
		const bool isAsioBackend = backendName.equalsIgnoreCase("ASIO");

		if (isAsioBackend)
		{
			StringArray devices;
			devices.addArray(type->getDeviceNames(true));
			for (const auto& output : type->getDeviceNames(false))
				devices.addIfNotAlreadyThere(output);

			for (const auto& device : devices)
			{
				BlockedAudioDeviceChoice choice;
				choice.backendName = backendName;
				choice.role = "device";
				choice.deviceName = device;
				config.deviceChoices.push_back(choice);
				config.deviceEnabled.push_back(!isAudioDeviceBlocked(backendName, "device", device));
			}

			continue;
		}

		for (const auto& input : type->getDeviceNames(true))
		{
			BlockedAudioDeviceChoice choice;
			choice.backendName = backendName;
			choice.role = "input";
			choice.deviceName = input;
			config.deviceChoices.push_back(choice);
			config.deviceEnabled.push_back(!isAudioDeviceBlocked(backendName, "input", input));
		}

		for (const auto& output : type->getDeviceNames(false))
		{
			BlockedAudioDeviceChoice choice;
			choice.backendName = backendName;
			choice.role = "output";
			choice.deviceName = output;
			config.deviceChoices.push_back(choice);
			config.deviceEnabled.push_back(!isAudioDeviceBlocked(backendName, "output", output));
		}
	}

    String identity = String(generation);
    const auto add = [&](const String& value) { identity += ":" + String(value.length()) + ":" + value; };
    for (size_t i = 0; i < config.backendNames.size(); ++i) { add(config.backendNames[i]); add(config.backendEnabled[i] ? "1" : "0"); }
    for (size_t i = 0; i < config.deviceChoices.size(); ++i) {
        const auto& choice = config.deviceChoices[i]; add(choice.backendName); add(choice.role); add(choice.deviceName); add(config.deviceEnabled[i] ? "1" : "0");
    }
    config.token = juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
    return config;
}

String DeviceController::updateEnabledChoices(const var& request)
{
    const auto current = getAvailableAudioChoicesConfiguration();
    if (!request["token"].isString() || request["token"].toString() != current.token)
        return "The audio device list changed. Reopen Enabled devices and try again.";
    const auto* backends = request["backends"].getDynamicObject();
    const auto* devices = request["devices"].getDynamicObject();
    const auto* names = request["names"].getDynamicObject();
    if (!backends || !devices || !names) return "Invalid enabled-device transaction.";
    std::map<String, int> backendCounts, deviceCounts;
    for (const auto& name : current.backendNames) {
        if(name.containsAnyOf("|\r\n"))return "Ambiguous audio backend identity. No settings were changed.";
        ++backendCounts[name.toLowerCase()];
    }
    for (const auto& choice : current.deviceChoices) {
        if(choice.backendName.containsAnyOf("|\r\n")||choice.deviceName.containsAnyOf("|\r\n"))return "Ambiguous audio device identity. No settings were changed.";
        ++deviceCounts[(choice.backendName + "|" + choice.role + "|" + choice.deviceName).toLowerCase()];
    }
    auto nextBackends = readSettingLines(preferences, "blockedAudioBackends");
    auto nextDevices = readSettingLines(preferences, "blockedAudioDevices");
    auto aliases = lightHostModern::parseBoundedJson(preferences.getValue("audioDeviceAliases", "{}"));
    if (!aliases.isObject()) aliases = var(new DynamicObject);
    const auto editFlags = [](const DynamicObject& edits, const std::map<String,int>& available, StringArray& blocked) {
        for (const auto& edit : edits.getProperties()) {
            const auto key = edit.name.toString(); const auto it = available.find(key.toLowerCase());
            if (it == available.end() || it->second != 1 || !edit.value.isBool()) return false;
            for (int i = blocked.size(); --i >= 0;) if (blocked[i].equalsIgnoreCase(key)) blocked.remove(i);
            if (!static_cast<bool>(edit.value)) blocked.add(key);
        }
        return true;
    };
    if (!editFlags(*backends, backendCounts, nextBackends) || !editFlags(*devices, deviceCounts, nextDevices))
        return "An audio device is missing or ambiguous. No settings were changed.";
    for (const auto& edit : names->getProperties()) {
        const auto key = edit.name.toString(); String normalized;
        if (deviceCounts[key.toLowerCase()] != 1 || !edit.value.isString() || !lightHostModern::normalizeInstanceName(edit.value.toString(), normalized))
            return "Invalid or ambiguous device name. No settings were changed.";
        if (normalized.isEmpty()) aliases.getDynamicObject()->removeProperty(edit.name);
        else aliases.getDynamicObject()->setProperty(edit.name, normalized);
    }
    // All validation precedes all changes. One inventory and one invalidation.
    writeSettingLines(preferences, "blockedAudioBackends", nextBackends);
    writeSettingLines(preferences, "blockedAudioDevices", nextDevices);
    preferences.setValue("audioDeviceAliases", JSON::toString(aliases, true));
    invalidateConfiguration(); markSettingsDirty(); ++audioConfigVersion;
    closeCurrentAudioDeviceIfBlocked("enabled device transaction");
    return {};
}

bool DeviceController::isAudioBackendBlocked(const String& backendName) const
{
	if (backendName.isEmpty())
		return false;

	return stringArrayContainsIgnoreCase(readSettingLines(preferences, "blockedAudioBackends"), backendName);
}

bool DeviceController::isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const
{
	if (backendName.isEmpty() || deviceName.isEmpty())
		return false;

	const String normalisedRole = normaliseBlockedDeviceRole(role);
	for (const auto& entry : readSettingLines(preferences, "blockedAudioDevices"))
	{
		const auto choice = parseBlockedDeviceEntry(entry);
		if (!choice.backendName.equalsIgnoreCase(backendName) || !choice.deviceName.equalsIgnoreCase(deviceName))
			continue;

		if (choice.role == "device" || choice.role == normalisedRole)
			return true;
	}

	return false;
}

bool DeviceController::isAudioDeviceChoiceAllowed(const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName) const
{
	if (isAudioBackendBlocked(backendName))
		return false;

	if (backendName.equalsIgnoreCase("ASIO"))
	{
		const String deviceName = outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName;
		return !isAudioDeviceBlocked(backendName, "device", deviceName)
			&& !isAudioDeviceBlocked(backendName, "input", deviceName)
			&& !isAudioDeviceBlocked(backendName, "output", deviceName);
	}

	return !isAudioDeviceBlocked(backendName, "input", inputDeviceName)
		&& !isAudioDeviceBlocked(backendName, "output", outputDeviceName);
}

bool DeviceController::isAudioDeviceCreationAllowed(const String& backend, const String& input, const String& output) const
{
    const auto& targetBackend = openingBackend.isNotEmpty() ? openingBackend : configuredBackend;
    const auto& targetInput = openingBackend.isNotEmpty() ? openingInput : configuredSetup.inputDeviceName;
    const auto& targetOutput = openingBackend.isNotEmpty() ? openingOutput : configuredSetup.outputDeviceName;
    return backend == targetBackend && input == targetInput && output == targetOutput
        && isAudioDeviceChoiceAllowed(backend, input, output);
}

String DeviceController::monoInputsKey() const
{
    if (configuredBackend.isEmpty()) return {};
    // Length-delimited, reversible identity; device names may contain separators.
    String identity;
    for (const auto& part : { configuredBackend, configuredSetup.inputDeviceName, configuredSetup.outputDeviceName })
        identity += String(part.length()) + ":" + part;
    return "monoInputsV1_" + Base64::toBase64(identity);
}

String DeviceController::monoOutputKey() const
{
    const auto inputKey = monoInputsKey();
    return inputKey.isEmpty() ? String() : "monoOutputV1_" + inputKey.substring(13);
}

bool DeviceController::currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const
{
	const String mode = normaliseAudioPersistenceMode(recoveryConfig.mode);
	if (mode == "disabled")
		return true;

	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
	if (currentDevice == nullptr || !currentDevice->isOpen())
		return false;

	const String targetBackend = mode == "custom" ? recoveryConfig.customBackend : recoveryConfig.lastBackend;
	String targetInput = mode == "custom" ? recoveryConfig.customInputDevice : recoveryConfig.lastInputDevice;
	String targetOutput = mode == "custom" ? recoveryConfig.customOutputDevice : recoveryConfig.lastOutputDevice;

	if (targetBackend.isEmpty() || !currentDevice->getTypeName().equalsIgnoreCase(targetBackend))
		return false;

	const bool isAsioBackend = targetBackend.equalsIgnoreCase("ASIO");
	if (isAsioBackend)
	{
		const String targetDevice = targetOutput.isNotEmpty() ? targetOutput : targetInput;
		if (targetDevice.isEmpty())
			return false;

		return currentDevice->getName().equalsIgnoreCase(targetDevice)
			&& isAudioDeviceChoiceAllowed(targetBackend, targetDevice, targetDevice);
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);

	if (targetInput.isEmpty() && targetOutput.isEmpty())
		return false;

	if (targetInput.isNotEmpty() && !setup.inputDeviceName.equalsIgnoreCase(targetInput))
		return false;

	if (targetOutput.isNotEmpty() && !setup.outputDeviceName.equalsIgnoreCase(targetOutput))
		return false;

	return isAudioDeviceChoiceAllowed(targetBackend, setup.inputDeviceName, setup.outputDeviceName);
}

void DeviceController::closeCurrentAudioDeviceIfBlocked(const String& context)
{
	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
	if (currentDevice == nullptr)
		return;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);

	const String backendName = currentDevice->getTypeName();
	if (isAudioDeviceChoiceAllowed(backendName, setup.inputDeviceName, setup.outputDeviceName))
		return;

	lastAudioConfigurationError = "Current audio device is blocked by settings: "
		+ quotedTarget(backendName, setup.inputDeviceName, setup.outputDeviceName);
	audioRecoveryState = "failed";
	audioRecoveryMessage = lastAudioConfigurationError;
	lightHostModernLog("AudioEngine closed blocked audio device during " + context + ": " + lastAudioConfigurationError);
	deviceManager.closeAudioDevice();
	audioConfigVersion++;
}

void DeviceController::rememberLastSelectedAudioDevice()
{
	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
    if (!currentDevice || !currentDevice->isOpen()) return;
	auto* settings = preferencesPtr();

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);

	const String backend = currentDevice != nullptr ? currentDevice->getTypeName()
		: (deviceManager.getCurrentDeviceTypeObject() != nullptr ? deviceManager.getCurrentDeviceTypeObject()->getTypeName() : String());

	if (backend.isEmpty())
		return;

	settings->setValue("audioPersistenceLastBackend", backend);
	settings->setValue("audioPersistenceLastInputDevice", setup.inputDeviceName);
	settings->setValue("audioPersistenceLastOutputDevice", setup.outputDeviceName);
	markSettingsDirty();
}

void DeviceController::rememberManualSelectedAudioDevice()
{
	audioStartSuspended = false;
    preferences.setValue("audioSelectionSuspended", false); markSettingsDirty();
    scheduleRetry();
	rememberLastSelectedAudioDevice();

	const auto recoveryConfig = getAudioRecoveryConfiguration();
	if (normaliseAudioPersistenceMode(recoveryConfig.mode) != "custom")
		return;

	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);

	const String backend = currentDevice != nullptr ? currentDevice->getTypeName()
		: (deviceManager.getCurrentDeviceTypeObject() != nullptr ? deviceManager.getCurrentDeviceTypeObject()->getTypeName() : String());

	if (backend.isEmpty())
		return;

	auto* settings = preferencesPtr();
	settings->setValue("audioPersistenceCustomBackend", backend);
	settings->setValue("audioPersistenceCustomInputDevice", setup.inputDeviceName);
	settings->setValue("audioPersistenceCustomOutputDevice", setup.outputDeviceName);
	markSettingsDirty();
	lightHostModernLog("AudioEngine manual audio selection updated custom persistence target='"
		+ quotedTarget(backend, setup.inputDeviceName, setup.outputDeviceName) + "'");
}

bool DeviceController::applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry)
{
	const String mode = normaliseAudioPersistenceMode(recoveryConfig.mode);
	if (mode == "disabled")
		return false;

	const String targetBackend = mode == "custom" ? recoveryConfig.customBackend : recoveryConfig.lastBackend;
	String targetInput = mode == "custom" ? recoveryConfig.customInputDevice : recoveryConfig.lastInputDevice;
	String targetOutput = mode == "custom" ? recoveryConfig.customOutputDevice : recoveryConfig.lastOutputDevice;

	if (targetBackend.isEmpty())
	{
		lastAudioConfigurationError = "Device persistence is enabled but no preferred audio backend is configured.";
		audioRecoveryState = "failed";
		audioRecoveryMessage = lastAudioConfigurationError;
		return false;
	}

	const bool isAsioBackend = targetBackend.equalsIgnoreCase("ASIO");
	if (isAsioBackend)
	{
		const String asioDevice = targetOutput.isNotEmpty() ? targetOutput : targetInput;
		targetInput = asioDevice;
		targetOutput = asioDevice;
	}

	if (!isAsioBackend && targetInput.isEmpty() && targetOutput.isEmpty())
	{
		lastAudioConfigurationError = "Device persistence is enabled but no preferred audio device is configured.";
		audioRecoveryState = "failed";
		audioRecoveryMessage = lastAudioConfigurationError;
		return false;
	}

	if (!isAudioDeviceChoiceAllowed(targetBackend, targetInput, targetOutput))
	{
		applicationsSuspended = true;
		closeCurrentAudioDeviceIfBlocked("preferred device policy");
		lastAudioConfigurationError = "Preferred audio device is blocked by settings: "
			+ quotedTarget(targetBackend, targetInput, targetOutput);
		audioRecoveryState = "failed";
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostModernLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostModernLog(String("AudioEngine audio persistence retry ")
		+ (manualRetry ? "manual" : "automatic")
		+ " target='" + quotedTarget(targetBackend, targetInput, targetOutput) + "'");

	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	AudioIODeviceType* targetType = nullptr;
	for (auto* type : deviceTypes)
	{
		if (type != nullptr && type->getTypeName() == targetBackend)
		{
			targetType = type;
			break;
		}
	}

	if (targetType == nullptr)
	{
		lastAudioConfigurationError = "Preferred audio backend is unavailable: " + targetBackend;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostModernLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	targetType->scanForDevices();
	auto* currentType = targetType;
	const auto inputDevices = currentType->getDeviceNames(true);
	const auto outputDevices = currentType->getDeviceNames(false);

	if (targetInput.isNotEmpty() && !inputDevices.contains(targetInput))
	{
		lastAudioConfigurationError = "Preferred input device is unavailable: " + targetInput;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostModernLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	if (targetOutput.isNotEmpty() && !outputDevices.contains(targetOutput))
	{
		lastAudioConfigurationError = "Preferred output device is unavailable: " + targetOutput;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostModernLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.inputDeviceName = targetInput;
	setup.outputDeviceName = targetOutput;
	setup.useDefaultInputChannels = true;
	setup.useDefaultOutputChannels = true;
	setup.inputChannels.clear();
	setup.outputChannels.clear();
	applySavedAudioChannelState(setup, targetBackend, setup.inputDeviceName, setup.outputDeviceName);

	const String error = apply(deviceManager, targetBackend, setup);
    if (error == "Audio configuration was superseded") return false;
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	AudioDeviceManager::AudioDeviceSetup selectedSetup;
	deviceManager.getAudioDeviceSetup(selectedSetup);
	const bool selectedBackendMismatch = selectedDevice == nullptr
		|| !selectedDevice->getTypeName().equalsIgnoreCase(targetBackend);
	const bool selectedDeviceMismatch = isAsioBackend
		&& selectedDevice != nullptr
		&& targetOutput.isNotEmpty()
		&& !selectedDevice->getName().equalsIgnoreCase(targetOutput);
	const bool selectedSetupMismatch = !isAsioBackend
		&& ((targetInput.isNotEmpty() && !selectedSetup.inputDeviceName.equalsIgnoreCase(targetInput))
			|| (targetOutput.isNotEmpty() && !selectedSetup.outputDeviceName.equalsIgnoreCase(targetOutput)));

	if (error.isNotEmpty()
		|| selectedDevice == nullptr
		|| !selectedDevice->isOpen()
		|| selectedBackendMismatch
		|| selectedDeviceMismatch
		|| selectedSetupMismatch)
	{
		lastAudioConfigurationError = "Failed to reconnect preferred audio device '"
			+ quotedTarget(targetBackend, targetInput, targetOutput) + "': "
			+ (error.isNotEmpty() ? error
				: (selectedBackendMismatch ? "selected backend did not match requested backend"
					: (selectedDeviceMismatch ? "selected ASIO device did not match requested device"
						: (selectedSetupMismatch ? "selected input/output device did not match requested device" : "device did not open"))));
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostModernLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	lastAudioConfigurationError.clear();
	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioBackendByIndex(int backendIndex)
{
	ScopedValueSetter<bool> manualAudioSelectionScope(manualAudioSelectionInProgress, true);
	invalidateConfiguration();
	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	lastAudioConfigurationError.clear();
	lightHostModernLog("AudioEngine setAudioBackendByIndex requested index=" + String(backendIndex)
		+ " availableTypes=" + String(deviceTypes.size()));

	if (backendIndex < 0)
	{
		lastAudioConfigurationError = "Invalid audio backend index: " + String(backendIndex);
		lightHostModernLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioIODeviceType* selectedType = nullptr;
	int visibleBackendIndex = -1;
	for (auto* type : deviceTypes)
	{
		if (type == nullptr)
			continue;

		if (isAudioBackendBlocked(type->getTypeName()))
		{
			lightHostModernLog("AudioEngine backend candidate blocked type='" + type->getTypeName() + "'");
			continue;
		}

		++visibleBackendIndex;
		lightHostModernLog("AudioEngine backend candidate visibleIndex=" + String(visibleBackendIndex)
			+ " type='" + type->getTypeName() + "'");
		if (visibleBackendIndex == backendIndex)
		{
			selectedType = type;
			break;
		}
	}

	if (selectedType == nullptr)
	{
		lastAudioConfigurationError = "Audio backend index was not found: " + String(backendIndex);
		lightHostModernLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	const String typeName = selectedType->getTypeName();
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
	{
		lightHostModernLog("AudioEngine current audio backend='" + currentDevice->getTypeName()
			+ "' device='" + currentDevice->getName()
			+ "' open=" + String(currentDevice->isOpen() ? "true" : "false"));
		if (currentDevice->getTypeName() == typeName)
		{
			lightHostModernLog("AudioEngine setAudioBackendByIndex no-op; already using backend '" + typeName + "'");
			rememberManualSelectedAudioDevice();
			return true;
		}
	}

	AudioDeviceManager::AudioDeviceSetup previousSetup;
	deviceManager.getAudioDeviceSetup(previousSetup);
	String previousTypeName;
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
		previousTypeName = currentDevice->getTypeName();

	lightHostModernLog("AudioEngine scanning requested backend '" + typeName + "'");
	selectedType->scanForDevices();
	auto* currentType = selectedType;
	const auto inputDevices = currentType->getDeviceNames(true);
	const auto outputDevices = currentType->getDeviceNames(false);
	lightHostModernLog("AudioEngine backend '" + typeName + "' device scan: inputs="
		+ String(inputDevices.size()) + " outputs=" + String(outputDevices.size()));
	for (int i = 0; i < inputDevices.size(); ++i)
		lightHostModernLog("AudioEngine backend '" + typeName + "' input[" + String(i) + "]='" + inputDevices[i] + "'");
	for (int i = 0; i < outputDevices.size(); ++i)
		lightHostModernLog("AudioEngine backend '" + typeName + "' output[" + String(i) + "]='" + outputDevices[i] + "'");

	struct DeviceCandidate
	{
		String input;
		String output;
	};

	std::vector<DeviceCandidate> candidates;
	for (const auto& output : outputDevices)
	{
		if (inputDevices.contains(output) && isAudioDeviceChoiceAllowed(typeName, output, output))
			candidates.push_back({ output, output });
	}

    if (candidates.empty())
    {
        auto inputs = inputDevices;
        auto outputs = outputDevices;
        if (inputs.isEmpty()) inputs.add(String());
        if (outputs.isEmpty()) outputs.add(String());
        for (const auto& input : inputs)
            for (const auto& output : outputs)
                if ((input.isNotEmpty() || output.isNotEmpty()) && isAudioDeviceChoiceAllowed(typeName, input, output))
                    candidates.push_back({input, output});
    }

	if (candidates.empty())
	{
		lastAudioConfigurationError = "No allowed devices are available for audio backend '" + typeName + "'.";
		lightHostModernLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		if (previousTypeName.isNotEmpty() && isAudioDeviceChoiceAllowed(previousTypeName, previousSetup.inputDeviceName, previousSetup.outputDeviceName))
			apply(deviceManager, previousTypeName, previousSetup);
		return false;
	}

	StringArray attemptErrors;
	for (int attempt = 0; attempt < (int) candidates.size(); ++attempt)
	{
		AudioDeviceManager::AudioDeviceSetup setup;
		deviceManager.getAudioDeviceSetup(setup);
		setup.inputDeviceName = candidates[(size_t) attempt].input;
		setup.outputDeviceName = candidates[(size_t) attempt].output;
		setup.useDefaultInputChannels = true;
		setup.useDefaultOutputChannels = true;
		setup.inputChannels.clear();
		setup.outputChannels.clear();
		setup.sampleRate = 0.0;
		setup.bufferSize = 0;
		applySavedAudioChannelState(setup, typeName, setup.inputDeviceName, setup.outputDeviceName);

		lightHostModernLog("AudioEngine setAudioDeviceSetup attempt=" + String(attempt + 1)
			+ "/" + String((int) candidates.size())
			+ " backend='" + typeName
			+ "' input='" + setup.inputDeviceName
			+ "' output='" + setup.outputDeviceName
			+ "' sampleRate=default bufferSize=default");

		const String error = apply(deviceManager, typeName, setup);
    if (error == "Audio configuration was superseded") return false;
		AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
		lightHostModernLog("AudioEngine setAudioDeviceSetup attempt=" + String(attempt + 1)
			+ " returned backend='" + typeName
			+ "' error='" + error
			+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

		if (selectedDevice != nullptr)
		{
			lightHostModernLog("AudioEngine selected device type='" + selectedDevice->getTypeName()
				+ "' name='" + selectedDevice->getName()
				+ "' open=" + String(selectedDevice->isOpen() ? "true" : "false")
				+ " sampleRate=" + String(selectedDevice->getCurrentSampleRate(), 0)
				+ " bufferSize=" + String(selectedDevice->getCurrentBufferSizeSamples())
				+ " inputLatency=" + String(selectedDevice->getInputLatencyInSamples())
				+ " outputLatency=" + String(selectedDevice->getOutputLatencyInSamples())
				+ " activeInputs=" + selectedDevice->getActiveInputChannels().toString(2)
				+ " activeOutputs=" + selectedDevice->getActiveOutputChannels().toString(2));
		}

		if (error.isEmpty()
			&& selectedDevice != nullptr
			&& selectedDevice->getTypeName() == typeName
			&& selectedDevice->isOpen())
		{
			saveAudioDeviceState();
			rememberManualSelectedAudioDevice();
			failedAudioRecoveryAttempts = 0;
			audioRecoveryState = "running";
			audioRecoveryMessage.clear();
			audioConfigVersion++;
			lightHostModernLog("AudioEngine setAudioBackendByIndex succeeded backend='" + typeName
				+ "' input='" + setup.inputDeviceName
				+ "' output='" + setup.outputDeviceName + "'");
			loadActivePlugins();
			return true;
		}

		attemptErrors.add("input='" + setup.inputDeviceName
			+ "' output='" + setup.outputDeviceName
			+ "' error='" + (error.isEmpty() ? "device did not open" : error) + "'");
	}

	lastAudioConfigurationError = "Failed to open audio backend '" + typeName + "': "
		+ attemptErrors.joinIntoString("; ");
	Logger::writeToLog("LightHostModern: " + lastAudioConfigurationError);
	lightHostModernLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
	if (previousTypeName.isNotEmpty() && isAudioDeviceChoiceAllowed(previousTypeName, previousSetup.inputDeviceName, previousSetup.outputDeviceName))
	{
		lightHostModernLog("AudioEngine restoring previous audio backend '" + previousTypeName + "'");
		apply(deviceManager, previousTypeName, previousSetup);
	}

	if (auto* restoredDevice = deviceManager.getCurrentAudioDevice())
	{
		lightHostModernLog("AudioEngine restored device type='" + restoredDevice->getTypeName()
			+ "' name='" + restoredDevice->getName()
			+ "' open=" + String(restoredDevice->isOpen() ? "true" : "false"));
	}

	return false;
}

bool DeviceController::setAudioInputDeviceByIndex(int deviceIndex)
{
	ScopedValueSetter<bool> manualAudioSelectionScope(manualAudioSelectionInProgress, true);
	invalidateConfiguration();
	lastAudioConfigurationError.clear();
	lightHostModernLog("AudioEngine setAudioInputDeviceByIndex requested index=" + String(deviceIndex));

	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr)
	{
		lastAudioConfigurationError = "No current audio backend is selected while setting input device";
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostModernLog("AudioEngine setAudioInputDeviceByIndex backend='" + currentType->getTypeName() + "'");

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(true);
	lightHostModernLog("AudioEngine input device scan backend='" + currentType->getTypeName()
		+ "' count=" + String(devices.size()));
	for (int i = 0; i < devices.size(); ++i)
		lightHostModernLog("AudioEngine input candidate[" + String(i) + "]='" + devices[i] + "'");

	const bool isAsioBackend = currentType->getTypeName().equalsIgnoreCase("ASIO");
	StringArray allowedDevices;
	for (const auto& device : devices)
	{
		if (!isAudioDeviceBlocked(currentType->getTypeName(), isAsioBackend ? "device" : "input", device))
			allowedDevices.add(device);
	}

	if (deviceIndex < 0 || deviceIndex >= allowedDevices.size())
	{
		lastAudioConfigurationError = "Invalid input device index " + String(deviceIndex)
			+ " for backend '" + currentType->getTypeName()
			+ "' with " + String(allowedDevices.size()) + " allowed devices";
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	const auto previousSetup = setup;
	const String requestedInputDevice = allowedDevices[deviceIndex];
	lightHostModernLog("AudioEngine current setup before input change backend='" + currentType->getTypeName()
		+ "' input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName
		+ "' sampleRate=" + String(setup.sampleRate, 0)
		+ " bufferSize=" + String(setup.bufferSize));

	if (isAsioBackend
		&& setup.inputDeviceName == requestedInputDevice
		&& setup.outputDeviceName == requestedInputDevice)
	{
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex no-op; already using ASIO device='" + requestedInputDevice + "'");
		rememberManualSelectedAudioDevice();
		return true;
	}

	if (!isAsioBackend && setup.inputDeviceName == requestedInputDevice)
	{
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex no-op; already using input='" + setup.inputDeviceName + "'");
		rememberManualSelectedAudioDevice();
		return true;
	}

	if (!isAudioDeviceChoiceAllowed(currentType->getTypeName(),
	                                requestedInputDevice,
	                                isAsioBackend ? requestedInputDevice : setup.outputDeviceName))
	{
		lastAudioConfigurationError = "Input device is blocked by settings: " + requestedInputDevice;
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	setup.inputDeviceName = requestedInputDevice;
	if (isAsioBackend)
	{
		setup.outputDeviceName = requestedInputDevice;
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex ASIO mode; input and output will use the same device");
	}
	applySavedAudioChannelState(setup, currentType->getTypeName(), setup.inputDeviceName, setup.outputDeviceName);

	lightHostModernLog("AudioEngine setAudioInputDeviceByIndex applying input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName + "'");

	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	lightHostModernLog("AudioEngine setAudioInputDeviceByIndex setAudioDeviceSetup returned error='" + error
		+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

	if (selectedDevice != nullptr)
	{
		lightHostModernLog("AudioEngine device after input change type='" + selectedDevice->getTypeName()
			+ "' name='" + selectedDevice->getName()
			+ "' open=" + String(selectedDevice->isOpen() ? "true" : "false")
			+ " sampleRate=" + String(selectedDevice->getCurrentSampleRate(), 0)
			+ " bufferSize=" + String(selectedDevice->getCurrentBufferSizeSamples())
			+ " inputLatency=" + String(selectedDevice->getInputLatencyInSamples())
			+ " outputLatency=" + String(selectedDevice->getOutputLatencyInSamples())
			+ " activeInputs=" + selectedDevice->getActiveInputChannels().toString(2)
			+ " activeOutputs=" + selectedDevice->getActiveOutputChannels().toString(2));
	}

	const bool selectedDeviceMismatch = isAsioBackend
		&& selectedDevice != nullptr
		&& selectedDevice->getName() != requestedInputDevice;

	if (selectedDeviceMismatch)
	{
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex ASIO selected device mismatch requested='"
			+ requestedInputDevice + "' actual='" + selectedDevice->getName() + "'");
	}

	if (error.isNotEmpty() || selectedDevice == nullptr || !selectedDevice->isOpen() || selectedDeviceMismatch)
	{
		lastAudioConfigurationError = "Failed to set input device '" + setup.inputDeviceName
			+ "' on backend '" + currentType->getTypeName() + "': "
			+ (error.isNotEmpty() ? error : (selectedDeviceMismatch ? "selected ASIO device did not match requested device" : "device did not open"));
		Logger::writeToLog("LightHostModern: " + lastAudioConfigurationError);
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		const String restoreError = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), previousSetup);
		lightHostModernLog("AudioEngine setAudioInputDeviceByIndex restored previous setup input='"
			+ previousSetup.inputDeviceName + "' output='" + previousSetup.outputDeviceName
			+ "' restoreError='" + restoreError + "'");
		return false;
	}

	saveAudioDeviceState();
	rememberManualSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioOutputDeviceByIndex(int deviceIndex)
{
	ScopedValueSetter<bool> manualAudioSelectionScope(manualAudioSelectionInProgress, true);
	invalidateConfiguration();
	lastAudioConfigurationError.clear();
	lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex requested index=" + String(deviceIndex));

	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr)
	{
		lastAudioConfigurationError = "No current audio backend is selected while setting output device";
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex backend='" + currentType->getTypeName() + "'");

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(false);
	lightHostModernLog("AudioEngine output device scan backend='" + currentType->getTypeName()
		+ "' count=" + String(devices.size()));
	for (int i = 0; i < devices.size(); ++i)
		lightHostModernLog("AudioEngine output candidate[" + String(i) + "]='" + devices[i] + "'");

	const bool isAsioBackend = currentType->getTypeName().equalsIgnoreCase("ASIO");
	StringArray allowedDevices;
	for (const auto& device : devices)
	{
		if (!isAudioDeviceBlocked(currentType->getTypeName(), isAsioBackend ? "device" : "output", device))
			allowedDevices.add(device);
	}

	if (deviceIndex < 0 || deviceIndex >= allowedDevices.size())
	{
		lastAudioConfigurationError = "Invalid output device index " + String(deviceIndex)
			+ " for backend '" + currentType->getTypeName()
			+ "' with " + String(allowedDevices.size()) + " allowed devices";
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	const auto previousSetup = setup;
	const String requestedOutputDevice = allowedDevices[deviceIndex];
	lightHostModernLog("AudioEngine current setup before output change backend='" + currentType->getTypeName()
		+ "' input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName
		+ "' sampleRate=" + String(setup.sampleRate, 0)
		+ " bufferSize=" + String(setup.bufferSize));

	if (isAsioBackend
		&& setup.inputDeviceName == requestedOutputDevice
		&& setup.outputDeviceName == requestedOutputDevice)
	{
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex no-op; already using ASIO device='" + requestedOutputDevice + "'");
		rememberManualSelectedAudioDevice();
		return true;
	}

	if (!isAsioBackend && setup.outputDeviceName == requestedOutputDevice)
	{
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex no-op; already using output='" + setup.outputDeviceName + "'");
		rememberManualSelectedAudioDevice();
		return true;
	}

	if (!isAudioDeviceChoiceAllowed(currentType->getTypeName(),
	                                isAsioBackend ? requestedOutputDevice : setup.inputDeviceName,
	                                requestedOutputDevice))
	{
		lastAudioConfigurationError = "Output device is blocked by settings: " + requestedOutputDevice;
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	setup.outputDeviceName = requestedOutputDevice;
	if (isAsioBackend)
	{
		setup.inputDeviceName = requestedOutputDevice;
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex ASIO mode; input and output will use the same device");
	}
	applySavedAudioChannelState(setup, currentType->getTypeName(), setup.inputDeviceName, setup.outputDeviceName);

	lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex applying input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName + "'");

	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex setAudioDeviceSetup returned error='" + error
		+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

	if (selectedDevice != nullptr)
	{
		lightHostModernLog("AudioEngine device after output change type='" + selectedDevice->getTypeName()
			+ "' name='" + selectedDevice->getName()
			+ "' open=" + String(selectedDevice->isOpen() ? "true" : "false")
			+ " sampleRate=" + String(selectedDevice->getCurrentSampleRate(), 0)
			+ " bufferSize=" + String(selectedDevice->getCurrentBufferSizeSamples())
			+ " inputLatency=" + String(selectedDevice->getInputLatencyInSamples())
			+ " outputLatency=" + String(selectedDevice->getOutputLatencyInSamples())
			+ " activeInputs=" + selectedDevice->getActiveInputChannels().toString(2)
			+ " activeOutputs=" + selectedDevice->getActiveOutputChannels().toString(2));
	}

	const bool selectedDeviceMismatch = isAsioBackend
		&& selectedDevice != nullptr
		&& selectedDevice->getName() != requestedOutputDevice;

	if (selectedDeviceMismatch)
	{
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex ASIO selected device mismatch requested='"
			+ requestedOutputDevice + "' actual='" + selectedDevice->getName() + "'");
	}

	if (error.isNotEmpty() || selectedDevice == nullptr || !selectedDevice->isOpen() || selectedDeviceMismatch)
	{
		lastAudioConfigurationError = "Failed to set output device '" + setup.outputDeviceName
			+ "' on backend '" + currentType->getTypeName() + "': "
			+ (error.isNotEmpty() ? error : (selectedDeviceMismatch ? "selected ASIO device did not match requested device" : "device did not open"));
		Logger::writeToLog("LightHostModern: " + lastAudioConfigurationError);
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		const String restoreError = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), previousSetup);
		lightHostModernLog("AudioEngine setAudioOutputDeviceByIndex restored previous setup input='"
			+ previousSetup.inputDeviceName + "' output='" + previousSetup.outputDeviceName
			+ "' restoreError='" + restoreError + "'");
		return false;
	}

	saveAudioDeviceState();
	rememberManualSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioPersistenceMode(const String& mode)
{
    invalidateConfiguration();
	const String normalised = normaliseAudioPersistenceMode(mode);
	auto* settings = preferencesPtr();
	settings->setValue("audioPersistenceMode", normalised);
	if (normalised == "disabled")
	{
		failedAudioRecoveryAttempts = 0;
		audioRecoveryState = "running";
		audioRecoveryMessage.clear();
	}

	markSettingsDirty();
	scheduleRetry();
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioPersistenceRetrySeconds(int seconds)
{
    invalidateConfiguration();
	preferencesPtr()->setValue("audioPersistenceRetrySeconds", clampRecoveryRetrySeconds(seconds));
	markSettingsDirty();
	scheduleRetry();
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioPersistenceRetryAttempts(int attempts)
{
    invalidateConfiguration();
	preferencesPtr()->setValue("audioPersistenceRetryAttempts", clampRecoveryRetryAttempts(attempts));
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

AudioIODeviceType* DeviceController::customBackendType() const
{
    const auto backend = getAudioRecoveryConfiguration().customBackend;
    if (backend.isEmpty() || isAudioBackendBlocked(backend)) return nullptr;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type && type->getTypeName() == backend) return type;
    return nullptr;
}

bool DeviceController::setAudioPersistenceCustomBackendByIndex(int backendIndex)
{
    invalidateConfiguration();
	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	if (backendIndex < 0)
		return false;

	AudioIODeviceType* selectedType = nullptr;
	int visibleIndex = -1;
	for (auto* type : deviceTypes)
	{
		if (type == nullptr || isAudioBackendBlocked(type->getTypeName()))
			continue;

		++visibleIndex;
		if (visibleIndex == backendIndex)
		{
			selectedType = type;
			break;
		}
	}

	if (selectedType == nullptr)
		return false;

	preferencesPtr()->setValue("audioPersistenceCustomBackend", selectedType->getTypeName());
	selectedType->scanForDevices();
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioPersistenceCustomInputByIndex(int deviceIndex)
{
    invalidateConfiguration();
	auto* currentType = customBackendType();
	if (currentType == nullptr)
		return false;

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(true);
	const bool isAsioBackend = currentType->getTypeName().equalsIgnoreCase("ASIO");
	StringArray allowedDevices;
	for (const auto& device : devices)
	{
		if (!isAudioDeviceBlocked(currentType->getTypeName(), isAsioBackend ? "device" : "input", device))
			allowedDevices.add(device);
	}

	if (deviceIndex < 0 || deviceIndex >= allowedDevices.size())
		return false;

	auto* settings = preferencesPtr();
	settings->setValue("audioPersistenceCustomBackend", currentType->getTypeName());
	settings->setValue("audioPersistenceCustomInputDevice", allowedDevices[deviceIndex]);
	if (isAsioBackend)
		settings->setValue("audioPersistenceCustomOutputDevice", allowedDevices[deviceIndex]);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioPersistenceCustomOutputByIndex(int deviceIndex)
{
    invalidateConfiguration();
	auto* currentType = customBackendType();
	if (currentType == nullptr)
		return false;

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(false);
	const bool isAsioBackend = currentType->getTypeName().equalsIgnoreCase("ASIO");
	StringArray allowedDevices;
	for (const auto& device : devices)
	{
		if (!isAudioDeviceBlocked(currentType->getTypeName(), isAsioBackend ? "device" : "output", device))
			allowedDevices.add(device);
	}

	if (deviceIndex < 0 || deviceIndex >= allowedDevices.size())
		return false;

	auto* settings = preferencesPtr();
	settings->setValue("audioPersistenceCustomBackend", currentType->getTypeName());
	settings->setValue("audioPersistenceCustomOutputDevice", allowedDevices[deviceIndex]);
	if (isAsioBackend)
		settings->setValue("audioPersistenceCustomInputDevice", allowedDevices[deviceIndex]);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool DeviceController::retryPreferredAudioDeviceNow()
{
    invalidateConfiguration();
	audioStartSuspended = false;
    preferences.setValue("audioSelectionSuspended", false); markSettingsDirty();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "retrying";
	audioRecoveryMessage = "Manual retry requested.";
	const auto config = getAudioRecoveryConfiguration();
	if (config.mode == "disabled")
	{
		tick();
		auto* device = deviceManager.getCurrentAudioDevice();
		return device && device->isOpen() && device->isPlaying();
	}
	return applyPreferredAudioDevice(config, true);
}

bool DeviceController::addBlockedAudioBackend(const String& backendName)
{
    invalidateConfiguration();
	const String trimmed = backendName.trim();
	if (trimmed.isEmpty())
		return false;

	auto values = readSettingLines(preferences, "blockedAudioBackends");
	if (!stringArrayContainsIgnoreCase(values, trimmed))
		values.add(trimmed);

	writeSettingLines(preferences, "blockedAudioBackends", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("backend blocklist update");
	audioConfigVersion++;
	return true;
}

bool DeviceController::addBlockedAudioInputDevice(const String& deviceName)
{
    invalidateConfiguration();
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || deviceName.trim().isEmpty())
		return false;

	const String backendName = currentType->getTypeName();
	const String role = backendName.equalsIgnoreCase("ASIO") ? "device" : "input";
	const String entry = makeBlockedDeviceEntry(backendName, role, deviceName);
	auto values = readSettingLines(preferences, "blockedAudioDevices");
	if (!stringArrayContainsIgnoreCase(values, entry))
		values.add(entry);

	writeSettingLines(preferences, "blockedAudioDevices", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("input device blocklist update");
	audioConfigVersion++;
	return true;
}

bool DeviceController::addBlockedAudioOutputDevice(const String& deviceName)
{
    invalidateConfiguration();
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || deviceName.trim().isEmpty())
		return false;

	const String backendName = currentType->getTypeName();
	const String role = backendName.equalsIgnoreCase("ASIO") ? "device" : "output";
	const String entry = makeBlockedDeviceEntry(backendName, role, deviceName);
	auto values = readSettingLines(preferences, "blockedAudioDevices");
	if (!stringArrayContainsIgnoreCase(values, entry))
		values.add(entry);

	writeSettingLines(preferences, "blockedAudioDevices", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("output device blocklist update");
	audioConfigVersion++;
	return true;
}

bool DeviceController::removeBlockedAudioBackend(int index)
{
    invalidateConfiguration();
	auto values = readSettingLines(preferences, "blockedAudioBackends");
	if (index < 0 || index >= values.size())
		return false;

	values.remove(index);
	writeSettingLines(preferences, "blockedAudioBackends", values);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool DeviceController::removeBlockedAudioDevice(int index)
{
    invalidateConfiguration();
	auto values = readSettingLines(preferences, "blockedAudioDevices");
	if (index < 0 || index >= values.size())
		return false;

	values.remove(index);
	writeSettingLines(preferences, "blockedAudioDevices", values);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioBackendEnabledByIndex(int index, bool enabled)
{
    invalidateConfiguration();
	const auto choices = getAvailableAudioChoicesConfiguration();
	if (index < 0 || index >= (int) choices.backendNames.size())
		return false;

	const String backendName = choices.backendNames[(size_t) index].trim();
	if (backendName.isEmpty())
		return false;

	auto values = readSettingLines(preferences, "blockedAudioBackends");
	bool changed = false;
	if (enabled)
	{
		changed = removeStringIgnoreCase(values, backendName);
	}
	else if (!stringArrayContainsIgnoreCase(values, backendName))
	{
		values.add(backendName);
		changed = true;
	}

	if (!changed)
		return true;

	writeSettingLines(preferences, "blockedAudioBackends", values);
	markSettingsDirty();
	if (!enabled)
		closeCurrentAudioDeviceIfBlocked("enabled backend update");
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioDeviceChoiceEnabledByIndex(int index, bool enabled)
{
    invalidateConfiguration();
	const auto choices = getAvailableAudioChoicesConfiguration();
	if (index < 0 || index >= (int) choices.deviceChoices.size())
		return false;

	const auto& choice = choices.deviceChoices[(size_t) index];
	const String entry = makeBlockedDeviceEntry(choice.backendName, choice.role, choice.deviceName);
	if (entry.trim().isEmpty())
		return false;

	auto values = readSettingLines(preferences, "blockedAudioDevices");
	bool changed = false;
	if (enabled)
	{
		changed = removeStringIgnoreCase(values, entry);
	}
	else if (!stringArrayContainsIgnoreCase(values, entry))
	{
		values.add(entry);
		changed = true;
	}

	if (!changed)
		return true;

	writeSettingLines(preferences, "blockedAudioDevices", values);
	markSettingsDirty();
	if (!enabled)
		closeCurrentAudioDeviceIfBlocked("enabled device update");
	audioConfigVersion++;
	return true;
}

bool DeviceController::setAudioSampleRate(double sampleRate)
{
    invalidateConfiguration();
	if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || sampleRate > 2147483647.0)
	{
		lastAudioConfigurationError = "Invalid sample rate: " + String(sampleRate, 0);
		return false;
	}

	lastAudioConfigurationError.clear();
	const int requestedRate = roundToInt(sampleRate);

	AudioDeviceManager::AudioDeviceSetup previousSetup;
	deviceManager.getAudioDeviceSetup(previousSetup);
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
	{
		if (roundToInt(currentDevice->getCurrentSampleRate()) == requestedRate)
		{
			rememberLastSelectedAudioDevice();
			return true;
		}
	}
	else if (roundToInt(previousSetup.sampleRate) == requestedRate)
	{
		return true;
	}

	AudioDeviceManager::AudioDeviceSetup setup = previousSetup;
	setup.sampleRate = sampleRate;
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	const bool deviceOpen = selectedDevice != nullptr && selectedDevice->isOpen();
	const int actualRate = selectedDevice != nullptr ? roundToInt(selectedDevice->getCurrentSampleRate()) : 0;

	if (error.isNotEmpty() || !deviceOpen || actualRate != requestedRate)
	{
		lastAudioConfigurationError = "Failed to set sample rate to " + String(requestedRate) + " Hz";
		if (error.isNotEmpty())
			lastAudioConfigurationError += ": " + error;
		else if (!deviceOpen)
			lastAudioConfigurationError += ": audio device did not open";
		else
			lastAudioConfigurationError += ": driver kept " + String(actualRate) + " Hz";

		Logger::writeToLog("LightHostModern: " + lastAudioConfigurationError);
		lightHostModernLog("AudioEngine setAudioSampleRate failed: " + lastAudioConfigurationError);
		const String restoreError = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), previousSetup);
		if (restoreError.isNotEmpty())
			lightHostModernLog("AudioEngine setAudioSampleRate restore failed: " + restoreError);
		return false;
	}

	lightHostModernLog("AudioEngine setAudioSampleRate succeeded requested=" + String(requestedRate)
		+ " actual=" + String(actualRate));
	saveAudioDeviceState();
	rememberLastSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioBufferSize(int bufferSize)
{
    invalidateConfiguration();
	if (bufferSize <= 0)
	{
		lastAudioConfigurationError = "Invalid audio buffer size: " + String(bufferSize);
		return false;
	}

	lastAudioConfigurationError.clear();

	AudioDeviceManager::AudioDeviceSetup previousSetup;
	deviceManager.getAudioDeviceSetup(previousSetup);
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
	{
		if (currentDevice->getCurrentBufferSizeSamples() == bufferSize)
		{
			rememberLastSelectedAudioDevice();
			return true;
		}
	}
	else if (previousSetup.bufferSize == bufferSize)
	{
		return true;
	}

	AudioDeviceManager::AudioDeviceSetup setup = previousSetup;
	setup.bufferSize = bufferSize;
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	const bool deviceOpen = selectedDevice != nullptr && selectedDevice->isOpen();
	const int actualBufferSize = selectedDevice != nullptr ? selectedDevice->getCurrentBufferSizeSamples() : 0;

	if (error.isNotEmpty() || !deviceOpen || actualBufferSize != bufferSize)
	{
		lastAudioConfigurationError = "Failed to set audio buffer size to " + String(bufferSize) + " samples";
		if (error.isNotEmpty())
			lastAudioConfigurationError += ": " + error;
		else if (!deviceOpen)
			lastAudioConfigurationError += ": audio device did not open";
		else
			lastAudioConfigurationError += ": driver kept " + String(actualBufferSize) + " samples";

		Logger::writeToLog("LightHostModern: " + lastAudioConfigurationError);
		lightHostModernLog("AudioEngine setAudioBufferSize failed: " + lastAudioConfigurationError);
		const String restoreError = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), previousSetup);
		if (restoreError.isNotEmpty())
			lightHostModernLog("AudioEngine setAudioBufferSize restore failed: " + restoreError);
		return false;
	}

	lightHostModernLog("AudioEngine setAudioBufferSize succeeded requested=" + String(bufferSize)
		+ " actual=" + String(actualBufferSize));
	saveAudioDeviceState();
	rememberLastSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioInputChannelEnabled(int channelIndex, bool enabled)
{
    invalidateConfiguration();
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr || channelIndex < 0 || channelIndex >= device->getInputChannelNames().size())
		return false;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultInputChannels = false;
	if (setup.inputChannels.isZero())
		setup.inputChannels = device->getActiveInputChannels();

	if (setup.inputChannels[channelIndex] == enabled)
		return true;

	setup.inputChannels.setBit(channelIndex, enabled);
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set input channel: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioOutputChannelEnabled(int channelIndex, bool enabled)
{
    invalidateConfiguration();
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr || channelIndex < 0 || channelIndex >= device->getOutputChannelNames().size())
		return false;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultOutputChannels = false;
	if (setup.outputChannels.isZero())
		setup.outputChannels = device->getActiveOutputChannels();

	if (setup.outputChannels[channelIndex] == enabled)
		return true;

	setup.outputChannels.setBit(channelIndex, enabled);
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set output channel: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAllAudioInputChannelsEnabled(bool enabled)
{
    invalidateConfiguration();
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr)
		return false;

	const int channelCount = device->getInputChannelNames().size();
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultInputChannels = false;
	setup.inputChannels.clear();
	setup.inputChannels.setRange(0, channelCount, enabled);

	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set all input channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAllAudioOutputChannelsEnabled(bool enabled)
{
    invalidateConfiguration();
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr)
		return false;

	const int channelCount = device->getOutputChannelNames().size();
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultOutputChannels = false;
	setup.outputChannels.clear();
	setup.outputChannels.setRange(0, channelCount, enabled);

	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set all output channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioInputChannelCount(int channelCount)
{
    invalidateConfiguration();
	if (channelCount < 0)
		return false;

	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr || channelCount > device->getInputChannelNames().size())
		return false;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultInputChannels = false;
	setup.inputChannels.clear();
	setup.inputChannels.setRange(0, channelCount, true);
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set input channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool DeviceController::setAudioOutputChannelCount(int channelCount)
{
    invalidateConfiguration();
	if (channelCount < 0)
		return false;

	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr || channelCount > device->getOutputChannelNames().size())
		return false;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultOutputChannels = false;
	setup.outputChannels.clear();
	setup.outputChannels.setRange(0, channelCount, true);
	const String error = apply(deviceManager, deviceManager.getCurrentAudioDeviceType(), setup);
    if (error == "Audio configuration was superseded") return false;
	if (error.isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: failed to set output channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

void DeviceController::saveCurrentAudioChannelState()
{
	auto* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr)
		return;

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	const auto inputChannels = setup.inputChannels.isZero() ? device->getActiveInputChannels() : setup.inputChannels;
	const auto outputChannels = setup.outputChannels.isZero() ? device->getActiveOutputChannels() : setup.outputChannels;

	auto state = std::make_unique<XmlElement>("CHANNELS");
	state->setAttribute("backend", device->getTypeName());
	state->setAttribute("input", setup.inputDeviceName);
	state->setAttribute("output", setup.outputDeviceName);
	state->setAttribute("inputChannels", inputChannels.toString(2));
	state->setAttribute("outputChannels", outputChannels.toString(2));
	state->setAttribute("useDefaultInputChannels", setup.useDefaultInputChannels);
	state->setAttribute("useDefaultOutputChannels", setup.useDefaultOutputChannels);

	preferencesPtr()->setValue(
		audioChannelStateKey(device->getTypeName(), setup.inputDeviceName, setup.outputDeviceName),
		state.get());
	markSettingsDirty();
}

void DeviceController::applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
                                             const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName)
{
	std::unique_ptr<XmlElement> state(getXmlValueOrClear(audioChannelStateKey(backendName, inputDeviceName, outputDeviceName)));
	if (state == nullptr)
		return;

	const auto inputMask = state->getStringAttribute("inputChannels");
	const auto outputMask = state->getStringAttribute("outputChannels");
	if (inputMask.isNotEmpty())
	{
		setup.useDefaultInputChannels = false;
		auto* currentType = deviceManager.getCurrentDeviceTypeObject();
		int channelCount = 0;
		if (currentType != nullptr)
		{
			const auto inputNames = currentType->getDeviceNames(true);
			channelCount = inputNames.contains(inputDeviceName) ? 256 : 0;
		}

		if (auto* device = deviceManager.getCurrentAudioDevice())
			channelCount = (std::max)(channelCount, device->getInputChannelNames().size());

		applyStoredChannelMask(setup.inputChannels, inputMask, channelCount > 0 ? channelCount : 256);
	}

	if (outputMask.isNotEmpty())
	{
		setup.useDefaultOutputChannels = false;
		auto* currentType = deviceManager.getCurrentDeviceTypeObject();
		int channelCount = 0;
		if (currentType != nullptr)
		{
			const auto outputNames = currentType->getDeviceNames(false);
			channelCount = outputNames.contains(outputDeviceName) ? 256 : 0;
		}

		if (auto* device = deviceManager.getCurrentAudioDevice())
			channelCount = (std::max)(channelCount, device->getOutputChannelNames().size());

		applyStoredChannelMask(setup.outputChannels, outputMask, channelCount > 0 ? channelCount : 256);
	}
}

void DeviceController::saveAudioDeviceState()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (!device || !device->isOpen()) return;
    AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    if (!isAudioDeviceChoiceAllowed(device->getTypeName(), setup.inputDeviceName, setup.outputDeviceName)) return;
    saveCurrentAudioChannelState();
    if (auto state = deviceManager.createStateXml()) preferences.setValue("audioDeviceState", state.get());
    markSettingsDirty();
}

