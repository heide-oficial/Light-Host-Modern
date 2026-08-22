# Dashboard

The Dashboard is the default page. It summarizes the active audio configuration, realtime levels, plugin counts, performance, and recovery state without changing the configuration.

## Audio status

The main cards show the backend and device names currently opened by JUCE. ASIO is represented as one driver selection because its input and output belong to the same driver. Other backends can expose separate input and output devices.

The stream format includes:

- sample rate;
- buffer size;
- enabled input and output channel counts;
- input and output latency reported by the audio device.

If no usable device is open, the page reports the unavailable state rather than presenting stale device information.

## Live meters

Input and output meters are peak values calculated by the realtime processor. The input level is measured before the running chain and the output level after all active or bypassed slots have been processed.

The UI requests lightweight telemetry more frequently than a complete state snapshot. This keeps meters responsive without repeatedly serializing the plugin database and every audio choice.

## Plugin status

The Dashboard shows:

- active plugin instances in the running chain;
- installed plugins in the known-plugin database;
- total chain latency in samples;
- processing failures recorded by realtime guards.

Multiple instances of the same plugin count separately in the active chain.

## Performance and recovery

CPU usage and x-run information come from the active JUCE audio device. Recovery text describes whether preferred-device handling is running, waiting for another attempt, paused after exhausting attempts, or disabled.

Configuration changes increment host-side version counters. The WinUI shell requests a new complete snapshot only when the chain, plugin database, or audio configuration version changes.

## Related documentation

- Configure devices on [Audio](audio.md).
- Manage plugins on [Plugins](plugins.md).
- Understand telemetry and snapshots in [Architecture and IPC](architecture-and-ipc.md).
- Diagnose device restoration in [Persistence and recovery](persistence-and-recovery.md).
