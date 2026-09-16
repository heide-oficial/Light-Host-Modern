#include "MeterJson.h"
#include "HostIpcServer.h"
#include "DebugLog.h"
#include "RuntimeProfile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <utility>
#include <chrono>
#include "IpcPipe.h"

struct HostIpcServer::Transport
{
    lightHost::ipc::StopEvent stop;
};

namespace
{
	const wchar_t* startupRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	const wchar_t* startupValueName = L"Light Host Modern";

	String startupCommand()
	{
		return "\"" + File::getSpecialLocation(File::currentExecutableFile).getFullPathName() + "\" --startup";
	}

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
		if (lightHost::RuntimeProfile::current().test) return false;
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, startupRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
			return false;

		wchar_t value[2048] = {};
		DWORD valueSize = sizeof(value);
		DWORD valueType = 0;
		const auto result = RegQueryValueExW(key, startupValueName, nullptr, &valueType, reinterpret_cast<LPBYTE>(value), &valueSize);
		RegCloseKey(key);
		if (result != ERROR_SUCCESS || (valueType != REG_SZ && valueType != REG_EXPAND_SZ) || value[0] == L'\0')
			return false;

		return String(value).trim().equalsIgnoreCase(startupCommand());
	}

	bool setStartWithWindows(bool enabled)
	{
		if (lightHost::RuntimeProfile::current().test) return false;
		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, startupRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
			return false;

		bool success = false;
		if (enabled)
		{
			const String command = startupCommand();
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
	  pipeName(lightHost::RuntimeProfile::current().pipeName().c_str())
{
    lifetime = std::make_shared<lightHost::ipc::LifetimeGate<HostIpcServer>>(*this);
    transport = std::make_unique<Transport>();
    worker = std::thread([this] { run(); });
    eventWorker = std::thread([this] { runEvents(); });
    meterWorker = std::thread([this] { run(true); });
    startTimer(100);
	lightHostLog("IPC server started at " + pipeName);
}

HostIpcServer::~HostIpcServer()
{
    stopTimer();
	operations.close([this](const std::string& id) {
        lightHost::ipc::Request request;
        request.id = String(id);
        request.errorCode = "shutting_down";
        request.errorMessage = "Host is shutting down";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id).toStdString();
    });
	stopping.store(true, std::memory_order_release);
	transport->stop.signal();
	events.close();
	lifetime->close();

	if (worker.joinable())
		worker.join();
	if (eventWorker.joinable()) eventWorker.join();
	if (meterWorker.joinable()) meterWorker.join();

	lightHostLog("IPC server stopped.");
}

var HostIpcServer::revisionsJson(const lightHost::ipc::StateRevisions& revisions) const
{
    auto* object = new DynamicObject();
    for (size_t index = 0; index < revisions.size(); ++index)
        object->setProperty(lightHost::ipc::revisionNames[index], static_cast<int64>(revisions[index]));
    return var(object);
}

void HostIpcServer::timerCallback()
{
    using namespace lightHost::ipc;
    const auto currentScan = engine.getPluginScanVersion();
    auto revisions = publishedRevisions;
    revisions[0] = engine.getChainVersion(); revisions[1] = engine.getPluginDatabaseVersion();
    revisions[2] = engine.getAudioConfigVersion(); revisions[4] = operationRevision.load();
    if (currentScan != scanRevision) { scanRevision = currentScan; ++revisions[3]; }
    if (revisions == publishedRevisions) return;
    EntityChanges changes;
    const auto diff = [&](const char* domain, auto& previous, auto next)
    {
        auto& ids = changes[domain];
        for (const auto& item : next)
        {
            const auto old = previous.find(item.first);
            if (old == previous.end() || old->second != item.second) ids.push_back(item.first);
        }
        for (const auto& item : previous) if (next.find(item.first) == next.end()) ids.push_back(item.first);
        if (ids.empty()) ids.push_back("*");
        previous = std::move(next);
    };
    if (revisions[0] != publishedRevisions[0])
    {
        std::map<std::string, std::string> next;
        int position = 0;
        for (const auto& item : engine.getPluginInstances())
            next[item.id.toStdString()] = String(position++).toStdString() + "\n" + item.customName.toStdString()
                + "\n" + item.loading.toStdString() + "\n" + item.error.toStdString() + (item.bypassed ? "\n1" : "\n0");
        diff("chain", chainEntities, std::move(next));
    }
    if (revisions[1] != publishedRevisions[1])
    {
        std::map<std::string, std::string> next;
        for (const auto& item : engine.getKnownPluginsSorted())
            next[lightHost::knownPluginId(item).toStdString()] = item.createXml()->toString().toStdString() + "\n" + engine.getKnownPluginCustomName(item).toStdString();
        diff("database", databaseEntities, std::move(next));
    }
    if (revisions[2] != publishedRevisions[2]) changes["devices"] = {"*"};
    if (revisions[3] != publishedRevisions[3]) changes["scan"] = {currentScan.first.toStdString()};
    if (revisions[4] != publishedRevisions[4]) changes["operations"].swap(completedOperationIds);
    events.publish(revisions, std::move(changes));
    publishedRevisions = revisions;
}

String HostIpcServer::eventRequest(const String& json)
{
    ++eventRequests;
    using namespace lightHost::ipc;
    auto request = parseRequest(json);
    const auto fail = [&](const char* code, const char* message) {
        request.errorCode = code; request.errorMessage = message;
        return withEnvelope(errorResponse(request), request.id);
    };
    if (!request) return withEnvelope(errorResponse(request), request.id);
    if (request.command == "hello") return withEnvelope(commandOk(), request.id);
    if (request.command != "events") return fail("wrong_transport", "This pipe only delivers events");
    const auto& options = request.args[0];
    const auto integer = [](const var& value) { return (value.isInt() || value.isInt64()) && static_cast<int64>(value) >= 0; };
    if (!options["hostSession"].isString() || !integer(options["afterSequence"]) || !integer(options["waitMs"])
        || static_cast<int64>(options["waitMs"]) > 4000) return fail("invalid_arguments", "Invalid event cursor or wait interval");
    if (options["hostSession"].toString() != hostSession) return fail("host_restarted", "Obtain a current snapshot before receiving events");
    const auto batch = events.read(static_cast<uint64_t>(static_cast<int64>(options["afterSequence"])),
        std::chrono::milliseconds(static_cast<int64>(options["waitMs"])));
    if (batch.stopped) return fail("shutting_down", "Host is shutting down");
    auto* result = new DynamicObject();
    result->setProperty("status", "ok"); result->setProperty("sequence", static_cast<int64>(batch.sequence));
    result->setProperty("revisions", revisionsJson(batch.revisions)); result->setProperty("resyncRequired", batch.resyncRequired);
    auto* changes = new DynamicObject();
    for (const auto& group : batch.changes)
    {
        Array<var> ids; for (const auto& id : group.second) ids.add(String(id));
        changes->setProperty(String(group.first), ids);
    }
    result->setProperty("changes", var(changes));
    return withEnvelope(JSON::toString(var(result), true), request.id);
}

void HostIpcServer::runEvents()
{
    using namespace lightHost::ipc;
    const auto eventPipe = pipeName + "-events";
    while (!stopping.load())
    {
        Handle pipe(CreateNamedPipeW(eventPipe.toWideCharPointer(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 64 * 1024, 64 * 1024, 1000, nullptr));
        if (!pipe) { if (WaitForSingleObject(transport->stop.get(), 100) == WAIT_OBJECT_0) break; continue; }
        PipeIo connection(pipe.get(), transport->stop.get(), 5000);
        if (connection.connect() == ERROR_SUCCESS)
        {
            std::string wire;
            PipeIo input(pipe.get(), transport->stop.get(), 5000);
            if (input.read(wire) == ERROR_SUCCESS)
            {
                String response;
                try { response = eventRequest(String::fromUTF8(wire.data(), static_cast<int>(wire.size()))); }
                catch (...)
                {
                    auto request = parseRequest(String::fromUTF8(wire.data(), static_cast<int>(wire.size())));
                    request.errorCode = "internal_error"; request.errorMessage = "Event transport failed";
                    response = withEnvelope(errorResponse(request), request.id);
                }
                PipeIo output(pipe.get(), transport->stop.get(), 5000);
                if (output.write(response.toStdString()) == ERROR_SUCCESS)
                {
                    std::string receipt;
                    PipeIo acknowledgement(pipe.get(), transport->stop.get(), 5000); acknowledgement.read(receipt);
                }
            }
        }
        DisconnectNamedPipe(pipe.get());
    }
}

String HostIpcServer::meterRequest(const String& json)
{
    auto request = lightHost::ipc::parseRequest(json);
    if (!request) return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    if (request.command != "meter-levels") {
        request.errorCode = "wrong_transport";
        request.errorMessage = "This pipe only delivers current meter levels";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    }
    ++meterRequests;
    const auto levels = engine.getMeterPeaks();
    auto* result = new DynamicObject();
    result->setProperty("status", "ok");
    result->setProperty("inputPeak", levels.first);
    result->setProperty("outputPeak", levels.second);
    return withEnvelope(JSON::toString(var(result), true), request.id);
}

void HostIpcServer::run(bool metersOnly)
{
    using namespace lightHost::ipc;
    while (!stopping.load(std::memory_order_acquire))
    {
        const auto endpoint = metersOnly ? pipeName + "-meters" : pipeName;
        Handle pipe(CreateNamedPipeW(endpoint.toWideCharPointer(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 64 * 1024, 64 * 1024, 1000, nullptr));
        if (!pipe)
        {
            if (WaitForSingleObject(transport->stop.get(), 100) == WAIT_OBJECT_0) break;
            continue;
        }

        PipeIo connection(pipe.get(), transport->stop.get(), 5000);
        if (connection.connect() == ERROR_SUCCESS && !stopping.load(std::memory_order_acquire))
        {
            std::string request;
            PipeIo input(pipe.get(), transport->stop.get(), 5000);
            if (input.read(request) == ERROR_SUCCESS)
            {
                if (!metersOnly) responseInFlight.store(true);
                const auto json = String::fromUTF8(request.data(), static_cast<int>(request.size())).trim();
                String response;
                try { response = metersOnly ? meterRequest(json) : acceptRequest(json); }
                catch (...)
                {
                    auto error = parseRequest(json);
                    error.errorCode = "command_exception";
                    error.errorMessage = "Host command threw an exception";
                    response = withEnvelope(errorResponse(error), error.id);
                }
                PipeIo output(pipe.get(), transport->stop.get(), 5000);
                if (output.write(response.toStdString()) == ERROR_SUCCESS)
                {
                    // Acknowledgement (or client close) proves the response was
                    // consumed before DisconnectNamedPipe discards unread bytes.
                    std::string acknowledgement;
                    PipeIo receipt(pipe.get(), transport->stop.get(), 5000);
                    receipt.read(acknowledgement);
                }
                if (!metersOnly) {
                    responseInFlight.store(false);
                    if (quitRequested.load()) MessageManager::callAsync([] { JUCEApplication::getInstance()->quit(); });
                }
            }
        }
        // Never FlushFileBuffers: a client that stops reading must not hold up
        // shutdown. Completed overlapped writes own the bytes until completion.
        DisconnectNamedPipe(pipe.get());
    }
}

String HostIpcServer::withEnvelope(const String& json, const String& id) const
{
    auto result = JSON::parse(json);
    if (auto* object = result.getDynamicObject())
    {
        object->setProperty("version", lightHost::ipc::protocolVersion);
        object->setProperty("id", id);
        object->setProperty("hostSession", hostSession);
        if (object->getProperty("status").toString() == "error" && !object->getProperty("error").isObject())
        {
            auto* error = new DynamicObject();
            error->setProperty("code", "command_failed");
            const auto message = object->getProperty("message").toString();
            error->setProperty("message", message.isNotEmpty() ? message : "The host could not complete the operation");
            object->setProperty("error", var(error));
        }
        return JSON::toString(result, true);
    }
    lightHost::ipc::Request error;
    error.id = id;
    error.errorCode = "internal_error";
    error.errorMessage = "Host produced an invalid response";
    return withEnvelope(lightHost::ipc::errorResponse(error), id);
}

String HostIpcServer::operationResponse(const lightHost::ipc::Request& request,
                                      const lightHost::ipc::OperationRegistry::Record& record)
{
    auto object = new DynamicObject();
    object->setProperty("status", "operation");
    object->setProperty("operationId", String(record.id));
    object->setProperty("operationState", lightHost::ipc::stateName(record.state));
    if (!record.response.empty()) object->setProperty("result", JSON::parse(String(record.response)));
    return withEnvelope(JSON::toString(var(object), true), request.id);
}

String HostIpcServer::acceptRequest(const String& json)
{
    using namespace lightHost::ipc;
    auto request = parseRequest(json);
    const auto fail = [&](const char* code, const char* message) {
        request.errorCode = code;
        request.errorMessage = message;
        return withEnvelope(errorResponse(request), request.id);
    };
    if (!request) return withEnvelope(errorResponse(request), request.id);
    if (request.command == "hello") { ++heartbeatRequests; return withEnvelope(commandOk(), request.id); }
    if (request.command == "telemetry") ++telemetryRequests;
    if (request.command == "meter-levels") return meterRequest(json);
    if (request.command == "snapshot-manifest" || request.command == "state-snapshot" || request.command == "snapshot") ++snapshotRequests;
    if (request.command == "transport-diagnostics")
    {
        auto* result = new DynamicObject(); result->setProperty("status", "ok");
        result->setProperty("telemetryRequests", static_cast<int64>(telemetryRequests.load()));
        result->setProperty("meterRequests", static_cast<int64>(meterRequests.load()));
        result->setProperty("snapshotRequests", static_cast<int64>(snapshotRequests.load()));
        result->setProperty("heartbeatRequests", static_cast<int64>(heartbeatRequests.load()));
        result->setProperty("eventRequests", static_cast<int64>(eventRequests.load()));
        result->setProperty("retainedEvents", static_cast<int64>(events.retainedRecords()));
        result->setProperty("retainedEventBytes", static_cast<int64>(events.retainedBytes()));
        return withEnvelope(JSON::toString(var(result), true), request.id);
    }
    if (request.command == "operation-status")
    {
        if (request.args[1].toString() != hostSession)
            return fail("host_restarted", "The host restarted; the previous operation must not be repeated automatically");
        const auto record = operations.find(request.args[0].toString().toStdString());
        if (!record) return fail("operation_unknown", "The operation result is no longer available; refresh the current state");
        return operationResponse(request, *record);
    }
    if (isReadOnly(request.command.toStdString())) return processRequestOnMessageThread(json);
    if (request.hostSession != hostSession)
        return fail("host_restarted", "Obtain a current host snapshot before submitting an operation");

    auto content = new DynamicObject();
    content->setProperty("command", request.command);
    content->setProperty("args", request.args);
    auto admission = operations.accept(request.id.toStdString(), JSON::toString(var(content), true).toStdString());
    if (!admission.error.empty())
        return fail(admission.error.c_str(), "The operation could not be accepted");
    if (admission.inserted)
    {
        const auto gate = lifetime;
        if (!MessageManager::callAsync([gate, request, json] {
            gate->invoke([&](HostIpcServer& server) {
                if (!server.operations.start(request.id.toStdString())) return;
                String result;
                try { result = server.processRequest(json); }
                catch (...)
                {
                    auto error = request;
                    error.errorCode = "command_exception";
                    error.errorMessage = "Host command threw an exception";
                    result = server.withEnvelope(errorResponse(error), request.id);
                }
                if (result.getNumBytesAsUTF8() > maxMessageBytes - 4096)
                {
                    auto error = request;
                    error.errorCode = "message_too_large";
                    error.errorMessage = "Operation result exceeds the message limit";
                    result = server.withEnvelope(errorResponse(error), request.id);
                }
                server.operations.finish(request.id.toStdString(), result.toStdString(),
                    JSON::parse(result)["status"].toString() != "error");
                ++server.operationRevision;
                if (server.completedOperationIds.size() < 257) server.completedOperationIds.push_back(request.id.toStdString());
            });
        }))
        {
            auto error = request;
            error.errorCode = "shutting_down";
            error.errorMessage = "Host is shutting down";
            operations.cancel(request.id.toStdString(), withEnvelope(errorResponse(error), request.id).toStdString());
            return fail("shutting_down", "Host is shutting down");
        }
    }
    return operationResponse(request, *admission.record);
}

String HostIpcServer::processRequestOnMessageThread(const String& request)
{
    using Operation = lightHost::ipc::Operation<String>;
    auto operation = std::make_shared<Operation>();
    const auto gate = lifetime;
    const bool posted = MessageManager::callAsync([gate, operation, request]
    {
        gate->invoke([&](HostIpcServer& server)
        {
            operation->execute([&]() -> String
            {
                try { return server.processRequest(request); }
                catch (...)
                {
                    auto error = lightHost::ipc::parseRequest(request);
                    error.errorCode = "command_exception";
                    error.errorMessage = "Host command threw an exception";
                    return server.withEnvelope(lightHost::ipc::errorResponse(error), error.id);
                }
            });
        });
    });
    auto shutdownError = lightHost::ipc::parseRequest(request);
    shutdownError.errorCode = "shutting_down";
    shutdownError.errorMessage = "Host is shutting down";
    if (!posted) return withEnvelope(lightHost::ipc::errorResponse(shutdownError), shutdownError.id);

    while (!stopping.load(std::memory_order_acquire))
    {
        if (auto result = operation->waitFor(std::chrono::milliseconds(25)))
            return *result;
    }
    operation->cancelPending();
    return withEnvelope(lightHost::ipc::errorResponse(shutdownError), shutdownError.id);
}

String HostIpcServer::processRequest(const String& json)
{
    auto request = lightHost::ipc::parseRequest(json);
    if (!request) return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    var response;
    try { response = JSON::parse(dispatchRequest(request)); }
    catch (...)
    {
        request.errorCode = "command_exception";
        request.errorMessage = "Host command threw an exception";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    }
    auto object = response.getDynamicObject();
    if (object == nullptr)
    {
        request.errorCode = "internal_error";
        request.errorMessage = "Host produced an invalid response";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    }
    object->setProperty("version", lightHost::ipc::protocolVersion);
    object->setProperty("id", request.id);
    if (response["status"].toString() == "error")
    {
        request.errorCode = response["error"]["code"].toString();
        if (request.errorCode.isEmpty()) request.errorCode = "command_failed";
        request.errorMessage = response["error"]["message"].toString();
        if (request.errorMessage.isEmpty()) request.errorMessage = response["message"].toString();
        if (request.errorMessage.isEmpty()) request.errorMessage = "Host command failed";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    }
    const auto serialized = withEnvelope(JSON::toString(response, true), request.id);
    if (serialized.getNumBytesAsUTF8() > lightHost::ipc::maxMessageBytes)
    {
        request.errorCode = "message_too_large";
        request.errorMessage = "Use a snapshot manifest and paginated collections";
        return withEnvelope(lightHost::ipc::errorResponse(request), request.id);
    }
    return serialized;
}

String HostIpcServer::dispatchRequest(const lightHost::ipc::Request& request)
{
    const auto& command = request.command;
    const auto& args = request.args;
    if (command == "snapshot" || command == "state-snapshot") return buildSnapshot();
    if (command == "telemetry") return buildTelemetry();
    if (command == "enabled-audio-choices") return buildEnabledAudioChoices();
    const auto fail = [&](const String& code, const String& message) {
        auto error = request; error.errorCode = code; error.errorMessage = message;
        return lightHost::ipc::errorResponse(error);
    };
    if (command == "audio-device-options") return JSON::toString(engine.getAudioDeviceOptions(args[0].toString()), true);
    if (command == "measure-callbacks")
    {
        if (!lightHost::RuntimeProfile::current().test) return fail("test_profile_required", "Callback measurement requires a temporary test profile");
        const int warmup = args[0], duration = args[1];
        if (warmup < 0 || warmup > 300 || duration < 1 || duration > 1800) return fail("invalid_arguments", "Measurement needs 0-300 warmup seconds and 1-1800 measured seconds");
        if (!engine.configureCallbackMeasurement(static_cast<unsigned>(warmup), static_cast<unsigned>(duration)))
            return fail("measurement_unavailable", "Arm the measurement once, before opening an audio device");
        return commandOk();
    }
    if (command == "callback-measurement")
    {
        if (!lightHost::RuntimeProfile::current().test) return fail("test_profile_required", "Callback measurement requires a temporary test profile");
        const auto data = engine.getCallbackMeasurement();
        auto* result = new DynamicObject(); result->setProperty("status", "ok");
        static const char* phases[] = {"disabled", "armed", "warmingUp", "measuring", "completed", "interrupted"};
        result->setProperty("phase", phases[static_cast<unsigned>(data.phase)]);
        result->setProperty("frequency", String(data.frequency));
        result->setProperty("firstCallbackTick", String(data.firstCallbackTick));
        result->setProperty("windowStartTick", String(data.windowStartTick));
        result->setProperty("windowEndTick", String(data.windowEndTick));
        result->setProperty("callbacks", String(data.callbacks)); result->setProperty("samples", String(data.samples));
        result->setProperty("totalTicks", String(data.totalTicks)); result->setProperty("maximumTicks", String(data.maximumTicks));
        result->setProperty("p95UpperTicks", data.quantilesAvailable ? var(String(data.p95UpperTicks)) : var());
        result->setProperty("p99UpperTicks", data.quantilesAvailable ? var(String(data.p99UpperTicks)) : var());
        result->setProperty("quantileRelativeErrorBound", 1.0 / 256.0);
        result->setProperty("hostAllocationAuditAvailable", lightHost::realtimeAudit::available.load());
        result->setProperty("hostAllocations", lightHost::realtimeAudit::available.load() ? var(String(lightHost::realtimeAudit::hostAllocations.load())) : var());
        result->setProperty("hostFrees", lightHost::realtimeAudit::available.load() ? var(String(lightHost::realtimeAudit::hostFrees.load())) : var());
        result->setProperty("thirdPartyAllocationAuditAvailable", false);
        return JSON::toString(var(result), true);
    }
    if (command == "select-audio-device" || command == "set-preferred-audio-device")
    {
        const auto& value = args[0];
        AudioDeviceSelection selection;
        if (command == "select-audio-device")
        {
            if (!lightHost::audioSelection::parse(value, selection)) return fail("invalid_arguments", "A complete named audio configuration is required");
        }
        else
        {
            if (!lightHost::audioSelection::names(value) || !lightHost::audioSelection::generation(value["expectedGeneration"], selection.expectedGeneration))
                return fail("invalid_arguments", "A named recovery target and configuration generation are required");
            selection.backend = value["backend"].toString(); selection.setup.inputDeviceName = value["input"].toString(); selection.setup.outputDeviceName = value["output"].toString();
        }
        if (engine.getAudioSelectionState()["generation"].toString() != String(selection.expectedGeneration))
            return fail("configuration_superseded", "The audio configuration changed; refresh before selecting again");
        const bool applied = command == "select-audio-device" ? engine.selectAudioDevice(selection)
            : engine.setPreferredAudioDevice(selection.backend, selection.setup.inputDeviceName, selection.setup.outputDeviceName, selection.expectedGeneration);
        if (!applied) return fail("audio_configuration_failed", engine.getLastAudioConfigurationError());
        auto* result = new DynamicObject;
        result->setProperty("status", "ok"); result->setProperty("audioSelection", engine.getAudioSelectionState());
        return JSON::toString(var(result), true);
    }
    if (!engine.isSessionWritable() && (command == "add-known-plugin" || command == "remove-plugin"
        || command == "duplicate-plugin" || command == "rename-plugin" || command == "toggle-bypass"
        || command == "move-plugin-up" || command == "move-plugin-down" || command == "move-plugin-to"
        || command == "swap-plugin-with" || command == "remove-known-plugin" || command == "clear-known-plugins"
        || command == "delete-plugin-states"))
        return fail("session_read_only", "Session changes are disabled while loading is suppressed or recovery is incomplete");
    if (command == "snapshot-manifest")
    {
        timerCallback();
        const auto cursor = events.read(UINT64_MAX);
        return JSON::toString(snapshots.capture(buildSnapshot(), hostSession, cursor.sequence, revisionsJson(cursor.revisions)), true);
    }
    if (command == "snapshot-page")
    {
        const auto& options = args[0];
        const auto positiveInteger = [](const var& value) { return (value.isInt() || value.isInt64()) && static_cast<int64>(value) >= 0; };
        if (!options["snapshotId"].isString() || !options["collection"].isString()
            || !positiveInteger(options["offset"]) || !positiveInteger(options["limit"]))
            return fail("invalid_arguments", "Expected snapshot ID, collection, offset and limit");
        return JSON::toString(snapshots.page(options["snapshotId"].toString(), options["collection"].toString(),
            static_cast<size_t>(static_cast<int64>(options["offset"])), static_cast<size_t>(static_cast<int64>(options["limit"]))), true);
    }
    if (command == "events") return fail("wrong_transport", "Use the independent event pipe");
    if (command == "plugin-scan-failures" || command == "retry-plugin-scan-selection")
    {
        const auto& options = args[0];
        const auto nonnegativeInteger = [](const var& value) { return (value.isInt() || value.isInt64()) && static_cast<int64>(value) >= 0; };
        if (!options["scanId"].isString() || !nonnegativeInteger(options["revision"])) return fail("invalid_arguments", "Expected scan ID and revision");
        const auto status = engine.getPluginScanStatus();
        if (options["scanId"].toString() != status.scanId || static_cast<uint64>(static_cast<int64>(options["revision"])) != status.revision)
            return fail("stale_revision", "The scan changed; refresh before continuing");
        if (command == "retry-plugin-scan-selection")
        {
            const auto* selected = options["ids"].getArray();
            if (!selected || selected->isEmpty()) return fail("invalid_arguments", "Select one or more failure IDs");
            StringArray ids;
            for (const auto& id : *selected)
            {
                if (!id.isString() || id.toString().isEmpty()) return fail("invalid_arguments", "Invalid failure ID");
                ids.addIfNotAlreadyThere(id.toString());
            }
            return engine.retryPluginScanFailures(ids) ? commandOk() : fail("scan_busy", "The scan is busy or the selected failure no longer exists");
        }
        if (!nonnegativeInteger(options["offset"]) || !nonnegativeInteger(options["limit"])
            || static_cast<int64>(options["limit"]) < 1 || static_cast<int64>(options["limit"]) > 100)
            return fail("invalid_arguments", "Failure pages require an offset and a limit from 1 to 100");
        const auto page = engine.getPluginScanFailures(status.scanId, status.revision,
            static_cast<size_t>(static_cast<int64>(options["offset"])), static_cast<size_t>(static_cast<int64>(options["limit"])));
        if (page.stale) return fail("stale_revision", "The scan changed; refresh before continuing");
        auto* result = new DynamicObject();
        result->setProperty("status", "ok"); result->setProperty("scanId", status.scanId);
        result->setProperty("revision", static_cast<int64>(status.revision)); result->setProperty("total", static_cast<int64>(page.total));
        Array<var> items;
        for (const auto& failure : page.failures)
        {
            auto* item = new DynamicObject();
            item->setProperty("id", failure.id); item->setProperty("path", failure.path); item->setProperty("reason", failure.reason);
            item->setProperty("format", failure.format); item->setProperty("attempt", failure.attempt); item->setProperty("kind", failure.kind);
            items.add(var(item));
        }
        result->setProperty("failures", items);
        return JSON::toString(var(result), true);
    }
    if (command == "known-plugin-details" || command == "instance-details")
    {
        const auto id = args[0].toString();
        const bool instanceDetails = command == "instance-details";
        const int knownIndex = instanceDetails ? engine.findPluginIndexById(id) : engine.findKnownPluginIndexById(id);
        if (knownIndex < 0) return fail("known_plugin_not_found", "The installed plugin no longer exists in the database");
        const auto* record = instanceDetails ? &engine.getPluginInstances()[static_cast<size_t>(knownIndex)] : nullptr;
        const auto plugin = record ? record->description : engine.getKnownPluginsSorted()[static_cast<size_t>(knownIndex)];
        auto* details = new DynamicObject();
        details->setProperty("status", "ok"); details->setProperty("knownId", record ? record->originalIdentity : id);
        if (record) { details->setProperty("instanceId", record->id); details->setProperty("customName", record->customName); details->setProperty("loading", record->loading); details->setProperty("error", record->error); }
        details->setProperty("customName", record ? record->customName : engine.getKnownPluginCustomName(plugin));
        details->setProperty("name", plugin.name); details->setProperty("manufacturer", plugin.manufacturerName);
        details->setProperty("format", plugin.pluginFormatName); details->setProperty("version", plugin.version);
        details->setProperty("path", plugin.fileOrIdentifier); details->setProperty("identity", plugin.createIdentifierString());
        details->setProperty("availability", "unverified");
        Array<var> buses;
        const auto metadata = XmlDocument::parse(engine.getPluginMetadata(record ? record->originalIdentity : id));
        details->setProperty("declaredMetadata", metadata ? metadata->getStringAttribute("declaredMetadata", "unavailable") : "unavailable");
        details->setProperty("verifiedMetadata", metadata ? metadata->getStringAttribute("verifiedMetadata", "unavailable") : "unavailable");
        if (metadata)
        {
            details->setProperty("availability", "verifiedAtLastScan");
            for (const auto* bus : metadata->getChildIterator()) if (bus->hasTagName("BUS"))
            {
                auto* item = new DynamicObject();
                for (const auto* field : {"direction", "name", "layout"}) item->setProperty(field, bus->getStringAttribute(field));
                for (const auto* field : {"channels", "defaultChannels"}) item->setProperty(field, bus->getIntAttribute(field));
                for (const auto* field : {"main", "enabled"}) item->setProperty(field, bus->getBoolAttribute(field));
                buses.add(var(item));
            }
        }
        details->setProperty("buses", buses);
        return JSON::toString(var(details), true);
    }
    const auto payload = args.isEmpty() ? String() : args[0].toString();
    const bool instanceCommand = command == "toggle-bypass" || command == "remove-plugin"
        || command == "duplicate-plugin" || command == "move-plugin-up" || command == "move-plugin-down"
        || command == "move-plugin-to" || command == "swap-plugin-with" || command == "open-plugin-editor" || command == "rename-plugin";
    const bool knownCommand = command == "add-known-plugin" || command == "remove-known-plugin"
        || command == "open-known-plugin-location" || command == "rename-known-plugin";
    const int index = instanceCommand ? engine.findPluginIndexById(payload)
        : knownCommand ? engine.findKnownPluginIndexById(payload) : (args.isEmpty() ? 0 : static_cast<int>(args[0]));
    if (knownCommand && index < 0)
    {
        auto error = request;
        error.errorCode = "known_plugin_not_found";
        error.errorMessage = "The installed plugin no longer exists in the database";
        return lightHost::ipc::errorResponse(error);
    }
    if (instanceCommand && (index < 0 || ((command == "move-plugin-to" || command == "swap-plugin-with") && engine.findPluginIndexById(args[1].toString()) < 0)))
    {
        auto error = request;
        error.errorCode = "instance_not_found";
        error.errorMessage = "The plugin instance no longer exists";
        return lightHost::ipc::errorResponse(error);
    }

    if (command == "set-diagnostics-enabled") { engine.setDiagnosticsEnabled(static_cast<bool>(args[0])); return commandOk(); }
    if (command == "rename-known-plugin")
        return engine.renameKnownPlugin(index, args[1].toString()) ? commandOk()
            : fail("invalid_instance_name", "Use a single-line name with at most 128 Unicode characters");
    if (command == "rename-plugin")
        return engine.renamePlugin(index, args[1].toString()) ? commandOk()
            : fail("invalid_instance_name", "Use a single-line name with at most 128 Unicode characters");
    if (command == "reset-clipping")
    {
        const auto& options = args[0];
        const auto direction = options["direction"].toString();
        if (direction != "input" && direction != "output" && direction != "all")
            return fail("invalid_arguments", "Expected input, output or all");
        int channel = -1;
        if (options.hasProperty("channel"))
        {
            if (!(options["channel"].isInt() || options["channel"].isInt64())
                || static_cast<int64>(options["channel"]) < 0 || static_cast<int64>(options["channel"]) >= 256)
                return fail("invalid_arguments", "Channel must be an integer from 0 to 255");
            channel = static_cast<int>(options["channel"]);
        }
        engine.resetClipping(direction != "output", direction != "input", channel);
        return commandOk();
    }

    if (command == "set-global-mute") { engine.setGlobalMuted(static_cast<bool>(args[0])); return commandOk(); }
    if (command == "set-global-bypass") { engine.setGlobalBypassed(static_cast<bool>(args[0])); return commandOk(); }
    if (command == "set-mono-inputs") { engine.setMonoInputs(static_cast<bool>(args[0])); return commandOk(); }

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
		const int fromIndex = index;
		const int toIndex = engine.findPluginIndexById(args[1].toString());
		engine.movePluginToIndex(fromIndex, toIndex);
		return commandOk();
	}

	if (command == "swap-plugin-with")
	{
		const int fromIndex = index;
		const int toIndex = engine.findPluginIndexById(args[1].toString());
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
		const bool changed = engine.setAudioSampleRate(static_cast<double>(args[0]));
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
		const auto channelIndex = static_cast<int>(args[0]);
		const auto enabled = static_cast<bool>(args[1]);
		return commandResult(engine.setAudioInputChannelEnabled(channelIndex, enabled));
	}

	if (command == "set-output-channel")
	{
		const auto channelIndex = static_cast<int>(args[0]);
		const auto enabled = static_cast<bool>(args[1]);
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
		const int backendIndex = static_cast<int>(args[0]);
		const bool enabled = static_cast<bool>(args[1]);
		return commandResult(engine.setAudioBackendEnabledByIndex(backendIndex, enabled));
	}

	if (command == "set-enabled-audio-device")
	{
		const int deviceIndex = static_cast<int>(args[0]);
		const bool enabled = static_cast<bool>(args[1]);
		return commandResult(engine.setAudioDeviceChoiceEnabledByIndex(deviceIndex, enabled));
	}

	if (command == "scan-default-plugins")
	{
		const auto scanMode = payload.toLowerCase();
		const bool scanAll = scanMode.isEmpty() || scanMode == "all";
		const bool scanVst = scanAll || scanMode == "vst";
		const bool scanVst3 = scanAll || scanMode == "vst3";
		engine.scanDefaultPluginLocations(scanVst, scanVst3);
		return "{\"status\":\"ok\",\"queued\":true}";
	}

	if (command == "scan-plugin-path")
	{
		engine.scanPluginPath(payload, true, true);
		return "{\"status\":\"ok\",\"queued\":true}";
	}

    if (command == "cancel-plugin-scan") { engine.cancelPluginScan(); return commandOk(); }
    if (command == "begin-plugin-scan") { return commandResult(engine.beginPluginScan()); }
    if (command == "retry-plugin-scan") { engine.retryPluginScanFailures(); return commandOk(); }
    if (command == "plugin-scan-status")
    {
        engine.collectPluginScanResults();
        const auto status = engine.getPluginScanStatus();
        auto result = new DynamicObject();
        result->setProperty("status", "ok");
        result->setProperty("active", status.active);
        result->setProperty("cancelled", status.cancelled);
        result->setProperty("completed", status.completed);
        result->setProperty("total", status.total);
        result->setProperty("cached", status.cached);
        result->setProperty("scanId", status.scanId);
        result->setProperty("revision", static_cast<int64>(status.revision));
        result->setProperty("enumerations", status.enumerations);
        result->setProperty("examined", status.examined);
        result->setProperty("currentFile", status.currentFile);
        result->setProperty("failureCount", (int64) status.failureCount);
        Array<var> failures;
        for (const auto& failure : status.failures)
        {
            if (failures.size() == 100) break;
            auto entry = new DynamicObject();
            entry->setProperty("id", failure.id);
            entry->setProperty("attempt", failure.attempt);
            // Full paths and reasons are obtained through revision-bound pages.
            failures.add(var(entry));
        }
        result->setProperty("failures", failures);
        return JSON::toString(var(result), true);
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

    if (command == "flush-session")
    {
        if (!engine.flushSession()) return fail("session_save_failed", engine.getSessionSaveStatus().error.isNotEmpty()
            ? engine.getSessionSaveStatus().error : engine.getSessionRecoveryError());
        return commandOk();
    }
	if (command == "quit-host")
	{
		quitRequested.store(true);
		if (!responseInFlight.load()) JUCEApplication::getInstance()->quit();
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

String HostIpcServer::buildDiagnostics(const DiagnosticsSnapshot& data)
{
    auto* result = new DynamicObject();
    const auto field = [&](const char* name, const var& value) { result->setProperty(name, value); };
    field("backend", data.backend); field("deviceName", data.deviceName);
    field("cpuUsagePercent", data.cpuUsagePercent); field("dspLoadPercent", data.cpuUsagePercent);
    field("hostCpuPercent", data.hostCpuPercent ? var(*data.hostCpuPercent) : var());
    field("workerCpuPercent", data.workerCpuPercent ? var(*data.workerCpuPercent) : var());
    field("xRunCount", data.xRunCount);
    field("configurationGeneration", static_cast<int64>(data.configurationGeneration));
    field("requestedSampleRate", data.requestedSampleRate > 0 ? var(data.requestedSampleRate) : var());
    field("requestedBufferSize", data.requestedBufferSize > 0 ? var(data.requestedBufferSize) : var());
    field("sampleRate", data.driverAvailable && data.sampleRate > 0 ? var(data.sampleRate) : var());
    field("bufferSize", data.driverAvailable && data.bufferSize > 0 ? var(data.bufferSize) : var());
    field("inputLatency", data.driverAvailable && data.inputChannels > 0 && data.inputLatency >= 0 ? var(data.inputLatency) : var());
    field("outputLatency", data.driverAvailable && data.outputChannels > 0 && data.outputLatency >= 0 ? var(data.outputLatency) : var());
    field("inputChannels", data.inputChannels); field("outputChannels", data.outputChannels);
    field("inputLevel", data.driverAvailable ? data.inputLevel : 0.0f);
    field("outputLevel", data.driverAvailable ? data.outputLevel : 0.0f);
    field("loadedPlugins", data.loadedPlugins); field("chainLatencySamples", data.chainLatencySamples);
    field("recoveryState", data.recoveryState); field("recoveryMessage", data.recoveryMessage);
    field("recoveryAttempt", data.recoveryAttempt); field("recoveryMaxAttempts", data.recoveryMaxAttempts);
    field("recoveryTargetBackend", data.recoveryTargetBackend);
    field("recoveryTargetInputDevice", data.recoveryTargetInputDevice);
    field("recoveryTargetOutputDevice", data.recoveryTargetOutputDevice);
    field("processFailures", static_cast<int64>(data.processFailures));
    const bool allocationAuditAvailable = engine.isDiagnosticsEnabled() && lightHost::realtimeAudit::available.load();
    field("hostAllocationAuditAvailable", allocationAuditAvailable);
    field("hostCallbackAllocations", allocationAuditAvailable ? var(static_cast<int64>(lightHost::realtimeAudit::hostAllocations.load())) : var());
    field("hostCallbackFrees", allocationAuditAvailable ? var(static_cast<int64>(lightHost::realtimeAudit::hostFrees.load())) : var());
    field("thirdPartyAllocationAuditAvailable", false);
    field("midiOverflow", static_cast<int64>(data.midiOverflow));
    field("processedBlocks", static_cast<int64>(data.processedBlocks));
    field("processedSamples", static_cast<int64>(data.processedSamples));
    field("inputMidiEvents", static_cast<int64>(data.inputMidiEvents));
    field("outputMidiEvents", static_cast<int64>(data.outputMidiEvents));
    field("chainReloads", static_cast<int64>(data.chainReloads));
    field("settingsFlushes", static_cast<int64>(data.settingsFlushes));
    field("sessionRecoveryError", engine.getSessionRecoveryError());
    const auto session = engine.getSessionSaveStatus();
    auto* sessionStatus = new DynamicObject();
    sessionStatus->setProperty("writable", engine.isSessionWritable());
    sessionStatus->setProperty("pending", session.pending);
    sessionStatus->setProperty("saving", session.saving);
    sessionStatus->setProperty("requestedRevision", String(session.requestedRevision));
    sessionStatus->setProperty("savedRevision", String(session.savedRevision));
    sessionStatus->setProperty("error", session.error);
    sessionStatus->setProperty("recoveryError", engine.getSessionRecoveryError());
    Array<var> captureFailures;
    for (const auto& id : engine.getStateCaptureFailures()) captureFailures.add(id);
    sessionStatus->setProperty("captureFailures", captureFailures);
    field("session", var(sessionStatus));
    auto* meters = new DynamicObject();
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    meters->setProperty("input", lightHost::meterJson(data.inputMeters,
        device ? device->getInputChannelNames() : StringArray(), device ? device->getActiveInputChannels() : BigInteger(), device != nullptr));
    meters->setProperty("output", lightHost::meterJson(data.outputMeters,
        device ? device->getOutputChannelNames() : StringArray(), device ? device->getActiveOutputChannels() : BigInteger(), device != nullptr));
    field("meters", var(meters));
    return JSON::toString(var(result), true);
}

String HostIpcServer::buildTelemetry()
{
	const auto diagnostics = engine.getDiagnosticsSnapshot();
	return "{"
		"\"status\":\"online\","
        "\"diagnosticsEnabled\":" + String(engine.isDiagnosticsEnabled() ? "true" : "false") + ","
		"\"knownPlugins\":" + String((int) engine.getKnownPluginList().getNumTypes()) + ","
		"\"activePluginCount\":" + String(diagnostics.activePlugins) + ","
		"\"globalMuted\":" + String(engine.isGlobalMuted() ? "true" : "false") + ","
        "\"globalBypassed\":" + String(engine.isGlobalBypassed() ? "true" : "false") + ","
        "\"chainVersion\":" + String((int64) engine.getChainVersion()) + ","
		"\"pluginDbVersion\":" + String((int64) engine.getPluginDatabaseVersion()) + ","
		"\"audioConfigVersion\":" + String((int64) engine.getAudioConfigVersion()) + ","
		"\"diagnostics\":" + buildDiagnostics(diagnostics) + ""
	"}";
}

String HostIpcServer::buildSnapshot()
{
	const auto diagnostics = engine.getDiagnosticsSnapshot();
	const auto& activePlugins = engine.getPluginInstances();
	const auto knownPluginList = engine.getKnownPluginsSorted();
	const auto audioConfig = engine.getAudioDeviceConfiguration();
	const auto recoveryConfig = engine.getAudioRecoveryConfiguration();
	const auto blocklistConfig = engine.getAudioBlocklistConfiguration();
	const auto knownPlugins = (int) knownPluginList.size();

	StringArray plugins;
	for (int i = 0; i < (int) activePlugins.size(); ++i)
	{
		const auto& record = activePlugins[(size_t) i];
        const auto& plugin = record.description;
		plugins.add("{\"instanceId\":" + quote(record.id)
            + ",\"knownId\":" + quote(record.originalIdentity)
            + ",\"name\":" + quote(record.displayName())
            + ",\"originalName\":" + quote(plugin.name)
            + ",\"customName\":" + quote(record.customName)
            + ",\"loading\":" + quote(record.loading)
            + ",\"error\":" + quote(record.error)
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
		knownPluginItems.add("{\"knownId\":" + quote(lightHost::knownPluginId(plugin))
			+ ",\"name\":" + quote(engine.getKnownPluginCustomName(plugin).isNotEmpty() ? engine.getKnownPluginCustomName(plugin) : plugin.name)
            + ",\"originalName\":" + quote(plugin.name)
            + ",\"customName\":" + quote(engine.getKnownPluginCustomName(plugin))
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
        "\"diagnosticsEnabled\":" + String(engine.isDiagnosticsEnabled() ? "true" : "false") + ","
		"\"knownPlugins\":" + String(knownPlugins) + ","
        "\"audioSelection\":" + JSON::toString(engine.getAudioSelectionState(), true) + ","
        "\"hostPid\":" + String((int64) GetCurrentProcessId()) + ","
        "\"hostExecutable\":" + quote(File::getSpecialLocation(File::currentExecutableFile).getFullPathName()) + ","
		"\"activePluginCount\":" + String((int) activePlugins.size()) + ","
		"\"globalMuted\":" + String(engine.isGlobalMuted() ? "true" : "false") + ","
        "\"globalBypassed\":" + String(engine.isGlobalBypassed() ? "true" : "false") + ","
        "\"monoInputs\":" + String(engine.isMonoInputs() ? "true" : "false") + ","
        "\"chainVersion\":" + String((int64) engine.getChainVersion()) + ","
		"\"pluginDbVersion\":" + String((int64) engine.getPluginDatabaseVersion()) + ","
		"\"audioConfigVersion\":" + String((int64) engine.getAudioConfigVersion()) + ","
		"\"diagnostics\":" + buildDiagnostics(diagnostics) + ","
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
			"\"customInputDeviceNames\":" + stringArrayJson(audioConfig.customInputDeviceNames) + ","
			"\"customOutputDeviceNames\":" + stringArrayJson(audioConfig.customOutputDeviceNames) + ","
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

String HostIpcServer::quote(const String& value)
{
	return JSON::toString(var(value), true);
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
