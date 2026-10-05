#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include "AudioMeters.h"
#include <optional>
using namespace juce;

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
	std::optional<double> hostCpuPercent, workerCpuPercent;
    std::optional<double> hostResidentMiB, hostCommittedMiB, workerResidentMiB, workerCommittedMiB;
	bool driverAvailable = false;
	int xRunCount = 0;
	double sampleRate = 0.0;
	double requestedSampleRate = 0.0;
	int requestedBufferSize = 0;
	uint64 configurationGeneration = 0;
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
	uint64 midiOverflow = 0;
	uint64 processedBlocks = 0, processedSamples = 0, inputMidiEvents = 0, outputMidiEvents = 0;
	lightHostModern::MeterSnapshot inputMeters, outputMeters;
	uint64 reusedSlots = 0;
	uint64 rebuiltSlots = 0;
};

struct AudioDeviceConfiguration
{
	std::vector<String> backendNames;
	std::vector<String> customInputDeviceNames, customOutputDeviceNames;
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
    String token;
	std::vector<String> backendNames;
	std::vector<bool> backendEnabled;
	std::vector<BlockedAudioDeviceChoice> deviceChoices;
	std::vector<bool> deviceEnabled;
};

