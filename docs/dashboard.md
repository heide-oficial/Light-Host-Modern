# Dashboard

The Dashboard is the default page. It summarizes the active audio configuration,
realtime levels, operating mode, active profile, plugin counts, resource usage and
application version without changing the configuration.

## Audio status

The main cards show the backend and device names currently opened by JUCE. ASIO is represented as one driver selection because its input and output belong to the same driver. Other backends can expose separate input and output devices.

The stream format card shows the sample rate and buffer size. Channel controls
are on Audio; detailed latency is on Diagnostics.

If no usable device is open, the page reports the unavailable state rather than presenting stale device information.

## Live meters

Input and output meters are peak values calculated by the realtime processor in both List and Chain mode. Input is measured before input mono processing and the plugin path. Output is measured after processing, global bypass, mute, and output mono processing.

The 28-segment bars show peak level on a −60 to 0 dBFS scale with green, yellow, and red segments. A one-decimal reading in a fixed-width field to the left of each bar remains unclamped above 0 dBFS. Silence reads −∞ dBFS; an unavailable stream reads —. Bars decay smoothly and short peaks are retained for 75 ms to survive the display interval. No maximum reading is shown. A dedicated lightweight channel refreshes visible meters at up to 20 Hz without waiting behind commands, diagnostic queries, or database snapshots. Meter requests stop while another page is visible or the window is minimized.

## Profile and plugin status

Below the audio cards, the Dashboard uses these rows:

- Operating mode | Active profile
- Active plugins | Installed plugins
- CPU usage | VRAM usage | RAM usage
- App version (full width)

Multiple instances of the same plugin count separately in the active chain.
The active profile name follows profile creation, activation and renaming. Long
names wrap within their card and are available in full through hover help.

## Resource usage

CPU is the combined host, UI and helper-process usage, normalized to the machine's
total CPU capacity. RAM is private resident memory for those processes, excluding
shared pages. These use the same host counters as Diagnostics.

VRAM is dedicated GPU memory attributed by Windows to the UI, audio host and
isolated plugin processes, summed across adapters. It excludes shared GPU memory
and scanner processes. Windows per-process GPU counters can include graphics
allocations shared between processes; this is attribution, not a count of unique
physical bytes. See Microsoft's [GPU memory accounting explanation](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/).

Resource readings refresh about once per second while their cards are visible.
GPU queries run in the background, away from the UI and audio threads. Hiding or
minimizing the window stops new Dashboard requests; an open tray Performance submenu can still request its own readings. Disabling Diagnostics shows Disabled
and stops collection; unsupported or missing readings show Unavailable rather
than zero. The resource cards stack on narrow windows.

The [Diagnostics](diagnostics.md) page provides detailed CPU, reliability, format, latency, and activity cards. Global mute and chain bypass are available in the List-mode Running toolbar, the Chain canvas menu, and the notification-area menu.

The WinUI shell uses host state events and version counters to refresh changed configuration. Startup, reconnection, or an event resynchronization also requests a complete snapshot. Meter and resource updates use their own lightweight requests.

## Related documentation

- Configure devices on [Audio](audio.md).
- Manage plugins on [Plugins](plugins.md).
- Save setups on [Operating modes and profiles](chain-and-profiles.md).
- Understand telemetry and snapshots in [Architecture and IPC](architecture-and-ipc.md).
- Diagnose device restoration in [Persistence and recovery](persistence-and-recovery.md).
