# Diagnostics

Diagnostics appears after Profiles and before Support me in the sidebar and presents local measurements in cards. Each card has an icon, heading, description, and readings below it.

- **Performance:** whole-app CPU, DSP load and CPU use by the host, interface, scanner and isolated plugin workers.
- **App memory:** private resident RAM and committed memory for the app processes.
- **Audio reliability:** xruns, processing failures, and MIDI events dropped after exceeding capacity.
- **Stream format:** requested sample rate and buffer size alongside the driver's actual values.
- **Latency:** plugin-chain latency and driver input/output latency.
- **Processing activity:** processed audio blocks/samples and input/output MIDI event counts.

Visible diagnostic readings refresh at approximately one-second intervals. These measurements are local and are not uploaded.

**Settings > General > Diagnostics** is enabled by default. Turning it off requests confirmation and stops ordinary diagnostics collection. Dashboard and tray resource readings show **Disabled**; audio processing, plugin scanning, and Dashboard peak meters continue. The page is hidden unless detailed-log capture management still needs it. Turning the setting on restores the page and resumes monitoring.

## Resource readings and explanations

App CPU is the sum of the audio host, this interface, scanner and isolated plugin processes, normalized to the whole computer. Directly loaded plugins already belong to host usage. Resident RAM counts private physical pages; committed memory counts private committed bytes. Shared pages are excluded from both process sums. Missing OS measurements show Unavailable rather than zero. An additional section lists each isolated plugin's PID, resources, latency and missed blocks; see [plugin isolation](plugin-isolation.md).

Visible Diagnostics cards, Dashboard resource cards, and the tray **Performance** submenu request resource readings at approximately 1 Hz. The host memory-sampling lease expires after two seconds without requests. Hiding or minimizing the main window stops its requests; an open tray Performance submenu can continue requesting readings independently.

Each metric has localized English/Portuguese hover text and accessible help text; numeric values remain keyboard selectable. **Enable tooltips when hovering options** controls hover explanations without removing accessible help. Independent Dashboard meters continue when Diagnostics is disabled.

## Detailed log capture

Turn on the **Detailed logs for troubleshooting** toggle to schedule a capture. The dialog offers **Restart now** or **Restart later**; collection starts only on the next complete host startup. Closing the interface to the tray does not restart the host. Restart now saves the session and uses the local helper to wait for both old processes before reopening the same app/profile, without installation or elevation.

The **Detailed logs for troubleshooting** card appears first in Diagnostics and follows the Settings layout: icon on the left, title/description and action in the middle, and an On/Off label beside the toggle on the right. **Waiting for restart** is a blue link that opens the restart confirmation dialog. During collection, **Stop collecting and save logs** is a blue link in the same style; the card omits the redundant Active label, start time and capture size. When disabled or after successful export, only the Off toggle indicates the state: no Disabled or Logs saved text is shown. Save remains available after stopping, and relevant error/paused messages are retained.

Reproduce the problem, then choose **Stop collecting and save logs**. This stops writers in the host, interface and existing scanner processes, while audio and scanning continue. The native Windows Save As dialog exports a UTF-8 `.txt`. Cancelling or a write error keeps the stopped capture available through **Save collected logs**, including after closing/reopening the app. Successful export disables capture immediately, without another restart. Nothing is uploaded.

A pending capture keeps Diagnostics accessible even when its performance-metrics setting is off. Settings only controls the usual Diagnostics visibility/metrics; log management and capture status are shown in Diagnostics itself.

Logs include UTC time, process/thread identifiers, operation IDs, scanner paths/classes/cache decisions/failures, plugin loading, device recovery, IPC results, and periodic process resource summaries. They exclude audio, MIDI contents and binary plugin states. The personal user-folder prefix is replaced with `<USERPROFILE>`; other paths and plugin/device names remain useful diagnostic information. Third-party private logs are not collected automatically.

Each process writes asynchronously to segments of up to 16 MiB, with a 2 MiB pending queue and a shared 256 MiB capture limit. Overflow is reported as dropped events; storage failures/limit exhaustion pause collection and preserve existing files. Resource summaries are taken off the audio thread. The text groups process segments and provides UTC/tick fields for correlation; it is not a global ordering of simultaneous events.

Staging lives in `%LOCALAPPDATA%/LightHostModern/Logs/Captures` (or the isolated test profile). Captures survive restart/crash; stale writer markers produce an interruption notice. A stopped session remains stopped. After a verified export, the host persists the disabled state before deleting that capture's intermediate files. Crash records and buffered writes are best effort; an abrupt power loss may lose the last events.
