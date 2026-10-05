#ifndef AudioEngine_h
#define AudioEngine_h

#include "RealtimeHostProcessor.h"
#include "GuardedAudioDeviceManager.h"
#include "DeviceController.h"
#include "PluginScanController.h"
#include "PluginInstances.h"
#include "KnownPluginNames.h"
#include "HostAudioPlayer.h"
#include "ProcessMetrics.h"
#include "SessionStore.h"
#include "OperatingProfiles.h"
#include "IsolatedPlugin.h"

ApplicationProperties& getAppProperties();

class PluginStateStore
{
public:
	static String getKey(String type, const PluginDescription& plugin);
	static String getLegacyKey(String type, const PluginDescription& plugin);

	String getValue(String type, const PluginDescription& plugin, const String& defaultValue = String()) const;
	void setValue(String type, const PluginDescription& plugin, const var& value);
	void removeValue(String type, const PluginDescription& plugin);
	void markDirty();
	void flushIfDirty();
	bool isDirty() const noexcept { return dirty; }

private:
	bool dirty = false;
};

class AudioEngine : private ChangeListener, private MultiTimer
{
public:
	explicit AudioEngine(bool startInSafeMode, bool restoreActivePluginsOnStartup);
	~AudioEngine() override;

	AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
	AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
	KnownPluginList& getKnownPluginList() noexcept { return knownPluginList; }

	std::vector<PluginDescription> getActivePluginsSorted() const;
	const std::vector<lightHostModern::PluginInstanceRecord>& getPluginInstances() const { return instances.records; }
    juce::var getPluginBuses(const juce::String& id);
    void refreshPluginLayouts();
    std::vector<size_t> getPluginProcessingOrder() const { return instances.processingOrder(); }
	String getSessionRecoveryError() const { return instances.recoveryError; }
	lightHostModern::SessionSaveStatus getSessionSaveStatus() const { return sessionStore ? sessionStore->status() : lightHostModern::SessionSaveStatus{}; }
	const StringArray& getStateCaptureFailures() const { return stateCaptureFailures; }
	bool isSessionWritable() const { return instances.writable && !sessionLoadSuppressed; }
	bool flushSession();
    bool prepareIsolatedStateCapture(bool start);
    void finishIsolatedStateCapture() { isolatedCaptureBarrier = false; }
    juce::var isolatedPluginDiagnostics(const PluginInstanceId&) const;
    juce::var isolatedPluginDiagnostics() const;
	int findKnownPluginIndexById(const String& id) const;
	int findPluginIndexById(const PluginInstanceId& id) const;
	void setGlobalMuted(bool value) { if (hostProcessor.setGlobalMuted(value)) ++chainVersion; }
	void setGlobalBypassed(bool value) { if (hostProcessor.setGlobalBypassed(value)) ++chainVersion; }
	bool isMonoInputs() const { return hostProcessor.isMonoInputs(); }
    void setMonoInputs(bool value);
    bool isMonoOutput() const { return hostProcessor.isMonoOutput(); }
    void setMonoOutput(bool value);
    bool isGlobalMuted() const { return hostProcessor.isGlobalMuted(); }
	bool isGlobalBypassed() const { return hostProcessor.isGlobalBypassed(); }
	void resetClipping(bool input, bool output, int channel = -1)
	{ hostProcessor.resetClipping(input, output, channel); }
	std::vector<PluginDescription> getKnownPluginsSorted() const;
	AudioDeviceConfiguration getAudioDeviceConfiguration();
	AudioRecoveryConfiguration getAudioRecoveryConfiguration() const;
	AudioBlocklistConfiguration getAudioBlocklistConfiguration() const;
	AvailableAudioChoicesConfiguration getAvailableAudioChoicesConfiguration();
    String updateEnabledChoices(const var& request) { const auto error = deviceController.updateEnabledChoices(request); if (error.isEmpty()) ++chainVersion; return error; }
	bool isVst2FormatActive() const;
	bool isPluginBypassed(int sortedIndex) const;
	bool isKnownPluginMenuId(int menuId) const;
	void addKnownPluginsToMenu(PopupMenu& menu) const;

    var getAudioChannelAliases() const;
    var getAudioDeviceAliases() const;
    String renameAudioDevices(const var& request);
    String restoreAllOriginalNames();
    String renameAudioChannel(const var& request);
	bool setAudioBackendByIndex(int backendIndex);
    bool selectAudioDevice(const AudioDeviceSelection& selection) { return deviceController.selectConfiguration(selection); }
    bool setPreferredAudioDevice(const String& backend, const String& input, const String& output, uint64 generation)
    { return deviceController.setPreferredDevice(backend, input, output, generation); }
    var getAudioSelectionState() const { return deviceController.selectionState(); }
    var getAudioDeviceOptions(const String& backend) { return deviceController.optionsForBackend(backend); }
	bool setAudioInputDeviceByIndex(int deviceIndex);
	bool setAudioOutputDeviceByIndex(int deviceIndex);
	String getLastAudioConfigurationError() const { return deviceController.getLastAudioConfigurationError(); }
	bool setAudioSampleRate(double sampleRate);
	bool setAudioBufferSize(int bufferSize);
	bool setAudioInputChannelCount(int channelCount);
	bool setAudioOutputChannelCount(int channelCount);
	bool setAudioInputChannelEnabled(int channelIndex, bool enabled);
	bool setAudioOutputChannelEnabled(int channelIndex, bool enabled);
	bool setAllAudioInputChannelsEnabled(bool enabled);
	bool setAllAudioOutputChannelsEnabled(bool enabled);
	bool setAudioPersistenceMode(const String& mode);
	bool setAudioPersistenceRetrySeconds(int seconds);
	bool setAudioPersistenceRetryAttempts(int attempts);
	bool setAudioPersistenceCustomBackendByIndex(int backendIndex);
	bool setAudioPersistenceCustomInputByIndex(int deviceIndex);
	bool setAudioPersistenceCustomOutputByIndex(int deviceIndex);
	bool retryPreferredAudioDeviceNow();
	bool addBlockedAudioBackend(const String& backendName);
	bool addBlockedAudioInputDevice(const String& deviceName);
	bool addBlockedAudioOutputDevice(const String& deviceName);
	bool removeBlockedAudioBackend(int index);
	bool removeBlockedAudioDevice(int index);
	bool setAudioBackendEnabledByIndex(int index, bool enabled);
	bool setAudioDeviceChoiceEnabledByIndex(int index, bool enabled);

	void scanDefaultPluginLocations(bool scanVst, bool scanVst3);
	void scanPluginPath(const String& path, bool scanVst, bool scanVst3);
    void scanPluginRoots(const var& roots);
	PluginScanController::Status getPluginScanStatus() const { return pluginScanner.status(); }
	std::pair<String, uint64_t> getPluginScanVersion() const { return pluginScanner.version(); }
	void cancelPluginScan();
	bool beginPluginScan() { collectPluginScanResults(); return pluginScanner.begin(); }
	void retryPluginScanFailures() { pluginScanner.retryFailures(); }
    bool retryPluginScanFailures(const StringArray& ids) { return pluginScanner.retryFailures(ids); }
    PluginScanController::FailurePage getPluginScanFailures(const String& scanId, uint64 revision, size_t offset, size_t limit) const
    { return pluginScanner.failures(scanId, revision, offset, limit); }
    String getPluginMetadata(const String& id) const { return pluginScanner.metadata(id); }
	void collectPluginScanResults();

	void addPluginFromMenuId(int menuId);
	bool addKnownPluginByIndex(int sortedIndex, PluginInstanceId* createdId = nullptr);
	void duplicatePlugin(int sortedIndex, PluginInstanceId* createdId = nullptr);
	int removeKnownPluginByIndex(int sortedIndex);
	int clearKnownPlugins();
	void openKnownPluginLocation(int sortedIndex) const;
	void removePlugin(int sortedIndex);
	void movePluginUp(int sortedIndex);
	void movePluginDown(int sortedIndex);
	void movePluginToIndex(int fromSortedIndex, int toSortedIndex);
	void setPluginBypassed(int sortedIndex, bool shouldBypass);
	bool renamePlugin(int sortedIndex, const String& name);
	bool renameKnownPlugin(int sortedIndex, const String& name);
	String getKnownPluginCustomName(const PluginDescription& plugin) const
	{ return lightHostModern::knownPluginCustomName(*getAppProperties().getUserSettings(), plugin); }
	bool isDiagnosticsEnabled() const { return lightHostModern::diagnosticsCollectionEnabled.load(); }
	void setDiagnosticsEnabled(bool enabled);
	void deletePluginStates();
	void savePluginStates(bool captureAll = true);
	void saveAudioDeviceState();
	void flushPendingSaves();
	void removePluginsLackingInputOutput();
	void removeMissingKnownPlugins();
	void loadActivePlugins();
	void showPluginEditor(int sortedIndex);

	DiagnosticsSnapshot getDiagnosticsSnapshot() const;
	std::pair<float, float> getMeterPeaks() const noexcept { return hostProcessor.getMeterPeaks(); }
    bool configureCallbackMeasurement(unsigned warmupSeconds, unsigned durationSeconds);
    lightHostModern::CallbackMeasurement::Snapshot getCallbackMeasurement() const { return player.callbackMeasurement().snapshot(); }
	uint64 getChainVersion() const noexcept { return chainVersion; }
	uint64 getPluginDatabaseVersion() const noexcept { return pluginDatabaseVersion; }
	uint64 getAudioConfigVersion() const noexcept { return deviceController.getVersion(); }
    var getOperatingState() const;
    var getRoutingMeters() const { return hostProcessor.getRoutingMeters(); }
    String handleOperatingCommand(const var& request);
    bool isChainMode() const { return operatingMode == "chain"; }

private:
    void pollIsolatedPlugins();
    size_t isolatedCaptureBudget(const PluginInstanceId&) const;
    std::map<PluginInstanceId, std::shared_ptr<lightHostModern::IsolatedPluginSession>> isolatedPlugins;
    struct IsolatedCapture {
        uint64_t ticket = 0, revision = 0, generation = 0;
        std::weak_ptr<lightHostModern::IsolatedPluginSession> session;
        std::weak_ptr<PluginSlot> slot;
    };
    std::map<PluginInstanceId, IsolatedCapture> isolatedCaptures;
    bool isolatedCaptureBarrier = false;
    void initializeOperatingProfiles();
    void synchronizeGraph(ChainSnapshot&);
    void applyProfile(const lightHostModern::OperatingProfile&);
    lightHostModern::OperatingProfile captureProfile(const String& name, bool includeAudio, const String& description);
    String currentProfileDigest() const;
    bool profileHasChanges() const;
    std::unique_ptr<lightHostModern::OperatingProfiles> operatingProfiles;
    String operatingMode = "list", profileError;
    bool profilesReady = false;
    std::vector<lightHostModern::PluginInstances> routingUndo, routingRedo;
	enum TimerIds
	{
		audioWatchdogTimerId = 1,
		persistenceTimerId = 2,
		diagnosticsTimerId = 3
	};

	void timerCallback(int timerId) override;
	void changeListenerCallback(ChangeBroadcaster* changed) override;
	void markSettingsDirty();
	PluginSlot* findActiveSlotFor(const PluginInstanceId& id) const;
	void recordProcessFailures();
	void logDiagnosticsSnapshot();
	std::unique_ptr<XmlElement> getXmlValuePreserving(const String& key);
	void saveActivePluginList();
	void saveActivePluginChain(bool saveProcessorStates);
	void saveCurrentAudioChannelState();
	void applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
	                                 const String& backendName,
	                                 const String& inputDeviceName,
	                                 const String& outputDeviceName);
	void rememberLastSelectedAudioDevice();
	void rememberManualSelectedAudioDevice();
	bool applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry);
	bool isAudioBackendBlocked(const String& backendName) const;
	bool isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const;
	bool isAudioDeviceChoiceAllowed(const String& backendName,
	                                const String& inputDeviceName,
	                                const String& outputDeviceName) const;
	bool currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const;
	void closeCurrentAudioDeviceIfBlocked(const String& context);

	bool safeMode = false;
	bool restoreActivePluginsOnStartup = false;
	bool settingsDirty = false;
	uint64 chainReloadCount = 0;
	uint64 bypassToggleCount = 0;
	uint64 settingsFlushCount = 0;
	uint64 pluginStateSaveCount = 0;
	uint64 chainVersion = 0;
	uint64 pluginDatabaseVersion = 0;

	PluginStateStore pluginStateStore;
	PluginScanController pluginScanner;
	GuardedAudioDeviceManager deviceManager;
	DeviceController deviceController;
	AudioPluginFormatManager formatManager;
	KnownPluginList knownPluginList;
	lightHostModern::PluginInstances instances;
	std::unique_ptr<lightHostModern::SessionStore> sessionStore;
	String sessionMigrationId;
	StringArray stateCaptureFailures;
	double stateCaptureDue = 0;
    std::set<PluginInstanceId> pendingStateCaptures;
    uint64 operatingGeneration = 1;
    const String operatingEpoch = Uuid().toString();
	bool sessionLoadSuppressed = false;
	KnownPluginList::SortMethod pluginSortMethod = KnownPluginList::sortByManufacturer;
	RealtimeHostProcessor hostProcessor;
	HostAudioPlayer player;
	mutable lightHostModern::ProcessMemory cachedMemory;
    mutable uint64_t lastMemorySample = 0;
    mutable lightHostModern::CpuUsageSampler hostCpuSampler, workerCpuSampler;
};

#endif /* AudioEngine_h */
