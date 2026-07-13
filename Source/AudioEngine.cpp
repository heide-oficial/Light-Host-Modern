#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PluginWindow.h"
#include <algorithm>
#include <cmath>
#include <vector>

void lightHostLog(const String& message);
void setLightHostCrashContext(const String& context);
void clearLightHostCrashContext();

namespace
{
	String getEnvironmentPath(const char* name)
	{
		return SystemStats::getEnvironmentVariable(name, {});
	}

	void addSearchFolder(FileSearchPath& searchPath, const String& folder)
	{
		if (folder.isEmpty())
			return;

		const File directory(folder);
		if (directory.isDirectory())
			searchPath.addIfNotAlreadyThere(directory);
	}

	void addSearchFolderFromBase(FileSearchPath& searchPath, const String& baseFolder, const String& relativeFolder)
	{
		if (baseFolder.isNotEmpty())
			addSearchFolder(searchPath, baseFolder + relativeFolder);
	}

	FileSearchPath getWindowsDefaultPluginSearchPath(AudioPluginFormat& format, bool isVst, bool isVst3)
	{
		FileSearchPath searchPath;
		searchPath.addPath(format.getDefaultLocationsToSearch());

		const auto programFiles = getEnvironmentPath("ProgramFiles");
		const auto programFilesX86 = getEnvironmentPath("ProgramFiles(x86)");
		const auto commonProgramFiles = getEnvironmentPath("CommonProgramFiles");
		const auto commonProgramFilesX86 = getEnvironmentPath("CommonProgramFiles(x86)");
		const auto localAppData = getEnvironmentPath("LOCALAPPDATA");

		if (isVst3)
		{
			addSearchFolderFromBase(searchPath, commonProgramFiles, "\\VST3");
			addSearchFolderFromBase(searchPath, commonProgramFilesX86, "\\VST3");
			addSearchFolderFromBase(searchPath, localAppData, "\\Programs\\Common\\VST3");
		}

		if (isVst)
		{
			addSearchFolderFromBase(searchPath, programFiles, "\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFiles, "\\Steinberg\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFiles, "\\Common Files\\VST2");
			addSearchFolderFromBase(searchPath, programFiles, "\\Common Files\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Steinberg\\VSTPlugins");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Common Files\\VST2");
			addSearchFolderFromBase(searchPath, programFilesX86, "\\Common Files\\VSTPlugins");
		}

		return searchPath;
	}

	bool isVst2PluginHostEnabled()
	{
	#if JUCE_PLUGINHOST_VST
		return getAppProperties().getUserSettings()->getBoolValue("enableVst2", false);
	#else
		return false;
	#endif
	}

	void addEnabledPluginFormats(AudioPluginFormatManager& manager)
	{
	#if JUCE_PLUGINHOST_VST
		if (isVst2PluginHostEnabled())
			manager.addFormat(std::make_unique<VSTPluginFormat>());
	#endif

	#if JUCE_PLUGINHOST_VST3
		manager.addFormat(std::make_unique<VST3PluginFormat>());
	#endif
	}

	String getPluginStateBaseKey(String type, const PluginDescription& plugin)
	{
		return "plugin-" + type.toLowerCase() + "-" + String::toHexString(plugin.createIdentifierString().hashCode64());
	}

	bool describesSamePluginBinary(const PluginDescription& a, const PluginDescription& b)
	{
		return a.name == b.name
			&& a.manufacturerName == b.manufacturerName
			&& a.pluginFormatName == b.pluginFormatName
			&& a.fileOrIdentifier == b.fileOrIdentifier;
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

	StringArray readSettingLines(const String& key)
	{
		StringArray values;
		values.addLines(getAppProperties().getUserSettings()->getValue(key));
		values.trim();
		values.removeEmptyStrings();
		return values;
	}

	void writeSettingLines(const String& key, const StringArray& values)
	{
		getAppProperties().getUserSettings()->setValue(key, values.joinIntoString("\n"));
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

String PluginStateStore::getKey(String type, const PluginDescription& plugin)
{
	return getPluginStateBaseKey(type, plugin) + "-" + String::toHexString(plugin.deprecatedUid);
}

String PluginStateStore::getLegacyKey(String type, const PluginDescription& plugin)
{
	return "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

String PluginStateStore::getValue(String type, const PluginDescription& plugin, const String& defaultValue) const
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	const String key = getKey(type, plugin);
	const String value = settings->getValue(key);
	if (value.isNotEmpty())
		return value;

	const String baseValue = settings->getValue(getPluginStateBaseKey(type, plugin));
	if (baseValue.isNotEmpty())
		return baseValue;

	return settings->getValue(getLegacyKey(type, plugin), defaultValue);
}

void PluginStateStore::setValue(String type, const PluginDescription& plugin, const var& value)
{
	getAppProperties().getUserSettings()->setValue(getKey(type, plugin), value);
	dirty = true;
}

void PluginStateStore::removeValue(String type, const PluginDescription& plugin)
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	settings->removeValue(getKey(type, plugin));
	settings->removeValue(getPluginStateBaseKey(type, plugin));
	settings->removeValue(getLegacyKey(type, plugin));
	dirty = true;
}

void PluginStateStore::markDirty()
{
	dirty = true;
}

void PluginStateStore::flushIfDirty()
{
	if (!dirty)
		return;

	dirty = false;
	getAppProperties().getUserSettings()->saveIfNeeded();
}

String DeviceController::initialise(AudioDeviceManager& deviceManager, const XmlElement* savedAudioState, bool allowDefaultFallback)
{
	String audioError = deviceManager.initialise(256, 256, savedAudioState, allowDefaultFallback);
	if (audioError.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: audio device initialisation failed: " + audioError);
		getAppProperties().getUserSettings()->removeValue("audioDeviceState");
		if (allowDefaultFallback)
		{
			audioError = deviceManager.initialiseWithDefaultDevices(256, 256);
			if (audioError.isNotEmpty())
				Logger::writeToLog("Light Host Modern: default audio device initialisation failed: " + audioError);
		}
		else
		{
			Logger::writeToLog("Light Host Modern: default audio fallback is disabled while device persistence is active");
			deviceManager.initialise(0, 0, nullptr, false);
		}
	}

	return audioError;
}

void DeviceController::recoverIfNeeded(AudioDeviceManager& deviceManager,
	AudioRecoveryConfiguration const& recoveryConfig,
	int& failedAudioRecoveryAttempts,
	String& recoveryState,
	String& recoveryMessage)
{
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
	recoveryState = "fallback";
	recoveryMessage = "Audio device stopped; restarting the last audio device.";
	Logger::writeToLog("Light Host Modern: audio device is not running; attempting restart");
	deviceManager.restartLastAudioDevice();

	if (deviceManager.getCurrentAudioDevice() == nullptr
		|| !deviceManager.getCurrentAudioDevice()->isOpen()
		|| !deviceManager.getCurrentAudioDevice()->isPlaying())
	{
		Logger::writeToLog("Light Host Modern: audio device restart failed; falling back to default devices");
		recoveryMessage = "Audio device restart failed; falling back to default devices.";
		getAppProperties().getUserSettings()->removeValue("audioDeviceState");
		deviceManager.initialiseWithDefaultDevices(256, 256);
	}
}

DiagnosticsSnapshot DeviceController::createDiagnosticsSnapshot(AudioDeviceManager& deviceManager) const
{
	DiagnosticsSnapshot snapshot;
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();

	snapshot.backend = device != nullptr ? device->getTypeName() : "none";
	snapshot.deviceName = device != nullptr ? device->getName() : "none";
	snapshot.cpuUsagePercent = deviceManager.getCpuUsage() * 100.0;
	snapshot.xRunCount = deviceManager.getXRunCount();
	snapshot.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
	snapshot.bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
	snapshot.inputLatency = device != nullptr ? device->getInputLatencyInSamples() : 0;
	snapshot.outputLatency = device != nullptr ? device->getOutputLatencyInSamples() : 0;
	snapshot.inputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
	snapshot.outputChannels = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 0;

	return snapshot;
}

AudioEngine::AudioEngine(bool startInSafeMode, bool shouldRestoreActivePluginsOnStartup)
	: safeMode(startInSafeMode),
	  restoreActivePluginsOnStartup(shouldRestoreActivePluginsOnStartup)
{
	addEnabledPluginFormats(formatManager);

	const auto startupRecoveryConfig = getAudioRecoveryConfiguration();
	const String startupRecoveryMode = normaliseAudioPersistenceMode(startupRecoveryConfig.mode);
	const bool audioPersistenceEnabled = !safeMode && startupRecoveryMode != "disabled";
	std::unique_ptr<XmlElement> savedAudioState((safeMode || audioPersistenceEnabled) ? nullptr : getXmlValueOrClear("audioDeviceState"));
	deviceController.initialise(deviceManager, savedAudioState.get(), !audioPersistenceEnabled);
	if (audioPersistenceEnabled)
	{
		audioRecoveryState = "retrying";
		audioRecoveryMessage = "Restoring preferred audio device.";
		if (!applyPreferredAudioDevice(startupRecoveryConfig, false))
		{
			failedAudioRecoveryAttempts = 0;
			audioRecoveryState = "retrying";
			if (audioRecoveryMessage.isEmpty())
				audioRecoveryMessage = "Waiting for preferred audio device.";
			lightHostLog("AudioEngine startup preferred audio restore pending: " + audioRecoveryMessage);
		}
	}
	else
	{
		closeCurrentAudioDeviceIfBlocked("startup");
	}

	player.setProcessor(&hostProcessor);
	deviceManager.addAudioCallback(&player);
	deviceManager.addChangeListener(this);
	startTimer(audioWatchdogTimerId, 5000);
	startTimer(diagnosticsTimerId, 30000);

	std::unique_ptr<XmlElement> savedPluginList(getXmlValueOrClear("pluginList"));
	if (savedPluginList != nullptr)
		knownPluginList.recreateFromXml(*savedPluginList);

	knownPluginList.addChangeListener(this);

	std::unique_ptr<XmlElement> savedPluginListActive((safeMode || !restoreActivePluginsOnStartup) ? nullptr : getXmlValueOrClear("pluginListActive"));
	if (savedPluginListActive != nullptr)
		activePluginList.recreateFromXml(*savedPluginListActive);
	else if (!safeMode && !restoreActivePluginsOnStartup)
		Logger::writeToLog("Light Host Modern: active plugin restore skipped during startup");

	loadActivePlugins();
	activePluginList.addChangeListener(this);
}

AudioEngine::~AudioEngine()
{
	stopTimer(audioWatchdogTimerId);
	stopTimer(diagnosticsTimerId);
	stopTimer(persistenceTimerId);

	knownPluginList.removeChangeListener(this);
	activePluginList.removeChangeListener(this);
	deviceManager.removeChangeListener(this);
	deviceManager.removeAudioCallback(&player);
	player.setProcessor(nullptr);

	saveActivePluginChain(true);
	hostProcessor.publishSnapshot(nullptr);
}

std::unique_ptr<XmlElement> AudioEngine::getXmlValueOrClear(const String& key)
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	auto xml = settings->getXmlValue(key);
	if (xml == nullptr && settings->getValue(key).isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: removed invalid XML setting '" + key + "'");
		settings->removeValue(key);
		settingsDirty = true;
		flushPendingSaves();
	}

	return xml;
}

std::vector<PluginDescription> AudioEngine::getActivePluginsSorted() const
{
	std::vector<PluginDescription> list;
	const auto types = activePluginList.getTypes();
	for (auto& plugin : types)
		list.push_back(plugin);

	std::sort(list.begin(), list.end(), [this](const PluginDescription& a, const PluginDescription& b)
	{
		const int orderA = pluginStateStore.getValue("order", a).getIntValue();
		const int orderB = pluginStateStore.getValue("order", b).getIntValue();

		if (orderA == orderB)
			return a.name.compareNatural(b.name) < 0;

		if (orderA <= 0)
			return false;

		if (orderB <= 0)
			return true;

		return orderA < orderB;
	});

	return list;
}

std::vector<PluginDescription> AudioEngine::getKnownPluginsSorted() const
{
	std::vector<PluginDescription> list;
	const auto types = knownPluginList.getTypes();
	for (auto& plugin : types)
		list.push_back(plugin);

	std::sort(list.begin(), list.end(), [](const PluginDescription& a, const PluginDescription& b)
	{
		const int format = a.pluginFormatName.compareNatural(b.pluginFormatName);
		if (format != 0)
			return format < 0;

		const int manufacturer = a.manufacturerName.compareNatural(b.manufacturerName);
		if (manufacturer != 0)
			return manufacturer < 0;

		return a.name.compareNatural(b.name) < 0;
	});

	return list;
}

bool AudioEngine::isVst2FormatActive() const
{
	for (int i = 0; i < formatManager.getNumFormats(); ++i)
	{
		auto* format = formatManager.getFormat(i);
		if (format == nullptr)
			continue;

		const String formatName = format->getName();
		if (formatName.containsIgnoreCase("VST") && !formatName.containsIgnoreCase("VST3"))
			return true;
	}

	return false;
}

AudioDeviceConfiguration AudioEngine::getAudioDeviceConfiguration()
{
	AudioDeviceConfiguration config;
	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
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
		if (currentDevice != nullptr && currentDevice->getTypeName() == type->getTypeName())
			config.currentBackendIndex = (int) config.backendNames.size() - 1;
	}

	if (auto* currentType = deviceManager.getCurrentDeviceTypeObject())
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
			if (inputs[i] == setup.inputDeviceName)
				config.currentInputDeviceIndex = (int) config.inputDeviceNames.size() - 1;
		}

		for (int i = 0; i < outputs.size(); ++i)
		{
			if (isAudioDeviceBlocked(backendName, isAsioBackend ? "device" : "output", outputs[i]))
				continue;

			config.outputDeviceNames.push_back(outputs[i]);
			if (outputs[i] == setup.outputDeviceName)
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

AudioRecoveryConfiguration AudioEngine::getAudioRecoveryConfiguration() const
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
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

AudioBlocklistConfiguration AudioEngine::getAudioBlocklistConfiguration() const
{
	AudioBlocklistConfiguration config;

	for (const auto& backend : readSettingLines("blockedAudioBackends"))
		config.blockedBackends.push_back(backend);

	for (const auto& entry : readSettingLines("blockedAudioDevices"))
	{
		auto choice = parseBlockedDeviceEntry(entry);
		if (choice.backendName.isNotEmpty() && choice.role.isNotEmpty() && choice.deviceName.isNotEmpty())
			config.blockedDevices.push_back(choice);
	}

	return config;
}

AvailableAudioChoicesConfiguration AudioEngine::getAvailableAudioChoicesConfiguration()
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

	return config;
}

bool AudioEngine::isAudioBackendBlocked(const String& backendName) const
{
	if (backendName.isEmpty())
		return false;

	return stringArrayContainsIgnoreCase(readSettingLines("blockedAudioBackends"), backendName);
}

bool AudioEngine::isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const
{
	if (backendName.isEmpty() || deviceName.isEmpty())
		return false;

	const String normalisedRole = normaliseBlockedDeviceRole(role);
	for (const auto& entry : readSettingLines("blockedAudioDevices"))
	{
		const auto choice = parseBlockedDeviceEntry(entry);
		if (!choice.backendName.equalsIgnoreCase(backendName) || !choice.deviceName.equalsIgnoreCase(deviceName))
			continue;

		if (choice.role == "device" || choice.role == normalisedRole)
			return true;
	}

	return false;
}

bool AudioEngine::isAudioDeviceChoiceAllowed(const String& backendName,
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

bool AudioEngine::currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const
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
	const_cast<AudioDeviceManager&>(deviceManager).getAudioDeviceSetup(setup);

	if (targetInput.isEmpty() && targetOutput.isEmpty())
		return false;

	if (targetInput.isNotEmpty() && !setup.inputDeviceName.equalsIgnoreCase(targetInput))
		return false;

	if (targetOutput.isNotEmpty() && !setup.outputDeviceName.equalsIgnoreCase(targetOutput))
		return false;

	return isAudioDeviceChoiceAllowed(targetBackend, setup.inputDeviceName, setup.outputDeviceName);
}

void AudioEngine::closeCurrentAudioDeviceIfBlocked(const String& context)
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
	lightHostLog("AudioEngine closed blocked audio device during " + context + ": " + lastAudioConfigurationError);
	deviceManager.closeAudioDevice();
	audioConfigVersion++;
}

void AudioEngine::rememberLastSelectedAudioDevice()
{
	AudioIODevice* currentDevice = deviceManager.getCurrentAudioDevice();
	auto* settings = getAppProperties().getUserSettings();

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

bool AudioEngine::applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry)
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
		lastAudioConfigurationError = "Preferred audio device is blocked by settings: "
			+ quotedTarget(targetBackend, targetInput, targetOutput);
		audioRecoveryState = "failed";
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostLog(String("AudioEngine audio persistence retry ")
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
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	targetType->scanForDevices();
	deviceManager.setCurrentAudioDeviceType(targetBackend, true);
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || currentType->getTypeName() != targetBackend)
	{
		lastAudioConfigurationError = "Failed to switch to preferred audio backend: " + targetBackend;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	currentType->scanForDevices();
	const auto inputDevices = currentType->getDeviceNames(true);
	const auto outputDevices = currentType->getDeviceNames(false);

	if (targetInput.isNotEmpty() && !inputDevices.contains(targetInput))
	{
		lastAudioConfigurationError = "Preferred input device is unavailable: " + targetInput;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
		return false;
	}

	if (targetOutput.isNotEmpty() && !outputDevices.contains(targetOutput))
	{
		lastAudioConfigurationError = "Preferred output device is unavailable: " + targetOutput;
		audioRecoveryMessage = lastAudioConfigurationError;
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
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

	const String error = deviceManager.setAudioDeviceSetup(setup, true);
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
		lightHostLog("AudioEngine audio persistence failed: " + lastAudioConfigurationError);
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

bool AudioEngine::setAudioBackendByIndex(int backendIndex)
{
	auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
	lastAudioConfigurationError.clear();
	lightHostLog("AudioEngine setAudioBackendByIndex requested index=" + String(backendIndex)
		+ " availableTypes=" + String(deviceTypes.size()));

	if (backendIndex < 0)
	{
		lastAudioConfigurationError = "Invalid audio backend index: " + String(backendIndex);
		lightHostLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
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
			lightHostLog("AudioEngine backend candidate blocked type='" + type->getTypeName() + "'");
			continue;
		}

		++visibleBackendIndex;
		lightHostLog("AudioEngine backend candidate visibleIndex=" + String(visibleBackendIndex)
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
		lightHostLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	const String typeName = selectedType->getTypeName();
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
	{
		lightHostLog("AudioEngine current audio backend='" + currentDevice->getTypeName()
			+ "' device='" + currentDevice->getName()
			+ "' open=" + String(currentDevice->isOpen() ? "true" : "false"));
		if (currentDevice->getTypeName() == typeName)
		{
			lightHostLog("AudioEngine setAudioBackendByIndex no-op; already using backend '" + typeName + "'");
			rememberLastSelectedAudioDevice();
			return true;
		}
	}

	String previousTypeName;
	if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
		previousTypeName = currentDevice->getTypeName();

	lightHostLog("AudioEngine scanning requested backend '" + typeName + "'");
	selectedType->scanForDevices();
	lightHostLog("AudioEngine setCurrentAudioDeviceType begin requested='" + typeName + "' previous='" + previousTypeName + "'");
	deviceManager.setCurrentAudioDeviceType(typeName, true);
	lightHostLog("AudioEngine setCurrentAudioDeviceType returned requested='" + typeName + "'");

	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || currentType->getTypeName() != typeName)
	{
		lastAudioConfigurationError = "Failed to select audio backend '" + typeName + "'";
		if (currentType != nullptr)
			lastAudioConfigurationError += "; current type is '" + currentType->getTypeName() + "'";
		Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
		lightHostLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	currentType->scanForDevices();
	const auto inputDevices = currentType->getDeviceNames(true);
	const auto outputDevices = currentType->getDeviceNames(false);
	lightHostLog("AudioEngine backend '" + typeName + "' device scan: inputs="
		+ String(inputDevices.size()) + " outputs=" + String(outputDevices.size()));
	for (int i = 0; i < inputDevices.size(); ++i)
		lightHostLog("AudioEngine backend '" + typeName + "' input[" + String(i) + "]='" + inputDevices[i] + "'");
	for (int i = 0; i < outputDevices.size(); ++i)
		lightHostLog("AudioEngine backend '" + typeName + "' output[" + String(i) + "]='" + outputDevices[i] + "'");

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
		if (!inputDevices.isEmpty() || !outputDevices.isEmpty())
		{
			const String fallbackInput = inputDevices.isEmpty() ? String() : inputDevices[0];
			const String fallbackOutput = outputDevices.isEmpty() ? String() : outputDevices[0];
			if (isAudioDeviceChoiceAllowed(typeName, fallbackInput, fallbackOutput))
				candidates.push_back({ fallbackInput, fallbackOutput });
		}
	}

	if (candidates.empty() && isAudioDeviceChoiceAllowed(typeName, {}, {}))
		candidates.push_back({ {}, {} });

	if (candidates.empty())
	{
		lastAudioConfigurationError = "No allowed devices are available for audio backend '" + typeName + "'.";
		lightHostLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
		if (previousTypeName.isNotEmpty())
			deviceManager.setCurrentAudioDeviceType(previousTypeName, true);
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

		lightHostLog("AudioEngine setAudioDeviceSetup attempt=" + String(attempt + 1)
			+ "/" + String((int) candidates.size())
			+ " backend='" + typeName
			+ "' input='" + setup.inputDeviceName
			+ "' output='" + setup.outputDeviceName
			+ "' sampleRate=default bufferSize=default");

		const String error = deviceManager.setAudioDeviceSetup(setup, true);
		AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
		lightHostLog("AudioEngine setAudioDeviceSetup attempt=" + String(attempt + 1)
			+ " returned backend='" + typeName
			+ "' error='" + error
			+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

		if (selectedDevice != nullptr)
		{
			lightHostLog("AudioEngine selected device type='" + selectedDevice->getTypeName()
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
			rememberLastSelectedAudioDevice();
			failedAudioRecoveryAttempts = 0;
			audioRecoveryState = "running";
			audioRecoveryMessage.clear();
			audioConfigVersion++;
			lightHostLog("AudioEngine setAudioBackendByIndex succeeded backend='" + typeName
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
	Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
	lightHostLog("AudioEngine setAudioBackendByIndex failed: " + lastAudioConfigurationError);
	if (previousTypeName.isNotEmpty())
	{
		lightHostLog("AudioEngine restoring previous audio backend '" + previousTypeName + "'");
		deviceManager.setCurrentAudioDeviceType(previousTypeName, true);
	}

	if (auto* restoredDevice = deviceManager.getCurrentAudioDevice())
	{
		lightHostLog("AudioEngine restored device type='" + restoredDevice->getTypeName()
			+ "' name='" + restoredDevice->getName()
			+ "' open=" + String(restoredDevice->isOpen() ? "true" : "false"));
	}

	return false;
}

bool AudioEngine::setAudioInputDeviceByIndex(int deviceIndex)
{
	lastAudioConfigurationError.clear();
	lightHostLog("AudioEngine setAudioInputDeviceByIndex requested index=" + String(deviceIndex));

	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr)
	{
		lastAudioConfigurationError = "No current audio backend is selected while setting input device";
		lightHostLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostLog("AudioEngine setAudioInputDeviceByIndex backend='" + currentType->getTypeName() + "'");

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(true);
	lightHostLog("AudioEngine input device scan backend='" + currentType->getTypeName()
		+ "' count=" + String(devices.size()));
	for (int i = 0; i < devices.size(); ++i)
		lightHostLog("AudioEngine input candidate[" + String(i) + "]='" + devices[i] + "'");

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
		lightHostLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	const auto previousSetup = setup;
	const String requestedInputDevice = allowedDevices[deviceIndex];
	lightHostLog("AudioEngine current setup before input change backend='" + currentType->getTypeName()
		+ "' input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName
		+ "' sampleRate=" + String(setup.sampleRate, 0)
		+ " bufferSize=" + String(setup.bufferSize));

	if (isAsioBackend
		&& setup.inputDeviceName == requestedInputDevice
		&& setup.outputDeviceName == requestedInputDevice)
	{
		lightHostLog("AudioEngine setAudioInputDeviceByIndex no-op; already using ASIO device='" + requestedInputDevice + "'");
		rememberLastSelectedAudioDevice();
		return true;
	}

	if (!isAsioBackend && setup.inputDeviceName == requestedInputDevice)
	{
		lightHostLog("AudioEngine setAudioInputDeviceByIndex no-op; already using input='" + setup.inputDeviceName + "'");
		rememberLastSelectedAudioDevice();
		return true;
	}

	if (!isAudioDeviceChoiceAllowed(currentType->getTypeName(),
	                                requestedInputDevice,
	                                isAsioBackend ? requestedInputDevice : setup.outputDeviceName))
	{
		lastAudioConfigurationError = "Input device is blocked by settings: " + requestedInputDevice;
		lightHostLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	setup.inputDeviceName = requestedInputDevice;
	if (isAsioBackend)
	{
		setup.outputDeviceName = requestedInputDevice;
		lightHostLog("AudioEngine setAudioInputDeviceByIndex ASIO mode; input and output will use the same device");
	}

	lightHostLog("AudioEngine setAudioInputDeviceByIndex applying input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName + "'");

	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	lightHostLog("AudioEngine setAudioInputDeviceByIndex setAudioDeviceSetup returned error='" + error
		+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

	if (selectedDevice != nullptr)
	{
		lightHostLog("AudioEngine device after input change type='" + selectedDevice->getTypeName()
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
		lightHostLog("AudioEngine setAudioInputDeviceByIndex ASIO selected device mismatch requested='"
			+ requestedInputDevice + "' actual='" + selectedDevice->getName() + "'");
	}

	if (error.isNotEmpty() || selectedDevice == nullptr || !selectedDevice->isOpen() || selectedDeviceMismatch)
	{
		lastAudioConfigurationError = "Failed to set input device '" + setup.inputDeviceName
			+ "' on backend '" + currentType->getTypeName() + "': "
			+ (error.isNotEmpty() ? error : (selectedDeviceMismatch ? "selected ASIO device did not match requested device" : "device did not open"));
		Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
		lightHostLog("AudioEngine setAudioInputDeviceByIndex failed: " + lastAudioConfigurationError);
		const String restoreError = deviceManager.setAudioDeviceSetup(previousSetup, true);
		lightHostLog("AudioEngine setAudioInputDeviceByIndex restored previous setup input='"
			+ previousSetup.inputDeviceName + "' output='" + previousSetup.outputDeviceName
			+ "' restoreError='" + restoreError + "'");
		return false;
	}

	saveAudioDeviceState();
	rememberLastSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAudioOutputDeviceByIndex(int deviceIndex)
{
	lastAudioConfigurationError.clear();
	lightHostLog("AudioEngine setAudioOutputDeviceByIndex requested index=" + String(deviceIndex));

	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr)
	{
		lastAudioConfigurationError = "No current audio backend is selected while setting output device";
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	lightHostLog("AudioEngine setAudioOutputDeviceByIndex backend='" + currentType->getTypeName() + "'");

	currentType->scanForDevices();
	const auto devices = currentType->getDeviceNames(false);
	lightHostLog("AudioEngine output device scan backend='" + currentType->getTypeName()
		+ "' count=" + String(devices.size()));
	for (int i = 0; i < devices.size(); ++i)
		lightHostLog("AudioEngine output candidate[" + String(i) + "]='" + devices[i] + "'");

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
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	const auto previousSetup = setup;
	const String requestedOutputDevice = allowedDevices[deviceIndex];
	lightHostLog("AudioEngine current setup before output change backend='" + currentType->getTypeName()
		+ "' input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName
		+ "' sampleRate=" + String(setup.sampleRate, 0)
		+ " bufferSize=" + String(setup.bufferSize));

	if (isAsioBackend
		&& setup.inputDeviceName == requestedOutputDevice
		&& setup.outputDeviceName == requestedOutputDevice)
	{
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex no-op; already using ASIO device='" + requestedOutputDevice + "'");
		rememberLastSelectedAudioDevice();
		return true;
	}

	if (!isAsioBackend && setup.outputDeviceName == requestedOutputDevice)
	{
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex no-op; already using output='" + setup.outputDeviceName + "'");
		rememberLastSelectedAudioDevice();
		return true;
	}

	if (!isAudioDeviceChoiceAllowed(currentType->getTypeName(),
	                                isAsioBackend ? requestedOutputDevice : setup.inputDeviceName,
	                                requestedOutputDevice))
	{
		lastAudioConfigurationError = "Output device is blocked by settings: " + requestedOutputDevice;
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		return false;
	}

	setup.outputDeviceName = requestedOutputDevice;
	if (isAsioBackend)
	{
		setup.inputDeviceName = requestedOutputDevice;
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex ASIO mode; input and output will use the same device");
	}

	lightHostLog("AudioEngine setAudioOutputDeviceByIndex applying input='" + setup.inputDeviceName
		+ "' output='" + setup.outputDeviceName + "'");

	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	AudioIODevice* selectedDevice = deviceManager.getCurrentAudioDevice();
	lightHostLog("AudioEngine setAudioOutputDeviceByIndex setAudioDeviceSetup returned error='" + error
		+ "' selectedDevice=" + String(selectedDevice != nullptr ? "yes" : "no"));

	if (selectedDevice != nullptr)
	{
		lightHostLog("AudioEngine device after output change type='" + selectedDevice->getTypeName()
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
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex ASIO selected device mismatch requested='"
			+ requestedOutputDevice + "' actual='" + selectedDevice->getName() + "'");
	}

	if (error.isNotEmpty() || selectedDevice == nullptr || !selectedDevice->isOpen() || selectedDeviceMismatch)
	{
		lastAudioConfigurationError = "Failed to set output device '" + setup.outputDeviceName
			+ "' on backend '" + currentType->getTypeName() + "': "
			+ (error.isNotEmpty() ? error : (selectedDeviceMismatch ? "selected ASIO device did not match requested device" : "device did not open"));
		Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex failed: " + lastAudioConfigurationError);
		const String restoreError = deviceManager.setAudioDeviceSetup(previousSetup, true);
		lightHostLog("AudioEngine setAudioOutputDeviceByIndex restored previous setup input='"
			+ previousSetup.inputDeviceName + "' output='" + previousSetup.outputDeviceName
			+ "' restoreError='" + restoreError + "'");
		return false;
	}

	saveAudioDeviceState();
	rememberLastSelectedAudioDevice();
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "running";
	audioRecoveryMessage.clear();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAudioPersistenceMode(const String& mode)
{
	const String normalised = normaliseAudioPersistenceMode(mode);
	auto* settings = getAppProperties().getUserSettings();
	settings->setValue("audioPersistenceMode", normalised);
	if (normalised != "disabled")
		rememberLastSelectedAudioDevice();
	else
	{
		failedAudioRecoveryAttempts = 0;
		audioRecoveryState = "running";
		audioRecoveryMessage.clear();
	}

	markSettingsDirty();
	startTimer(audioWatchdogTimerId, getAudioRecoveryConfiguration().retrySeconds * 1000);
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioPersistenceRetrySeconds(int seconds)
{
	getAppProperties().getUserSettings()->setValue("audioPersistenceRetrySeconds", clampRecoveryRetrySeconds(seconds));
	markSettingsDirty();
	startTimer(audioWatchdogTimerId, getAudioRecoveryConfiguration().retrySeconds * 1000);
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioPersistenceRetryAttempts(int attempts)
{
	getAppProperties().getUserSettings()->setValue("audioPersistenceRetryAttempts", clampRecoveryRetryAttempts(attempts));
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioPersistenceCustomBackendByIndex(int backendIndex)
{
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

	getAppProperties().getUserSettings()->setValue("audioPersistenceCustomBackend", selectedType->getTypeName());
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioPersistenceCustomInputByIndex(int deviceIndex)
{
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
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

	auto* settings = getAppProperties().getUserSettings();
	settings->setValue("audioPersistenceCustomBackend", currentType->getTypeName());
	settings->setValue("audioPersistenceCustomInputDevice", allowedDevices[deviceIndex]);
	if (isAsioBackend)
		settings->setValue("audioPersistenceCustomOutputDevice", allowedDevices[deviceIndex]);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioPersistenceCustomOutputByIndex(int deviceIndex)
{
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
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

	auto* settings = getAppProperties().getUserSettings();
	settings->setValue("audioPersistenceCustomBackend", currentType->getTypeName());
	settings->setValue("audioPersistenceCustomOutputDevice", allowedDevices[deviceIndex]);
	if (isAsioBackend)
		settings->setValue("audioPersistenceCustomInputDevice", allowedDevices[deviceIndex]);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::retryPreferredAudioDeviceNow()
{
	failedAudioRecoveryAttempts = 0;
	audioRecoveryState = "retrying";
	audioRecoveryMessage = "Manual retry requested.";
	return applyPreferredAudioDevice(getAudioRecoveryConfiguration(), true);
}

bool AudioEngine::addBlockedAudioBackend(const String& backendName)
{
	const String trimmed = backendName.trim();
	if (trimmed.isEmpty())
		return false;

	auto values = readSettingLines("blockedAudioBackends");
	if (!stringArrayContainsIgnoreCase(values, trimmed))
		values.add(trimmed);

	writeSettingLines("blockedAudioBackends", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("backend blocklist update");
	audioConfigVersion++;
	return true;
}

bool AudioEngine::addBlockedAudioInputDevice(const String& deviceName)
{
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || deviceName.trim().isEmpty())
		return false;

	const String backendName = currentType->getTypeName();
	const String role = backendName.equalsIgnoreCase("ASIO") ? "device" : "input";
	const String entry = makeBlockedDeviceEntry(backendName, role, deviceName);
	auto values = readSettingLines("blockedAudioDevices");
	if (!stringArrayContainsIgnoreCase(values, entry))
		values.add(entry);

	writeSettingLines("blockedAudioDevices", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("input device blocklist update");
	audioConfigVersion++;
	return true;
}

bool AudioEngine::addBlockedAudioOutputDevice(const String& deviceName)
{
	auto* currentType = deviceManager.getCurrentDeviceTypeObject();
	if (currentType == nullptr || deviceName.trim().isEmpty())
		return false;

	const String backendName = currentType->getTypeName();
	const String role = backendName.equalsIgnoreCase("ASIO") ? "device" : "output";
	const String entry = makeBlockedDeviceEntry(backendName, role, deviceName);
	auto values = readSettingLines("blockedAudioDevices");
	if (!stringArrayContainsIgnoreCase(values, entry))
		values.add(entry);

	writeSettingLines("blockedAudioDevices", values);
	markSettingsDirty();
	closeCurrentAudioDeviceIfBlocked("output device blocklist update");
	audioConfigVersion++;
	return true;
}

bool AudioEngine::removeBlockedAudioBackend(int index)
{
	auto values = readSettingLines("blockedAudioBackends");
	if (index < 0 || index >= values.size())
		return false;

	values.remove(index);
	writeSettingLines("blockedAudioBackends", values);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::removeBlockedAudioDevice(int index)
{
	auto values = readSettingLines("blockedAudioDevices");
	if (index < 0 || index >= values.size())
		return false;

	values.remove(index);
	writeSettingLines("blockedAudioDevices", values);
	markSettingsDirty();
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioBackendEnabledByIndex(int index, bool enabled)
{
	const auto choices = getAvailableAudioChoicesConfiguration();
	if (index < 0 || index >= (int) choices.backendNames.size())
		return false;

	const String backendName = choices.backendNames[(size_t) index].trim();
	if (backendName.isEmpty())
		return false;

	auto values = readSettingLines("blockedAudioBackends");
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

	writeSettingLines("blockedAudioBackends", values);
	markSettingsDirty();
	if (!enabled)
		closeCurrentAudioDeviceIfBlocked("enabled backend update");
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioDeviceChoiceEnabledByIndex(int index, bool enabled)
{
	const auto choices = getAvailableAudioChoicesConfiguration();
	if (index < 0 || index >= (int) choices.deviceChoices.size())
		return false;

	const auto& choice = choices.deviceChoices[(size_t) index];
	const String entry = makeBlockedDeviceEntry(choice.backendName, choice.role, choice.deviceName);
	if (entry.trim().isEmpty())
		return false;

	auto values = readSettingLines("blockedAudioDevices");
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

	writeSettingLines("blockedAudioDevices", values);
	markSettingsDirty();
	if (!enabled)
		closeCurrentAudioDeviceIfBlocked("enabled device update");
	audioConfigVersion++;
	return true;
}

bool AudioEngine::setAudioSampleRate(double sampleRate)
{
	if (sampleRate <= 0.0)
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
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

		Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
		lightHostLog("AudioEngine setAudioSampleRate failed: " + lastAudioConfigurationError);
		const String restoreError = deviceManager.setAudioDeviceSetup(previousSetup, true);
		if (restoreError.isNotEmpty())
			lightHostLog("AudioEngine setAudioSampleRate restore failed: " + restoreError);
		return false;
	}

	lightHostLog("AudioEngine setAudioSampleRate succeeded requested=" + String(requestedRate)
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

bool AudioEngine::setAudioBufferSize(int bufferSize)
{
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
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

		Logger::writeToLog("Light Host Modern: " + lastAudioConfigurationError);
		lightHostLog("AudioEngine setAudioBufferSize failed: " + lastAudioConfigurationError);
		const String restoreError = deviceManager.setAudioDeviceSetup(previousSetup, true);
		if (restoreError.isNotEmpty())
			lightHostLog("AudioEngine setAudioBufferSize restore failed: " + restoreError);
		return false;
	}

	lightHostLog("AudioEngine setAudioBufferSize succeeded requested=" + String(bufferSize)
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

bool AudioEngine::setAudioInputChannelEnabled(int channelIndex, bool enabled)
{
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set input channel: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAudioOutputChannelEnabled(int channelIndex, bool enabled)
{
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set output channel: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAllAudioInputChannelsEnabled(bool enabled)
{
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr)
		return false;

	const int channelCount = device->getInputChannelNames().size();
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultInputChannels = false;
	setup.inputChannels.clear();
	setup.inputChannels.setRange(0, channelCount, enabled);

	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set all input channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAllAudioOutputChannelsEnabled(bool enabled)
{
	AudioIODevice* device = deviceManager.getCurrentAudioDevice();
	if (device == nullptr)
		return false;

	const int channelCount = device->getOutputChannelNames().size();
	AudioDeviceManager::AudioDeviceSetup setup;
	deviceManager.getAudioDeviceSetup(setup);
	setup.useDefaultOutputChannels = false;
	setup.outputChannels.clear();
	setup.outputChannels.setRange(0, channelCount, enabled);

	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set all output channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAudioInputChannelCount(int channelCount)
{
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set input channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

bool AudioEngine::setAudioOutputChannelCount(int channelCount)
{
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
	const String error = deviceManager.setAudioDeviceSetup(setup, true);
	if (error.isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: failed to set output channels: " + error);
		return false;
	}

	saveAudioDeviceState();
	audioConfigVersion++;
	loadActivePlugins();
	return true;
}

void AudioEngine::scanPluginPath(const String& path, bool scanVst, bool scanVst3)
{
	const FileSearchPath searchPath(path);
	if (searchPath.getNumPaths() == 0)
		return;

	const File deadMansPedalFile(getAppProperties().getUserSettings()
		->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

	for (int i = 0; i < formatManager.getNumFormats(); ++i)
	{
		auto* format = formatManager.getFormat(i);
		if (format == nullptr)
			continue;

		const String formatName = format->getName();
		const bool isVst3 = formatName.containsIgnoreCase("VST3");
		const bool isVst = formatName.containsIgnoreCase("VST") && !isVst3;
		if ((isVst && !scanVst) || (isVst3 && !scanVst3) || (!isVst && !isVst3))
			continue;

		PluginDirectoryScanner scanner(knownPluginList, *format, searchPath, true, deadMansPedalFile, false);
		String pluginBeingScanned;
		while (scanner.scanNextFile(true, pluginBeingScanned)) {}
	}

	flushPendingSaves();
}

void AudioEngine::scanDefaultPluginLocations(bool scanVst, bool scanVst3)
{
	const File deadMansPedalFile(getAppProperties().getUserSettings()
		->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

	for (int i = 0; i < formatManager.getNumFormats(); ++i)
	{
		auto* format = formatManager.getFormat(i);
		if (format == nullptr)
			continue;

		const String formatName = format->getName();
		const bool isVst3 = formatName.containsIgnoreCase("VST3");
		const bool isVst = formatName.containsIgnoreCase("VST") && !isVst3;
		if ((isVst && !scanVst) || (isVst3 && !scanVst3) || (!isVst && !isVst3))
			continue;

		const auto searchPath = getWindowsDefaultPluginSearchPath(*format, isVst, isVst3);
		PluginDirectoryScanner scanner(knownPluginList, *format, searchPath, true, deadMansPedalFile, false);
		String pluginBeingScanned;
		while (scanner.scanNextFile(true, pluginBeingScanned)) {}
	}

	flushPendingSaves();
}

bool AudioEngine::isPluginBypassed(int sortedIndex) const
{
	const auto plugins = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) plugins.size())
		return false;

	return pluginStateStore.getValue("bypass", plugins[(size_t) sortedIndex]).getIntValue() != 0;
}

bool AudioEngine::isKnownPluginMenuId(int menuId) const
{
	return KnownPluginList::getIndexChosenByMenu(knownPluginList.getTypes(), menuId) > -1;
}

void AudioEngine::addKnownPluginsToMenu(PopupMenu& menu) const
{
	KnownPluginList::addToMenu(menu, knownPluginList.getTypes(), pluginSortMethod);
}

void AudioEngine::loadActivePlugins()
{
	lightHostLog("AudioEngine loadActivePlugins begin.");
	setLightHostCrashContext("AudioEngine::loadActivePlugins begin");
	PluginWindow::closeAllCurrentlyOpenWindows();
	chainReloadCount++;

	int inputChannels = 2;
	int outputChannels = 2;
	if (AudioIODevice* device = deviceManager.getCurrentAudioDevice())
	{
		inputChannels = jmax(1, device->getActiveInputChannels().countNumberOfSetBits());
		outputChannels = jmax(1, device->getActiveOutputChannels().countNumberOfSetBits());
	}

	auto snapshot = std::make_shared<ChainSnapshot>();
	snapshot->inputChannels = inputChannels;
	snapshot->outputChannels = outputChannels;
	auto previousSnapshot = hostProcessor.getActiveSnapshot();
	std::vector<bool> reusedPreviousSlots(previousSnapshot != nullptr ? previousSnapshot->slots.size() : 0, false);

	const auto timeSorted = getActivePluginsSorted();
	lightHostLog("AudioEngine loadActivePlugins activeCount=" + String((int) timeSorted.size())
		+ " inputChannels=" + String(inputChannels)
		+ " outputChannels=" + String(outputChannels));

	for (int i = 0; i < (int) timeSorted.size(); i++)
	{
		PluginDescription plugin = timeSorted[(size_t) i];
		const String pluginContext = "AudioEngine::loadActivePlugins plugin[" + String(i) + "] '" + plugin.name
			+ "' format='" + plugin.pluginFormatName
			+ "' inputs=" + String(plugin.numInputChannels)
			+ " outputs=" + String(plugin.numOutputChannels)
			+ " path='" + plugin.fileOrIdentifier + "'";
		setLightHostCrashContext(pluginContext);
		lightHostLog("AudioEngine loading " + pluginContext);

		const String failedReason = pluginStateStore.getValue("failed", plugin);
		if (failedReason.isNotEmpty())
		{
			const auto message = "Light Host Modern: skipping quarantined plugin '" + plugin.name + "': " + failedReason;
			Logger::writeToLog(message);
			lightHostLog(message);
			continue;
		}

		std::shared_ptr<PluginSlot> reusableSlot;
		if (previousSnapshot != nullptr)
		{
			for (int previousIndex = 0; previousIndex < (int) previousSnapshot->slots.size(); ++previousIndex)
			{
				auto& previousSlot = previousSnapshot->slots[(size_t) previousIndex];
				if (reusedPreviousSlots[(size_t) previousIndex]
					|| previousSlot == nullptr
					|| previousSlot->processDisabled.load(std::memory_order_acquire)
					|| !previousSlot->description.isDuplicateOf(plugin))
					continue;

				reusableSlot = previousSlot;
				reusedPreviousSlots[(size_t) previousIndex] = true;
				break;
			}
		}

		if (!formatManager.doesPluginStillExist(plugin))
		{
			const String errorMessage = "Plugin file or identifier no longer exists";
			const auto message = "Light Host Modern: failed to load plugin '" + plugin.name + "': " + errorMessage;
			Logger::writeToLog(message);
			lightHostLog(message);
			pluginStateStore.setValue("failed", plugin, errorMessage);
			markSettingsDirty();
			continue;
		}

		if (plugin.numInputChannels <= 0 || plugin.numOutputChannels <= 0)
		{
			const String errorMessage = "Plugin does not expose audio input and output channels";
			const auto message = "Light Host Modern: failed to load plugin '" + plugin.name + "': " + errorMessage;
			Logger::writeToLog(message);
			lightHostLog(message);
			pluginStateStore.setValue("failed", plugin, errorMessage);
			markSettingsDirty();
			continue;
		}

		const bool bypass = pluginStateStore.getValue("bypass", plugin).getIntValue() != 0;
		if (reusableSlot != nullptr)
		{
			lightHostLog("AudioEngine reusing active plugin slot '" + plugin.name + "'");
			reusableSlot->bypassed.store(bypass, std::memory_order_release);
			snapshot->maxPluginChannels = jmax(snapshot->maxPluginChannels, jmax(reusableSlot->inputChannels, reusableSlot->outputChannels));
			snapshot->reusedSlots++;
			snapshot->slots.push_back(std::move(reusableSlot));
			continue;
		}

		String errorMessage;
		std::unique_ptr<AudioPluginInstance> instance;
		try
		{
			setLightHostCrashContext(pluginContext + " createPluginInstance");
			lightHostLog("AudioEngine createPluginInstance begin '" + plugin.name + "'");
			instance = formatManager.createPluginInstance(plugin,
				hostProcessor.getCurrentSampleRateForPlugins(),
				hostProcessor.getCurrentBlockSizeForPlugins(),
				errorMessage);
			lightHostLog("AudioEngine createPluginInstance returned '" + plugin.name + "' instance=" + String(instance != nullptr ? "yes" : "no"));
		}
		catch (const std::exception& e)
		{
			errorMessage = "Plugin threw C++ exception while creating instance: " + String(e.what());
			const auto message = "Light Host Modern: failed to load plugin '" + plugin.name + "': " + errorMessage;
			Logger::writeToLog(message);
			lightHostLog(message);
			pluginStateStore.setValue("failed", plugin, errorMessage);
			markSettingsDirty();
			continue;
		}
		catch (...)
		{
			errorMessage = "Plugin threw unknown exception while creating instance";
			const auto message = "Light Host Modern: failed to load plugin '" + plugin.name + "': " + errorMessage;
			Logger::writeToLog(message);
			lightHostLog(message);
			pluginStateStore.setValue("failed", plugin, errorMessage);
			markSettingsDirty();
			continue;
		}

		if (instance == nullptr)
		{
			if (errorMessage.isEmpty())
				errorMessage = "Unknown error";

			const auto message = "Light Host Modern: failed to load plugin '" + plugin.name + "': " + errorMessage;
			Logger::writeToLog(message);
			lightHostLog(message);
			pluginStateStore.setValue("failed", plugin, errorMessage);
			markSettingsDirty();
			continue;
		}

		String savedPluginState = pluginStateStore.getValue("state", plugin);
		MemoryBlock savedPluginBinary;
		const bool stateDecoded = savedPluginState.isEmpty() || savedPluginBinary.fromBase64Encoding(savedPluginState);
		if (!stateDecoded)
		{
			Logger::writeToLog("Light Host Modern: ignored invalid saved state for plugin '" + plugin.name + "'");
			pluginStateStore.removeValue("state", plugin);
			markSettingsDirty();
		}
		if (savedPluginBinary.getSize() > 0)
		{
			try
			{
				setLightHostCrashContext(pluginContext + " restore state");
				lightHostLog("AudioEngine restoring state begin '" + plugin.name + "' bytes=" + String((int) savedPluginBinary.getSize()));
				instance->setStateInformation(savedPluginBinary.getData(), (int) savedPluginBinary.getSize());
				lightHostLog("AudioEngine restoring state completed '" + plugin.name + "'");
			}
			catch (...)
			{
				const auto message = "Light Host Modern: plugin threw while restoring state '" + plugin.name + "'";
				Logger::writeToLog(message);
				lightHostLog(message);
				pluginStateStore.removeValue("state", plugin);
				markSettingsDirty();
			}
		}

		pluginStateStore.removeValue("failed", plugin);
		setLightHostCrashContext(pluginContext + " create PluginSlot");
		lightHostLog("AudioEngine creating PluginSlot '" + plugin.name + "'");
		auto slot = std::make_shared<PluginSlot>(plugin, std::move(instance));
		slot->bypassed.store(bypass, std::memory_order_relaxed);
		snapshot->maxPluginChannels = jmax(snapshot->maxPluginChannels, jmax(slot->inputChannels, slot->outputChannels));
		snapshot->rebuiltSlots++;
		snapshot->slots.push_back(std::move(slot));
		lightHostLog("AudioEngine added PluginSlot '" + plugin.name + "'");
	}

	setLightHostCrashContext("AudioEngine::loadActivePlugins publishSnapshot slots=" + String((int) snapshot->slots.size()));
	lightHostLog("AudioEngine publishSnapshot begin slots=" + String((int) snapshot->slots.size())
		+ " rebuilt=" + String((int) snapshot->rebuiltSlots)
		+ " reused=" + String((int) snapshot->reusedSlots));
	hostProcessor.publishSnapshot(std::move(snapshot));
	chainVersion++;
	lightHostLog("AudioEngine publishSnapshot completed.");
	clearLightHostCrashContext();
	markSettingsDirty();
	lightHostLog("AudioEngine loadActivePlugins completed.");
}

void AudioEngine::addPluginFromMenuId(int menuId)
{
	const auto knownTypes = knownPluginList.getTypes();
	const int knownIndex = KnownPluginList::getIndexChosenByMenu(knownTypes, menuId);
	if (knownIndex < 0)
		return;

	const auto sortedKnownTypes = getKnownPluginsSorted();
	for (int i = 0; i < (int) sortedKnownTypes.size(); ++i)
	{
		if (sortedKnownTypes[(size_t) i].isDuplicateOf(knownTypes[knownIndex]))
		{
			addKnownPluginByIndex(i);
			return;
		}
	}
}

bool AudioEngine::addKnownPluginByIndex(int sortedIndex)
{
	const auto knownTypes = getKnownPluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) knownTypes.size())
		return false;

	PluginDescription plugin = knownTypes[(size_t) sortedIndex];
	const auto addMessage = "Light Host Modern: requested active plugin add '" + plugin.name
		+ "' format='" + plugin.pluginFormatName
		+ "' inputs=" + String(plugin.numInputChannels)
		+ " outputs=" + String(plugin.numOutputChannels)
		+ " path='" + plugin.fileOrIdentifier + "'";
	Logger::writeToLog(addMessage);
	lightHostLog(addMessage);

	if (plugin.numInputChannels <= 0 || plugin.numOutputChannels <= 0)
	{
		const auto rejectMessage = "Light Host Modern: rejected plugin without audio input/output before load '" + plugin.name + "'";
		Logger::writeToLog(rejectMessage);
		lightHostLog(rejectMessage);
		return false;
	}

	const auto activeTypes = activePluginList.getTypes();
	bool needsInstanceUid = false;
	for (const auto& activePlugin : activeTypes)
	{
		if (plugin.isDuplicateOf(activePlugin))
		{
			needsInstanceUid = true;
			break;
		}
	}

	if (needsInstanceUid)
	{
		int candidateUid = plugin.deprecatedUid == 0 ? 1 : plugin.deprecatedUid + 1;
		for (;;)
		{
			plugin.deprecatedUid = candidateUid;
			bool isUnique = true;
			for (const auto& activePlugin : activeTypes)
			{
				if (plugin.isDuplicateOf(activePlugin))
				{
					isUnique = false;
					break;
				}
			}

			if (isUnique)
				break;

			++candidateUid;
		}
	}

	savePluginStates();

	const auto timeSorted = getActivePluginsSorted();
	int nextOrder = 0;
	for (auto& activePlugin : timeSorted)
	{
		const int order = pluginStateStore.getValue("order", activePlugin).getIntValue();
		if (order > nextOrder)
			nextOrder = order;
	}

	pluginStateStore.setValue("order", plugin, nextOrder + 1);
	pluginStateStore.removeValue("state", plugin);
	pluginStateStore.removeValue("failed", plugin);
	pluginStateStore.removeValue("bypass", plugin);
	const auto cleanStateMessage = "Light Host Modern: starting plugin with clean state '" + plugin.name + "'";
	Logger::writeToLog(cleanStateMessage);
	lightHostLog(cleanStateMessage);
	markSettingsDirty();
	activePluginList.addType(plugin);
	const auto loadMessage = "Light Host Modern: loading active plugin chain after adding '" + plugin.name + "'";
	Logger::writeToLog(loadMessage);
	lightHostLog(loadMessage);
	loadActivePlugins();
	if (pluginStateStore.getValue("failed", plugin).isEmpty())
	{
		const auto loadedMessage = "Light Host Modern: plugin loaded successfully '" + plugin.name + "'";
		Logger::writeToLog(loadedMessage);
		lightHostLog(loadedMessage);
		saveActivePluginChain(true);
		return true;
	}

	const auto failedMessage = "Light Host Modern: plugin failed after load attempt '" + plugin.name + "'";
	Logger::writeToLog(failedMessage);
	lightHostLog(failedMessage);
	pluginStateStore.removeValue("order", plugin);
	pluginStateStore.removeValue("bypass", plugin);
	pluginStateStore.removeValue("state", plugin);
	pluginStateStore.removeValue("failed", plugin);
	activePluginList.removeType(plugin);
	markSettingsDirty();
	loadActivePlugins();
	saveActivePluginChain(false);
	return false;
}

void AudioEngine::duplicatePlugin(int sortedIndex)
{
	const auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size())
		return;

	PluginDescription plugin = timeSorted[(size_t) sortedIndex];
	const auto activeTypes = activePluginList.getTypes();

	int candidateUid = plugin.deprecatedUid == 0 ? 1 : plugin.deprecatedUid + 1;
	for (;;)
	{
		plugin.deprecatedUid = candidateUid;
		bool isUnique = true;
		for (const auto& activePlugin : activeTypes)
		{
			if (plugin.isDuplicateOf(activePlugin))
			{
				isUnique = false;
				break;
			}
		}

		if (isUnique)
			break;

		++candidateUid;
	}

	savePluginStates();

	int nextOrder = 0;
	for (auto& activePlugin : timeSorted)
	{
		const int order = pluginStateStore.getValue("order", activePlugin).getIntValue();
		if (order > nextOrder)
			nextOrder = order;
	}

	pluginStateStore.setValue("order", plugin, nextOrder + 1);
	markSettingsDirty();
	activePluginList.addType(plugin);
	loadActivePlugins();
	saveActivePluginChain(true);
}

int AudioEngine::removeKnownPluginByIndex(int sortedIndex)
{
	const auto knownTypes = getKnownPluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) knownTypes.size())
		return 0;

	const auto pluginToRemove = knownTypes[(size_t) sortedIndex];
	int activeRemoved = 0;

	savePluginStates();
	const auto activeTypes = activePluginList.getTypes();
	for (auto& activePlugin : activeTypes)
	{
		if (!describesSamePluginBinary(pluginToRemove, activePlugin))
			continue;

		pluginStateStore.removeValue("order", activePlugin);
		pluginStateStore.removeValue("bypass", activePlugin);
		pluginStateStore.removeValue("state", activePlugin);
		pluginStateStore.removeValue("failed", activePlugin);
		activePluginList.removeType(activePlugin);
		++activeRemoved;
	}

	knownPluginList.removeType(pluginToRemove);
	if (activeRemoved > 0)
	{
		markSettingsDirty();
		loadActivePlugins();
	}
	saveActivePluginChain(activeRemoved > 0);
	return activeRemoved;
}

int AudioEngine::clearKnownPlugins()
{
	savePluginStates();

	const auto activeTypes = activePluginList.getTypes();
	for (auto& plugin : activeTypes)
	{
		pluginStateStore.removeValue("order", plugin);
		pluginStateStore.removeValue("bypass", plugin);
		pluginStateStore.removeValue("state", plugin);
		pluginStateStore.removeValue("failed", plugin);
		activePluginList.removeType(plugin);
	}

	const auto knownTypes = knownPluginList.getTypes();
	for (auto& plugin : knownTypes)
		knownPluginList.removeType(plugin);

	if (!activeTypes.isEmpty())
	{
		markSettingsDirty();
		loadActivePlugins();
	}

	saveActivePluginChain(!activeTypes.isEmpty());
	return activeTypes.size();
}

void AudioEngine::openKnownPluginLocation(int sortedIndex) const
{
	const auto knownTypes = getKnownPluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) knownTypes.size())
		return;

	File location(knownTypes[(size_t) sortedIndex].fileOrIdentifier);
	if (location.existsAsFile())
		location.revealToUser();
	else if (location.getParentDirectory().exists())
		location.getParentDirectory().revealToUser();
}

void AudioEngine::removePlugin(int sortedIndex)
{
	const auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size())
		return;

	savePluginStates();

	PluginDescription pluginToDelete = timeSorted[(size_t) sortedIndex];
	bool foundPluginToDelete = false;
	const auto activeTypes = activePluginList.getTypes();
	for (auto& current : activeTypes)
	{
		if (pluginToDelete.isDuplicateOf(current))
		{
			foundPluginToDelete = true;
			break;
		}
	}

	if (!foundPluginToDelete)
		return;

	pluginStateStore.removeValue("order", pluginToDelete);
	pluginStateStore.removeValue("bypass", pluginToDelete);
	pluginStateStore.removeValue("state", pluginToDelete);
	pluginStateStore.removeValue("failed", pluginToDelete);
	markSettingsDirty();

	activePluginList.removeType(pluginToDelete);
	loadActivePlugins();
	saveActivePluginChain(false);
}

void AudioEngine::movePluginUp(int sortedIndex)
{
	if (sortedIndex <= 0)
		return;

	auto timeSorted = getActivePluginsSorted();
	if (sortedIndex >= (int) timeSorted.size())
		return;

	savePluginStates();
	std::swap(timeSorted[(size_t) sortedIndex], timeSorted[(size_t) sortedIndex - 1]);
	for (int i = 0; i < (int) timeSorted.size(); i++)
		pluginStateStore.setValue("order", timeSorted[(size_t) i], i + 1);

	markSettingsDirty();
	loadActivePlugins();
	saveActivePluginChain(false);
}

void AudioEngine::movePluginDown(int sortedIndex)
{
	auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size() - 1)
		return;

	savePluginStates();
	std::swap(timeSorted[(size_t) sortedIndex], timeSorted[(size_t) sortedIndex + 1]);
	for (int i = 0; i < (int) timeSorted.size(); i++)
		pluginStateStore.setValue("order", timeSorted[(size_t) i], i + 1);

	markSettingsDirty();
	loadActivePlugins();
	saveActivePluginChain(false);
}

void AudioEngine::movePluginToIndex(int fromSortedIndex, int toSortedIndex)
{
	auto timeSorted = getActivePluginsSorted();
	if (fromSortedIndex < 0 || fromSortedIndex >= (int) timeSorted.size())
		return;

	if (toSortedIndex < 0)
		toSortedIndex = 0;
	if (toSortedIndex >= (int) timeSorted.size())
		toSortedIndex = (int) timeSorted.size() - 1;
	if (fromSortedIndex == toSortedIndex)
		return;

	savePluginStates();
	auto plugin = timeSorted[(size_t) fromSortedIndex];
	timeSorted.erase(timeSorted.begin() + fromSortedIndex);
	timeSorted.insert(timeSorted.begin() + toSortedIndex, plugin);

	for (int i = 0; i < (int) timeSorted.size(); i++)
		pluginStateStore.setValue("order", timeSorted[(size_t) i], i + 1);

	markSettingsDirty();
	loadActivePlugins();
	saveActivePluginChain(false);
}

void AudioEngine::setPluginBypassed(int sortedIndex, bool shouldBypass)
{
	const auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size())
		return;

	pluginStateStore.setValue("bypass", timeSorted[(size_t) sortedIndex], shouldBypass);
	markSettingsDirty();
	bypassToggleCount++;
	chainVersion++;

	if (auto* const slot = findActiveSlotFor(timeSorted[(size_t) sortedIndex]))
		slot->bypassed.store(shouldBypass, std::memory_order_release);

	saveActivePluginChain(false);
}

void AudioEngine::deletePluginStates()
{
	const auto list = getActivePluginsSorted();
	for (auto& plugin : list)
		pluginStateStore.removeValue("state", plugin);

	markSettingsDirty();
	flushPendingSaves();
}

void AudioEngine::savePluginStates()
{
	auto snapshot = hostProcessor.getActiveSnapshot();
	if (snapshot == nullptr)
		return;

	bool savedAnyState = false;

	for (auto& slot : snapshot->slots)
	{
		if (slot == nullptr || slot->processor == nullptr)
			continue;

		AudioProcessor& processor = *slot->processor;
		MemoryBlock savedStateBinary;
		try
		{
			processor.getStateInformation(savedStateBinary);
			pluginStateStore.setValue("state", slot->description, savedStateBinary.toBase64Encoding());
			getAppProperties().getUserSettings()->removeValue(PluginStateStore::getLegacyKey("state", slot->description));
			savedAnyState = true;
		}
		catch (...)
		{
			Logger::writeToLog("Light Host Modern: plugin threw while saving state '" + slot->description.name + "'");
		}
	}

	if (savedAnyState)
	{
		pluginStateSaveCount++;
		markSettingsDirty();
		flushPendingSaves();
	}
}

void AudioEngine::saveActivePluginList()
{
	std::unique_ptr<XmlElement> savedPluginList(activePluginList.createXml());
	if (savedPluginList == nullptr)
		return;

	getAppProperties().getUserSettings()->setValue("pluginListActive", savedPluginList.get());
	lightHostLog("AudioEngine saved active plugin list count=" + String(activePluginList.getNumTypes()));
	markSettingsDirty();
}

void AudioEngine::saveActivePluginChain(bool saveProcessorStates)
{
	if (saveProcessorStates)
		savePluginStates();

	saveActivePluginList();
	flushPendingSaves();
}

void AudioEngine::saveAudioDeviceState()
{
	audioDeviceStateDirty = true;
	startTimer(persistenceTimerId, 1000);
}

void AudioEngine::flushPendingSaves()
{
	stopTimer(persistenceTimerId);

	if (audioDeviceStateDirty)
	{
		audioDeviceStateDirty = false;

		std::unique_ptr<XmlElement> audioState(deviceManager.createStateXml());
		if (audioState != nullptr)
			getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState.get());
		else
			getAppProperties().getUserSettings()->removeValue("audioDeviceState");

		settingsDirty = true;
	}

	if (settingsDirty || pluginStateStore.isDirty())
	{
		settingsDirty = false;
		settingsFlushCount++;
		pluginStateStore.flushIfDirty();
		getAppProperties().getUserSettings()->saveIfNeeded();
	}
}

void AudioEngine::removePluginsLackingInputOutput()
{
	std::vector<PluginDescription> removeList;
	const auto knownTypes = knownPluginList.getTypes();
	for (auto& plugin : knownTypes)
	{
		if (plugin.numInputChannels <= 0 || plugin.numOutputChannels <= 0)
			removeList.push_back(plugin);
	}

	for (auto& plugin : removeList)
		knownPluginList.removeType(plugin);
}

void AudioEngine::removeMissingKnownPlugins()
{
	std::vector<PluginDescription> removeList;
	const auto knownTypes = knownPluginList.getTypes();
	for (auto& plugin : knownTypes)
	{
		const File pluginFile(plugin.fileOrIdentifier);
		const bool looksLikePath = plugin.fileOrIdentifier.containsChar('\\')
			|| plugin.fileOrIdentifier.containsChar('/')
			|| plugin.fileOrIdentifier.containsChar(':');

		if (plugin.fileOrIdentifier.isNotEmpty()
			&& looksLikePath
			&& !pluginFile.existsAsFile())
			removeList.push_back(plugin);
	}

	for (auto& plugin : removeList)
		knownPluginList.removeType(plugin);

	if (!removeList.empty())
		flushPendingSaves();
}

void AudioEngine::showPluginEditor(int sortedIndex)
{
	const auto timeSorted = getActivePluginsSorted();
	if (sortedIndex < 0 || sortedIndex >= (int) timeSorted.size())
		return;

	if (auto* const slot = findActiveSlotFor(timeSorted[(size_t) sortedIndex]))
		if (slot->processor != nullptr)
			if (PluginWindow* const window = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, PluginWindow::Normal))
				window->toFront(true);
}

DiagnosticsSnapshot AudioEngine::getDiagnosticsSnapshot() const
{
	DiagnosticsSnapshot snapshot = deviceController.createDiagnosticsSnapshot(const_cast<AudioDeviceManager&>(deviceManager));
	const RealtimeHostStats realtimeStats = hostProcessor.getStats();
	const auto recoveryConfig = getAudioRecoveryConfiguration();
	const String mode = normaliseAudioPersistenceMode(recoveryConfig.mode);
	snapshot.recoveryState = audioRecoveryState;
	snapshot.recoveryMessage = audioRecoveryMessage;
	snapshot.recoveryAttempt = failedAudioRecoveryAttempts;
	snapshot.recoveryMaxAttempts = recoveryConfig.retryAttempts;
	snapshot.recoveryTargetBackend = mode == "custom" ? recoveryConfig.customBackend : recoveryConfig.lastBackend;
	snapshot.recoveryTargetInputDevice = mode == "custom" ? recoveryConfig.customInputDevice : recoveryConfig.lastInputDevice;
	snapshot.recoveryTargetOutputDevice = mode == "custom" ? recoveryConfig.customOutputDevice : recoveryConfig.lastOutputDevice;
	snapshot.activePlugins = activePluginList.getNumTypes();
	snapshot.loadedPlugins = realtimeStats.loadedSlots;
	snapshot.chainLatencySamples = realtimeStats.chainLatencySamples;
	snapshot.chainReloads = chainReloadCount;
	snapshot.bypassToggles = bypassToggleCount;
	snapshot.pluginStateSaves = pluginStateSaveCount;
	snapshot.settingsFlushes = settingsFlushCount;
	snapshot.processFailures = realtimeStats.processFailures;
	snapshot.reusedSlots = realtimeStats.reusedSlots;
	snapshot.rebuiltSlots = realtimeStats.rebuiltSlots;
	snapshot.inputLevel = realtimeStats.inputLevel;
	snapshot.outputLevel = realtimeStats.outputLevel;
	return snapshot;
}

void AudioEngine::timerCallback(int timerId)
{
	hostProcessor.collectRetiredSnapshots();
	recordProcessFailures();

	if (timerId == audioWatchdogTimerId)
	{
		const auto recoveryConfig = getAudioRecoveryConfiguration();
		const String mode = normaliseAudioPersistenceMode(recoveryConfig.mode);
		startTimer(audioWatchdogTimerId, recoveryConfig.retrySeconds * 1000);

		AudioIODevice* device = deviceManager.getCurrentAudioDevice();
		const bool isRunning = device != nullptr && device->isOpen() && device->isPlaying();
		if (isRunning)
		{
			closeCurrentAudioDeviceIfBlocked("watchdog");
			device = deviceManager.getCurrentAudioDevice();
			if (device == nullptr || !device->isOpen() || !device->isPlaying())
				return;

			if (mode != "disabled" && !currentAudioDeviceMatchesPreferred(recoveryConfig))
			{
				AudioDeviceManager::AudioDeviceSetup setup;
				deviceManager.getAudioDeviceSetup(setup);
				audioRecoveryState = "retrying";
				audioRecoveryMessage = "Current audio device does not match the preferred device; reconnecting.";
				lightHostLog("AudioEngine audio persistence rejected fallback device current='"
					+ quotedTarget(device->getTypeName(), setup.inputDeviceName, setup.outputDeviceName) + "'");
				deviceManager.closeAudioDevice();
			}
			else
			{
				failedAudioRecoveryAttempts = 0;
				audioRecoveryState = "running";
				audioRecoveryMessage.clear();
				return;
			}
		}

		if (mode == "disabled")
		{
			deviceController.recoverIfNeeded(deviceManager, recoveryConfig, failedAudioRecoveryAttempts, audioRecoveryState, audioRecoveryMessage);
			closeCurrentAudioDeviceIfBlocked("automatic recovery");
			if (failedAudioRecoveryAttempts > 0)
				saveAudioDeviceState();
			return;
		}

		if (failedAudioRecoveryAttempts >= recoveryConfig.retryAttempts)
		{
			audioRecoveryState = "failed";
			audioRecoveryMessage = "Preferred audio device did not reconnect after "
				+ String(recoveryConfig.retryAttempts) + " attempts. Choose another device or retry manually.";
			return;
		}

		++failedAudioRecoveryAttempts;
		audioRecoveryState = "retrying";
		audioRecoveryMessage = "Retrying preferred audio device (" + String(failedAudioRecoveryAttempts)
			+ "/" + String(recoveryConfig.retryAttempts) + ").";
		lightHostLog("AudioEngine audio persistence " + audioRecoveryMessage);
		applyPreferredAudioDevice(recoveryConfig, false);
		return;
	}

	if (timerId == persistenceTimerId)
	{
		flushPendingSaves();
		return;
	}

	if (timerId == diagnosticsTimerId)
	{
		logDiagnosticsSnapshot();
		return;
	}
}

void AudioEngine::changeListenerCallback(ChangeBroadcaster* changed)
{
	if (changed == &knownPluginList)
	{
		pluginDatabaseVersion++;
		std::unique_ptr<XmlElement> savedPluginList(knownPluginList.createXml());
		if (savedPluginList != nullptr)
		{
			getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList.get());
			markSettingsDirty();
		}
	}
	else if (changed == &activePluginList)
	{
		chainVersion++;
		saveActivePluginList();
	}
	else if (changed == &deviceManager)
	{
		const auto recoveryConfig = getAudioRecoveryConfiguration();
		const String mode = normaliseAudioPersistenceMode(recoveryConfig.mode);
		if (mode != "disabled")
		{
			AudioIODevice* device = deviceManager.getCurrentAudioDevice();
			if (device == nullptr || !device->isOpen())
			{
				audioRecoveryState = "retrying";
				audioRecoveryMessage = "Preferred audio device is unavailable; waiting to reconnect.";
				audioConfigVersion++;
				startTimer(audioWatchdogTimerId, 250);
				return;
			}

			if (!currentAudioDeviceMatchesPreferred(recoveryConfig))
			{
				AudioDeviceManager::AudioDeviceSetup setup;
				deviceManager.getAudioDeviceSetup(setup);
				audioRecoveryState = "retrying";
				audioRecoveryMessage = "Audio device changed away from the preferred device; waiting to reconnect.";
				audioConfigVersion++;
				lightHostLog("AudioEngine audio persistence ignored non-preferred device change current='"
					+ quotedTarget(device->getTypeName(), setup.inputDeviceName, setup.outputDeviceName) + "'");
				startTimer(audioWatchdogTimerId, 250);
				return;
			}
		}

		failedAudioRecoveryAttempts = 0;
		audioRecoveryState = "running";
		audioRecoveryMessage.clear();
		audioConfigVersion++;
		saveAudioDeviceState();
	}
}

void AudioEngine::markSettingsDirty()
{
	settingsDirty = true;
	pluginStateStore.markDirty();
	startTimer(persistenceTimerId, 1000);
}

PluginSlot* AudioEngine::findActiveSlotFor(const PluginDescription& plugin) const
{
	auto snapshot = hostProcessor.getActiveSnapshot();
	if (snapshot == nullptr)
		return nullptr;

	for (auto& slot : snapshot->slots)
		if (slot != nullptr && slot->description.isDuplicateOf(plugin))
			return slot.get();

	return nullptr;
}

void AudioEngine::recordProcessFailures()
{
	auto snapshot = hostProcessor.getActiveSnapshot();
	if (snapshot == nullptr)
		return;

	for (auto& slot : snapshot->slots)
	{
		if (slot == nullptr || !slot->processFailed.exchange(false, std::memory_order_acq_rel))
			continue;

		const String errorMessage = "Plugin threw while processing audio";
		Logger::writeToLog("Light Host Modern: failed plugin disabled in audio chain '" + slot->description.name + "': " + errorMessage);
		pluginStateStore.setValue("failed", slot->description, errorMessage);
		markSettingsDirty();
	}
}

void AudioEngine::logDiagnosticsSnapshot()
{
	const DiagnosticsSnapshot snapshot = getDiagnosticsSnapshot();
	Logger::writeToLog("Light Host Modern diagnostics: backend=" + snapshot.backend
		+ ", device=" + snapshot.deviceName
		+ ", cpu=" + String(snapshot.cpuUsagePercent, 2) + "%"
		+ ", xruns=" + String(snapshot.xRunCount)
		+ ", sampleRate=" + String(snapshot.sampleRate, 0)
		+ ", buffer=" + String(snapshot.bufferSize)
		+ ", inputLatency=" + String(snapshot.inputLatency)
		+ ", outputLatency=" + String(snapshot.outputLatency)
		+ ", inputChannels=" + String(snapshot.inputChannels)
		+ ", outputChannels=" + String(snapshot.outputChannels)
		+ ", inputLevel=" + String(snapshot.inputLevel, 3)
		+ ", outputLevel=" + String(snapshot.outputLevel, 3)
		+ ", activePlugins=" + String(snapshot.activePlugins)
		+ ", loadedPlugins=" + String(snapshot.loadedPlugins)
		+ ", chainLatency=" + String(snapshot.chainLatencySamples)
		+ ", chainReloads=" + String(snapshot.chainReloads)
		+ ", bypassToggles=" + String(snapshot.bypassToggles)
		+ ", pluginStateSaves=" + String(snapshot.pluginStateSaves)
		+ ", settingsFlushes=" + String(snapshot.settingsFlushes)
		+ ", processFailures=" + String(snapshot.processFailures)
		+ ", reusedSlots=" + String(snapshot.reusedSlots)
		+ ", rebuiltSlots=" + String(snapshot.rebuiltSlots));
}
