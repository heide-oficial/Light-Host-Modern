#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PluginStateCapture.h"
#include "PluginWindow.h"
#include "RuntimeProfile.h"
#include "VerboseLog.h"
#include <algorithm>
#include <cmath>
#include <vector>

void lightHostModernLog(const String& message);
void setLightHostModernCrashContext(const String& context);
void clearLightHostModernCrashContext();

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

		// Filesystem existence checks belong to the isolated enumeration worker.
		searchPath.addIfNotAlreadyThere(File(folder));
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
	if (settings->containsKey(key))
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

AudioEngine::AudioEngine(bool startInSafeMode, bool shouldRestoreActivePluginsOnStartup)
	: safeMode(startInSafeMode),
	  restoreActivePluginsOnStartup(shouldRestoreActivePluginsOnStartup),
      deviceController(deviceManager, *getAppProperties().getUserSettings(),
          [this] { markSettingsDirty(); }, [this] { loadActivePlugins(); })
{
    deviceManager.allowed = [this](const String& backend, const String& input, const String& output) {
        return deviceController.isAudioDeviceCreationAllowed(backend, input, output);
    };
    addEnabledPluginFormats(formatManager);
	std::unique_ptr<XmlElement> savedPluginList(getXmlValuePreserving("pluginList"));
	if (savedPluginList != nullptr)
		knownPluginList.recreateFromXml(*savedPluginList);

	knownPluginList.addChangeListener(this);

    sessionLoadSuppressed = safeMode || !restoreActivePluginsOnStartup;
    auto* settings = getAppProperties().getUserSettings();
    setDiagnosticsEnabled(settings->getBoolValue("diagnosticsEnabled", true));
    auto storage = std::make_shared<lightHostModern::DiskSessionStorage>(settings->getFile());
    const auto recovered = lightHostModern::SessionStore::recover(*storage);
    if (recovered.document)
    {
        instances = recovered.document->instances;
        sessionMigrationId = recovered.document->migrationId;
        if (recovered.warning.isNotEmpty())
            instances.recoveryError = (instances.recoveryError.isEmpty() ? String() : instances.recoveryError + "\n") + recovered.warning;
    }
    else if (recovered.found)
    {
        instances.writable = false;
        instances.recoveryError = recovered.warning;
    }
    else
    {
        // Copy the exact previous file before device initialization or migration
        // can alter its keys. Legacy material remains untouched after activation.
        const auto backupError = lightHostModern::SessionStore::backupLegacy(*storage, sessionMigrationId);
        if (backupError.isNotEmpty())
        {
            instances.writable = false;
            instances.recoveryError = "Could not back up legacy preferences: " + backupError;
        }
        else if (settings->containsKey("pluginInstancesV1"))
        {
            const auto saved = lightHostModern::parseBoundedXml(settings->getValue("pluginInstancesV1"));
            if (!saved || !instances.deserialize(*saved))
            { instances.writable = false; instances.recoveryError = "Invalid instance data; original settings preserved"; }
        }
        else if (settings->containsKey("pluginListActive"))
        {
            const auto legacy = lightHostModern::parseBoundedXml(settings->getValue("pluginListActive"));
            if (legacy) instances.migrate(*legacy, *settings, knownPluginList.getTypes(), sessionMigrationId);
            else { instances.writable = false; instances.recoveryError = "Invalid legacy session; original settings preserved"; }
        }
    }
    sessionStore = std::make_unique<lightHostModern::SessionStore>(std::move(storage), recovered);
    initializeOperatingProfiles();
    deviceController.start(safeMode, lightHostModern::RuntimeProfile::current().noAudio);
    if (operatingProfiles && !sessionLoadSuppressed && !lightHostModern::RuntimeProfile::current().noAudio)
        if (const auto* profile = operatingProfiles->find(instances.profileId); profile && profile->includeAudio)
        {
            auto audio = lightHostModern::parseBoundedJson(profile->audioXml);
            if (auto* object = audio.getDynamicObject()) object->setProperty("expectedGeneration", String(deviceController.getGeneration()));
            AudioDeviceSelection selection;
            if (lightHostModern::audioSelection::parse(audio, selection)) deviceController.restoreProfileConfiguration(selection);
            setMonoInputs(profile->monoInput); setMonoOutput(profile->monoOutput);
        }
    player.setProcessor(&hostProcessor);
    deviceManager.addAudioCallback(&player);
    deviceManager.addChangeListener(this);
    startTimer(audioWatchdogTimerId, 250);
    if (isDiagnosticsEnabled() || lightHostModern::verbose::logger().active()) startTimer(diagnosticsTimerId, 30000);
    loadActivePlugins();
    if (!sessionLoadSuppressed && instances.writable) saveActivePluginList();
}

AudioEngine::~AudioEngine()
{
	cancelPluginScan();
	stopTimer(audioWatchdogTimerId);
	stopTimer(diagnosticsTimerId);
	stopTimer(persistenceTimerId);

    // State capture must precede releaseResources as well as destruction.
    if (!flushSession()) Logger::writeToLog("LightHostModern: final session remains pending: " + getSessionSaveStatus().error);
    if (sessionStore) sessionStore->shutdown();
	flushPendingSaves();

	knownPluginList.removeChangeListener(this);
	deviceManager.removeChangeListener(this);
	deviceManager.removeAudioCallback(&player);
	player.setProcessor(nullptr);

	hostProcessor.publishSnapshot(nullptr);
}

std::unique_ptr<XmlElement> AudioEngine::getXmlValuePreserving(const String& key)
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	auto xml = lightHostModern::parseBoundedXml(settings->getValue(key));
	if (xml == nullptr && settings->getValue(key).isNotEmpty())
	{
		Logger::writeToLog("LightHostModern: preserved invalid XML setting '" + key + "'");
	}

	return xml;
}

int AudioEngine::findKnownPluginIndexById(const String& id) const
{
    const auto known = getKnownPluginsSorted();
    for (size_t i = 0; i < known.size(); ++i)
        if (lightHostModern::knownPluginId(known[i]) == id) return static_cast<int>(i);
    return -1;
}

int AudioEngine::findPluginIndexById(const PluginInstanceId& id) const
{
    return instances.indexOf(id);
}

std::vector<PluginDescription> AudioEngine::getActivePluginsSorted() const
{
    std::vector<PluginDescription> result;
    result.reserve(instances.records.size());
    for (const auto& record : instances.records) result.push_back(record.description);
    return result;
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
    return deviceController.getAudioDeviceConfiguration();
}

AudioRecoveryConfiguration AudioEngine::getAudioRecoveryConfiguration() const
{
    return deviceController.getAudioRecoveryConfiguration();
}

AudioBlocklistConfiguration AudioEngine::getAudioBlocklistConfiguration() const
{
    return deviceController.getAudioBlocklistConfiguration();
}

AvailableAudioChoicesConfiguration AudioEngine::getAvailableAudioChoicesConfiguration()
{
    return deviceController.getAvailableAudioChoicesConfiguration();
}

bool AudioEngine::isAudioBackendBlocked(const String& backendName) const
{
    return deviceController.isAudioBackendBlocked(backendName);
}

bool AudioEngine::isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const
{
    return deviceController.isAudioDeviceBlocked(backendName, role, deviceName);
}

bool AudioEngine::isAudioDeviceChoiceAllowed(const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName) const
{
    return deviceController.isAudioDeviceChoiceAllowed(backendName, inputDeviceName, outputDeviceName);
}

bool AudioEngine::currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const
{
    return deviceController.currentAudioDeviceMatchesPreferred(recoveryConfig);
}

void AudioEngine::closeCurrentAudioDeviceIfBlocked(const String& context)
{
    return deviceController.closeCurrentAudioDeviceIfBlocked(context);
}

void AudioEngine::rememberLastSelectedAudioDevice()
{
    return deviceController.rememberLastSelectedAudioDevice();
}

void AudioEngine::rememberManualSelectedAudioDevice()
{
    return deviceController.rememberManualSelectedAudioDevice();
}

bool AudioEngine::applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry)
{
    return deviceController.applyPreferredAudioDevice(recoveryConfig, manualRetry);
}

bool AudioEngine::setAudioBackendByIndex(int backendIndex)
{
    return deviceController.setAudioBackendByIndex(backendIndex);
}

bool AudioEngine::setAudioInputDeviceByIndex(int deviceIndex)
{
    return deviceController.setAudioInputDeviceByIndex(deviceIndex);
}

bool AudioEngine::setAudioOutputDeviceByIndex(int deviceIndex)
{
    return deviceController.setAudioOutputDeviceByIndex(deviceIndex);
}

bool AudioEngine::setAudioPersistenceMode(const String& mode)
{
    return deviceController.setAudioPersistenceMode(mode);
}

bool AudioEngine::setAudioPersistenceRetrySeconds(int seconds)
{
    return deviceController.setAudioPersistenceRetrySeconds(seconds);
}

bool AudioEngine::setAudioPersistenceRetryAttempts(int attempts)
{
    return deviceController.setAudioPersistenceRetryAttempts(attempts);
}

bool AudioEngine::setAudioPersistenceCustomBackendByIndex(int backendIndex)
{
    return deviceController.setAudioPersistenceCustomBackendByIndex(backendIndex);
}

bool AudioEngine::setAudioPersistenceCustomInputByIndex(int deviceIndex)
{
    return deviceController.setAudioPersistenceCustomInputByIndex(deviceIndex);
}

bool AudioEngine::setAudioPersistenceCustomOutputByIndex(int deviceIndex)
{
    return deviceController.setAudioPersistenceCustomOutputByIndex(deviceIndex);
}

bool AudioEngine::retryPreferredAudioDeviceNow()
{
    return deviceController.retryPreferredAudioDeviceNow();
}

bool AudioEngine::addBlockedAudioBackend(const String& backendName)
{
    return deviceController.addBlockedAudioBackend(backendName);
}

bool AudioEngine::addBlockedAudioInputDevice(const String& deviceName)
{
    return deviceController.addBlockedAudioInputDevice(deviceName);
}

bool AudioEngine::addBlockedAudioOutputDevice(const String& deviceName)
{
    return deviceController.addBlockedAudioOutputDevice(deviceName);
}

bool AudioEngine::removeBlockedAudioBackend(int index)
{
    return deviceController.removeBlockedAudioBackend(index);
}

bool AudioEngine::removeBlockedAudioDevice(int index)
{
    return deviceController.removeBlockedAudioDevice(index);
}

bool AudioEngine::setAudioBackendEnabledByIndex(int index, bool enabled)
{
    return deviceController.setAudioBackendEnabledByIndex(index, enabled);
}

bool AudioEngine::setAudioDeviceChoiceEnabledByIndex(int index, bool enabled)
{
    return deviceController.setAudioDeviceChoiceEnabledByIndex(index, enabled);
}

bool AudioEngine::setAudioSampleRate(double sampleRate)
{
    return deviceController.setAudioSampleRate(sampleRate);
}

bool AudioEngine::setAudioBufferSize(int bufferSize)
{
    return deviceController.setAudioBufferSize(bufferSize);
}

bool AudioEngine::setAudioInputChannelEnabled(int channelIndex, bool enabled)
{
    return deviceController.setAudioInputChannelEnabled(channelIndex, enabled);
}

bool AudioEngine::setAudioOutputChannelEnabled(int channelIndex, bool enabled)
{
    return deviceController.setAudioOutputChannelEnabled(channelIndex, enabled);
}

bool AudioEngine::setAllAudioInputChannelsEnabled(bool enabled)
{
    return deviceController.setAllAudioInputChannelsEnabled(enabled);
}

bool AudioEngine::setAllAudioOutputChannelsEnabled(bool enabled)
{
    return deviceController.setAllAudioOutputChannelsEnabled(enabled);
}

bool AudioEngine::setAudioInputChannelCount(int channelCount)
{
    return deviceController.setAudioInputChannelCount(channelCount);
}

bool AudioEngine::setAudioOutputChannelCount(int channelCount)
{
    return deviceController.setAudioOutputChannelCount(channelCount);
}

void AudioEngine::saveCurrentAudioChannelState()
{
    return deviceController.saveCurrentAudioChannelState();
}

void AudioEngine::applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
                                             const String& backendName,
                                             const String& inputDeviceName,
                                             const String& outputDeviceName)
{
    return deviceController.applySavedAudioChannelState(setup, backendName, inputDeviceName, outputDeviceName);
}

void AudioEngine::saveAudioDeviceState()
{
    return deviceController.saveAudioDeviceState();
}

void AudioEngine::scanPluginPath(const String& path, bool scanVst, bool scanVst3)
{
	const FileSearchPath searchPath(path);
	if (searchPath.getNumPaths() == 0)
		return;


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

		pluginScanner.enqueue(searchPath, formatName, knownPluginList.getTypes());
	}

}

void AudioEngine::scanDefaultPluginLocations(bool scanVst, bool scanVst3)
{
    for (auto* format : formatManager.getFormats()) {
        const auto name=format->getName();
        if((name=="VST"&&!scanVst)||(name=="VST3"&&!scanVst3))continue;
        pluginScanner.enqueue(getWindowsDefaultPluginSearchPath(*format,name=="VST",name=="VST3"),name,knownPluginList.getTypes(),false,true);
    }
}

void AudioEngine::scanPluginRoots(const var& roots)
{
    for(auto* format:formatManager.getFormats()) {
        std::map<bool,FileSearchPath> groups;
        for(const auto& root:*roots.getArray()) {
            const auto kind=root["format"].toString();
            if(kind!="all"&&kind!=format->getName())continue;
            groups[(bool)root["optional"]].add(File(root["path"].toString()));
        }
        for(auto& [optional,paths]:groups) {
            paths.removeRedundantPaths();
            pluginScanner.enqueue(paths,format->getName(),knownPluginList.getTypes(),false,optional);
        }
    }
}

void AudioEngine::collectPluginScanResults()
{
    auto results = pluginScanner.takeResults();
    if (results.empty()) return;
    for (auto plugin : results) {
        // Keep the persisted path/ID (and therefore aliases/session links) when
        // a manifest adds or removes the equivalent bundle representation.
        for(const auto& existing:knownPluginList.getTypes())
            if(lightHostModern::samePluginClass(existing,plugin)) {plugin.fileOrIdentifier=existing.fileOrIdentifier;break;}
        knownPluginList.addType(plugin);
    }
    knownPluginList.sendSynchronousChangeMessage();
    flushPendingSaves();
}

void AudioEngine::cancelPluginScan()
{
    pluginScanner.cancel();
    collectPluginScanResults();
}

bool AudioEngine::isPluginBypassed(int sortedIndex) const
{
    return isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))
        && instances.records[static_cast<size_t>(sortedIndex)].bypassed;
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
    for (auto it = isolatedPlugins.begin(); it != isolatedPlugins.end();) {
        const auto index = instances.indexOf(it->first);
        if (index < 0 || !instances.records[static_cast<size_t>(index)].isolated) { isolatedCaptures.erase(it->first); it = isolatedPlugins.erase(it); }
        else ++it;
    }
    const auto monoKey = deviceController.monoInputsKey();
    hostProcessor.setMonoInputs(monoKey.isNotEmpty() && getAppProperties().getUserSettings()->getBoolValue(monoKey, false));
    const auto outputKey = deviceController.monoOutputKey();
    hostProcessor.setMonoOutput(outputKey.isNotEmpty() && getAppProperties().getUserSettings()->getBoolValue(outputKey, false));
    if (isDiagnosticsEnabled()) ++chainReloadCount;
    auto snapshot = std::make_shared<ChainSnapshot>();
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        snapshot->inputChannels = jmax(1, device->getActiveInputChannels().countNumberOfSetBits());
        snapshot->outputChannels = jmax(1, device->getActiveOutputChannels().countNumberOfSetBits());
    }
    auto previous = hostProcessor.getActiveSnapshot();
    for (auto& record : instances.records)
    {
        if (sessionLoadSuppressed) { record.loading = "suspended"; continue; }
        if (!record.isolated && record.error.isNotEmpty() && record.loading != "missing") { record.loading = "failed"; continue; }
        const auto& description = record.description;
        setLightHostModernCrashContext("Loading instance " + record.id + " " + description.name);
        std::shared_ptr<PluginSlot> slot;
        if (previous)
            for (const auto& candidate : previous->slots)
                if (candidate && candidate->instanceId == record.id && !candidate->processDisabled.load()
                    && lightHostModern::IsolatedPluginSession::isProxy(candidate->processor.get()) == record.isolated
                    && (!record.isolated || (isolatedPlugins.count(record.id) && !isolatedPlugins.at(record.id)->layoutChanged())))
                { slot = candidate; break; }
        if (slot) ++snapshot->reusedSlots;
        else
        {
            if (!formatManager.doesPluginStillExist(description))
            {
                record.loading = "missing";
                record.error = "Plugin file or identifier no longer exists";
                continue;
            }
            record.loading = "loading";
            record.error.clear();
            try
            {
                std::unique_ptr<AudioPluginInstance> processor;
                if (record.isolated) {
                    auto& remote = isolatedPlugins[record.id];
                    if (!remote) {
                        Logger::writeToLog("LightHostModern: starting isolated plugin instance=" + record.id + " name=" + record.displayName());
                        remote = std::make_shared<lightHostModern::IsolatedPluginSession>(description, record.lastValidState,
                            hostProcessor.getCurrentSampleRateForPlugins(), hostProcessor.getCurrentBlockSizeForPlugins(), File{}, false, record.busLayout);
                    }
                    if (!remote->initialized()) {
                        record.error = remote->failure(); record.loading = record.error.isEmpty() ? "loading" : "failed"; continue;
                    }
                    processor = remote->createProxy();
                } else processor = formatManager.createPluginInstance(description,
                    hostProcessor.getCurrentSampleRateForPlugins(), hostProcessor.getCurrentBlockSizeForPlugins(), record.error);
                if (!processor)
                {
                    if (record.error.isEmpty()) record.error = "Could not create plugin instance";
                    record.loading = "failed";
                    continue;
                }
                if (jmax(processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels()) > RealtimeHostProcessor::maxScratchChannels)
                    throw std::runtime_error("Plugin layout exceeds 256 channels");
                if (processor->getTotalNumInputChannels() == 0 && processor->getTotalNumOutputChannels() == 0 && !processor->isMidiEffect())
                    throw std::runtime_error("Plugin exposes no audio channels");
                PluginDescription actual;
                processor->fillInPluginDescription(actual);
                if (!record.isolated) lightHostModern::restorePluginState(record, actual, [&](const void* data, int size) { processor->setStateInformation(data, size); });
                if (record.busLayout.isObject() && !record.isolated) {
                    AudioProcessor::BusesLayout layout;
                    if (!lightHostModern::pluginBuses::decode(record.busLayout, layout) || !processor->setBusesLayout(layout))
                        throw std::runtime_error("Saved plugin channel configuration is no longer supported");
                }
                record.busLayout = lightHostModern::pluginBuses::encode(processor->getBusesLayout());
                slot = std::make_shared<PluginSlot>(description, std::move(processor));
                slot->instanceId = record.id;
                ++snapshot->rebuiltSlots;
            }
            catch (const std::exception& error) { record.error = String::fromUTF8(error.what()); }
            catch (...) { record.error = "Plugin threw while creating instance"; }
            if (!slot) { record.loading = "failed"; continue; }
        }
        record.loading = "loaded";
        record.error.clear();
        if (record.isolated) if (const auto remote = isolatedPlugins.find(record.id); remote != isolatedPlugins.end()) {
            record.error = remote->second->failure(); if (record.error.isNotEmpty()) record.loading = "failed";
        }
        slot->bypassed.store(record.bypassed, std::memory_order_release);
        snapshot->maxPluginChannels = jmax(snapshot->maxPluginChannels, jmax(slot->inputChannels, slot->outputChannels));
        snapshot->slots.push_back(std::move(slot));
    }
    synchronizeGraph(*snapshot);
    {
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        if (previous)
            for (const auto& old : previous->slots)
                if (old && std::find(snapshot->slots.begin(), snapshot->slots.end(), old) == snapshot->slots.end())
                    PluginWindow::closeCurrentlyOpenWindowsFor(*old->processor);
        hostProcessor.publishSnapshot(std::move(snapshot));
    }
    ++chainVersion;
    clearLightHostModernCrashContext();
    markSettingsDirty();
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

bool AudioEngine::addKnownPluginByIndex(int sortedIndex, PluginInstanceId* createdId)
{
    if (isChainMode() && instances.graph.nodes.size() >= lightHostModern::RoutingGraph::maxNodes) return false;
    if (createdId) createdId->clear();
    const auto known = getKnownPluginsSorted();
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return false;
    auto record = lightHostModern::newKnownPluginInstance(*getAppProperties().getUserSettings(), known[static_cast<size_t>(sortedIndex)]);
    const auto id = record.id;
    if (createdId) *createdId = id;
    instances.records.push_back(std::move(record));
    routingUndo.clear(); routingRedo.clear();
    loadActivePlugins();
    saveActivePluginChain(false);
    return findActiveSlotFor(id) != nullptr;
}

void AudioEngine::duplicatePlugin(int sortedIndex, PluginInstanceId* createdId)
{
    if (isChainMode() && instances.graph.nodes.size() >= lightHostModern::RoutingGraph::maxNodes) return;
    if (createdId) createdId->clear();
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    savePluginStates();
    auto record = instances.records[static_cast<size_t>(sortedIndex)];
    record.id = Uuid().toString();
    if (createdId) *createdId = record.id;
    instances.records.insert(instances.records.begin() + sortedIndex + 1, std::move(record));
    routingUndo.clear(); routingRedo.clear();
    loadActivePlugins();
    saveActivePluginChain(false);
}

int AudioEngine::removeKnownPluginByIndex(int sortedIndex)
{
    const auto known = getKnownPluginsSorted();
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return 0;
    const auto& description = known[static_cast<size_t>(sortedIndex)];
    const auto identity = lightHostModern::knownPluginId(description);
    const auto before = instances.records.size();
    instances.records.erase(std::remove_if(instances.records.begin(), instances.records.end(), [&](const auto& record) {
        return record.identityResolved && record.originalIdentity == identity;
    }), instances.records.end());
    knownPluginList.removeType(description);
    routingUndo.clear(); routingRedo.clear();
    const int removed = static_cast<int>(before - instances.records.size());
    if (removed > 0) loadActivePlugins();
    saveActivePluginChain(false);
    return removed;
}

int AudioEngine::clearKnownPlugins()
{
    if (!instances.writable || sessionLoadSuppressed) return 0;
    cancelPluginScan();
    const int removed = static_cast<int>(instances.records.size());
    instances.records.clear();
    routingUndo.clear(); routingRedo.clear();
    knownPluginList.clear();
    loadActivePlugins();
    saveActivePluginChain(false);
    return removed;
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
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    instances.records.erase(instances.records.begin() + sortedIndex);
    routingUndo.clear(); routingRedo.clear();
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::movePluginUp(int sortedIndex)
{
    if (sortedIndex > 0) movePluginToIndex(sortedIndex, sortedIndex - 1);
}

void AudioEngine::movePluginDown(int sortedIndex)
{
    movePluginToIndex(sortedIndex, sortedIndex + 1);
}

void AudioEngine::movePluginToIndex(int fromSortedIndex, int toSortedIndex)
{
    if (isChainMode()) return; // Signal order is defined by connections.
    const int count = static_cast<int>(instances.records.size());
    if (!isSessionWritable() || !isPositiveAndBelow(fromSortedIndex, count) || count == 0) return;
    toSortedIndex = jlimit(0, count - 1, toSortedIndex);
    if (fromSortedIndex == toSortedIndex) return;
    auto record = std::move(instances.records[static_cast<size_t>(fromSortedIndex)]);
    instances.records.erase(instances.records.begin() + fromSortedIndex);
    instances.records.insert(instances.records.begin() + toSortedIndex, std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::setPluginBypassed(int sortedIndex, bool shouldBypass)
{
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    auto& record = instances.records[static_cast<size_t>(sortedIndex)];
    if (record.bypassed == shouldBypass) return;
    record.bypassed = shouldBypass;
    if (auto* slot = findActiveSlotFor(record.id)) slot->bypassed.store(shouldBypass, std::memory_order_release);
    if (isDiagnosticsEnabled()) ++bypassToggleCount;
    ++chainVersion;
    saveActivePluginChain(false);
}

void AudioEngine::deletePluginStates()
{
    if (!instances.writable || sessionLoadSuppressed) return;
    routingUndo.clear(); routingRedo.clear();
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    if (auto snapshot = hostProcessor.getActiveSnapshot())
        for (const auto& slot : snapshot->slots) if (slot) PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
    hostProcessor.publishSnapshot(nullptr);
    // Reset must also discard the remote processor, not recreate a proxy for
    // the worker that still holds the state the user asked to clear.
    isolatedPlugins.clear(); isolatedCaptures.clear();
    for (auto& record : instances.records)
    {
        record.lastValidState.clear();
        record.recoveryState.clear();
        record.stateCaptureAllowed = true;
        record.error.clear();
        record.loading = "unloaded";
    }
    loadActivePlugins();
    saveActivePluginChain(false);
}

void AudioEngine::savePluginStates(bool captureAll)
{
    if (!instances.writable || sessionLoadSuppressed) return;
    if (!captureAll && isolatedCaptureBarrier) return;
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    const auto snapshot = hostProcessor.getActiveSnapshot();
    if (!snapshot) return;
    const auto generation = operatingGeneration;
    bool captured = false;
    bool newerStatePending = false;
    stateCaptureFailures.clear();
    for (const auto& slot : snapshot->slots)
    {
        if (!slot || !slot->processor) continue;
        const auto id = slot->instanceId;
        const int index = instances.indexOf(id);
        if (index < 0 || (!captureAll && pendingStateCaptures.count(id) == 0)) continue;
        const auto& record = instances.records[static_cast<size_t>(index)];
        if (record.isolated) {
            const auto found = isolatedPlugins.find(id);
            if (found == isolatedPlugins.end()) { stateCaptureFailures.add(id); continue; }
            auto& ticket = isolatedCaptures[id];
            if (ticket.ticket && (ticket.generation != operatingGeneration || ticket.session.lock() != found->second
                || ticket.slot.lock() != slot)) ticket = {};
            if (!ticket.ticket) {
                slot->stateDirty.exchange(false);
                const auto revision = slot->stateRevision.load();
                ticket = {found->second->requestCapture(isolatedCaptureBudget(id)), revision,
                    operatingGeneration, found->second, slot};
            }
            if (!found->second->captureFinished(ticket.ticket)) { pendingStateCaptures.insert(id); continue; }
            const auto result = found->second->captureResult();
            if (found->second->failure().isNotEmpty() || result.error.isNotEmpty() || result.ticket != ticket.ticket) {
                stateCaptureFailures.add(id); pendingStateCaptures.insert(id); isolatedCaptures.erase(id); continue;
            }
            const auto state = result.state;
            const bool changed = slot->stateChangedSince(ticket.revision) || found->second->stateRevision() != result.revision;
            // Other captures may have used some headroom while this worker was
            // busy. Revalidate against the current session before committing.
            size_t available = lightHostModern::maximumSessionStateBytes - 4u * 1024 * 1024;
            for (const auto& other : instances.records) {
                const size_t bytes = (other.id == id ? 0 : other.lastValidState.getNumBytesAsUTF8()) + other.recoveryState.getNumBytesAsUTF8();
                available = bytes >= available ? 0 : available - bytes;
            }
            isolatedCaptures.erase(id);
            if (state.getNumBytesAsUTF8() > available) { stateCaptureFailures.add(id); pendingStateCaptures.insert(id); continue; }
            instances.records[static_cast<size_t>(index)].lastValidState = state;
            if (changed) { pendingStateCaptures.insert(id); newerStatePending = true; }
            else pendingStateCaptures.erase(id);
            captured = true;
            continue;
        }
        if (!record.stateCaptureAllowed || slot->processDisabled.load()) {
            if(pendingStateCaptures.count(id))stateCaptureFailures.add(id);
            continue;
        }
        // Stage the capture independently: a nested message cannot invalidate a
        // reference to the session record. Encoding happens after audio resumes.
        auto staged = record;
        slot->stateDirty.exchange(false, std::memory_order_relaxed);
        double captureMs = 0;
        size_t remaining=lightHostModern::maximumSessionStateBytes-4u*1024*1024;
        for(const auto& other:instances.records) {
            const size_t bytes=(other.id==id?0:other.lastValidState.getNumBytesAsUTF8())+other.recoveryState.getNumBytesAsUTF8();
            remaining=bytes>=remaining?0:remaining-bytes;
        }
        const bool ok = lightHostModern::capturePluginState(staged, [&](MemoryBlock& binary) {
            RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
            const auto start = Time::getMillisecondCounterHiRes();
            slot->processor->getStateInformation(binary);
            captureMs = Time::getMillisecondCounterHiRes() - start;
        },remaining);
        if (operatingGeneration != generation) return;
        const auto current = instances.indexOf(id);
        if (current < 0) continue;
        if (captureMs >= 20.0)
            Logger::writeToLog("LightHostModern: slow plugin state capture instance=" + id + " suspendedMs=" + String(captureMs, 2));
        if (ok) {
            instances.records[static_cast<size_t>(current)].lastValidState = std::move(staged.lastValidState);
            pendingStateCaptures.erase(id);
            captured = true;
        } else {
            pendingStateCaptures.insert(id);
            stateCaptureFailures.add(id);
            Logger::writeToLog("LightHostModern: state capture failed; previous state retained for " + id);
        }
        // A notification raised during capture must survive this pass.
        if (slot->stateDirty.load(std::memory_order_relaxed)) { pendingStateCaptures.insert(id); newerStatePending = true; }
    }
    // A completed capture may have covered A while the timer already consumed
    // the notification for B. Keep that revision scheduled after removing A's ticket.
    // Failed/oversized captures remain pending for an edit or explicit retry;
    // repeating those every second would unnecessarily suspend their audio.
    stateCaptureDue = isolatedCaptures.empty() && !newerStatePending ? 0
        : Time::getMillisecondCounterHiRes() + (isolatedCaptures.empty() ? 1000 : 250);
    ++chainVersion;
    if (captured) { if (isDiagnosticsEnabled()) ++pluginStateSaveCount; saveActivePluginList(); }
}

void AudioEngine::saveActivePluginList()
{
    if (!instances.writable || sessionLoadSuppressed) return;
    if (sessionStore) sessionStore->submit(instances, instances.records.empty(), sessionMigrationId);
}

bool AudioEngine::renamePlugin(int sortedIndex, const String& name)
{
    String normalized;
    if (!isSessionWritable() || sortedIndex < 0 || sortedIndex >= static_cast<int>(instances.records.size())
        || !lightHostModern::normalizeInstanceName(name, normalized)) return false;
    auto& record = instances.records[static_cast<size_t>(sortedIndex)];
    if (normalized == record.description.name) normalized.clear();
    if (record.customName == normalized) return true;
    record.customName = normalized;
    if (auto* node = instances.graph.find(record.id)) node->name = record.displayName();
    ++chainVersion;
    saveActivePluginChain(false);
    return true;
}

bool AudioEngine::renameKnownPlugin(int sortedIndex, const String& name)
{
    const auto known = getKnownPluginsSorted();
    if (!isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return false;
    const auto& plugin = known[static_cast<size_t>(sortedIndex)];
    const auto previous = getKnownPluginCustomName(plugin);
    if (!lightHostModern::setKnownPluginCustomName(*getAppProperties().getUserSettings(), plugin, name)) return false;
    if (previous != getKnownPluginCustomName(plugin)) { ++pluginDatabaseVersion; markSettingsDirty(); }
    return true;
}

void AudioEngine::setMonoInputs(bool value)
{
    const auto key = deviceController.monoInputsKey();
    if (key.isEmpty()) return;
    hostProcessor.setMonoInputs(value);
    getAppProperties().getUserSettings()->setValue(key, value);
    markSettingsDirty(); ++chainVersion;
}

void AudioEngine::setMonoOutput(bool value)
{
    const auto key = deviceController.monoOutputKey();
    if (key.isEmpty()) return;
    hostProcessor.setMonoOutput(value);
    getAppProperties().getUserSettings()->setValue(key, value);
    markSettingsDirty(); ++chainVersion;
}

void AudioEngine::setDiagnosticsEnabled(bool enabled)
{
    hostProcessor.setDiagnosticsEnabled(enabled);
    {
        const ScopedLock callbackLock(deviceManager.getAudioCallbackLock());
        deviceManager.setDiagnosticsEnabled(enabled);
        if (!enabled) player.callbackMeasurement().stop();
    }
    hostCpuSampler.reset(); workerCpuSampler.reset();
    if (enabled || lightHostModern::verbose::logger().active()) startTimer(diagnosticsTimerId, 30000);
    else stopTimer(diagnosticsTimerId);
    getAppProperties().getUserSettings()->setValue("diagnosticsEnabled", enabled);
    markSettingsDirty();
}

void AudioEngine::saveActivePluginChain(bool saveProcessorStates)
{
	if (saveProcessorStates)
		savePluginStates();

	saveActivePluginList();
}

bool AudioEngine::flushSession()
{
    savePluginStates();
    saveActivePluginList();
    if (!instances.writable) return false;
    return !sessionStore || sessionStore->flush();
}

bool AudioEngine::prepareIsolatedStateCapture(bool start)
{
    if (start) isolatedCaptureBarrier = true;
    bool ready = true;
    for (const auto& item : isolatedPlugins) {
        if (item.second->failure().isNotEmpty()) continue; // Preserve the last valid state of a failed worker.
        auto& ticket = isolatedCaptures[item.first];
        const auto snapshot = hostProcessor.getActiveSnapshot();
        std::shared_ptr<PluginSlot> slot;
        if (snapshot) for (const auto& candidate : snapshot->slots)
            if (candidate && candidate->instanceId == item.first) { slot = candidate; break; }
        if (!slot) continue;
        bool needsCapture = start || !ticket.ticket || ticket.generation != operatingGeneration
            || ticket.session.lock() != item.second || ticket.slot.lock() != slot;
        if (!needsCapture && item.second->captureFinished(ticket.ticket)) {
            const auto result = item.second->captureResult();
            // Mutations wait for the newer revision too. The host barrier's
            // absolute deadline prevents continuously changing plugins starving the queue.
            needsCapture = result.error.isEmpty() && (result.ticket != ticket.ticket
                || slot->stateChangedSince(ticket.revision) || item.second->stateRevision() != result.revision);
        }
        if (needsCapture) {
            slot->stateDirty.exchange(false);
            const auto revision = slot->stateRevision.load();
            ticket = {item.second->requestCapture(isolatedCaptureBudget(item.first)), revision,
                operatingGeneration, item.second, slot};
            // Autosave must still harvest this ticket once the barrier releases.
            pendingStateCaptures.insert(item.first);
            stateCaptureDue = Time::getMillisecondCounterHiRes() + 250;
        }
        if (!item.second->captureFinished(ticket.ticket)) ready = false;
    }
    return ready;
}

size_t AudioEngine::isolatedCaptureBudget(const PluginInstanceId& id) const
{
    size_t remaining = lightHostModern::maximumSessionStateBytes - 4u * 1024 * 1024, current = 0;
    for (const auto& record : instances.records) {
        const size_t state = record.lastValidState.getNumBytesAsUTF8();
        const size_t used = state + record.recoveryState.getNumBytesAsUTF8();
        remaining = used >= remaining ? 0 : remaining - used;
        if (record.id == id) current = state;
    }
    // Concurrent captures share headroom, rather than each spending the whole
    // session budget independently. Previous valid states remain reserved.
    return current + remaining / juce::jmax(size_t(1), isolatedPlugins.size());
}

var AudioEngine::isolatedPluginDiagnostics(const PluginInstanceId& id) const
{
    const auto found = isolatedPlugins.find(id);
    return found == isolatedPlugins.end() ? var(new DynamicObject()) : found->second->diagnostics();
}

var AudioEngine::isolatedPluginDiagnostics() const
{
    Array<var> result;
    for (const auto& record : instances.records) if (record.isolated) {
        auto value = isolatedPluginDiagnostics(record.id);
        value.getDynamicObject()->setProperty("name", record.displayName()); value.getDynamicObject()->setProperty("instanceId", record.id); result.add(value);
    }
    return result;
}

void AudioEngine::pollIsolatedPlugins()
{
    bool reload = false, changed = false;
    for (auto& item : isolatedPlugins) {
        const int index = instances.indexOf(item.first); if (index < 0) continue;
        auto& record = instances.records[static_cast<size_t>(index)];
        const auto error = item.second->failure();
        if (error.isNotEmpty()) {
            if (record.error != error) {
                record.error = error; record.loading = "failed"; changed = true;
                Logger::writeToLog("LightHostModern: isolated plugin failure instance=" + record.id + " detail=" + error);
            }
            continue;
        }
        if (item.second->initialized() && (!findActiveSlotFor(item.first) || item.second->layoutChanged())) {
            if (auto* slot = findActiveSlotFor(item.first)) slot->processDisabled.store(true);
            const auto inventory = item.second->busInventory(); record.busLayout = inventory["layout"];
            profileError = inventory["layoutError"].toString(); reload = true;
        }
        if (item.second->needsPoll()) { RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor, false); changed |= item.second->poll(); }
    }
    if (reload) { ++operatingGeneration; routingUndo.clear(); routingRedo.clear(); loadActivePlugins(); saveActivePluginList(); }
    if (changed) { ++chainVersion; saveActivePluginList(); }
}

void AudioEngine::flushPendingSaves()
{
	stopTimer(persistenceTimerId);

	if (settingsDirty || pluginStateStore.isDirty())
	{
		settingsDirty = false;
		if (isDiagnosticsEnabled()) settingsFlushCount++;
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
			&& !pluginFile.exists())
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
    if (const auto remote = isolatedPlugins.find(instances.records[static_cast<size_t>(sortedIndex)].id); remote != isolatedPlugins.end()) {
        remote->second->showEditor(); return;
    }

	if (auto* const slot = findActiveSlotFor(instances.records[(size_t) sortedIndex].id))
		if (slot->processor != nullptr)
			if (PluginWindow* const window = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, PluginWindow::Normal))
			{
				window->setMinimised(false);
				window->setVisible(true);
				window->toFront(true);
			}
}

DiagnosticsSnapshot AudioEngine::getDiagnosticsSnapshot() const
{
	DiagnosticsSnapshot snapshot = deviceController.createDiagnosticsSnapshot(const_cast<GuardedAudioDeviceManager&>(deviceManager), isDiagnosticsEnabled());
	snapshot.activePlugins = static_cast<int>(instances.records.size());
	if (!isDiagnosticsEnabled()) return snapshot;
	const RealtimeHostStats realtimeStats = hostProcessor.getStats();
#if JUCE_WINDOWS
    const auto now = GetTickCount64();
    if (now <= lightHostModern::diagnosticsVisibleUntil.load() && now - lastMemorySample >= 1000)
    { cachedMemory = lightHostModern::processMemory(); lastMemorySample = now; }
    const auto memory = now <= lightHostModern::diagnosticsVisibleUntil.load() ? cachedMemory : lightHostModern::ProcessMemory{};
    if (memory.resident) snapshot.hostResidentMiB = *memory.resident / 1048576.0;
    if (memory.committed) snapshot.hostCommittedMiB = *memory.committed / 1048576.0;
    if (lightHostModern::workerMemoryUnavailable.load() == 0) {
        snapshot.workerResidentMiB = lightHostModern::workerResidentBytes.load() / 1048576.0;
        snapshot.workerCommittedMiB = lightHostModern::workerCommittedBytes.load() / 1048576.0;
    }
    snapshot.hostCpuPercent = hostCpuSampler.sample(lightHostModern::processCpuTicks(), GetTickCount64(), lightHostModern::processorCount());
    snapshot.workerCpuPercent = workerCpuSampler.sample(lightHostModern::workerCpuTicks.load(), GetTickCount64(), lightHostModern::processorCount());
    for (const auto& item : isolatedPlugins) {
        const auto metrics = item.second->diagnostics();
        if (snapshot.workerResidentMiB) *snapshot.workerResidentMiB += static_cast<double>(metrics["residentBytes"]) / 1048576.0;
        if (snapshot.workerCommittedMiB) *snapshot.workerCommittedMiB += static_cast<double>(metrics["committedBytes"]) / 1048576.0;
        if (snapshot.workerCpuPercent && !metrics["cpuPercent"].isVoid()) *snapshot.workerCpuPercent += static_cast<double>(metrics["cpuPercent"]);
    }
#endif
	snapshot.activePlugins = static_cast<int>(instances.records.size());
	snapshot.loadedPlugins = realtimeStats.loadedSlots;
	snapshot.chainLatencySamples = realtimeStats.chainLatencySamples;
	snapshot.chainReloads = chainReloadCount;
	snapshot.bypassToggles = bypassToggleCount;
	snapshot.pluginStateSaves = pluginStateSaveCount;
	snapshot.settingsFlushes = settingsFlushCount;
	snapshot.processFailures = realtimeStats.processFailures;
	snapshot.midiOverflow = realtimeStats.midiOverflow;
	snapshot.processedBlocks = realtimeStats.processedBlocks;
	snapshot.processedSamples = realtimeStats.processedSamples;
	snapshot.inputMidiEvents = realtimeStats.inputMidiEvents;
	snapshot.outputMidiEvents = realtimeStats.outputMidiEvents;
	snapshot.inputMeters = hostProcessor.getInputMeters();
	snapshot.outputMeters = hostProcessor.getOutputMeters();
	snapshot.reusedSlots = realtimeStats.reusedSlots;
	snapshot.rebuiltSlots = realtimeStats.rebuiltSlots;
	snapshot.inputLevel = realtimeStats.inputLevel;
	snapshot.outputLevel = realtimeStats.outputLevel;
	return snapshot;
}

bool AudioEngine::configureCallbackMeasurement(unsigned warmupSeconds, unsigned durationSeconds)
{
    if (!isDiagnosticsEnabled() || !lightHostModern::RuntimeProfile::current().test || deviceManager.getCurrentAudioDevice()) return false;
    try
    {
        player.callbackMeasurement().configure(static_cast<uint64>(Time::getHighResolutionTicksPerSecond()), warmupSeconds, durationSeconds);
        return true;
    }
    catch (const std::invalid_argument&) { return false; }
}

void AudioEngine::timerCallback(int timerId)
{
	pollIsolatedPlugins();
    if (timerId == audioWatchdogTimerId) refreshPluginLayouts();
	collectPluginScanResults();
	hostProcessor.collectRetiredSnapshots();
	hostProcessor.refreshLatencies();
	recordProcessFailures();

    if (timerId == audioWatchdogTimerId)
    {
        const auto now = Time::getMillisecondCounterHiRes();
        for (auto it=pendingStateCaptures.begin();it!=pendingStateCaptures.end();)
            if (instances.indexOf(*it)<0) it=pendingStateCaptures.erase(it); else ++it;
        if (const auto snapshot = hostProcessor.getActiveSnapshot(); snapshot && isSessionWritable())
            for (const auto& slot : snapshot->slots)
                if (slot && slot->stateDirty.exchange(false, std::memory_order_relaxed)) {
                    pendingStateCaptures.insert(slot->instanceId); stateCaptureDue = now + 1000.0;
                }
        if (stateCaptureDue > 0 && now >= stateCaptureDue) savePluginStates(false);
        deviceController.tick();
        startTimer(audioWatchdogTimerId, 250);
        return;
    }

	if (timerId == persistenceTimerId)
	{
		flushPendingSaves();
		return;
	}

	if (timerId == diagnosticsTimerId)
	{
		if (isDiagnosticsEnabled() || lightHostModern::verbose::logger().active()) logDiagnosticsSnapshot();
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
	else if (changed == &deviceManager) deviceController.devicesChanged();
}

void AudioEngine::markSettingsDirty()
{
	settingsDirty = true;
	pluginStateStore.markDirty();
	startTimer(persistenceTimerId, 1000);
}

PluginSlot* AudioEngine::findActiveSlotFor(const PluginInstanceId& id) const
{
    const auto snapshot = hostProcessor.getActiveSnapshot();
    if (snapshot) for (const auto& slot : snapshot->slots) if (slot && slot->instanceId == id) return slot.get();
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
		Logger::writeToLog("LightHostModern: failed plugin disabled in audio chain '" + slot->description.name + "': " + errorMessage);
		const auto index = instances.indexOf(slot->instanceId);
        if (index >= 0)
        {
            auto& record = instances.records[static_cast<size_t>(index)];
            record.error = errorMessage;
            record.loading = "failed";
            ++chainVersion;
            saveActivePluginList();
        }
		markSettingsDirty();
	}
}

void AudioEngine::logDiagnosticsSnapshot()
{
	const DiagnosticsSnapshot snapshot = getDiagnosticsSnapshot();
	Logger::writeToLog("LightHostModern diagnostics: backend=" + snapshot.backend
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
