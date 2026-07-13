#include "HostIpcServer.h"
#include "DebugLog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <utility>

namespace
{
	const wchar_t* startupRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const wchar_t* startupValueName = L"Light Host Modern";

	String normaliseTrayIconMode(String mode)
	{
		mode = mode.trim().toLowerCase();
		if (mode == "white" || mode == "black")
			return mode;
		return "color";
	}

	String getTrayIconMode()
	{
		auto settings = getAppProperties().getUserSettings();
		const auto mode = settings->getValue("trayIconMode", settings->getValue("icon", "color"));
		return normaliseTrayIconMode(mode);
	}

	bool setTrayIconMode(String mode)
	{
		mode = normaliseTrayIconMode(mode);
		auto settings = getAppProperties().getUserSettings();
		settings->setValue("trayIconMode", mode);
		settings->setValue("icon", mode);
		settings->saveIfNeeded();
		return true;
	}

	String getCloseBehavior()
	{
		return getAppProperties().getUserSettings()->getValue("closeBehavior", "tray");
	}

	bool setCloseBehavior(const String& behavior)
	{
		if (behavior != "tray" && behavior != "quit")
			return false;

		getAppProperties().getUserSettings()->setValue("closeBehavior", behavior);
		getAppProperties().getUserSettings()->saveIfNeeded();
		return true;
	}

	bool isStartWithWindowsEnabled()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, startupRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
			return false;

		wchar_t value[2048] = {};
		DWORD valueSize = sizeof(value);
		const auto result = RegQueryValueExW(key, startupValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &valueSize);
		RegCloseKey(key);
		return result == ERROR_SUCCESS && value[0] != L'\0';
	}

	bool setStartWithWindows(bool enabled)
	{
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, startupRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
			return false;

		bool success = false;
		if (enabled)
		{
			const String command = "\"" + File::getSpecialLocation(File::currentExecutableFile).getFullPathName() + "\"";
			const auto wide = command.toWideCharPointer();
			const DWORD byteCount = (DWORD) ((wcslen(wide) + 1) * sizeof(wchar_t));
			success = RegSetValueExW(key, startupValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(wide), byteCount) == ERROR_SUCCESS;
		}
		else
		{
			const auto result = RegDeleteValueW(key, startupValueName);
			success = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
		}

		RegCloseKey(key);
		return success;
	}

	bool isVst2HostAvailable()
	{
	#if JUCE_PLUGINHOST_VST
		return true;
	#else
		return false;
	#endif
	}

	bool isVst2SettingEnabled()
	{
		return isVst2HostAvailable()
			&& getAppProperties().getUserSettings()->getBoolValue("enableVst2", false);
	}

	bool setVst2RuntimeEnabled(bool enabled)
	{
		if (enabled && !isVst2HostAvailable())
			return false;

		getAppProperties().getUserSettings()->setValue("enableVst2", enabled);
		getAppProperties().getUserSettings()->saveIfNeeded();
		return true;
	}
}

HostIpcServer::HostIpcServer(AudioEngine& engineToExpose)
	: HostIpcServer(engineToExpose, {})
{
}

HostIpcServer::HostIpcServer(AudioEngine& engineToExpose, std::function<void()> trayIconChangedCallback)
	: engine(engineToExpose),
	  trayIconChanged(std::move(trayIconChangedCallback)),
	  pipeName("\\\\.\\pipe\\LightHost-" + String((int) GetCurrentProcessId()))
{
	worker = std::thread([this] { run(); });
	lightHostLog("IPC server started at " + pipeName);
}

HostIpcServer::~HostIpcServer()
{
	stopping.store(true, std::memory_order_release);
	wakeServer();

	if (worker.joinable())
		worker.join();

	lightHostLog("IPC server stopped.");
}

void HostIpcServer::wakeServer()
{
	HANDLE pipe = CreateFileW(pipeName.toWideCharPointer(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr);

	if (pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
}

void HostIpcServer::run()
{
	while (!stopping.load(std::memory_order_acquire))
	{
		HANDLE pipe = CreateNamedPipeW(pipeName.toWideCharPointer(),
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			1,
			1024 * 1024,
			4 * 1024,
			1000,
			nullptr);

		if (pipe == INVALID_HANDLE_VALUE)
			continue;

		const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		if (connected && !stopping.load(std::memory_order_acquire))
		{
			char buffer[256] = {};
			DWORD bytesRead = 0;
			const BOOL readOk = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
			if (!readOk)
				lightHostLog("IPC ReadFile failed. win32=" + String((int) GetLastError()));

			const auto request = String::fromUTF8(buffer, (int) bytesRead).trim();
			if (request.startsWith("add-known-plugin"))
				lightHostLog("IPC request received: " + request);

			const String response = processRequestOnMessageThread(request);
			const auto responseUtf8 = response.toRawUTF8();
			DWORD bytesWritten = 0;
			if (!WriteFile(pipe, responseUtf8, (DWORD) strlen(responseUtf8), &bytesWritten, nullptr))
				lightHostLog("IPC WriteFile failed for request '" + request + "'. win32=" + String((int) GetLastError()));
			FlushFileBuffers(pipe);
		}

		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
	}
}

String HostIpcServer::processRequestOnMessageThread(const String& request)
{
	if (MessageManager::getInstance()->isThisTheMessageThread())
	{
		try
		{
			return processRequest(request);
		}
		catch (const std::exception& e)
		{
			lightHostLog("IPC request threw std::exception on message thread for '" + request + "': " + String(e.what()));
			return "{\"status\":\"error\",\"message\":\"Host command threw an exception\"}";
		}
		catch (...)
		{
			lightHostLog("IPC request threw unknown exception on message thread for '" + request + "'");
			return "{\"status\":\"error\",\"message\":\"Host command threw an unknown exception\"}";
		}
	}

	String result;
	WaitableEvent done;

	MessageManager::callAsync([this, request, &result, &done]
	{
		try
		{
			result = processRequest(request);
		}
		catch (const std::exception& e)
		{
			lightHostLog("IPC request threw std::exception on message thread for '" + request + "': " + String(e.what()));
			result = "{\"status\":\"error\",\"message\":\"Host command threw an exception\"}";
		}
		catch (...)
		{
			lightHostLog("IPC request threw unknown exception on message thread for '" + request + "'");
			result = "{\"status\":\"error\",\"message\":\"Host command threw an unknown exception\"}";
		}
		done.signal();
	});

	if (!done.wait(120000))
	{
		lightHostLog("IPC request timed out on message thread: " + request);
		return "{\"status\":\"timeout\"}";
	}

	return result;
}

String HostIpcServer::processRequest(const String& request)
{
	const auto trimmed = request.trim();
	if (trimmed.isEmpty() || trimmed == "snapshot")
		return buildSnapshot();
	if (trimmed == "state-snapshot")
		return buildSnapshot();
	if (trimmed == "telemetry")
		return buildTelemetry();
	if (trimmed == "enabled-audio-choices")
		return buildEnabledAudioChoices();

	const int separator = trimmed.indexOfChar(':');
	const auto command = (separator >= 0 ? trimmed.substring(0, separator) : trimmed).trim().toLowerCase();
	const auto payload = separator >= 0 ? trimmed.substring(separator + 1).trim() : String();
	const int index = payload.getIntValue();

	if (command == "toggle-bypass")
	{
		if (index >= 0)
			engine.setPluginBypassed(index, !engine.isPluginBypassed(index));
		return commandOk();
	}

	if (command == "remove-plugin")
	{
		engine.removePlugin(index);
		return commandOk();
	}

	if (command == "duplicate-plugin")
	{
		try
		{
			engine.duplicatePlugin(index);
			return commandOk();
		}
		catch (...)
		{
			return "{\"status\":\"error\",\"message\":\"Plugin could not be duplicated\"}";
		}
	}

	if (command == "move-plugin-up")
	{
		engine.movePluginUp(index);
		return commandOk();
	}

	if (command == "move-plugin-down")
	{
		engine.movePluginDown(index);
		return commandOk();
	}

	if (command == "move-plugin-to")
	{
		const int fromIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const int toIndex = payload.fromFirstOccurrenceOf(":", false, false).getIntValue();
		engine.movePluginToIndex(fromIndex, toIndex);
		return commandOk();
	}

	if (command == "swap-plugin-with")
	{
		const int fromIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const int toIndex = payload.fromFirstOccurrenceOf(":", false, false).getIntValue();
		if (fromIndex >= 0 && toIndex >= 0 && fromIndex != toIndex)
		{
			if (fromIndex < toIndex)
			{
				engine.movePluginToIndex(fromIndex, toIndex);
				engine.movePluginToIndex(toIndex - 1, fromIndex);
			}
			else
			{
				engine.movePluginToIndex(fromIndex, toIndex);
				engine.movePluginToIndex(toIndex + 1, fromIndex);
			}
		}
		return commandOk();
	}

	if (command == "open-plugin-editor")
	{
		engine.showPluginEditor(index);
		return commandOk();
	}

	if (command == "add-known-plugin")
	{
		try
		{
			const auto knownPlugins = engine.getKnownPluginsSorted();
			if (index >= 0 && index < (int) knownPlugins.size())
			{
				const auto& plugin = knownPlugins[(size_t) index];
				Logger::writeToLog("Light Host Modern IPC: add-known-plugin index=" + String(index)
					+ " name='" + plugin.name
					+ "' format='" + plugin.pluginFormatName
					+ "' inputs=" + String(plugin.numInputChannels)
					+ " outputs=" + String(plugin.numOutputChannels)
					+ " path='" + plugin.fileOrIdentifier + "'");
				lightHostLog("IPC add-known-plugin index=" + String(index)
					+ " name='" + plugin.name
					+ "' format='" + plugin.pluginFormatName
					+ "' inputs=" + String(plugin.numInputChannels)
					+ " outputs=" + String(plugin.numOutputChannels)
					+ " path='" + plugin.fileOrIdentifier + "'");
			}
			else
			{
				Logger::writeToLog("Light Host Modern IPC: add-known-plugin invalid index=" + String(index));
				lightHostLog("IPC add-known-plugin invalid index=" + String(index));
			}

			const int beforeCount = (int) engine.getActivePluginsSorted().size();
			const bool loaded = engine.addKnownPluginByIndex(index);
			const int afterCount = (int) engine.getActivePluginsSorted().size();
			Logger::writeToLog("Light Host Modern IPC: add-known-plugin result=" + String(loaded ? "loaded" : "failed")
				+ " index=" + String(index));
			lightHostLog("IPC add-known-plugin result=" + String(loaded ? "loaded" : "failed")
				+ " index=" + String(index));
			if (!loaded)
				return "{\"status\":\"error\",\"message\":\"Plugin could not be loaded. It may not expose audio input and output channels.\"}";

			return "{\"status\":\"ok\",\"addedActive\":" + String(jmax(0, afterCount - beforeCount)) + "}";
		}
		catch (...)
		{
			Logger::writeToLog("Light Host Modern IPC: add-known-plugin threw index=" + String(index));
			lightHostLog("IPC add-known-plugin threw index=" + String(index));
			return "{\"status\":\"error\",\"message\":\"Plugin could not be loaded\"}";
		}
	}

	if (command == "remove-known-plugin")
	{
		const int removedActive = engine.removeKnownPluginByIndex(index);
		return "{\"status\":\"ok\",\"removedActive\":" + String(removedActive) + "}";
	}

	if (command == "open-known-plugin-location")
	{
		engine.openKnownPluginLocation(index);
		return commandOk();
	}

	if (command == "remove-missing-known-plugins")
	{
		const int beforeCount = (int) engine.getKnownPluginsSorted().size();
		engine.removeMissingKnownPlugins();
		const int afterCount = (int) engine.getKnownPluginsSorted().size();
		return "{\"status\":\"ok\",\"removed\":" + String(jmax(0, beforeCount - afterCount)) + "}";
	}

	if (command == "clear-known-plugins")
	{
		const int beforeCount = (int) engine.getKnownPluginsSorted().size();
		const int removedActive = engine.clearKnownPlugins();
		return "{\"status\":\"ok\",\"removed\":" + String(jmax(0, beforeCount)) + ",\"removedActive\":" + String(jmax(0, removedActive)) + "}";
	}

	if (command == "set-audio-backend")
	{
		lightHostLog("IPC set-audio-backend index=" + String(index));
		const bool changed = engine.setAudioBackendByIndex(index);
		if (!changed)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Audio backend could not be selected";
			lightHostLog("IPC set-audio-backend failed index=" + String(index) + " message='" + message + "'");
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		lightHostLog("IPC set-audio-backend succeeded index=" + String(index));
		return commandOk();
	}

	if (command == "set-audio-input")
	{
		lightHostLog("IPC set-audio-input index=" + String(index));
		const bool changed = engine.setAudioInputDeviceByIndex(index);
		if (!changed)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Audio input device could not be selected";
			lightHostLog("IPC set-audio-input failed index=" + String(index) + " message='" + message + "'");
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		lightHostLog("IPC set-audio-input succeeded index=" + String(index));
		return commandOk();
	}

	if (command == "set-audio-output")
	{
		lightHostLog("IPC set-audio-output index=" + String(index));
		const bool changed = engine.setAudioOutputDeviceByIndex(index);
		if (!changed)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Audio output device could not be selected";
			lightHostLog("IPC set-audio-output failed index=" + String(index) + " message='" + message + "'");
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		lightHostLog("IPC set-audio-output succeeded index=" + String(index));
		return commandOk();
	}

	if (command == "set-sample-rate")
	{
		const bool changed = engine.setAudioSampleRate(payload.getDoubleValue());
		if (!changed)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Sample rate could not be changed";
			lightHostLog("IPC set-sample-rate failed value='" + payload + "' message='" + message + "'");
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		return commandOk();
	}

	if (command == "set-buffer-size")
	{
		const bool changed = engine.setAudioBufferSize(index);
		if (!changed)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Audio buffer size could not be changed";
			lightHostLog("IPC set-buffer-size failed value='" + payload + "' message='" + message + "'");
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		return commandOk();
	}

	if (command == "set-input-channels")
	{
		return commandResult(engine.setAudioInputChannelCount(index));
	}

	if (command == "set-output-channels")
	{
		return commandResult(engine.setAudioOutputChannelCount(index));
	}

	if (command == "set-input-channel")
	{
		const auto channelIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const auto enabled = payload.fromFirstOccurrenceOf(":", false, false).getIntValue() != 0;
		return commandResult(engine.setAudioInputChannelEnabled(channelIndex, enabled));
	}

	if (command == "set-output-channel")
	{
		const auto channelIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const auto enabled = payload.fromFirstOccurrenceOf(":", false, false).getIntValue() != 0;
		return commandResult(engine.setAudioOutputChannelEnabled(channelIndex, enabled));
	}

	if (command == "set-all-input-channels")
	{
		return commandResult(engine.setAllAudioInputChannelsEnabled(index != 0));
	}

	if (command == "set-all-output-channels")
	{
		return commandResult(engine.setAllAudioOutputChannelsEnabled(index != 0));
	}

	if (command == "set-audio-persistence-mode")
	{
		return commandResult(engine.setAudioPersistenceMode(payload));
	}

	if (command == "set-audio-persistence-retry-seconds")
	{
		return commandResult(engine.setAudioPersistenceRetrySeconds(index));
	}

	if (command == "set-audio-persistence-retry-attempts")
	{
		return commandResult(engine.setAudioPersistenceRetryAttempts(index));
	}

	if (command == "set-audio-persistence-custom-backend")
	{
		return commandResult(engine.setAudioPersistenceCustomBackendByIndex(index));
	}

	if (command == "set-audio-persistence-custom-input")
	{
		return commandResult(engine.setAudioPersistenceCustomInputByIndex(index));
	}

	if (command == "set-audio-persistence-custom-output")
	{
		return commandResult(engine.setAudioPersistenceCustomOutputByIndex(index));
	}

	if (command == "retry-audio-device")
	{
		const bool success = engine.retryPreferredAudioDeviceNow();
		if (!success)
		{
			const String message = engine.getLastAudioConfigurationError().isNotEmpty()
				? engine.getLastAudioConfigurationError()
				: "Preferred audio device could not be retried";
			return "{\"status\":\"error\",\"message\":" + quote(message) + "}";
		}

		return commandOk();
	}

	if (command == "block-audio-backend")
	{
		return commandResult(engine.addBlockedAudioBackend(payload));
	}

	if (command == "block-audio-input")
	{
		return commandResult(engine.addBlockedAudioInputDevice(payload));
	}

	if (command == "block-audio-output")
	{
		return commandResult(engine.addBlockedAudioOutputDevice(payload));
	}

	if (command == "remove-blocked-audio-backend")
	{
		return commandResult(engine.removeBlockedAudioBackend(index));
	}

	if (command == "remove-blocked-audio-device")
	{
		return commandResult(engine.removeBlockedAudioDevice(index));
	}

	if (command == "set-enabled-audio-backend")
	{
		const int backendIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const bool enabled = payload.fromFirstOccurrenceOf(":", false, false).getIntValue() != 0;
		return commandResult(engine.setAudioBackendEnabledByIndex(backendIndex, enabled));
	}

	if (command == "set-enabled-audio-device")
	{
		const int deviceIndex = payload.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const bool enabled = payload.fromFirstOccurrenceOf(":", false, false).getIntValue() != 0;
		return commandResult(engine.setAudioDeviceChoiceEnabledByIndex(deviceIndex, enabled));
	}

	if (command == "scan-default-plugins")
	{
		const int beforeCount = (int) engine.getKnownPluginsSorted().size();
		const auto scanMode = payload.toLowerCase();
		const bool scanAll = scanMode.isEmpty() || scanMode == "all";
		const bool scanVst = scanAll || scanMode == "vst";
		const bool scanVst3 = scanAll || scanMode == "vst3";
		engine.scanDefaultPluginLocations(scanVst, scanVst3);
		const int afterCount = (int) engine.getKnownPluginsSorted().size();
		return "{\"status\":\"ok\",\"added\":" + String(jmax(0, afterCount - beforeCount)) + "}";
	}

	if (command == "scan-plugin-path")
	{
		const int beforeCount = (int) engine.getKnownPluginsSorted().size();
		engine.scanPluginPath(payload, true, true);
		const int afterCount = (int) engine.getKnownPluginsSorted().size();
		return "{\"status\":\"ok\",\"added\":" + String(jmax(0, afterCount - beforeCount)) + "}";
	}

	if (command == "delete-plugin-states")
	{
		engine.deletePluginStates();
		engine.loadActivePlugins();
		return commandOk();
	}

	if (command == "set-start-with-windows")
	{
		return commandResult(setStartWithWindows(index != 0));
	}

	if (command == "set-close-behavior")
	{
		return commandResult(setCloseBehavior(payload.toLowerCase()));
	}

	if (command == "set-enable-vst2")
	{
		return commandResult(setVst2RuntimeEnabled(index != 0));
	}

	if (command == "set-tray-icon-mode")
	{
		const bool success = setTrayIconMode(payload);
		if (success && trayIconChanged)
			trayIconChanged();
		return commandResult(success);
	}

	if (command == "quit-host")
	{
		JUCEApplication::getInstance()->quit();
		return commandOk();
	}

	return "{\"status\":\"error\",\"message\":\"unknown command\"}";
}

String HostIpcServer::commandOk()
{
	return "{\"status\":\"ok\"}";
}

String HostIpcServer::commandResult(bool success)
{
	return success ? commandOk() : "{\"status\":\"error\"}";
}

String HostIpcServer::buildTelemetry()
{
	const auto diagnostics = engine.getDiagnosticsSnapshot();
	return "{"
		"\"status\":\"online\","
		"\"knownPlugins\":" + String((int) engine.getKnownPluginList().getNumTypes()) + ","
		"\"activePluginCount\":" + String(diagnostics.activePlugins) + ","
		"\"chainVersion\":" + String((int64) engine.getChainVersion()) + ","
		"\"pluginDbVersion\":" + String((int64) engine.getPluginDatabaseVersion()) + ","
		"\"audioConfigVersion\":" + String((int64) engine.getAudioConfigVersion()) + ","
		"\"diagnostics\":{"
			"\"backend\":" + quote(diagnostics.backend) + ","
			"\"deviceName\":" + quote(diagnostics.deviceName) + ","
			"\"cpuUsagePercent\":" + String(diagnostics.cpuUsagePercent, 2) + ","
			"\"xRunCount\":" + String(diagnostics.xRunCount) + ","
			"\"sampleRate\":" + String(diagnostics.sampleRate, 0) + ","
			"\"bufferSize\":" + String(diagnostics.bufferSize) + ","
			"\"inputLatency\":" + String(diagnostics.inputLatency) + ","
			"\"outputLatency\":" + String(diagnostics.outputLatency) + ","
			"\"inputChannels\":" + String(diagnostics.inputChannels) + ","
			"\"outputChannels\":" + String(diagnostics.outputChannels) + ","
			"\"inputLevel\":" + String(diagnostics.inputLevel, 3) + ","
			"\"outputLevel\":" + String(diagnostics.outputLevel, 3) + ","
			"\"loadedPlugins\":" + String(diagnostics.loadedPlugins) + ","
			"\"chainLatencySamples\":" + String(diagnostics.chainLatencySamples) + ","
			"\"recoveryState\":" + quote(diagnostics.recoveryState) + ","
			"\"recoveryMessage\":" + quote(diagnostics.recoveryMessage) + ","
			"\"recoveryAttempt\":" + String(diagnostics.recoveryAttempt) + ","
			"\"recoveryMaxAttempts\":" + String(diagnostics.recoveryMaxAttempts) + ","
			"\"recoveryTargetBackend\":" + quote(diagnostics.recoveryTargetBackend) + ","
			"\"recoveryTargetInputDevice\":" + quote(diagnostics.recoveryTargetInputDevice) + ","
			"\"recoveryTargetOutputDevice\":" + quote(diagnostics.recoveryTargetOutputDevice) + ","
			"\"processFailures\":" + String((int64) diagnostics.processFailures) + ","
			"\"chainReloads\":" + String((int64) diagnostics.chainReloads) + ","
			"\"settingsFlushes\":" + String((int64) diagnostics.settingsFlushes) +
		"}"
	"}";
}

String HostIpcServer::buildSnapshot()
{
	const auto diagnostics = engine.getDiagnosticsSnapshot();
	const auto activePlugins = engine.getActivePluginsSorted();
	const auto knownPluginList = engine.getKnownPluginsSorted();
	const auto audioConfig = engine.getAudioDeviceConfiguration();
	const auto recoveryConfig = engine.getAudioRecoveryConfiguration();
	const auto blocklistConfig = engine.getAudioBlocklistConfiguration();
	const auto knownPlugins = (int) knownPluginList.size();

	StringArray plugins;
	for (int i = 0; i < (int) activePlugins.size(); ++i)
	{
		const auto& plugin = activePlugins[(size_t) i];
		plugins.add("{\"name\":" + quote(plugin.name)
			+ ",\"manufacturer\":" + quote(plugin.manufacturerName)
			+ ",\"format\":" + quote(plugin.pluginFormatName)
			+ ",\"bypassed\":" + String(engine.isPluginBypassed(i) ? "true" : "false")
			+ ",\"path\":" + quote(plugin.fileOrIdentifier)
			+ ",\"order\":" + String(i + 1) + "}");
	}

	StringArray knownPluginItems;
	for (int i = 0; i < (int) knownPluginList.size(); ++i)
	{
		const auto& plugin = knownPluginList[(size_t) i];
		knownPluginItems.add("{\"index\":" + String(i)
			+ ",\"name\":" + quote(plugin.name)
			+ ",\"manufacturer\":" + quote(plugin.manufacturerName)
			+ ",\"format\":" + quote(plugin.pluginFormatName)
			+ ",\"path\":" + quote(plugin.fileOrIdentifier) + "}");
	}

	std::vector<String> blockedBackends;
	for (const auto& backend : blocklistConfig.blockedBackends)
		blockedBackends.push_back(backend);

	std::vector<String> blockedDeviceEntries;
	std::vector<String> blockedDeviceLabels;
	for (const auto& choice : blocklistConfig.blockedDevices)
	{
		blockedDeviceEntries.push_back(choice.backendName + "|" + choice.role + "|" + choice.deviceName);
		blockedDeviceLabels.push_back(choice.backendName + " " + choice.role + ": " + choice.deviceName);
	}

	return "{"
		"\"status\":\"online\","
		"\"knownPlugins\":" + String(knownPlugins) + ","
		"\"activePluginCount\":" + String((int) activePlugins.size()) + ","
		"\"chainVersion\":" + String((int64) engine.getChainVersion()) + ","
		"\"pluginDbVersion\":" + String((int64) engine.getPluginDatabaseVersion()) + ","
		"\"audioConfigVersion\":" + String((int64) engine.getAudioConfigVersion()) + ","
		"\"diagnostics\":{"
			"\"backend\":" + quote(diagnostics.backend) + ","
			"\"deviceName\":" + quote(diagnostics.deviceName) + ","
			"\"cpuUsagePercent\":" + String(diagnostics.cpuUsagePercent, 2) + ","
			"\"xRunCount\":" + String(diagnostics.xRunCount) + ","
			"\"sampleRate\":" + String(diagnostics.sampleRate, 0) + ","
			"\"bufferSize\":" + String(diagnostics.bufferSize) + ","
			"\"inputLatency\":" + String(diagnostics.inputLatency) + ","
			"\"outputLatency\":" + String(diagnostics.outputLatency) + ","
			"\"inputChannels\":" + String(diagnostics.inputChannels) + ","
			"\"outputChannels\":" + String(diagnostics.outputChannels) + ","
			"\"inputLevel\":" + String(diagnostics.inputLevel, 3) + ","
			"\"outputLevel\":" + String(diagnostics.outputLevel, 3) + ","
			"\"loadedPlugins\":" + String(diagnostics.loadedPlugins) + ","
			"\"chainLatencySamples\":" + String(diagnostics.chainLatencySamples) + ","
			"\"recoveryState\":" + quote(diagnostics.recoveryState) + ","
			"\"recoveryMessage\":" + quote(diagnostics.recoveryMessage) + ","
			"\"recoveryAttempt\":" + String(diagnostics.recoveryAttempt) + ","
			"\"recoveryMaxAttempts\":" + String(diagnostics.recoveryMaxAttempts) + ","
			"\"recoveryTargetBackend\":" + quote(diagnostics.recoveryTargetBackend) + ","
			"\"recoveryTargetInputDevice\":" + quote(diagnostics.recoveryTargetInputDevice) + ","
			"\"recoveryTargetOutputDevice\":" + quote(diagnostics.recoveryTargetOutputDevice) + ","
			"\"processFailures\":" + String((int64) diagnostics.processFailures) + ","
			"\"chainReloads\":" + String((int64) diagnostics.chainReloads) + ","
			"\"settingsFlushes\":" + String((int64) diagnostics.settingsFlushes) +
		"},"
		"\"appConfig\":{"
			"\"startWithWindows\":" + String(isStartWithWindowsEnabled() ? "true" : "false") + ","
			"\"closeBehavior\":" + quote(getCloseBehavior()) + ","
			"\"trayIconMode\":" + quote(getTrayIconMode()) + ","
			"\"vst2HostAvailable\":" + String(isVst2HostAvailable() ? "true" : "false") + ","
			"\"vst2RuntimeEnabled\":" + String(isVst2SettingEnabled() ? "true" : "false") + ","
			"\"vst2HostEnabled\":" + String(engine.isVst2FormatActive() ? "true" : "false") + ","
			"\"audioPersistenceMode\":" + quote(recoveryConfig.mode) + ","
			"\"audioPersistenceRetrySeconds\":" + String(recoveryConfig.retrySeconds) + ","
			"\"audioPersistenceRetryAttempts\":" + String(recoveryConfig.retryAttempts) + ","
			"\"audioPersistenceCustomBackend\":" + quote(recoveryConfig.customBackend) + ","
			"\"audioPersistenceCustomInputDevice\":" + quote(recoveryConfig.customInputDevice) + ","
			"\"audioPersistenceCustomOutputDevice\":" + quote(recoveryConfig.customOutputDevice) + ","
			"\"audioPersistenceLastBackend\":" + quote(recoveryConfig.lastBackend) + ","
			"\"audioPersistenceLastInputDevice\":" + quote(recoveryConfig.lastInputDevice) + ","
			"\"audioPersistenceLastOutputDevice\":" + quote(recoveryConfig.lastOutputDevice) + ","
			"\"blockedAudioBackends\":" + stringArrayJson(blockedBackends) + ","
			"\"blockedAudioDevices\":" + stringArrayJson(blockedDeviceEntries) + ","
			"\"blockedAudioDeviceLabels\":" + stringArrayJson(blockedDeviceLabels) +
		"},"
		"\"audioConfig\":{"
			"\"backendNames\":" + stringArrayJson(audioConfig.backendNames) + ","
			"\"inputDeviceNames\":" + stringArrayJson(audioConfig.inputDeviceNames) + ","
			"\"outputDeviceNames\":" + stringArrayJson(audioConfig.outputDeviceNames) + ","
			"\"inputChannelNames\":" + stringArrayJson(audioConfig.inputChannelNames) + ","
			"\"outputChannelNames\":" + stringArrayJson(audioConfig.outputChannelNames) + ","
			"\"activeInputChannels\":" + boolArrayJson(audioConfig.activeInputChannels) + ","
			"\"activeOutputChannels\":" + boolArrayJson(audioConfig.activeOutputChannels) + ","
			"\"sampleRates\":" + numberArrayJson(audioConfig.sampleRates) + ","
			"\"bufferSizes\":" + numberArrayJson(audioConfig.bufferSizes) + ","
			"\"currentBackendIndex\":" + String(audioConfig.currentBackendIndex) + ","
			"\"currentInputDeviceIndex\":" + String(audioConfig.currentInputDeviceIndex) + ","
			"\"currentOutputDeviceIndex\":" + String(audioConfig.currentOutputDeviceIndex) + ","
			"\"currentInputChannels\":" + String(audioConfig.currentInputChannels) + ","
			"\"currentOutputChannels\":" + String(audioConfig.currentOutputChannels) + ","
			"\"maxInputChannels\":" + String(audioConfig.maxInputChannels) + ","
			"\"maxOutputChannels\":" + String(audioConfig.maxOutputChannels) +
		"},"
		"\"activePlugins\":[" + plugins.joinIntoString(",") + "],"
		"\"knownPluginList\":[" + knownPluginItems.joinIntoString(",") + "]"
	"}";
}

String HostIpcServer::buildEnabledAudioChoices()
{
	const auto choices = engine.getAvailableAudioChoicesConfiguration();

	std::vector<String> deviceEntries;
	for (const auto& choice : choices.deviceChoices)
		deviceEntries.push_back(choice.backendName + "|" + choice.role + "|" + choice.deviceName);

	return "{"
		"\"status\":\"ok\","
		"\"allAudioBackendNames\":" + stringArrayJson(choices.backendNames) + ","
		"\"allAudioBackendEnabled\":" + boolArrayJson(choices.backendEnabled) + ","
		"\"allAudioDeviceChoices\":" + stringArrayJson(deviceEntries) + ","
		"\"allAudioDeviceChoiceEnabled\":" + boolArrayJson(choices.deviceEnabled) +
	"}";
}

String HostIpcServer::escapeJson(const String& value)
{
	String result;
	for (int i = 0; i < value.length(); ++i)
	{
		const auto c = value[i];
		if (c == '\\') result << "\\\\";
		else if (c == '"') result << "\\\"";
		else if (c == '\n') result << "\\n";
		else if (c == '\r') result << "\\r";
		else if (c == '\t') result << "\\t";
		else result << c;
	}

	return result;
}

String HostIpcServer::quote(const String& value)
{
	return "\"" + escapeJson(value) + "\"";
}

String HostIpcServer::stringArrayJson(const std::vector<String>& values)
{
	StringArray items;
	for (const auto& value : values)
		items.add(quote(value));
	return "[" + items.joinIntoString(",") + "]";
}

String HostIpcServer::numberArrayJson(const std::vector<int>& values)
{
	StringArray items;
	for (const auto value : values)
		items.add(String(value));
	return "[" + items.joinIntoString(",") + "]";
}

String HostIpcServer::numberArrayJson(const std::vector<double>& values)
{
	StringArray items;
	for (const auto value : values)
		items.add(String(value, 0));
	return "[" + items.joinIntoString(",") + "]";
}

String HostIpcServer::boolArrayJson(const std::vector<bool>& values)
{
	StringArray items;
	for (const auto value : values)
		items.add(value ? "true" : "false");
	return "[" + items.joinIntoString(",") + "]";
}
