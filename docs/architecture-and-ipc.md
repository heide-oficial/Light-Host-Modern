# Architecture and IPC

Light Host Modern uses two cooperating native desktop processes. This keeps the realtime host independent from the lifetime and rendering work of the WinUI shell.

## Process model

### Host process

`Light Host Modern.exe` owns:

- JUCE application lifetime;
- audio device and callback;
- plugin formats, database, instances, editors, and saved state;
- realtime serial processing;
- settings and recovery state;
- notification-area icon and native menu;
- named-pipe server;
- debug and crash diagnostics.

The main modules are:

- `Source/HostStartup.cpp` - command-line parsing, application properties, recovery options, and JUCE startup.
- `Source/IconMenu.cpp` - tray icon, UI launch/focus, and quit coordination.
- `Source/AudioEngine.*` - device management, plugin database, running chain, settings, and snapshots.
- `Source/RealtimeHostProcessor.*` - realtime processing and immutable chain snapshots.
- `Source/HostIpcServer.*` - named-pipe requests, commands, state snapshots, and telemetry.
- `Source/PluginWindow.*` - plugin editor windows and their saved positions.

### WinUI shell

`LightHostWinUI.exe` owns the Windows 11-style interface, navigation, dialogs, localization, update check, UI preferences, and IPC client. It does not process audio or own plugin instances.

## Startup sequence

1. The host parses command-line options and enables diagnostics when requested.
2. JUCE application properties are opened, reset, or repaired as requested.
3. `AudioEngine` initializes formats, audio state, plugin database, and optionally the saved chain.
4. `HostIpcServer` creates `\\.\pipe\LightHost-<host-pid>`.
5. The notification-area icon is created.
6. Opening the interface launches `LightHostWinUI.exe --host-pipe="<pipe>"`.
7. The shell requests a complete snapshot and begins periodic telemetry/state checks.

If the shell window already exists, the host restores and focuses it rather than opening a duplicate UI.

## Named-pipe protocol

The transport is a local Windows named pipe. The shell opens the pipe for each request, writes a UTF-8 command, reads a UTF-8 JSON response, and closes the handle. The server handles requests on its worker thread and marshals engine mutations to the JUCE message thread where required.

Read operations include:

- `state-snapshot` - complete audio, plugin, preference, and version state;
- `telemetry` - rapidly changing meters and performance values;
- `enabled-audio-choices` - backend/device data for the management dialog.

Mutation commands cover audio selection, channel masks, persistence options, plugin scanning, database actions, chain actions, startup, tray behavior, VST2, and icon changes.

## Snapshots and version counters

The host exposes separate version counters for:

- running chain changes;
- plugin database changes;
- audio configuration changes.

The shell polls lightweight telemetry and requests the larger state snapshot only after a counter changes. This reduces serialization and UI work while preserving live meters and status.

## Failure boundaries

An empty response indicates that the pipe closed or the host stopped responding during an operation. Plugin-load commands return explicit error JSON when the host can reject a plugin safely. The shell displays the failure without pretending the plugin was added.

The process split protects the long-running audio host from routine UI recreation, but a plugin fault inside the host can still terminate audio processing. Safe mode and quarantine mitigate repeated startup failures.
