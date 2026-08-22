# Light Host Modern documentation

Light Host Modern is a Windows-only audio plugin host. A JUCE host process owns the audio stream, plugin chain, persistence, and notification-area lifetime, while a native WinUI 3 process presents the interface and exchanges commands and snapshots with the host through a local named pipe.

The application is organized into five main areas: **Dashboard**, **Audio**, **Plugins**, **Support me**, and **Settings**. The documents below explain both the public behavior of those areas and the internal workflows behind them.

## Application areas

- [Dashboard](dashboard.md) - Read backend, device, stream, meter, plugin, latency, CPU, and recovery status.
- [Audio](audio.md) - Select the audio backend and devices, then configure channels, sample rate, and buffer size.
- [Plugins](plugins.md) - Scan plugin folders, manage the installed database, and build the running serial chain.
- [Settings](settings.md) - Configure startup, tray behavior, VST2, device persistence, enabled devices, language, layout, material, and icon.
- [Support me](support.md) - Open the Ko-fi, repository, and video showcase actions.

## Internal architecture and workflows

- [Architecture and IPC](architecture-and-ipc.md) - Understand the host/WinUI process split, startup sequence, named-pipe protocol, snapshots, and version counters.
- [Audio processing](audio-processing.md) - Follow audio from the selected device through immutable chain snapshots, plugin slots, bypass compensation, meters, and failure guards.
- [Persistence and recovery](persistence-and-recovery.md) - Learn how audio choices, channels, plugin state, quarantine, safe mode, and device retry are stored and restored.
- [Tray and window behavior](tray-and-window.md) - Understand background lifetime, UI launch/focus, close-to-tray, startup registration, and responsive navigation.
- [Localization](localization.md) - Add a community translation using the runtime JSON catalogue.
- [Build and release](build-and-release.md) - Build the host and WinUI shell and generate the MSI and portable packages.

## Typical workflow

1. Start the host and open the WinUI interface.
2. Configure the stream on [Audio](audio.md).
3. Add plugin folders and scan them from [Plugins](plugins.md).
4. Add installed plugins to the running chain, reorder them, and open their editors.
5. Review [Settings](settings.md) for recovery and background behavior.
6. Use the [Dashboard](dashboard.md) to monitor the active stream and diagnose failures.

Light Host Modern processes audio only while the host process is running and a usable audio device is open. Closing only the WinUI window can leave the host and chain active when close-to-tray is enabled.
