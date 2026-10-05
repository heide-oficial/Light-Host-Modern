# Persistence and recovery

LightHostModern stores enough state to restore the audio setup, installed database, and running chain, while providing recovery paths for unavailable devices and unsafe plugins.

## Stored state

JUCE application properties retain host-owned data such as:

- selected backend, input/output device, sample rate, and buffer size;
- per-device channel masks;
- per-device input/output mono preferences and audio device/channel names;
- installed plugin database;
- installed-plugin custom names;
- failed-plugin quarantine;
- device persistence and blocklist configuration;
- startup, tray, VST2, and icon preferences.

Running order, instance IDs, custom instance names, bypass state, isolation choices, plugin bus layouts, routing and processor state are stored in a versioned session file beside the host preferences. The profile catalogue has its own validated backup and atomic replacement. Atomic replacement and recoverable backups protect session writes; existing settings are migrated without merging distinct duplicate instances. See the [session contract](session-contract.md).

Saving an operating profile captures global Mute and Bypass in its catalogue entry. Applying the profile or restoring it at startup restores those values. Unsaved runtime toggles do not update the saved profile or the active-session XML. Profiles that include audio settings additionally store the device selection and mono preferences.

The WinUI shell stores interface-only options in `%LOCALAPPDATA%\LightHostModern\ui-settings.ini`, including language, layout, material, support visibility, and custom scan paths.

**Settings > Appearance > Sidebar on open** stores `Appearance/SidebarOnOpen` as `Collapsed` (default, also used for invalid values) or `Expanded`. It applies when constructing a new window, including reopening from the tray. Toggling the sidebar manually changes only the current window; changing the preference affects subsequent openings. Focusing an already open window does not reset its sidebar.

Settings writes are debounced during interactive operations and flushed explicitly during shutdown.

## Preferred-device recovery

Mono preferences use the existing length-delimited backend/input/output identity encoded as Base64. Input mono retains its `monoInputsV1_` key; output mono uses `monoOutputV1_` with the same identity and defaults to false. Channel masks are excluded from that identity. The host restores both settings when loading the chain, independently of whether WinUI is open.

WinUI saves hardware Individual/Pairs presentation as `InputMode` and `OutputMode` under `AudioChannels.<preferenceKey>` in its existing `ui-settings.ini`, shared by Audio and the Chain hardware cards. Missing values mean Individual inputs and Pairs outputs. The host supplies `preferenceKey` so the UI does not recreate the identity format. Changing this presentation writes only UI preferences and refreshes the view; it preserves enabled channels and graph connections. Plugin/mixer Individual/Pairs presentation is saved with the graph, also without changing existing connection endpoints or widths.

Device persistence supports three modes:

- **Disabled** - no preferred-device retry policy or default-device fallback. Only the exact saved device may open; its disappearance stops processing.
- **Last selected device** - remembers the last manual working configuration.
- **Custom device** - uses a backend/device choice saved from the preferred-device dialog.

The retry interval and maximum-attempt settings control the recovery loop. Failed attempts update the recovery state shown on the Dashboard. **Retry now** resets the paused state and starts an immediate attempt.

Disabled backends and devices are excluded from both manual selection and automatic recovery. If the current device becomes blocked, the host closes it and reports why.

## Audio configuration failures

Before changing a device, the engine retains the previous setup. If a new configuration fails to open, especially during ASIO switching, the previous working setup is restored when possible. The UI receives the host's last configuration error.

The audio watchdog can retry stopped or failed devices after sleep, driver restart, or Windows Audio lifecycle changes.

## Plugin state and quarantine

Running slots use persistent IDs so processor state remains associated with the correct instance across reorder and removal. Legacy state keys are imported during session migration; after migration, the versioned session is authoritative. Recovery preserves damaged originals rather than silently replacing a session with an empty chain.

Failed plugins can be marked with `plugin-failed-*` settings. This prevents repeatedly restoring a plugin known to fail during load or processing.

## Recovery command-line options

| Option | Behavior |
| --- | --- |
| `--safe-mode` | Starts without restoring the active plugin chain and disables normal preferred-device restoration for that launch. |
| `--no-restore-active-plugins` | Skips only active chain restoration. |
| `--reset-settings` | Requests a factory reset before startup, including session state, the profile catalogue and UI preferences. See the exact scope below. |
| `--clear-failed-plugins` | Removes failed-plugin quarantine keys. |
| `--debug` | Opens a console and writes detailed host/UI diagnostics. |
| `-multi-instance=<suffix>` | Uses an isolated settings suffix for advanced testing. |

## Factory reset

`--reset-settings` and the Settings reset action use the same durable reset marker. The UI action first flushes the session and coordinates a restart; the startup path performs the reset before preferences are opened. A failed reset leaves the marker pending so startup can retry instead of opening a partially reset configuration.

The reset removes the current preferences file, its session primary/backup/pending files and pre-session migration copy, the profile catalogue and its backup, owned damaged-session/catalogue recovery files, the recent crashed-plugin list, `ui-settings.ini`, and owned pending canvas-edit recovery files. It then creates empty host preferences, disables detailed-log collection and records completion so interrupted product-name migration cannot restore the removed state. Resetting normal settings through the UI also disables Start with Windows.

The reset does not recursively delete saved profile directories, product-name migration archives, diagnostic capture directories, exported files or plugin files. Those retained files are not automatically reapplied. Reset is an explicit destructive recovery action; safe mode and `--no-restore-active-plugins` suppress loading without deleting the session.

## Scanner cache recovery

Scanner cache v3 can pair its module XML with a `.journal` file during partial validation. The XML snapshot is published atomically with a fresh checkpoint ID before new checksummed class records are appended and flushed. Recovery accepts complete records with that ID; an incomplete/corrupt tail cannot discard earlier records. Final compaction publishes the consolidated XML before deleting the journal. If writing fails, scanning can continue in memory and reports the cache failure in verbose logs; available older checkpoints remain reusable. Journals are limited to 16 MiB and reconstructed entries retain the 4 MiB response limit.

## Debug logs and detailed captures

With `--debug`, the host writes timestamped files under `%APPDATA%\LightHostModern\Logs`. Logs include startup, IPC, device selection, plugin loading, chain rebuilds, and fatal crash context when available. Debug logging is disabled during ordinary launches.

Diagnostics capture is separate from legacy `--debug`. Its durable state (`off`, `armed`, `collecting`, `stopped`) and process segments are under `%LOCALAPPDATA%/LightHostModern/Logs/Captures`. A named event stops existing writers without stopping the app. A shared counter enforces the capture budget. Export writes a temporary destination, replaces the final file only after a successful flush, and records a receipt before the host disables and cleans the capture. Cancelled/failed export keeps the stopped capture intact. See [Diagnostics](diagnostics.md).

## Product name migration

On the first normal start, the host copies the complete `Light Host Modern.settings` family into the canonical `LightHostModern.settings` location before opening preferences. Session primary, backup, pending candidates, damaged archives and plugin quarantine are copied byte-for-byte. Staged files and a durable manifest allow interrupted publication to resume. Originals are retained. Existing canonical recovery candidates take precedence even when invalid, so migration cannot hide damaged newer data with an older session. Conflicting pending migrations stop startup and preserve both versions.

WinUI preferences already use `%LOCALAPPDATA%/LightHostModern/ui-settings.ini` and keep that path. Test profiles never import real user preferences. The WinUI package registration identity remains `LightHost.WinUI` for compatibility.
