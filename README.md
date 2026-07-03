# Light Host Modern

Light Host Modern is a Windows-focused fork of the original [Light Host](https://github.com/opencma/LightHost), updated for current Windows audio workflows, modern C++ builds, WinUI 3, VST3, VST2, ASIO, and Windows 11-style desktop usage.

The project keeps the original goal of being a small, practical audio plugin host, but removes the old cross-platform tray-menu workflow and replaces it with a native Windows application experience.

<p align="center">
  <img src="https://i.imgur.com/sEBRC3e.png" alt="Light Host Modern dashboard" width="90%" />
</p>

<p align="center">
  <img src="https://i.imgur.com/S0SbkSI.png" alt="Light Host Modern audio settings" width="90%" />
</p>

<p align="center">
  <img src="https://i.imgur.com/0NKJ9mP.png" alt="Light Host Modern plugins view" width="90%" />
</p>

<p align="center">
  <img src="https://i.imgur.com/r4lECTa.png" alt="Light Host Modern settings view" width="90%" />
</p>

## What Light Host Modern Is

Light Host Modern is a standalone audio plugin host for Windows. It can load VST/VST3 effects, route audio through an active plugin chain, and run from a lightweight tray-host process with a dedicated WinUI 3 interface.

This fork is maintained as a Windows-only application. Linux and macOS support from the original project were removed so the codebase can focus on Windows audio APIs, ASIO behavior, Windows App SDK, and a native Windows 11 interface.

## What Light Host Is For

Light Host is useful when you want to run audio plugins outside a DAW. Typical use cases include:

- Applying VST/VST3 effects to live audio.
- Building a simple processing chain for microphones, instruments, virtual audio devices, or system routing.
- Hosting lightweight plugin chains without opening a full DAW.
- Testing plugins, audio devices, routing, sample rates, buffers, and ASIO/Windows Audio behavior.

## Improvements

- Windows-only modernization with CMake, Visual Studio 2022 builds, x64 Release/Debug presets, and Windows SDK-based tooling.
- Updated audio/plugin foundation from the legacy JUCE 4-era codebase to JUCE 8.0.13.
- Updated VST3 support to Steinberg VST3 SDK `v3.8.0_build_66`.
- Added configurable VST2 host support through the Xaymar VST2 header provider, with optional legacy SDK discovery.
- Added ASIO SDK integration and improved ASIO backend selection for modern Windows audio setups.
- Replaced the old tray-menu-first UI with a native WinUI 3 interface.
- Added a Windows 11-style dark UI with sidebar navigation, Dashboard, Audio, Plugins, and Settings pages.
- Added native controls for audio backend, device selection, active channels, sample rate, and buffer size.
- For ASIO, the UI now treats the driver as a single `Device` selection instead of exposing invalid separate input/output selectors.
- Added live input/output audio meters to the Dashboard.
- Added running and installed plugin views with plugin counts, manufacturer, format, status, and contextual actions.
- Added support for multiple instances of the same plugin in the running chain.
- Added drag-and-drop reordering for the running plugin chain.
- Added plugin duplication, bypass, editor opening, removal, installed-plugin deletion, missing-plugin cleanup, and plugin database clearing.
- Added configurable plugin scan paths with default Windows VST/VST3 locations and editable scan path management.
- Added Windows startup and close-to-tray behavior settings.
- Added dark Windows material options: Mica, Mica Alt, Acrylic, and Solid.
- Added selectable app/tray icon variants using the new app icon set.
- Added native installer and portable release packaging.
- Added per-user installer behavior with shortcuts and an uninstall entry.
- Added a portable self-extracting executable that contains the full app payload.
- Added conditional `--debug` logging for host, WinUI, IPC, plugin loading, audio device selection, and crash diagnostics.
- Added `--safe-mode`, `--reset-settings`, and `--clear-failed-plugins` recovery options.

## Bug Fixes

The fork also fixes several operational problems present in the legacy Light Host workflow:

- Fixed startup crashes caused by bad saved settings or broken saved plugin chains.
- Invalid settings XML is now discarded per setting key instead of breaking the whole configuration.
- Missing plugins, failed plugins, and plugins with unsupported I/O can be skipped without crashing the app.
- Added failed-plugin quarantine and a command to clear quarantined plugins.
- Improved plugin state keys so plugin state survives reorder/remove operations more reliably.
- Kept compatibility with older plugin-state keys where possible.
- Fixed plugin reorder/remove behavior that could lose or swap plugin state.
- Fixed empty-chain and bypass routing so input can still pass to output when no active plugin is processing.
- Improved mono and non-standard channel routing support.
- Reworked the audio path into a serial realtime processor using immutable chain snapshots.
- Added bypass latency compensation so bypassed plugins keep chain latency aligned.
- Reused compatible plugin instances across chain rebuilds and reorder operations.
- Retired old plugin snapshots outside the audio callback to reduce realtime-thread risk.
- Added process-failure protection and deferred plugin failure logging outside the audio callback.
- Reduced unnecessary graph/device rebuilds during common actions.
- Debounced settings persistence and reduced synchronous disk writes during interactive plugin operations.
- Added device recovery attempts for stopped or failed audio devices after sleep, restart, or Windows Audio changes.
- Improved Windows Audio, DirectSound, and ASIO backend/device diagnostics.
- Fixed ASIO backend switching to validate that a selected driver actually opens before saving the audio state.
- Added fallback attempts across available matching ASIO driver pairs.
- Restored the previous audio setup when an ASIO device change fails or opens the wrong driver.
- Prevented invalid mixed ASIO input/output device states by treating ASIO as a single driver selection.
- Reduced UI snapshot traffic by adding backend version counters and requesting full snapshots only when state changes.
- Prevented plugin database clearing from leaving stale running plugin instances.
- Added explicit plugin-load failure responses so the UI can report load failures instead of silently losing the host connection.

## Development Guide

### Requirements

Install the following on Windows:

- Windows 11 is recommended. Windows 10 `10.0.17763` or newer is the current WinUI project minimum.
- Visual Studio 2022 or newer with the **Desktop development with C++** workload.
- MSVC x64 toolchain and MSBuild.
- Windows SDK `10.0.22621.0` or newer. The WinUI project currently targets `10.0.28000.0` when available.
- CMake `3.22` or newer.
- Git, because CMake fetches third-party dependencies.
- Windows App SDK / WinUI 3 tooling through Visual Studio/NuGet restore.

The CMake build fetches these dependencies automatically unless local paths are provided:

- JUCE `8.0.13`
- Steinberg VST3 SDK `v3.8.0_build_66`
- ASIO SDK from `audiosdk/asio`
- Xaymar `vst2sdk` `v0.4.0` when VST2 support is enabled through the default provider

### Build the Windows Host

Use the helper script from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1"
```

By default this builds Release x64 with the `windows-vs2022` CMake preset.

Useful options:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 OFF
```

Main build output:

```text
out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe
```

### Build the WinUI Shell

The release script builds the WinUI project automatically. For manual work, build:

```text
WinUI\LightHost.WinUI\LightHost.WinUI.sln
```

or build the project with MSBuild for `x64` and `Debug` or `Release`.

The host expects the WinUI executable beside the host output under:

```text
WinUI\x64\<Configuration>\LightHost.WinUI\LightHostWinUI.exe
```

### Run Locally

Run the host executable:

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe"
```

Run with debug logging:

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --debug
```

Recovery options:

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --safe-mode
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --reset-settings
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --clear-failed-plugins
```

### Create Release Artifacts

Generate the installer and portable executable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Release.ps1"
```

Outputs:

```text
out\release\LightHostModern-Setup.exe
out\release\LightHostModern-Portable.exe
```

The setup executable installs per-user under:

```text
%LOCALAPPDATA%\Programs\Light Host Modern
```

The portable executable self-extracts to a temporary payload folder and launches the app from there.

### VST2 Notes

VST2 support is compile-time optional and runtime-toggleable:

- `LIGHTHOST_ENABLE_VST2=AUTO` enables VST2 when headers are available.
- `LIGHTHOST_ENABLE_VST2=ON` requires headers and fails configure if unavailable.
- `LIGHTHOST_ENABLE_VST2=OFF` builds without VST2.
- The default provider is `XAYMAR`.
- A legacy SDK path can be provided with `-Vst2SdkDir` or `LIGHTHOST_VST2_SDK_DIR`.

Example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
```

### Project Layout

```text
Source\                         Host, audio engine, IPC, tray, plugin runtime
WinUI\LightHost.WinUI\          Native WinUI 3 shell
Icon\                           App and tray icon sources
ThirdParty\                     Compatibility shims and third-party notices
Utilities\Build Windows.ps1     Local Windows build script
Utilities\Build Release.ps1     Installer and portable release builder
changelog-*.md                  Internal development changelogs
```

## Credits

Light Host Modern is a Windows-focused fork updated and modernized by Matheus Heidemann.

Original Light Host project by Rolando Islas / OpenCMA: <https://github.com/opencma/LightHost>

This project follows the license lineage of the original Light Host project. Keep the original copyright, license notices, and source availability requirements when redistributing.
