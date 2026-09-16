#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PluginStateCapture.h"
#include "PluginWindow.h"
#include "RuntimeProfile.h"
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
        return deviceController.isAudioDeviceChoiceAllowed(backend, input, output);
    };
    addEnabledPluginFormats(formatManager);
	std::unique_ptr<XmlElement> savedPluginList(getXmlValuePreserving("pluginList"));
	if (savedPluginList != nullptr)
		knownPluginList.recreateFromXml(*savedPluginList);

	knownPluginList.addChangeListener(this);

    sessionLoadSuppressed = safeMode || !restoreActivePluginsOnStartup;
    auto* settings = getAppProperties().getUserSettings();
    setDiagnosticsEnabled(settings->getBoolValue("diagnosticsEnabled", true));
    hostProcessor.setMonoInputs(settings->getBoolValue("monoInputs", false));
    auto storage = std::make_shared<lightHost::DiskSessionStorage>(settings->getFile());
    const auto recovered = lightHost::SessionStore::recover(*storage);
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
        const auto backupError = lightHost::SessionStore::backupLegacy(*storage, sessionMigrationId);
        if (backupError.isNotEmpty())
        {
            instances.writable = false;
            instances.recoveryError = "Could not back up legacy preferences: " + backupError;
        }
        else if (settings->containsKey("pluginInstancesV1"))
        {
            const auto saved = settings->getXmlValue("pluginInstancesV1");
            if (!saved || !instances.deserialize(*saved))
            { instances.writable = false; instances.recoveryError = "Invalid instance data; original settings preserved"; }
        }
        else if (settings->containsKey("pluginListActive"))
        {
            const auto legacy = settings->getXmlValue("pluginListActive");
            if (legacy) instances.migrate(*legacy, *settings, knownPluginList.getTypes(), sessionMigrationId);
            else { instances.writable = false; instances.recoveryError = "Invalid legacy session; original settings preserved"; }
        }
    }
    sessionStore = std::make_unique<lightHost::SessionStore>(std::move(storage), recovered);
    deviceController.start(safeMode, lightHost::RuntimeProfile::current().noAudio);
    player.setProcessor(&hostProcessor);
    deviceManager.addAudioCallback(&player);
    deviceManager.addChangeListener(this);
    startTimer(audioWatchdogTimerId, 250);
    if (isDiagnosticsEnabled()) startTimer(diagnosticsTimerId, 30000);
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
    if (!flushSession()) Logger::writeToLog("Light Host Modern: final session remains pending: " + getSessionSaveStatus().error);
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
	auto xml = settings->getXmlValue(key);
	if (xml == nullptr && settings->getValue(key).isNotEmpty())
	{
		Logger::writeToLog("Light Host Modern: preserved invalid XML setting '" + key + "'");
	}

	return xml;
}

int AudioEngine::findKnownPluginIndexById(const String& id) const
{
    const auto known = getKnownPluginsSorted();
    for (size_t i = 0; i < known.size(); ++i)
        if (lightHost::knownPluginId(known[i]) == id) return static_cast<int>(i);
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
		pluginScanner.enqueue(searchPath, formatName, knownPluginList.getTypes());
	}

}

void AudioEngine::collectPluginScanResults()
{
    auto results = pluginScanner.takeResults();
    if (results.empty()) return;
    for (const auto& plugin : results) knownPluginList.addType(plugin);
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
        if (record.error.isNotEmpty() && record.loading != "missing") { record.loading = "failed"; continue; }
        const auto& description = record.description;
        setLightHostCrashContext("Loading instance " + record.id + " " + description.name);
        std::shared_ptr<PluginSlot> slot;
        if (previous)
            for (const auto& candidate : previous->slots)
                if (candidate && candidate->instanceId == record.id && !candidate->processDisabled.load())
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
                auto processor = formatManager.createPluginInstance(description,
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
                lightHost::restorePluginState(record, actual, [&](const void* data, int size) { processor->setStateInformation(data, size); });
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
        slot->bypassed.store(record.bypassed, std::memory_order_release);
        snapshot->maxPluginChannels = jmax(snapshot->maxPluginChannels, jmax(slot->inputChannels, slot->outputChannels));
        snapshot->slots.push_back(std::move(slot));
    }
    {
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        if (previous)
            for (const auto& old : previous->slots)
                if (old && std::find(snapshot->slots.begin(), snapshot->slots.end(), old) == snapshot->slots.end())
                    PluginWindow::closeCurrentlyOpenWindowsFor(*old->processor);
        hostProcessor.publishSnapshot(std::move(snapshot));
    }
    ++chainVersion;
    clearLightHostCrashContext();
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

bool AudioEngine::addKnownPluginByIndex(int sortedIndex)
{
    const auto known = getKnownPluginsSorted();
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return false;
    auto record = lightHost::newKnownPluginInstance(*getAppProperties().getUserSettings(), known[static_cast<size_t>(sortedIndex)]);
    const auto id = record.id;
    instances.records.push_back(std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
    return findActiveSlotFor(id) != nullptr;
}

void AudioEngine::duplicatePlugin(int sortedIndex)
{
    if (!instances.writable || sessionLoadSuppressed || !isPositiveAndBelow(sortedIndex, static_cast<int>(instances.records.size()))) return;
    savePluginStates();
    auto record = instances.records[static_cast<size_t>(sortedIndex)];
    record.id = Uuid().toString();
    instances.records.insert(instances.records.begin() + sortedIndex + 1, std::move(record));
    loadActivePlugins();
    saveActivePluginChain(false);
}

int AudioEngine::removeKnownPluginByIndex(int sortedIndex)
{
    const auto known = getKnownPluginsSorted();
    if (!isSessionWritable() || !isPositiveAndBelow(sortedIndex, static_cast<int>(known.size()))) return 0;
    const auto& description = known[static_cast<size_t>(sortedIndex)];
    const auto identity = lightHost::knownPluginId(description);
    const auto before = instances.records.size();
    instances.records.erase(std::remove_if(instances.records.begin(), instances.records.end(), [&](const auto& record) {
        return record.identityResolved && record.originalIdentity == identity;
    }), instances.records.end());
    knownPluginList.removeType(description);
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
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    if (auto snapshot = hostProcessor.getActiveSnapshot())
        for (const auto& slot : snapshot->slots) if (slot) PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
    hostProcessor.publishSnapshot(nullptr);
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

void AudioEngine::savePluginStates()
{
    if (!instances.writable || sessionLoadSuppressed) return;
    jassert(MessageManager::getInstance()->isThisTheMessageThread());
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    const auto snapshot = hostProcessor.getActiveSnapshot();
    if (!snapshot) return;
    bool captured = false;
    stateCaptureFailures.clear();
    for (const auto& slot : snapshot->slots)
    {
        if (!slot || !slot->processor) continue;
        const int index = instances.indexOf(slot->instanceId);
        if (index < 0) continue;
        auto& record = instances.records[static_cast<size_t>(index)];
        if (!record.stateCaptureAllowed || slot->processDisabled.load()) continue;
        slot->stateDirty.store(false, std::memory_order_relaxed);
        if (lightHost::capturePluginState(record, [&](MemoryBlock& binary) { slot->processor->getStateInformation(binary); }))
        {
            captured = true;
        }
        else
        {
            stateCaptureFailures.add(record.id);
            Logger::writeToLog("Light Host Modern: state capture failed; previous state retained for " + record.id);
        }
    }
    stateCaptureDue = 0;
    ++chainVersion; // Includes capture diagnostics, even when the last state is retained.
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
        || !lightHost::normalizeInstanceName(name, normalized)) return false;
    auto& record = instances.records[static_cast<size_t>(sortedIndex)];
    if (normalized == record.description.name) normalized.clear();
    if (record.customName == normalized) return true;
    record.customName = normalized;
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
    if (!lightHost::setKnownPluginCustomName(*getAppProperties().getUserSettings(), plugin, name)) return false;
    if (previous != getKnownPluginCustomName(plugin)) { ++pluginDatabaseVersion; markSettingsDirty(); }
    return true;
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
    if (enabled) startTimer(diagnosticsTimerId, 30000);
    else stopTimer(diagnosticsTimerId);
    getAppProperties().getUserSettings()->setValue("diagnosticsEnabled", enabled);
    markSettingsDirty();
}

void AudioEngine::setMonoInputs(bool enabled)
{
    if (hostProcessor.setMonoInputs(enabled)) ++chainVersion;
    getAppProperties().getUserSettings()->setValue("monoInputs", enabled);
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

	if (auto* const slot = findActiveSlotFor(instances.records[(size_t) sortedIndex].id))
		if (slot->processor != nullptr)
			if (PluginWindow* const window = PluginWindow::getWindowFor(*slot->processor, slot->windowProperties, PluginWindow::Normal))
				window->toFront(true);
}

DiagnosticsSnapshot AudioEngine::getDiagnosticsSnapshot() const
{
	DiagnosticsSnapshot snapshot = deviceController.createDiagnosticsSnapshot(const_cast<GuardedAudioDeviceManager&>(deviceManager), isDiagnosticsEnabled());
	snapshot.activePlugins = static_cast<int>(instances.records.size());
	if (!isDiagnosticsEnabled()) return snapshot;
	const RealtimeHostStats realtimeStats = hostProcessor.getStats();
#if JUCE_WINDOWS
    snapshot.hostCpuPercent = hostCpuSampler.sample(lightHost::processCpuTicks(), GetTickCount64(), lightHost::processorCount());
    snapshot.workerCpuPercent = workerCpuSampler.sample(lightHost::workerCpuTicks.load(), GetTickCount64(), lightHost::processorCount());
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
    if (!isDiagnosticsEnabled() || !lightHost::RuntimeProfile::current().test || deviceManager.getCurrentAudioDevice()) return false;
    try
    {
        player.callbackMeasurement().configure(static_cast<uint64>(Time::getHighResolutionTicksPerSecond()), warmupSeconds, durationSeconds);
        return true;
    }
    catch (const std::invalid_argument&) { return false; }
}

void AudioEngine::timerCallback(int timerId)
{
	collectPluginScanResults();
	hostProcessor.collectRetiredSnapshots();
	hostProcessor.refreshLatencies();
	recordProcessFailures();

    if (timerId == audioWatchdogTimerId)
    {
        const auto now = Time::getMillisecondCounterHiRes();
        if (const auto snapshot = hostProcessor.getActiveSnapshot(); snapshot && isSessionWritable())
            for (const auto& slot : snapshot->slots)
                if (slot && slot->stateDirty.exchange(false, std::memory_order_relaxed)) stateCaptureDue = now + 1000.0;
        if (stateCaptureDue > 0 && now >= stateCaptureDue) savePluginStates();
        if (const auto status = getSessionSaveStatus(); status.changeSerial != lastSessionStatusSerial)
        { lastSessionStatusSerial = status.changeSerial; ++chainVersion; }
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
		if (isDiagnosticsEnabled()) logDiagnosticsSnapshot();
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
		Logger::writeToLog("Light Host Modern: failed plugin disabled in audio chain '" + slot->description.name + "': " + errorMessage);
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
