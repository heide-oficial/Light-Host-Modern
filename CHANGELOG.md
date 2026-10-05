# Changelog

## [2.0.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v2.0.0) — 2026-10-05

Changes compared with **v1.4.1**. See the [release notes](docs/release-2.0.0-notes.md).

### Added

- **Chain mode:** build audio paths visually with plugins, hardware inputs/outputs and mixers, including parallel paths and latency compensation. List remains available for serial setups; changing mode requires restarting the app.
- **Routing canvas:** connect ports by dragging or keyboard, insert plugins into existing wires, select and move several cards, and use search, zoom, pan, Fit, Organize and undo/redo. Customize card sizes, layers, colors, channel colors, the dotted background and animated audio flow.
- **Built-in mixers:** combine and split audio paths, adjust gain and mute, and add or remove stereo input/output pairs.
- **Profiles:** save, activate, edit, duplicate and overwrite List or Chain setups, optionally including audio-device settings. Each mode has a protected default profile, and switching profiles prompts you to save or discard unsaved changes.
- **Plugin audio channels:** configure supported mono/stereo bus layouts, sidechains and auxiliary outputs, and choose which ports appear on the canvas.
- **Experimental process isolation:** run individual plugins in separate processes, inspect their resource use and retry a failed instance. See the explanation below.
- **Tray Quick Access:** open plugin editors, manage running instances and add installed plugins directly from the notification area. The Performance submenu shows CPU, RAM and VRAM usage.
- **Dashboard information:** operating mode, active profile, CPU, RAM, VRAM and app version, alongside the existing audio and plugin information.
- **Customization and notifications:** rename audio devices/channels, color running plugin cards, hide the sidebar's collapse control, and configure Performance mode, edge fades, hover help and separate action/error/update notifications. Windows update notifications and background checks also work with the main window closed to the tray.
- **Portable updates inside the app:** apply a new version and recover the previous version if the new launch is not confirmed. Release manifests authenticate update packages before application.

### Removed

- The flat portable folder layout, replaced by a root launcher and complete application versions inside `versions/`.
- Opening the main window immediately with a left-click on the tray icon. Both mouse buttons now open Quick Access, which includes **Open app UI**.
- Separate **Restore original name** entries in plugin context menus. The action remains available inside **Rename** dialogs.

### Improved

- Tray menus follow the app theme, Windows high-contrast settings, monitor DPI and accessibility text size.
- Plugin autosave captures changed instances and moves encoding and file writes outside the audio suspension, reducing the time audio needs to pause while saving.
- Audio uses explicit **Stereo/Mono** selectors instead of mono on/off switches, while retaining separate input/output settings and Individual/Pairs channel selection.
- Maintenance actions are grouped under a collapsible **Danger zone**, with confirmation and a three-second delay. It also offers restoring original names, deleting profiles and resetting the app.
- Session-saving feedback uses a small status icon with a hover explanation instead of a persistent text label.

### Fixed

- Normal shutdown leaving the interface process alive after the host had stopped.
- Opening an already minimized plugin editor failing to restore its window.
- Rapid audio-setting changes being dropped while another change was being applied.
- Clicking a partially selected channel pair disabling its channels instead of enabling the whole pair.
- MSI upgrades failing to retain a custom installation folder.

### Migrating from 1.x to 2.0.0

- **Installed version:** install the 2.0.0 MSI over your existing installation. Your settings are retained.
- **Portable version:** extract the complete 2.0.0 ZIP into a new folder and launch the root `LightHostModern.exe`. Keep `versions/` intact; do not overwrite only the executable in the old portable folder. Settings remain in the existing user profile.
- **Existing setup:** your current plugins become a **My plugins** profile. List keeps the familiar serial workflow; a new Chain starts without wires and needs connections before it passes audio.
- Back up your settings before upgrading. New profiles and channel configurations cannot all be read by older versions; reinstalling an older app does not convert those settings back.

### Experimental process isolation

Choose **Run in a separate process (experimental)** from a plugin's menu to give that instance its own worker process. This can keep a plugin crash from closing the entire host. If it fails, **Retry loading** starts it again using its last captured state. Choose **Run inside the host** to return to normal execution.

Isolation adds CPU and memory overhead and extra audio latency for each isolated plugin. It adds two worker blocks of delay, plus the plugin's own latency; several isolated plugins in sequence add their delays together.

A worker can miss its deadline under load. Effects then use delayed unprocessed audio, while instruments output silence for the missing block. Compatibility varies between plugins and drivers, so the feature remains experimental. Only plugins explicitly set to run separately receive this protection, and their processes retain the user's normal Windows permissions.

[Full changelog](https://github.com/heide-oficial/Light-Host-Modern/blob/v2.0.0/CHANGELOG.md) · [Changes since v1.4.1](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.4.1...v2.0.0)

## [1.4.1](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.4.1) — 2026-09-23

### Added

- Detailed troubleshooting logs in Diagnostics. Enable capture, restart when ready, reproduce the problem, then stop and save a TXT file through the Windows save dialog. Logs stay on the computer and are never sent automatically.
- A Sidebar on open preference under Settings > Appearance, applied both at startup and when reopening the interface from the tray.

### Removed

- None.

### Improved

- Faster plugin scanning through buffered file hashing, fewer repeated VST3 factory scans, combined discovery and validation for single-class VST3 modules, and smaller incremental cache writes.
- More reliable scan progress and time limits, with successful results retained for cancellation, retries and interrupted scans.
- Clearer scan failure details, including missing paths, incompatible architecture, class identity and loading failures.
- Localized troubleshooting controls with restart confirmation, cancelled-export recovery, capture size limits and masking of personal-folder paths in logs.

### Fixed

- VST3 bundle and binary paths being treated as different modules, and incomplete class-ID comparisons rejecting valid scan results.
- Productive scanner workers timing out despite making progress, and retries retaining outdated root failures or repeating completed work unnecessarily.
- Ending the interface through Windows End task leaving the audio host running. Normal close-to-tray behavior is preserved.

[Full comparison with v1.4.0](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.4.0...v1.4.1).

## [1.4.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.4.0) — 2026-09-21

### Added

- Independent Individual/Pairs selection for input and output channels, including partially selected pairs and device-specific preferences.
- Optional Mix inputs to mono before the plugin chain and independent Main output to mono monitoring for physical outputs 1 + 2, with smooth transitions and preserved auxiliary outputs.
- Whole-app CPU, resident RAM and committed memory readings, with English and Brazilian Portuguese explanations for every Diagnostics metric.
- Current dBFS readings beside the Dashboard's existing bar meters and clear notices when the selected audio device is unavailable.

### Removed

- Automatic fallback to a different device when audio recovery is Disabled.
- Unrestricted recursive cleanup of legacy installation folders; migration now backs up and verifies known app files while retaining unknown files and user settings.

### Improved

- Standardized current app names, executables, project folders and preference paths as LightHostModern, with migration and compatibility for older installations and update clients.
- Reorganized Audio into Devices, Format, side-by-side Input/Output settings, and separate Input/Output channel lists. Mono and selection controls align to the right; Settings is now the last sidebar item.
- More sensitive logarithmic meters, smooth decay and short peak retention. Fixed-width dBFS fields sit to the left of the bars without shifting them as values change.
- Versioned installer filename, LightHostModern-1.4.0-Setup.msi, alongside the identical LightHostModern-Setup.msi alias required by older updaters.
- Updated application documentation, screenshots and contributor credits for [k-ross's PR #5](https://github.com/heide-oficial/Light-Host-Modern/pull/5) and [Log1cFX's issue #6](https://github.com/heide-oficial/Light-Host-Modern/issues/6).

### Fixed

- Device creation and recovery silently substituting another backend or input/output device when the selected configuration cannot open.
- Diagnostics hover explanations being replaced during live updates, preventing tooltips from appearing reliably.
- Interrupted preference migration replaying after an intentional reset, and unsafe removal of files left by older installers.

[Full comparison with v1.3.1](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.3.1...v1.4.0).

## [1.3.1](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.3.1) — 2026-09-13

### Added

- None.

### Removed

- None.

### Improved

- None.

### Fixed

- Corrected the overly narrow Compact layout by increasing its maximum content width from 780 to 1000 device-independent pixels, while preserving aligned headers, cards and plugin toolbars, responsive sizing, and scrollbars at the window edge.

[Full comparison with v1.3.0](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.3.0...v1.3.1).

## [1.3.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v1.3.0) — 2026-09-12

### Added

- Global output mute and latency-compensated chain bypass in the Running toolbar and notification-area menu.
- Custom names for running instances and installed plugins, original-name restoration, and inherited names when adding plugins to the chain.
- Plugin details with original identity and bus information, instance position swapping, manufacturer grouping, and visual status badges.
- A dedicated Diagnostics page for DSP/process CPU, audio reliability, stream format, latency, and processing activity. Its General setting can hide the page and stop collection after confirmation.
- Isolated plugin scanning with progress, cancellation, readable per-path failures, and selected or complete retries.
- Versioned session files with atomic writes, recoverable backups, and migration of existing chains and plugin states.

### Removed

- Plugin discovery inside the audio host process; a separate scanner worker now handles each module.
- The old plugin-database action menu. Scanning and folder management share Scan for plugins; Remove missing and Clear database are in Settings.
- Legacy duplicate-instance identity workarounds and blocking host/UI message handling.

### Improved

- Redesigned Running and Installed views with consistent cards, search/action toolbars, grouped context menus, and connected manufacturer groups.
- More responsive dashboard peak meters, refreshed independently at up to 20 Hz while the Dashboard is visible.
- Compact layout alignment, separate input/output channel cards, checkbox and status alignment, scrolling, modal sizing, Windows materials, and repository buttons.
- Asynchronous host/UI communication, lazy page creation, and virtualized plugin lists that preserve selection, focus, and scroll position.
- Audio processing for larger channel layouts and callback blocks, smoother bypass/mute transitions, and coordinated plugin preparation and state capture.
- Device recovery that preserves unavailable preferred devices and respects enabled backend/device restrictions.
- Incremental scanning with cached metadata and preservation of completed results after cancellation or failure.
- Cancellable, streamed update downloads with progress, package verification, portable ZIP support, and orderly host shutdown before MSI updates.
- Release packaging that keeps host, UI, scanner, and update helper together and checks the exact portable payload.

### Fixed

- Initial high-DPI windows extending beyond the monitor work area.
- Stale WinUI files being reused in portable packages after interface-only changes.
- Outdated Windows executable version metadata surviving incremental builds; packaging now verifies all four app executables against the release version.
- Truncated IPC messages, late callbacks accessing expired state, and commands targeting the wrong instance after sorting or reordering.
- Duplicate instances losing source state or bypass settings, and empty saved state falling back to stale legacy values.
- Driver fallback bypassing device restrictions or erasing a saved preferred configuration after initialization errors.
- VST3 bundle identifiers being rejected, VST2 scans entering other plugin bundles, and missing scan paths failing silently.
- Plugin preparation/state capture overlapping audio callbacks, and valid plugins being skipped for large channel layouts or oversized blocks.

[Full comparison with v1.2.2](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.2.2...v1.3.0).
