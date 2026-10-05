# LightHostModern 2.0.0

2.0.0 adds visual audio routing, saved profiles and tray Quick Access while keeping the familiar List workflow.

Changes compared with **v1.4.1**.

### 🆕 Added

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

### 🗑️ Removed

- The flat portable folder layout, replaced by a root launcher and complete application versions inside `versions/`.
- Opening the main window immediately with a left-click on the tray icon. Both mouse buttons now open Quick Access, which includes **Open app UI**.
- Separate **Restore original name** entries in plugin context menus. The action remains available inside **Rename** dialogs.

### ⚡ Improved

- Tray menus follow the app theme, Windows high-contrast settings, monitor DPI and accessibility text size.
- Plugin autosave captures changed instances and moves encoding and file writes outside the audio suspension, reducing the time audio needs to pause while saving.
- Audio uses explicit **Stereo/Mono** selectors instead of mono on/off switches, while retaining separate input/output settings and Individual/Pairs channel selection.
- Maintenance actions are grouped under a collapsible **Danger zone**, with confirmation and a three-second delay. It also offers restoring original names, deleting profiles and resetting the app.
- Session-saving feedback uses a small status icon with a hover explanation instead of a persistent text label.

### 🔧 Fixed

- Normal shutdown leaving the interface process alive after the host had stopped.
- Opening an already minimized plugin editor failing to restore its window.
- Rapid audio-setting changes being dropped while another change was being applied.
- Clicking a partially selected channel pair disabling its channels instead of enabling the whole pair.
- MSI upgrades failing to retain a custom installation folder.

### 🔄 Migrating from 1.x to 2.0.0

- **Installed version:** install `LightHostModern-2.0.0-Setup.msi` over your existing installation. Your settings are retained.
- **Portable version:** extract the complete `LightHostModern-v2.0.0-Portable.zip` into a new folder and launch the root `LightHostModern.exe`. Keep `versions/` intact; do not overwrite only the executable in the old portable folder. Settings remain in the existing user profile.
- **Existing setup:** your current plugins become a **My plugins** profile. List keeps the familiar serial workflow; a new Chain starts without wires and needs connections before it passes audio.
- Back up your settings before upgrading. New profiles and channel configurations cannot all be read by older versions; reinstalling an older app does not convert those settings back.

If you downloaded the original unversioned 2.0.0 portable ZIP, download and extract the versioned package once. Its corrected updater recognizes the versioned packages used by future releases; the application version remains 2.0.0.

For source, use GitHub's automatic **Source code (zip)** or **Source code (tar.gz)** downloads for this tag. The [build guide](https://github.com/heide-oficial/Light-Host-Modern/blob/v2.0.0/docs/build-and-release.md#building-from-github-source-downloads) explains the required tools, pinned dependency downloads and automatically applied JUCE patches.

### 🧪 Experimental process isolation

Choose **Run in a separate process (experimental)** from a plugin's menu to give that instance its own worker process. This can keep a plugin crash from closing the entire host. If it fails, **Retry loading** starts it again using its last captured state. Choose **Run inside the host** to return to normal execution.

Isolation adds CPU and memory overhead and extra audio latency for each isolated plugin. It adds two worker blocks of delay, plus the plugin's own latency; several isolated plugins in sequence add their delays together.

A worker can miss its deadline under load. Effects then use delayed unprocessed audio, while instruments output silence for the missing block. Compatibility varies between plugins and drivers, so the feature remains experimental. Only plugins explicitly set to run separately receive this protection, and their processes retain the user's normal Windows permissions.

[Full changelog](https://github.com/heide-oficial/Light-Host-Modern/blob/v2.0.0/CHANGELOG.md) · [Changes since v1.4.1](https://github.com/heide-oficial/Light-Host-Modern/compare/v1.4.1...v2.0.0)
