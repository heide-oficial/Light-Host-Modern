# Settings

Settings contains persistent application preferences and system actions. Host-owned settings are sent over IPC; interface-only preferences are stored by the WinUI shell.

## Updates

When a newer stable release is available, the card at the top of Settings offers **Update options**:

- **Update in app** downloads and verifies the matching installed or portable package, saves the session, stops audio, applies the update, and reopens the app. Settings are retained. **Cancel** is available during download; it is disabled when preparation begins.
- **GitHub download** opens the package in the default browser, or the release page when a matching package is unavailable.

**Update in app** is disabled when a compatible, authenticated package is unavailable. The browser option remains available for the release page.

The next launch reports the update result and offers **View update log**. In-app portable updates require the versioned portable layout and an NTFS volume. Older flat portable folders need a fresh extraction of the complete ZIP; keep its root launcher and `versions/` folder together. See [Update contract](update-contract.md) for package verification, recovery, and failure handling.

## Operating mode

The **Plugin mode** control selects List or Chain. Changes require a host restart, with Restart now/later and a
clickable pending indicator. Profiles are applied from the Profiles page, which
shows only profiles for the current mode. Appearance > Always expand Chain (on by
default, shown in Chain mode) keeps Plugins at full width even with Compact layout.
See [Operating modes and profiles](chain-and-profiles.md).

In Chain mode, **Maximum space between elements** limits the gap when cards are moved away from the graph. It defaults to 2,400 canvas units, with a range of 600–12,000. Reducing it also brings existing distant cards closer. This setting is hidden in List mode.

## General

### Start with Windows

Registers the app for the current user under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, with `--startup`. Installed builds use the host executable; versioned portable builds use the stable `LightHostModern.exe` launcher in the portable root. Moving the portable folder requires registering its new location by toggling this option off and on.

### Close to tray

Enabled by default. Keeps the audio host and plugin chain running when the WinUI window is closed. The interface can be reopened from the notification-area icon. Disabling this option makes normal window closing save the session and terminate the host. Windows taskbar **End task** and an abrupt UI crash end the whole app regardless of this preference.

### Enable VST2 plugins

Enables VST2 on the next host startup when VST2 support exists in the build. The default is off. Changing this option displays **Restart this session to apply the changes**; closing and reopening only the UI is not enough. VST3 remains available independently. Builds without VST2 support disable this control.

### Diagnostics

Enabled by default. Shows Diagnostics after Profiles in the sidebar. Disabling it asks for confirmation and stops normal performance measurements, including Dashboard and tray resource readings. Audio processing and the Dashboard peak meters continue. The page is hidden unless a detailed-log capture still needs attention; log collection and export are managed separately on [Diagnostics](diagnostics.md).

## Danger zone

Enable **Show Danger zone options** to reveal these actions. Each action has its own confirmation with a three-second delay:

- **Remove missing plugins** / **Remove missing** removes database entries whose plugin files are no longer available.
- **Clear plugin database** / **Clear database** clears the installed database and running chain.
- **Restore all original names** / **Restore** removes custom device, channel, plugin and element names, including names in saved profiles.
- **Delete all profiles** / **Delete all** removes saved user profiles, keeps both built-in defaults, and activates the empty default for the current mode. The current plugin chain stops.
- **Restore all default settings** / **Reset** resets app preferences, profiles, plugin database, and audio/appearance settings, then restarts the app. Installed plugin files and exported logs are retained.

Scanning and folder management are available from **Plugins > Installed > Scan for plugins** in List mode and **Add plugin > Scan for plugins** in Chain mode. Danger zone appears near the bottom of Settings, before About.

## Audio recovery

### Device persistence

- **Disabled** opens only the saved device and permits one restart of that same device after an interruption. It never substitutes another device or backend.
- **Last selected device** retries the most recently selected working configuration.
- **Custom device** retries a backend and device explicitly selected in the preferred-device dialog.

### Retry interval

Sets the number of seconds between preferred-device attempts, from 1 to 60.

### Maximum attempts

Limits consecutive failures to 1–100 attempts. Recovery pauses after the limit instead of retrying forever. **Retry now** starts another immediate attempt.

### Preferred device

Opens a modal for choosing the backend and corresponding device. ASIO uses one driver selection; other backends can expose separate input and output choices. **Save** commits the selection and **Cancel** discards it.

### Enabled devices

**Manage enabled devices** opens a compact scrolling dialog for allowing or blocking detected backends and their input/output choices. It also offers device renaming. The backend selector and device groups scroll together. **Save** applies the batch; **Cancel** discards it. A changed device inventory requires refreshing the dialog. Blocked choices are never selected manually or by recovery.

See [Persistence and recovery](persistence-and-recovery.md) for the complete state machine.

## Appearance

### Theme

Select **System**, **Light**, or **Dark**. The tray menu and its submenus use the same preference on each opening, even while the main window is closed. Windows contrast themes are applied automatically.

### Language

Selects a JSON catalogue discovered from the `Locales` directory. The visible interface updates without restarting. Missing keys fall back to English. The native tray currently embeds English and Brazilian Portuguese; adding a UI JSON catalogue alone does not translate the tray. See [Localization](localization.md).

### Layout mode

- **Compact** applies one consistent maximum content width across pages.
- **Expanded** uses the available width while keeping cards and controls responsive.

### Sidebar on open

Select **Collapsed** or **Expanded** to choose the sidebar state each time the app window opens, including reopening from the tray. The default is Collapsed. This preference applies on the next opening; manual sidebar changes affect only the current window.

### Hide collapse/expand sidebar option

Hides the sidebar button while keeping **Sidebar on open** in effect. This
preference is saved between launches and applies immediately. The option appears
directly below **App icon**.

### Window material

Directly below Theme, selects **Mica**, **Mica Alt**, **Acrylic**, or **Solid**.
The same material applies to the window, canvas cards and corner panels,
dialogs, dropdowns, context menus and notifications. High contrast uses solid
system colors. The preference is retained between launches.

### App icon

Selects **Color**, **White**, or **Black** for the interface and notification-area icon.

## System, support and About

**System > Open Windows Sound Settings** opens the system sound page for driver and endpoint configuration. **Appearance > Hide the Support me tab** removes that item from the navigation sidebar and returns to Settings if Support is currently open.

**About**, below Danger zone, shows the app version and separate repository links for LightHostModern and the original Light Host project.

## Visual feedback and performance

**Notifications** contains four independent switches, all enabled by default:

- **Action notifications:** brief confirmations for completed actions in the lower-right corner.
- **Warning and error notifications:** warnings and errors shown in the same corner. Persistent status panels and confirmation dialogs remain available.
- **New release notifications on app:** an update notice on every opening when a newer release exists. Dismissing it suppresses repeats only for the current window session.
- **New release notifications on Windows:** checks for a newer stable release at startup and every six hours, including while only the tray host is running. A native Windows notification can appear once per release in each host session, independent of the in-app switch. Restarting the app allows the same release to be announced again; reopening only the UI does not repeat it. Windows controls banner visibility.

Hover hints are unaffected. Existing notification preferences are retained;
the previous combined release switch supplies the initial value of both new
release switches. Update checks and manual installation remain available even
when notifications are disabled.

**Appearance > Fade scroll edges** softens the top or bottom edge only where
more content is available to scroll. Enabled by default; disabled automatically
in high contrast or Performance mode. It does not change control positions or
scroll behavior.

**General > Performance mode** uses solid surfaces, removes scroll-edge fades,
stops animated audio flow, and reduces UI refresh frequency. Audio processing,
meters and useful status information stay active. The saved material, fade and
chain-animation preferences are retained and restored when Performance mode is
turned off. These changes apply immediately and persist after reopening.

### Canvas dots

In Chain mode, **Appearance > Dotted canvas background**, below **Always expand Chain**, toggles a subtle dot grid. It is disabled by default.
It follows canvas navigation, remembers the preference locally, and is hidden while
Performance mode is enabled. It does not change profiles or audio routing.

### Hover explanations

**General > Enable tooltips when hovering options** is enabled by default. It controls hover explanations throughout the app, including Diagnostics, and applies immediately without restarting. Settings cards retain their visible descriptions instead of receiving extra explanations. This preference is independent of action/warning notifications and does not remove screen-reader help.
