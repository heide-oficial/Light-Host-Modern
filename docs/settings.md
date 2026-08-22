# Settings

Settings contains persistent application preferences and system actions. Host-owned settings are sent over IPC; interface-only preferences are stored by the WinUI shell.

## General

### Start with Windows

Registers the host executable for the current user under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. The registration uses the executable that enabled the option.

### Close to tray

Keeps the audio host and plugin chain running when the WinUI window is closed. The interface can be reopened from the notification-area icon. Disabling this option allows the window close flow to terminate the host.

### Enable VST2 plugins

Activates the VST2 format at runtime when VST2 support exists in the build. VST3 remains available independently. Changing this option refreshes the available formats and plugin workflow.

## Audio recovery

### Device persistence

- **Disabled** allows the normal audio-device behavior without preferred-device retry.
- **Last selected device** retries the most recently selected working configuration.
- **Custom device** retries a backend and device explicitly selected in the preferred-device dialog.

### Retry interval

Sets the number of seconds between preferred-device attempts.

### Maximum attempts

Limits consecutive failures. Recovery pauses after the limit instead of retrying forever. **Retry now** starts another immediate attempt.

### Preferred device

Opens a modal for choosing the backend and corresponding device. ASIO uses one driver selection; other backends can expose separate input and output choices. **Save** commits the selection and **Cancel** discards it.

### Enabled devices

**Manage enabled devices** opens a wide modal for allowing or blocking detected backends and their input/output choices. Blocked choices are never selected manually or by recovery.

See [Persistence and recovery](persistence-and-recovery.md) for the complete state machine.

## Appearance

### Language

Selects a JSON catalogue discovered from the `Locales` directory. The visible interface updates without restarting. Missing keys fall back to English.

### Layout mode

- **Compact** applies one consistent maximum content width across pages.
- **Expanded** uses the available width while keeping cards and controls responsive.

### Window material

Selects **Mica**, **Mica Alt**, **Acrylic**, or **Solid**. The setting changes the native window backdrop and is retained between launches.

### App icon

Selects **Color**, **White**, or **Black** for the interface and notification-area icon.

## System and support

**Open Windows Sound Settings** opens the system sound page for driver and endpoint configuration. **Hide the Support me tab** removes that item from the navigation sidebar and returns to Settings if Support is currently open.
