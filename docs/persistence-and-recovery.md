# Persistence and recovery

Light Host Modern stores enough state to restore the audio setup, installed database, and running chain, while providing recovery paths for unavailable devices and unsafe plugins.

## Stored state

JUCE application properties retain host-owned data such as:

- selected backend, input/output device, sample rate, and buffer size;
- per-device channel masks;
- installed plugin database;
- running plugin order and bypass state;
- processor state for running plugin slots;
- failed-plugin quarantine;
- device persistence and blocklist configuration;
- startup, tray, VST2, and icon preferences.

The WinUI shell stores interface-only options in `%LOCALAPPDATA%\LightHostModern\ui-settings.ini`, including language, layout, material, support visibility, and custom scan paths.

Settings writes are debounced during interactive operations and flushed explicitly during shutdown.

## Preferred-device recovery

Device persistence supports three modes:

- **Disabled** - no preferred-device retry policy.
- **Last selected device** - remembers the last manual working configuration.
- **Custom device** - uses a backend/device choice saved from the preferred-device dialog.

The retry interval and maximum-attempt settings control the recovery loop. Failed attempts update the recovery state shown on the Dashboard. **Retry now** resets the paused state and starts an immediate attempt.

Disabled backends and devices are excluded from both manual selection and automatic recovery. If the current device becomes blocked, the host closes it and reports why.

## Audio configuration failures

Before changing a device, the engine retains the previous setup. If a new configuration fails to open, especially during ASIO switching, the previous working setup is restored when possible. The UI receives the host's last configuration error.

The audio watchdog can retry stopped or failed devices after sleep, driver restart, or Windows Audio lifecycle changes.

## Plugin state and quarantine

Running slots use stable state keys so processor state remains associated with the correct instance across reorder and removal. Older state keys are read when possible for compatibility.

Failed plugins can be marked with `plugin-failed-*` settings. This prevents repeatedly restoring a plugin known to fail during load or processing.

## Recovery command-line options

| Option | Behavior |
| --- | --- |
| `--safe-mode` | Starts without restoring the active plugin chain and disables normal preferred-device restoration for that launch. |
| `--no-restore-active-plugins` | Skips only active chain restoration. |
| `--reset-settings` | Removes the main settings file and recent crashed-plugin list before startup. |
| `--clear-failed-plugins` | Removes failed-plugin quarantine keys. |
| `--debug` | Opens a console and writes detailed host/UI diagnostics. |
| `-multi-instance=<suffix>` | Uses an isolated settings suffix for advanced testing. |

## Debug logs

With `--debug`, the host writes timestamped files under `%APPDATA%\LightHostModern\Logs`. Logs include startup, IPC, device selection, plugin loading, chain rebuilds, and fatal crash context when available. Debug logging is disabled during ordinary launches.
