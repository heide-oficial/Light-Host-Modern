# Light Host Modern Documentation

Light Host Modern is a Windows-only audio plugin host. It is a modernized fork of the original Light Host project, focused on current Windows audio workflows, WinUI 3, ASIO, VST3, optional VST2, release packaging, and operational stability.

This document is intended to work as a project wiki. It explains how the application is structured, how the runtime works, what each feature does, how the host and UI communicate, how audio and plugins are managed, and how to build, package, debug, and recover the app.

## Table of Contents

- [1. Project Overview](#1-project-overview)
- [2. What the App Is For](#2-what-the-app-is-for)
- [3. High-Level Architecture](#3-high-level-architecture)
- [4. Runtime Startup Flow](#4-runtime-startup-flow)
- [5. Command-Line Options](#5-command-line-options)
- [6. Tray Host](#6-tray-host)
- [7. Native WinUI Shell](#7-native-winui-shell)
- [8. Dashboard Page](#8-dashboard-page)
- [9. Audio Page](#9-audio-page)
- [10. Plugins Page](#10-plugins-page)
- [11. Settings Page](#11-settings-page)
- [12. Audio Engine](#12-audio-engine)
- [13. Realtime Processing Model](#13-realtime-processing-model)
- [14. Audio Backend and Device Handling](#14-audio-backend-and-device-handling)
- [15. Channel Handling](#15-channel-handling)
- [16. Plugin Formats](#16-plugin-formats)
- [17. Plugin Database](#17-plugin-database)
- [18. Active Plugin Chain](#18-active-plugin-chain)
- [19. Plugin Scanning](#19-plugin-scanning)
- [20. Plugin State and Persistence](#20-plugin-state-and-persistence)
- [21. IPC Protocol](#21-ipc-protocol)
- [22. UI Refresh and Telemetry](#22-ui-refresh-and-telemetry)
- [23. Settings Storage](#23-settings-storage)
- [24. Startup, Tray, and Close Behavior](#24-startup-tray-and-close-behavior)
- [25. App Icons](#25-app-icons)
- [26. Debugging and Recovery](#26-debugging-and-recovery)
- [27. Crash Diagnostics and Stability Guards](#27-crash-diagnostics-and-stability-guards)
- [28. Release Packages](#28-release-packages)
- [29. Build Requirements](#29-build-requirements)
- [30. Build Commands](#30-build-commands)
- [31. Repository Layout](#31-repository-layout)
- [32. Development Notes](#32-development-notes)
- [33. Operational Notes](#33-operational-notes)
- [34. Credits and License Lineage](#34-credits-and-license-lineage)

## 1. Project Overview

Light Host Modern is a standalone Windows audio plugin host. It runs as a lightweight native host process and exposes a dedicated Windows 11-style WinUI 3 interface.

The app can:

- Select a Windows audio backend.
- Select audio devices.
- Configure sample rate and buffer size.
- Enable or disable input/output channels.
- Scan VST/VST3 plugin folders.
- Maintain an installed plugin database.
- Add installed plugins to a running processing chain.
- Run multiple instances of the same plugin.
- Bypass, duplicate, remove, and reorder running plugins.
- Open plugin editors.
- Display live input and output meters.
- Persist audio settings, plugin database entries, plugin chain state, UI preferences, tray behavior, and recovery data.
- Generate installer and portable release executables.

The application is intentionally Windows-only. Linux and macOS support from the original project were removed so the codebase can focus on Windows audio APIs, ASIO behavior, Windows App SDK, and a native desktop experience.

## 2. What the App Is For

Light Host Modern is useful when a user wants to host audio effects outside a DAW.

Typical use cases:

- Process a microphone before sending it to another app.
- Process an instrument or line input through VST/VST3 effects.
- Build a lightweight live audio chain.
- Use virtual audio devices with a plugin chain.
- Test plugins outside a full DAW.
- Test Windows Audio, DirectSound, and ASIO driver behavior.
- Validate sample rate, buffer size, routing, and latency behavior.

The app is not a full DAW. It does not provide timeline editing, multitrack arrangement, recording, automation lanes, MIDI sequencing, or project session management. Its scope is focused on live hosting and routing through a plugin chain.

## 3. High-Level Architecture

The app is split into two major runtime parts:

- Host process: `Light Host Modern.exe`
- WinUI shell process: `LightHostWinUI.exe`

The host process owns audio, plugins, tray behavior, settings, crash diagnostics, IPC, and process lifetime. The WinUI shell owns the modern user interface and talks to the host through a named pipe.

The major native modules are:

- `Source/HostStartup.cpp`: application startup, command-line handling, settings initialization, debug console, crash diagnostics, and main app object.
- `Source/IconMenu.cpp`: tray icon, native tray context menu, WinUI launch/focus logic, quit flow, and tray icon variant handling.
- `Source/LightHostController.*`: high-level controller tying together audio, plugins, settings, tray, and IPC.
- `Source/AudioEngine.*`: audio device management, plugin format management, installed plugin database, active plugin chain, plugin state, audio state, and snapshots.
- `Source/RealtimeHostProcessor.*`: realtime audio processing, immutable chain snapshots, bypass routing, latency compensation, peak level tracking, and failure guards.
- `Source/HostIpcServer.*`: named-pipe IPC server used by WinUI.
- `WinUI/LightHost.WinUI/*`: WinUI 3 application, pages, controls, visuals, commands, and UI-side IPC client.

The host is built with JUCE and CMake. The UI is built as a native WinUI 3 app through Visual Studio/MSBuild and packaged beside the host by the release scripts.

## 4. Runtime Startup Flow

At launch, the host process performs this flow:

1. Parses command-line options.
2. Enables the debug console if `--debug` or `-debug` is present.
3. Installs crash diagnostics.
4. Initializes the application properties file under the Light Host Modern settings area.
5. Optionally resets settings or clears failed plugin quarantine data.
6. Creates the main application controller.
7. Initializes audio state and active plugin state.
8. Starts the IPC named-pipe server.
9. Creates the tray icon.
10. Waits for user interaction from the tray or command flow.

When the user opens the UI, the tray host locates `LightHostWinUI.exe`, launches it, and passes:

- the named pipe path through `--host-pipe=...`
- `--debug` when the host is running in debug mode

If a UI window is already running, the host does not launch a second UI. It focuses and restores the existing `Light Host Modern` window instead.

## 5. Command-Line Options

The host supports the following command-line options:

| Option | Purpose |
| --- | --- |
| `--debug` / `-debug` | Opens a debug console and enables verbose host/UI diagnostic logging. |
| `--reset-settings` / `-reset-settings` | Deletes saved settings and the recent crashed plugin list before normal startup. |
| `--clear-failed-plugins` / `-clear-failed-plugins` | Clears failed plugin quarantine keys from the settings file. |
| `--safe-mode` / `-safe-mode` | Starts without restoring potentially unsafe plugin state. Useful when a saved chain crashes on startup. |
| `--no-restore-active-plugins` | Skips active plugin chain restore at startup. Safe mode also skips active chain restore. |
| `-multi-instance=<suffix>` | Starts an isolated instance using a settings suffix. This is an advanced/test path. |

Recovery options are intentionally available from the executable itself so users can repair broken settings or failed plugin states without manually editing files.

## 6. Tray Host

The tray host is the main process lifetime owner. It remains running even when the WinUI shell is closed.

Tray behavior:

- Left-click opens or focuses the WinUI shell.
- Right-click opens a native context menu.
- The context menu includes:
  - `Open app UI`
  - `Quit`

When `Quit` is selected:

1. The host saves plugin state.
2. Pending settings writes are flushed.
3. The WinUI process is closed if it is running.
4. The host exits.

The tray icon uses the selected app icon variant:

- Color
- White
- Black

The icon assets are stored under `Icon/` and embedded into the native host binary through JUCE binary data.

## 7. Native WinUI Shell

The WinUI shell is the user-facing application window. It is a separate process so the JUCE host remains the owner of audio and plugin runtime state.

The shell provides:

- A Windows 11-style dark interface.
- Sidebar navigation.
- Collapsible sidebar.
- Dashboard, Audio, Plugins, and Settings pages.
- Native window material options:
  - Mica
  - Mica Alt
  - Acrylic
  - Solid
- Fluent-style controls and custom dropdown visuals.
- Live snapshot display from the host.
- IPC commands for all mutable host actions.
- Notifications for plugin and scan operations.
- Confirmation dialogs for destructive actions.

The UI is intentionally dark-only. Theme switching was removed to keep visuals consistent and reduce the maintenance cost of supporting multiple color systems.

## 8. Dashboard Page

The Dashboard is a status-oriented page. It summarizes the current runtime state without exposing detailed controls.

Displayed information:

- Current audio backend.
- Current stream format:
  - sample rate
  - buffer size
- Current input and output device, or a single device label for ASIO.
- Realtime input meter.
- Realtime output meter.
- Active plugin count.
- Installed plugin count.

The audio meters are driven by telemetry from the host. The host reports current peak levels from the realtime processor, and the UI renders those levels as segmented meters.

The Dashboard is intended for quick validation:

- Is the host online?
- Which backend is active?
- Which device is active?
- Is audio reaching the host?
- Is audio leaving the host?
- How many plugins are installed and active?

## 9. Audio Page

The Audio page configures audio backend, devices, channels, sample rate, and buffer size.

Controls:

- Audio backend dropdown.
- Device selection.
- Input channel toggles.
- Output channel toggles.
- Sample rate dropdown.
- Buffer size dropdown.

Backend behavior:

- For non-ASIO backends, the UI shows separate input and output device selectors.
- For ASIO, the UI shows a single `Device` selector because ASIO drivers are generally opened as one driver endpoint rather than independent input/output devices.

Supported backend types exposed by JUCE on Windows include:

- Windows Audio
- Windows Audio (Exclusive Mode)
- Windows Audio (Low Latency Mode)
- DirectSound
- ASIO

The exact backend list depends on JUCE and the local Windows audio environment.

When an audio option is changed, the UI sends an IPC command to the host. The host applies the change through `AudioDeviceManager`, validates the result, reloads active plugin I/O if needed, and reports success or an error message.

## 10. Plugins Page

The Plugins page manages both the running chain and the installed plugin database.

It has two tabs:

- `Running (N)`
- `Installed (N)`

### Running Plugins

The Running tab displays plugins currently inserted in the live chain.

Columns:

- Order
- Name
- Manufacturer
- Type
- Status
- Actions

Running plugin actions:

- Open editor.
- Duplicate plugin.
- Bypass or enable plugin.
- Remove plugin.

Running plugins can be reordered by dragging a plugin onto another plugin. The current behavior swaps the two positions. For example, dragging plugin 1 onto plugin 5 swaps plugin 1 and plugin 5.

If there are no running plugins, the UI shows an empty state message instead of rendering empty plugin cards.

### Installed Plugins

The Installed tab displays plugins known by the local plugin database.

Columns:

- Name
- Manufacturer
- Type
- Status
- Actions

Installed plugin actions:

- Add to chain.
- Open plugin folder.
- Remove from database.

Installed plugin status can indicate:

- Available
- Running
- Running (bypassed)
- Running (multiple instances)
- Missing
- Error

The installed plugin actions menu also exposes database-level operations:

- Scan
- Scan paths
- Remove missing
- Clear

Destructive operations use confirmation dialogs where needed. For example, clearing the installed database also clears the running chain.

## 11. Settings Page

The Settings page contains app-level preferences and project information.

### General

General settings:

- Start with Windows.
- Close to tray when pressing X.
- Enable VST2 plugins.

The VST2 toggle is runtime-configurable for the app flow, but VST2 support must also be compiled into the binary. If the app was built without VST2 support, enabling the setting cannot load VST2 plugins.

### Appearance

Appearance settings:

- Window material:
  - Mica
  - Mica Alt
  - Acrylic
  - Solid
- App icon:
  - Color
  - White
  - Black

The icon setting updates the in-app branding, the tray icon, and the taskbar/window icon where supported by the runtime.

### Windows Sound Settings

The Settings page includes an action to open Windows Sound Settings for device-level configuration outside the app.

### Debug

Debug controls are only visible when the app is launched with `--debug`.

Debug mode is designed for development and diagnostics. It exposes additional logging through the shared console and can help troubleshoot IPC, plugin loading, audio backend switching, and crash paths.

### Footer

The footer includes:

- Light Host Modern project card.
- Original Light Host project card.
- Repository link buttons.
- Version display.
- Original project credit.

The original project is credited as Light Host by Rolando Islas/OpenCMA.

## 12. Audio Engine

The audio engine is responsible for:

- Audio device management.
- Audio backend selection.
- Input/output device selection.
- Sample rate and buffer selection.
- Input and output channel configuration.
- Plugin format registration.
- Plugin scanning.
- Installed plugin database.
- Active plugin chain.
- Plugin state persistence.
- Audio state persistence.
- Realtime processor snapshot publishing.
- Telemetry and diagnostics.

The engine is built around JUCE:

- `AudioDeviceManager` owns the active audio device setup.
- `AudioPluginFormatManager` owns plugin format support.
- `KnownPluginList` stores scanned plugin descriptions.
- `RealtimeHostProcessor` runs the active plugin chain.

The engine exposes a snapshot model so the UI can read current state without directly touching audio or plugin objects.

## 13. Realtime Processing Model

Realtime audio processing is handled by `RealtimeHostProcessor`.

The processor uses a snapshot model:

- A plugin chain is built outside the audio callback.
- The chain is published as a snapshot.
- The audio callback reads the current snapshot atomically.
- Retired snapshots are cleaned up outside the realtime thread.

This reduces risk in the realtime callback because heavy operations such as plugin instantiation, state restoration, chain rebuilds, and memory ownership changes happen outside the audio thread.

The processor handles:

- Serial plugin processing.
- Empty-chain passthrough.
- Bypassed plugin routing.
- Bypass latency compensation.
- MIDI buffer ownership.
- Scratch buffers.
- Peak-level tracking.
- Plugin process failure tracking.
- Chain latency reporting.

The realtime chain is serial: audio flows through plugin 1, then plugin 2, then plugin 3, and so on.

## 14. Audio Backend and Device Handling

The host exposes Windows audio backends reported by JUCE.

When switching backend:

1. The host receives `set-audio-backend:<index>`.
2. It maps the visible UI index to an available JUCE backend type.
3. It stores the previous backend/device state.
4. It switches the JUCE device type.
5. It scans input and output candidates.
6. It attempts to open a valid device setup.
7. If successful, it saves the new audio state and reloads the active plugin chain.
8. If unsuccessful, it restores the previous audio setup and reports an error message.

ASIO has additional validation because some ASIO drivers expose names but fail to open. The app attempts matching ASIO driver pairs and reports device errors such as `Device didn't start correctly` when the driver fails.

For ASIO, the UI exposes a single device selector because choosing separate ASIO input/output entries can produce invalid mixed driver states.

## 15. Channel Handling

The app tracks active input and output channels.

Channel changes are sent through IPC commands:

- `set-input-channel:<index>:<enabled>`
- `set-output-channel:<index>:<enabled>`
- `set-input-channels:<count>`
- `set-output-channels:<count>`

After channel changes, the host reloads the active plugin chain so plugin buses and processor buffers match the current device configuration.

Channel labels are derived from the current driver/API data when available. Different APIs expose different naming conventions:

- Windows Audio can expose generic input/output channel names.
- DirectSound can expose stereo-style naming such as left/right.
- ASIO can expose driver-specific names from the ASIO driver.

When a driver does not provide rich channel metadata, the app falls back to safe generic labels.

## 16. Plugin Formats

Light Host Modern supports:

- VST3
- VST2, when compiled and enabled

Disabled legacy/cross-platform plugin formats include:

- Audio Unit
- LADSPA
- LV2

VST3 is built with Steinberg VST3 SDK `v3.8.0_build_66`.

VST2 support is compile-time optional and runtime-toggleable:

- Build option `LIGHTHOST_ENABLE_VST2=AUTO`: enable VST2 when headers are available.
- Build option `LIGHTHOST_ENABLE_VST2=ON`: require VST2 headers.
- Build option `LIGHTHOST_ENABLE_VST2=OFF`: build without VST2.
- Default VST2 provider: `XAYMAR`.
- Optional legacy SDK path: `LIGHTHOST_VST2_SDK_DIR`.

Runtime VST2 toggle:

- Enabled: VST2 scanning and loading flow can be used.
- Disabled: VST2 flow is skipped even if compiled.

## 17. Plugin Database

The installed plugin database is a JUCE `KnownPluginList`.

The database stores plugin descriptions, including:

- name
- manufacturer
- plugin format
- file path
- plugin metadata
- plugin I/O information when available

The database is populated by scanning default folders or configured custom scan paths.

Installed plugins can be:

- added to the running chain
- opened in Explorer
- removed from the database
- cleaned up if missing
- cleared entirely

Removing an installed plugin that is currently running also removes its running instance from the chain. The UI warns the user before doing this.

Clearing the installed plugin database also clears the running plugin chain.

## 18. Active Plugin Chain

The active plugin chain is the list of plugin instances currently processing audio.

Supported operations:

- Add installed plugin to chain.
- Add the same plugin more than once.
- Duplicate a running plugin.
- Bypass a plugin.
- Re-enable a bypassed plugin.
- Remove a plugin.
- Open a plugin editor.
- Swap plugin order through drag/drop.

The chain preserves plugin state where possible. State keys were improved so plugin state is less likely to be lost during reorder/remove operations.

When the chain changes:

1. The active plugin descriptions are updated.
2. Plugin states are saved or restored.
3. A new realtime chain snapshot is built.
4. Compatible instances may be reused.
5. The new snapshot is published to the realtime processor.
6. The UI receives updated chain state through snapshots/telemetry.

## 19. Plugin Scanning

The app can scan:

- all known/default plugin locations
- VST-only locations
- VST3-only locations
- custom scan paths

Default scan paths include standard Windows VST/VST3 locations, such as:

- `C:\Program Files\VSTPlugins`
- `C:\Program Files\Steinberg\VSTPlugins`
- `C:\Program Files\Common Files\VSTPlugins`
- `C:\Program Files\Common Files\VST3`
- equivalent `Program Files (x86)` locations where applicable

The Scan Paths modal allows users to:

- view configured scan paths
- edit existing paths
- add a new path
- browse for a folder
- delete a path
- restore default paths after confirmation
- save or cancel changes

Scanning reports how many plugins were discovered, and missing plugin cleanup reports how many entries were removed.

## 20. Plugin State and Persistence

The host persists:

- installed plugin database
- active plugin chain
- plugin states
- audio setup
- startup behavior
- close behavior
- VST2 enable setting
- icon variant
- window material
- failed plugin quarantine data

Plugin state is saved by plugin identity and chain position-aware keys. The app also preserves compatibility with older plugin-state keys where possible.

Settings writes are debounced where appropriate to reduce unnecessary synchronous disk writes during interactive operations.

## 21. IPC Protocol

The host and WinUI shell communicate through a Windows named pipe.

The host owns the pipe server. The UI sends text commands and receives JSON/text responses.

Important commands:

| Command | Purpose |
| --- | --- |
| `snapshot` | Returns full app state for UI rendering. |
| `state-snapshot` | Returns state snapshot data. |
| `telemetry` | Returns lightweight telemetry such as meters and counters. |
| `toggle-bypass:<index>` | Toggles bypass on a running plugin. |
| `remove-plugin:<index>` | Removes a running plugin. |
| `duplicate-plugin:<index>` | Duplicates a running plugin. |
| `move-plugin-up:<index>` | Moves a running plugin one position up. |
| `move-plugin-down:<index>` | Moves a running plugin one position down. |
| `move-plugin-to:<from>:<to>` | Moves a running plugin to another position. |
| `swap-plugin-with:<from>:<to>` | Swaps two running plugins. |
| `open-plugin-editor:<index>` | Opens a plugin editor window. |
| `add-known-plugin:<index>` | Adds an installed plugin to the running chain. |
| `remove-known-plugin:<index>` | Removes an installed plugin from the database. |
| `open-known-plugin-location:<index>` | Opens an installed plugin file location. |
| `remove-missing-known-plugins` | Removes missing installed plugins. |
| `clear-known-plugins` | Clears the installed database and running chain. |
| `set-audio-backend:<index>` | Changes the current backend. |
| `set-audio-input:<index>` | Changes the current input device. |
| `set-audio-output:<index>` | Changes the current output device. |
| `set-sample-rate:<value>` | Changes sample rate. |
| `set-buffer-size:<index>` | Changes buffer size by current UI option index. |
| `set-input-channels:<count>` | Sets active input channel count. |
| `set-output-channels:<count>` | Sets active output channel count. |
| `set-input-channel:<index>:<enabled>` | Enables/disables one input channel. |
| `set-output-channel:<index>:<enabled>` | Enables/disables one output channel. |
| `scan-default-plugins:all` | Scans all default plugin paths. |
| `scan-default-plugins:vst` | Scans default VST paths. |
| `scan-default-plugins:vst3` | Scans default VST3 paths. |
| `scan-plugin-path:<path>` | Scans a specific path. |
| `delete-plugin-states` | Deletes saved plugin state data. |
| `set-start-with-windows:<0|1>` | Sets Windows startup behavior. |
| `set-close-behavior:quit` | Makes window close quit the host. |
| `set-close-behavior:tray` | Makes window close return to tray. |
| `set-enable-vst2:<0|1>` | Enables/disables runtime VST2 flow. |
| `set-tray-icon-mode:<mode>` | Sets icon variant. |
| `quit-host` | Requests host shutdown. |

The UI treats empty responses as a serious failure because they can mean the host process crashed, closed the pipe, or froze while processing a plugin operation.

## 22. UI Refresh and Telemetry

The UI uses two refresh paths:

- Full snapshots for structural state.
- Lightweight telemetry for frequent values.

Full snapshots include:

- current audio backend
- current device configuration
- active plugin list
- known plugin list
- settings
- plugin counts
- status data

Telemetry includes:

- meter levels
- CPU usage
- xRun count
- chain version
- plugin database version
- audio configuration version
- diagnostic counters

The UI avoids unnecessary full snapshot refreshes. It checks version counters and requests full state only when relevant state changed.

Automatic snapshot refresh is paused while dropdowns are open so the UI does not close or reset interactive controls while the user is selecting values.

## 23. Settings Storage

Settings are stored in the user's application data area under the Light Host Modern settings directory.

The reset script targets:

```text
%APPDATA%\Light Host Modern
```

Important files include:

- `Light Host Modern.settings`
- `RecentlyCrashedPluginsList`

The app can reset settings through:

```powershell
& ".\Light Host Modern.exe" --reset-settings
```

or through:

```powershell
.\Utilities\Reset Settings.bat
```

## 24. Startup, Tray, and Close Behavior

Startup behavior:

- When enabled, the app registers itself to start with Windows.
- The setting is controlled from the Settings page.

Close behavior:

- Quit when pressing X.
- Close to tray when pressing X.

When close-to-tray is enabled, closing the UI does not stop audio hosting. The host remains in the tray.

The tray context menu always provides a direct quit option.

## 25. App Icons

The app uses icon assets from `Icon/`.

Icon variants:

- Color
- White
- Black

The selected icon variant is used for:

- sidebar branding
- in-app icon
- tray icon
- taskbar/window icon where supported

The release setup and portable executables also use the app icon.

## 26. Debugging and Recovery

Debug mode:

```powershell
& ".\Light Host Modern.exe" --debug
```

Debug mode logs:

- host startup
- WinUI startup
- named pipe path
- IPC requests/responses
- audio backend switching
- device setup attempts
- ASIO device errors
- plugin scanning
- plugin add/remove/bypass/reorder actions
- plugin loading
- active chain rebuilds
- crash diagnostics

Recovery options:

```powershell
& ".\Light Host Modern.exe" --safe-mode
& ".\Light Host Modern.exe" --reset-settings
& ".\Light Host Modern.exe" --clear-failed-plugins
```

Use cases:

- `--safe-mode`: start without restoring unsafe plugin state.
- `--reset-settings`: recover from corrupted or bad settings.
- `--clear-failed-plugins`: retry plugins that were previously quarantined.

## 27. Crash Diagnostics and Stability Guards

The app includes stability work around common plugin-host problems.

Stability features:

- Crash diagnostics installed during host startup.
- Failed plugin quarantine.
- Empty response detection in the UI IPC client.
- Native exception diagnostics around plugin load/state restore paths.
- Safe plugin state restoration flow.
- Process failure tracking in realtime processing.
- Realtime snapshot retirement outside the audio callback.
- Audio device recovery attempts.
- ASIO device validation and fallback attempts.

Important plugin-host reality: a plugin can crash the host process during instantiation, state restoration, editor opening, or processing. Light Host Modern adds diagnostics and recovery paths, but third-party plugin crashes can still terminate the native host if they occur inside plugin code.

## 28. Release Packages

The release script generates two release artifacts:

```text
out\release\LightHostModern-Setup.exe
out\release\LightHostModern-Portable.exe
```

### Installer

The setup executable:

- Shows a native Windows install prompt.
- Installs per-user.
- Installs under:

```text
%LOCALAPPDATA%\Programs\Light Host Modern
```

- Creates Start Menu shortcut.
- Creates Desktop shortcut.
- Adds uninstall registry entry.
- Includes bundled payload and required runtime files where available.

### Portable

The portable executable:

- Is a self-extracting executable.
- Contains the full app payload.
- Extracts to a temporary payload directory.
- Launches the app from the extracted location.

The portable package is designed for easy distribution and testing without running a traditional installer.

## 29. Build Requirements

Recommended environment:

- Windows 11.
- Visual Studio 2022 or newer.
- Desktop development with C++ workload.
- MSVC x64 toolchain.
- MSBuild.
- Windows SDK `10.0.22621.0` or newer.
- CMake `3.22` or newer.
- Git.
- Windows App SDK / WinUI 3 tooling.

The current CMake preset uses:

```text
Visual Studio 17 2022
x64
Windows SDK 10.0.22621.0
```

Automatically fetched dependencies:

- JUCE `8.0.13`
- Steinberg VST3 SDK `v3.8.0_build_66`
- ASIO SDK from `audiosdk/asio`
- Xaymar `vst2sdk` `v0.4.0` when VST2 support is enabled through the default provider

## 30. Build Commands

Build the host and dependencies:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1"
```

Build Debug:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -Configuration Debug
```

Force VST2 on:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
```

Build release artifacts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Release.ps1"
```

Run Release:

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe"
```

Run Release with debug console:

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --debug
```

Primary host output:

```text
out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe
```

Release package output:

```text
out\release\LightHostModern-Setup.exe
out\release\LightHostModern-Portable.exe
```

## 31. Repository Layout

```text
Source/
  Native host, JUCE app, audio engine, plugin chain, tray, IPC, diagnostics.

WinUI/LightHost.WinUI/
  Native WinUI 3 shell, pages, controls, UI-side IPC, visual state.

Icon/
  App icon source files and variants used by app, tray, taskbar, setup, and portable packages.

ThirdParty/
  Third-party compatibility shims and notices.

Utilities/
  Build Windows.ps1
  Build Release.ps1
  Reset Settings.bat

CMakeLists.txt
  Native host build definition, dependencies, JUCE/VST/ASIO settings.

CMakePresets.json
  Visual Studio 2022 x64 CMake preset.

README.md
  Public-facing project overview, improvements, fixes, and development guide.

documentation.md
  Detailed project wiki and implementation guide.
```

## 32. Development Notes

Important build options:

| Option | Purpose |
| --- | --- |
| `LIGHTHOST_JUCE_DIR` | Use a local JUCE checkout instead of FetchContent. |
| `LIGHTHOST_VST3_SDK_DIR` | Use a local VST3 SDK checkout. |
| `LIGHTHOST_ASIO_SDK_DIR` | Use a local ASIO SDK checkout. |
| `LIGHTHOST_ENABLE_VST2` | `AUTO`, `ON`, or `OFF`. |
| `LIGHTHOST_VST2_PROVIDER` | `XAYMAR` or `LEGACY`. |
| `LIGHTHOST_VST2_SDK_DIR` | Use a local VST2 SDK path. |

Important compile definitions:

- `JUCE_ASIO=1`
- `JUCE_ASIO_USE_EXTERNAL_SDK=1`
- `JUCE_DIRECTSOUND=1`
- `JUCE_WASAPI=1`
- `JUCE_PLUGINHOST_VST3=1`
- `JUCE_PLUGINHOST_VST` when VST2 is enabled
- `JUCE_PLUGINHOST_AU=0`
- `JUCE_PLUGINHOST_LADSPA=0`
- `JUCE_PLUGINHOST_LV2=0`

The project is built as a Windows GUI app and outputs:

```text
Light Host Modern.exe
```

The WinUI project is copied into the host output after build so the tray host can launch the UI.

## 33. Operational Notes

### ASIO4ALL

ASIO4ALL may enumerate as an ASIO driver but fail to start depending on its control panel configuration, exclusive device ownership, Windows device state, or conflicts with another audio client. When this happens, the host can report:

```text
Device didn't start correctly
```

This is reported by the driver/device setup path. The host logs the selected backend, input/output candidates, setup attempt, error message, and selected device state when debug mode is enabled.

### Plugin Crashes

Some plugins can crash the host during:

- plugin instantiation
- state restoration
- editor creation
- realtime processing

Use `--safe-mode` if a saved chain crashes on startup.

Use `--clear-failed-plugins` if a quarantined plugin should be retried.

### VST2

VST2 support requires both:

- compile-time support in the binary
- runtime enablement in Settings

If VST2 is disabled at build time, the runtime toggle cannot add VST2 support by itself.

### Multiple UI Windows

The host prevents multiple WinUI shells from running at the same time. If the UI is already open, a subsequent open request focuses the existing window.

### Debug Console

The debug console is only opened when `--debug` is passed. Normal app launch does not show a console window.

## 34. Credits and License Lineage

Light Host Modern is a Windows-focused fork updated and modernized by Matheus Heidemann.

Original Light Host project:

```text
https://github.com/opencma/LightHost
```

Original project by Rolando Islas/OpenCMA.

The project follows the license lineage of the original Light Host project. When redistributing, keep the original copyright, license notices, and source availability requirements.
