#pragma once
#include "AudioDeviceState.h"
#include "AudioSelection.h"
#include "BoundedInput.h"
#include <chrono>
#include <functional>

class DeviceController
{
public:
    using Clock = std::chrono::steady_clock;
    DeviceController(AudioDeviceManager& manager, PropertySet& preferences,
        std::function<void()> dirty = {}, std::function<void()> reconfigure = {},
        std::function<Clock::time_point()> clock = [] { return std::chrono::steady_clock::now(); });
    void start(bool safeMode, bool suspended);
    void tick();
    void devicesChanged();
    void invalidateConfiguration();
    void scheduleRetry();
    bool hasPermittedCandidate();
    String deviceInventory() const;
    uint64 getVersion() const { return audioConfigVersion; }
    uint64 getGeneration() const { return generation; }
    const String& getLastAudioConfigurationError() const { return lastAudioConfigurationError; }
    String initialise(AudioDeviceManager&, const XmlElement*, bool allowDefaultFallback);
    String apply(AudioDeviceManager&, const String&, const AudioDeviceManager::AudioDeviceSetup&);
    void recoverIfNeeded(AudioDeviceManager&, AudioRecoveryConfiguration const&, int&, String&, String&);
    DiagnosticsSnapshot createDiagnosticsSnapshot(AudioDeviceManager&, bool collect = true) const;
    AudioDeviceConfiguration getAudioDeviceConfiguration();
    AudioRecoveryConfiguration getAudioRecoveryConfiguration() const;
    AudioBlocklistConfiguration getAudioBlocklistConfiguration() const;
    AvailableAudioChoicesConfiguration getAvailableAudioChoicesConfiguration();
    String updateEnabledChoices(const var& request);
    bool isAudioBackendBlocked(const String& backendName) const;
    bool isAudioDeviceBlocked(const String& backendName, const String& role, const String& deviceName) const;
    bool isAudioDeviceChoiceAllowed(const String& backendName,
                                                 const String& inputDeviceName,
                                                 const String& outputDeviceName) const;
    bool isAudioDeviceCreationAllowed(const String& backend, const String& input, const String& output) const;
    String monoInputsKey() const;
    String monoOutputKey() const;
    bool currentAudioDeviceMatchesPreferred(AudioRecoveryConfiguration const& recoveryConfig) const;
    void closeCurrentAudioDeviceIfBlocked(const String& context);
    void rememberLastSelectedAudioDevice();
    void rememberManualSelectedAudioDevice();
    bool applyPreferredAudioDevice(AudioRecoveryConfiguration const& recoveryConfig, bool manualRetry);
    bool setAudioBackendByIndex(int backendIndex);
    bool selectConfiguration(const AudioDeviceSelection&);
    bool restoreProfileConfiguration(const AudioDeviceSelection&);
    bool setPreferredDevice(const String& backend, const String& input, const String& output, uint64 expectedGeneration);
    var selectionState() const;
    var optionsForBackend(const String& backend);
    bool setAudioInputDeviceByIndex(int deviceIndex);
    bool setAudioOutputDeviceByIndex(int deviceIndex);
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
    bool setAudioSampleRate(double sampleRate);
    bool setAudioBufferSize(int bufferSize);
    bool setAudioInputChannelEnabled(int channelIndex, bool enabled);
    bool setAudioOutputChannelEnabled(int channelIndex, bool enabled);
    bool setAllAudioInputChannelsEnabled(bool enabled);
    bool setAllAudioOutputChannelsEnabled(bool enabled);
    bool setAudioInputChannelCount(int channelCount);
    bool setAudioOutputChannelCount(int channelCount);
    void saveCurrentAudioChannelState();
    void applySavedAudioChannelState(AudioDeviceManager::AudioDeviceSetup& setup,
                                                 const String& backendName,
                                                 const String& inputDeviceName,
                                                 const String& outputDeviceName);
    void saveAudioDeviceState();
private:
    AudioIODeviceType* customBackendType() const;
    void markSettingsDirty() { if (dirty) dirty(); }
    void loadActivePlugins() { if (reconfigure) reconfigure(); }
    PropertySet* preferencesPtr() const { return &preferences; }
    std::unique_ptr<XmlElement> getXmlValueOrClear(const String& key) { return lightHostModern::parseBoundedXml(preferences.getValue(key), 4 * 1024 * 1024); }
    AudioDeviceManager& deviceManager;
    PropertySet& preferences;
    std::function<void()> dirty, reconfigure;
    std::function<Clock::time_point()> clock;
    Clock::time_point nextRetry{};
    uint64 generation = 1, scheduledGeneration = 1, audioConfigVersion = 0;
    int failedAudioRecoveryAttempts = 0;
    bool manualAudioSelectionInProgress = false, audioStartSuspended = false, applicationsSuspended = false;
    String audioRecoveryState = "running", audioRecoveryMessage, lastAudioConfigurationError;
    String configuredBackend;
    String openingBackend, openingInput, openingOutput;
    String attemptedBackend;
    uint64 attemptedGeneration = 0;
    String inventory;
    AudioDeviceManager::AudioDeviceSetup configuredSetup, effectiveSetup, attemptedSetup;
};
