# Architecture and IPC

LightHostModern 2.0.0 separates the native audio host from the WinUI shell. Scanner processes and optional per-instance plugin workers provide additional failure boundaries. The host can keep processing while its interface is closed.

## Process model

### Host process

`LightHostModern.exe` owns:

- JUCE application lifetime;
- audio device and callback;
- plugin formats, database, instances, editors, and saved state;
- realtime processing in List mode or the connected routing graph in Chain mode;
- settings and recovery state;
- notification-area icon and native menu;
- named-pipe server;
- debug and crash diagnostics.

The main modules are:

- `Source/HostStartup.cpp` - command-line parsing, application properties, recovery options, and JUCE startup.
- `Source/IconMenu.cpp` - tray icon, UI launch/focus, and quit coordination.
- `Source/AudioEngine.*` - device management, plugin database, running chain, settings, and snapshots.
- `Source/RealtimeHostProcessor.*` - realtime processing and immutable chain snapshots.
- `Source/RoutingGraph.h`, `RoutingRuntime.h` - graph validation, processing order, channel routing, mixers and latency compensation.
- `Source/HostIpcServer.*` - named-pipe requests, commands, state snapshots, and telemetry.
- `Source/PluginWindow.*` - plugin editor windows and their saved positions.
- `Source/OperatingProfiles.*` - profile catalogue, scoped edits, recovery and routing commands.
- `Source/IsolatedPlugin.*`, `PluginWorkerProtocol.h`, `PluginWorkerMain.cpp` - optional per-instance worker control and nonblocking shared audio/MIDI.

### WinUI shell

`LightHostModernWinUI.exe` owns the Windows 11-style interface, navigation, dialogs, localization, interactive update flow, UI preferences, and IPC client. It does not process audio or own plugin instances. The host also performs background release checks when Windows release notifications are enabled, independently of whether the shell is open; see the [update contract](update-contract.md).

## Startup sequence

1. The host parses command-line options and enables diagnostics when requested.
2. JUCE application properties are opened, reset, or repaired as requested.
3. `AudioEngine` initializes formats, audio state, plugin database, and optionally the saved chain.
4. `HostIpcServer` creates `\\.\pipe\LightHostModern-<host-pid>` plus independent `-events` and `-meters` endpoints.
5. The notification-area icon is created.
6. Opening the interface launches `LightHostModernWinUI.exe --host-pipe="<pipe>"`.
7. The shell requests a complete paginated snapshot, starts its structural event loop, and requests visual telemetry according to the visible page.

If the shell window already exists, the host restores and focuses it rather than opening a duplicate UI.

The host retains the launched shell's process handle and watches for its exit outside the audio and message threads. A unique `--ui-close-event` per launch acknowledges completion of a normal window close. Closing to the tray leaves audio running; an unacknowledged exit (including Windows taskbar **End task** or a shell crash) instead requests host shutdown. A separate ten-second deadline terminates the host if plugin code blocks orderly shutdown. This fallback has the same unsaved-state limitations as forced termination. No process-name searches or exit-code guesses are used, and a shell still starting or closing cannot be launched twice. Packaged activation is monitored using the returned process ID.

## Named-pipe protocol

Application version 2.0.0 uses IPC version 5 on all three local Windows message-mode named pipes. Requests and responses are UTF-8 JSON, limited to 4 MiB per message. Both ends use cancellable overlapped I/O and assemble partial reads. The shell queues command requests in FIFO order outside the UI thread; the server dispatches controller commands serially on the JUCE message thread. Events and meter reads have independent transports. Queued callbacks own their completion state and cannot access a destroyed server.

Each request has `version`, `id`, `command`, and a typed `args` array. Mutations also carry the `hostSession` obtained from `hello` or a snapshot. For example:

```json
{"version":5,"id":"ui-42","hostSession":"session-from-snapshot","command":"set-mono-output","args":[{"enabled":true,"expectedGeneration":12}]}
```

Responses echo `version` and `id`. Errors include `error.code` and `error.message`, with `status: "error"`. The command schema lives in `Source/IpcSchema.h`. The host uses JUCE JSON and the shell uses Windows.Data.Json; names and paths are not parsed using delimiter searches. Legacy text requests and incompatible protocol versions are rejected. Rebuild and restart both processes together.

Depth and structural limits are checked before recursive JSON parsing, including error responses and external metadata. Protocol 5 adds immutable device-inventory tokens and identity-based batch application; old numeric positions cannot select another device after hotplug. Graph edits carry profile ID/generation. Capture-dependent mutations wait for isolated workers asynchronously; read requests continue while those bounded captures are pending.

XML entry points, including JUCE properties loaded at startup, use the same bounded parser on the exact bytes being consumed. Scanner files retain their individual size limits. Paginated state has a separate 32 MiB logical budget on both host and shell; individual wire messages remain limited to 4 MiB. The shell adopts only a complete validated snapshot and preserves its previous state after a partial or invalid read.

Canvas edits retain their original graph revision and a local edit sequence. Structural commands flush earlier local changes first; acknowledgements only clear the sequence they actually cover. Audio control intents preserve device identity, coalesce unsent absolute values within ordering barriers, and are built from confirmed state before sending. They cannot carry channel selections over to a different device.

The client acknowledges a complete response before the server disconnects. Connect/read/write waits can be cancelled during shutdown; the host never calls blocking pipe `FlushFileBuffers`. Mutations return an `operationId` and `operationState` immediately, then execute serially on the JUCE controller. `operation-status` takes `[operationId, originalHostSession]` and returns queued, running, completed, failed or cancelled. Terminal responses include the original result or structured error.

Completed records are retained for at most ten minutes, bounded by 256 records and 32 MiB. In-flight operation records are not expired; their admission is also bounded. Repeating the same ID and command content retrieves the existing operation; a different command using that ID is rejected. Client operation waits have a default 120-second deadline covering queue admission, pipe connection and exchanges. Close/flush supplies a shorter shared absolute deadline, including the confirming snapshot. An expired unsent ticket is removed without blocking later requests. After timeout/reconnection the client queries the original ID without replaying the mutation. `host_restarted` distinguishes an old host session from a temporary disconnection; `operation_unknown` never authorizes automatic replay. Late errors are surfaced after reconciliation and the current snapshot refreshes visible state. A capture-dependent mutation has a separate absolute 30-second barrier: unstable plugin state produces `state_capture_timeout`, preserves pending state and leaves the requested mutation unapplied.

Shutdown rejects new operations and cancels queued work. Quit waits for a response already in transit to finish. The host does not attempt to interrupt plugin code already running on its message thread.

Read operations include:

- `snapshot-manifest` and `snapshot-page` - the shell's complete, paginated audio, plugin, preference, and version state;
- `state-snapshot` - the single-response snapshot, subject to the wire-size limit;
- `events` - structural revisions and operation completions on the event endpoint;
- `meter-levels` - current Dashboard levels on the meter endpoint;
- `telemetry` - diagnostic measurements and performance values;
- `enabled-audio-choices` - backend/device data for the management dialog.

Mutation commands cover audio selection, channel masks, persistence options, plugin scanning, database actions, chain actions, startup, tray behavior, VST2, and icon changes.

Protocol 5 uses persistent `instanceId` strings for remove, duplicate, bypass, editor, move-up/down, move-to, and swap commands. Move-to and swap receive two instance IDs, resolved against the current processing order on the host message thread. An absent instance returns `instance_not_found`; it cannot redirect an old action to the new occupant of a list position. Installed actions use a stable `knownId`, derived from the original format, module identifier and class ID. Every protocol version other than 5 is rejected before dispatch.

An ordered instance collection owns UUIDs independently of plugin descriptions. The transitional `pluginInstancesV1` adapter preserves legacy XML and exact state keys and backs up the original preferences before migration. Duplicating never modifies vendor UIDs. Reordering, filtering, missing modules, and UI recreation do not generate new IDs. Ambiguous legacy data is retained without guessing another plugin's identity. The [versioned session writer](session-contract.md) now persists this model with atomic replacement and recoverable backups.

`set-global-mute` and `set-global-bypass` accept one JSON boolean. Snapshot and telemetry responses expose `globalMuted` and `globalBypassed`. Changes update host runtime state and increment the chain counter. Saving a profile captures both flags in the profile catalogue, independently of the profile's optional audio-device settings; applying that profile or initializing it on startup restores its saved values. The active-session XML does not store these flags, so unsaved toggles alone do not change the profile values restored at startup. The Running toolbar, Chain controls and tray operate on the same host state. The host continuously captures a dry path delayed by the processing latency, mixes it with the processed signal over 5 ms, then applies a separate 5 ms mute gain. Global bypass selects that direct dry path independently of graph connections. Neither global control skips processing of otherwise active plugins. Delay storage is prepared under controller suspension, including after dynamic latency changes.

## Audio control state

`set-mono-inputs` and `set-mono-output` take one object with boolean `enabled` and positive `expectedGeneration`. The host rejects stale generations with `stale_configuration` and malformed values or an unconfigured device with `invalid_arguments`, using the normal operation error envelope. Snapshot and telemetry expose independent `monoInputs` and `monoOutput` flags. `audioSelection.preferenceKey` identifies the device combination; `audioSelection.mainOutputPairActive` indicates that processing is available and physical outputs 1/2 are both active. Enabling output mono does not reopen the stream or rebuild the chain.

Hardware Individual/Pairs presentation is stored locally by WinUI and shared between Audio and the Chain hardware cards. Switching that presentation preserves channel masks and existing graph connections. Enabling or disabling a hardware pair uses the generation-checked audio-selection transaction with both mask bits changed together. Plugin/mixer port presentation belongs to the saved graph and also preserves existing connection endpoints and widths. Mono operations use the captured device generation and the existing operation/snapshot path; an old command cannot apply to a newly selected device.

See the [device-selection contract](device-selection-contract.md) for named selections and the token-checked enabled-device transaction.

## Isolated discovery

Scan commands accept controller operations, whose results report queue acceptance. `begin-plugin-scan` resets an idle scan's progress; `scan-default-plugins` and `scan-plugin-path` enqueue work. `plugin-scan-status` includes scan ID, revision, activity, cancellation, module/enum/cache counts and a bounded summary of failure IDs. `cancel-plugin-scan` cancels queued/running work; `retry-plugin-scan` explicitly retries all failures while idle.

The sibling `LightHostModernScanner.exe` has separate enumeration and module examination modes, with only one child active. All filesystem operations on configured plugin locations, including fingerprints, run in that child. A kill-on-close Windows Job Object owns it before it resumes. Each enumeration root and module examination has a 60-second deadline. Immutable, bounded candidate batches preserve partial progress; a blocked root does not prevent later roots from being attempted. Canonical paths deduplicate overlapping directories, but the path sent to JUCE and the identity returned by JUCE are preserved. Junctions are not traversed recursively.

Private temporary XML uses scanner protocol 3 and correlates format, module path, request ID, batch sequence and content fingerprint. VST3 static metadata accelerates enumeration through JUCE; the scanner also creates each class and verifies its identity and buses. Cache version 3 separates catalog, class and fingerprint verification and records input/output bus names, main/auxiliary roles, enabled state and default layouts. Fingerprints include relevant file names, sizes, modification times and streamed SHA-256 digests for binaries and JSON metadata. Valid sibling classes survive another class's failure. Existing known entries survive failure, cancellation and retry; clearing the database invalidates older scan generations.

`plugin-scan-failures` takes one object with `scanId`, `revision`, `offset` and `limit` (1–100). Pages return stable failure IDs, full paths, reason, format and attempt; a changed revision returns `stale_revision`. `retry-plugin-scan-selection` takes `scanId`, `revision` and selected `ids`, preserving unrelated failures and valid results. The UI's failure dialog provides all pages and preserves selections by ID. `known-plugin-details` accepts a stable class ID and distinguishes availability verified at the last scan from unavailable metadata. Scan isolation protects discovery only; running instances execute directly in the host unless [optional plugin isolation](plugin-isolation.md) is enabled for that instance.

`scan-plugin-roots` accepts `{ roots: [{path, optional, format}] }`, with `format` equal to `all`, `VST` or `VST3`. It consolidates roots by enabled format/origin before enumeration. Scan status includes `recognized`, `ignored`, `enumerating`, and `incomplete`.

## Snapshots and version counters

The host exposes separate version counters for:

- running chain changes;
- plugin database changes;
- audio configuration changes.

The shell follows structural changes through the independent event endpoint and refreshes a complete paginated snapshot when those changes require it. Telemetry counters provide an additional refresh check, while reconnects and explicit refreshes can also request a snapshot. Dashboard meters use their own endpoint at up to 20 Hz. With diagnostic collection enabled, the visible Diagnostics page or Dashboard resource cards request telemetry at up to 1 Hz. A minimized window retains event delivery and a five-second command heartbeat without issuing visual telemetry requests. The native tray Performance submenu reads resource measurements independently while visible, including when WinUI is closed or minimized. See the [state and event contract](state-event-contract.md).

## Failure boundaries

Detailed captures use IPC commands `verbose-log-status` (read-only), `set-verbose-logs` (boolean), `stop-verbose-logs`, `complete-verbose-logs` (capture ID), and `restart-host` (`uiPid`, `uiCreated`). The same IPC 5 request IDs and operation reconciliation apply. The host owns state transitions; the UI exports a stopped capture asynchronously and then confirms completion. Restart helper mode validates process IDs/creation times and relaunches the same executable/profile only after both processes exit. `factory-reset` uses the same restart coordination after scheduling an explicit reset; its deletion scope is documented under [Persistence and recovery](persistence-and-recovery.md#factory-reset).

An empty response indicates that the pipe closed or the host stopped responding during an operation. Plugin-load commands return explicit error JSON when the host can reject a plugin safely. The shell displays the failure without pretending the plugin was added.

The process split protects the long-running audio host from routine UI recreation, but a plugin fault inside the host can still terminate audio processing. Safe mode and quarantine mitigate repeated startup failures.
