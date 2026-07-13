#ifndef AudioEngine_h
#define AudioEngine_h

#include "RealtimeHostProcessor.h"

ApplicationProperties& getAppProperties();

struct DiagnosticsSnapshot
{
	String backend;
	String deviceName;
	String recoveryState;
	String recoveryMessage;
	String recoveryTargetBackend;
	String recoveryTargetInputDevice;
	String recoveryTargetOutputDevice;
	double cpuUsagePercent = 0.0;
	int xRunCount = 0;
	double sampleRate = 0.0;
	int bufferSize = 0;
	int inputLatency = 0;
	int outputLatency = 0;
	int inputChannels = 0;
	int outputChannels = 0;
	int recoveryAttempt = 0;
	int recoveryMaxAttempts = 0;
	float inputLevel = 0.0f;
	float outputLevel = 0.0f;
	int activePlugins = 0;
	int loadedPlugins = 0;
	int chainLatencySamples = 0;
	uint64 chainReloads = 0;
	uint64 bypassToggles = 0;
	uint64 pluginStateSaves = 0;
	uint64 settingsFlushes = 0;
	uint64 processFailures = 0;
	uint64 reusedSlots = 0;
	uint64 rebuiltSlots = 0;
};

struct AudioDeviceConfiguration
{
	std::vector<String> backendNames;
	std::vector<String> inputDeviceNames;
	std::vector<String> outputDeviceNames;
	std::vector<String> inputChannelNames;
	std::vector<String> outputChannelNames;
	std::vector<bool> activeInputChannels;
	std::vector<bool> activeOutputChannels;
	std::vector<double> sampleRates;
	std::vector<int> bufferSizes;
	int currentBackendIndex = -1;
	int currentInputDeviceIndex = -1;
	int currentOutputDeviceIndex = -1;
	int currentInputChannels = 0;
	int currentOutputChannels = 0;
	int maxInputChannels = 0;
	int maxOutputChannels = 0;
};

struct AudioRecoveryConfiguration
{
	String mode;
	int retrySeconds = 5;
	int retryAttempts = 10;
	String customBackend;
	String customInputDevice;
	String customOutputDevice;
	String lastBackend;
	String lastInputDevice;
	String lastOutputDevice;
};

struct BlockedAudioDeviceChoice
{
	String backendName;
	String role;
	String deviceName;
};

struct AudioBlocklistConfiguration
{
	std::vector<String> blockedBackends;
	std::vector<BlockedAudioDeviceChoice> blockedDevices;
};

struct AvailableAudioChoicesConfiguration
{
	std::vector<String> backendNames;
	std::vector<bool> backendEnabled;
	std::vector<BlockedAudioDeviceChoice> deviceChoices;
	std::vector<bool> deviceEnabled;
};

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

class DeviceController
{
public:
	String initialise(AudioDeviceManager& deviceManager, const XmlElement* savedAudioState, bool allowDefaultFallback);
	void recoverIfNeeded(AudioDeviceManager& deviceManager, AudioRecoveryConfiguration const& recoveryConfig,
		int& failedAudioRecoveryAttempts, String& recoveryState, String& recoveryMessage);
	DiagnosticsSnapshot createDiagnosticsSnapshot(AudioDeviceManager& deviceManager) const;
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
	std::vector<PluginDescription> getKnownPluginsSorted() const;
	AudioDeviceConfiguration getAudioDeviceConfiguration();
	AudioRecoveryConfiguration getAudioRecoveryConfiguration() const;
	AudioBlocklistConfiguration getAudioBlocklistConfiguration() const;
	AvailableAudioChoicesConfiguration getAvailableAudioChoicesConfiguration();
	bool isVst2FormatActive() const;
	bool isPluginBypassed(int sortedIndex) const;
	bool isKnownPluginMenuId(int menuId) const;
	void addKnownPluginsToMenu(PopupMenu& menu) const;

	bool setAudioBackendByIndex(int backendIndex);
	bool setAudioInputDeviceByIndex(int deviceIndex);
	bool setAudioOutputDeviceByIndex(int deviceIndex);
	String getLastAudioConfigurationError() const { return lastAudioConfigurationError; }
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

	void addPluginFromMenuId(int menuId);
	bool addKnownPluginByIndex(int sortedIndex);
	void duplicatePlugin(int sortedIndex);
	int removeKnownPluginByIndex(int sortedIndex);
	int clearKnownPlugins();
	void openKnownPluginLocation(int sortedIndex) const;
	void removePlugin(int sortedIndex);
	void movePluginUp(int sortedIndex);
	void movePluginDown(int sortedIndex);
	void movePluginToIndex(int fromSortedIndex, int toSortedIndex);
	void setPluginBypassed(int sortedIndex, bool shouldBypass);
	void deletePluginStates();
	void savePluginStates();
	void saveAudioDeviceState();
	void flushPendingSaves();
	void removePluginsLackingInputOutput();
	void removeMissingKnownPlugins();
	void loadActivePlugins();
	void showPluginEditor(int sortedIndex);

	DiagnosticsSnapshot getDiagnosticsSnapshot() const;
	uint64 getChainVersion() const noexcept { return chainVersion; }
	uint64 getPluginDatabaseVersion() const noexcept { return pluginDatabaseVersion; }
	uint64 getAudioConfigVersion() const noexcept { return audioConfigVersion; }

private:
	enum TimerIds
	{
		audioWatchdogTimerId = 1,
		persistenceTimerId = 2,
		diagnosticsTimerId = 3
	};

	void timerCallback(int timerId) override;
	void changeListenerCallback(ChangeBroadcaster* changed) override;
	void markSettingsDirty();
	PluginSlot* findActiveSlotFor(const PluginDescription& plugin) const;
	void recordProcessFailures();
	void logDiagnosticsSnapshot();
	std::unique_ptr<XmlElement> getXmlValueOrClear(const String& key);
	void saveActivePluginList();
	void saveActivePluginChain(bool saveProcessorStates);
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
	bool audioDeviceStateDirty = false;
	int failedAudioRecoveryAttempts = 0;
	uint32 lastManualAudioConfigurationChangeMs = 0;
	bool manualAudioSelectionInProgress = false;
	String audioRecoveryState = "running";
	String audioRecoveryMessage;
	uint64 chainReloadCount = 0;
	uint64 bypassToggleCount = 0;
	uint64 settingsFlushCount = 0;
	uint64 pluginStateSaveCount = 0;
	uint64 chainVersion = 0;
	uint64 pluginDatabaseVersion = 0;
	uint64 audioConfigVersion = 0;
	String lastAudioConfigurationError;

	PluginStateStore pluginStateStore;
	DeviceController deviceController;
	AudioDeviceManager deviceManager;
	AudioPluginFormatManager formatManager;
	KnownPluginList knownPluginList;
	KnownPluginList activePluginList;
	KnownPluginList::SortMethod pluginSortMethod = KnownPluginList::sortByManufacturer;
	RealtimeHostProcessor hostProcessor;
	AudioProcessorPlayer player;
};

#endif /* AudioEngine_h */
