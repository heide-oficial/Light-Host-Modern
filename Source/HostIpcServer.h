#ifndef HostIpcServer_h
#define HostIpcServer_h

#include "AudioEngine.h"
#include "IpcOperation.h"
#include "IpcProtocol.h"
#include "OperationRegistry.h"
#include "StateEvents.h"
#include "StateSnapshots.h"
#include <atomic>
#include <functional>
#include <thread>
#include <deque>

class HostIpcServer : private Timer
{
public:
	explicit HostIpcServer(AudioEngine& engineToExpose);
	HostIpcServer(AudioEngine& engineToExpose, std::function<void()> trayIconChangedCallback, std::function<bool(const String&)> releaseNotificationCallback = {});
	~HostIpcServer();

	String getPipeName() const { return pipeName; }
    void requestShutdown();
    void requestLocal(const String&, const Array<var>&, std::function<void(const var&)> completed = {});

private:
	void run(bool metersOnly = false);
	void runEvents();
	String eventRequest(const String& request);
	void timerCallback() override;
	var revisionsJson(const lightHostModern::ipc::StateRevisions&) const;
	String acceptRequest(const String& json);
    void drainMutations();
    std::deque<String> pendingMutations;
    lightHostModern::IsolatedCaptureBarrier captureBarrier;
    std::map<std::string, std::function<void(const var&)>> localCompletions;
	String operationResponse(const lightHostModern::ipc::Request&, const lightHostModern::ipc::OperationRegistry::Record&);
	String withEnvelope(const String& json, const String& id) const;
	String processRequestOnMessageThread(const String& request);
	String processRequest(const String& request);
	String dispatchRequest(const lightHostModern::ipc::Request& request);
	String buildTelemetry();
	String meterRequest(const String& request);
	String buildDiagnostics(const DiagnosticsSnapshot&);
	String buildSnapshot();
	String buildEnabledAudioChoices();
	String commandOk();
	String commandResult(bool success);

	static String quote(const String& value);
	static String stringArrayJson(const std::vector<String>& values);
	static String numberArrayJson(const std::vector<int>& values);
	static String numberArrayJson(const std::vector<double>& values);
	static String boolArrayJson(const std::vector<bool>& values);

	AudioEngine& engine;
	std::function<void()> trayIconChanged;
    std::function<bool(const String&)> releaseNotification;
	String pipeName;
	const String hostSession = Uuid().toString();
	lightHostModern::ipc::OperationRegistry operations;
	lightHostModern::ipc::StateEvents events;
	lightHostModern::ipc::StateSnapshots snapshots;
	lightHostModern::ipc::StateRevisions publishedRevisions{};
	std::atomic<uint64_t> operationRevision{0};
    uint64_t lastSessionStatusSerial = 0;
	std::atomic<uint64_t> telemetryRequests{0}, snapshotRequests{0}, heartbeatRequests{0}, eventRequests{0};
	std::atomic<uint64_t> meterRequests{0};
	std::pair<String, uint64_t> scanRevision;
	std::map<std::string, std::string> chainEntities, databaseEntities;
	std::vector<std::string> completedOperationIds;
	std::atomic<bool> stopping { false };
	std::atomic<bool> responseInFlight { false }, quitRequested { false };
	std::shared_ptr<lightHostModern::ipc::LifetimeGate<HostIpcServer>> lifetime;
	struct Transport;
	std::unique_ptr<Transport> transport;
	std::thread worker;
	std::thread eventWorker;
	std::thread meterWorker;
};

#endif /* HostIpcServer_h */
