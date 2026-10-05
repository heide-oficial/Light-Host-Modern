# Dashboard profile and resource cards

Kept the audio backend/format and device cards above the requested layout:
Operating mode / Active profile; Active plugins / Installed plugins;
CPU usage / VRAM usage / RAM usage; full-width App version.

The active profile follows the host catalogue, including the localized built-in
Default name. Version uses APP_VERSION. Long names are bounded and available in
hover help; resource cards stack on narrow windows.

CPU/private resident RAM reuse host telemetry and add the UI's local counters.
Dedicated VRAM uses Windows GPU Process Memory counters for the UI, host and
isolated plugins. Queries use language-neutral PDH paths on a background thread,
with bounded buffers and explicit unavailable states. See the documented
[counter array API](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw).
The GPU total is Windows per-process attribution, excludes shared GPU memory and
scanners, and may include allocations shared by app processes.

Sampling runs about once per second only while the Dashboard resource cards are
visible, and honors the Diagnostics setting. No audio-thread or native host
source changes were required.

Validation: Release UI build passed. DashboardUiTests passed in the isolated
profile dashboard-ui-4d63ee6bd58743749d1cfd0b5e90bfae, including real CPU/RAM/VRAM
readings, profile refresh, version and diagnostics off/on. Inspected its scripted
dashboard-resources.png capture. No full application suite was run.

Portable build 0034 generated under dev-test/LightHostModern-build-0034.
Verified all 251 payload file sizes/hashes, current UI executable/XBF/locales and
absence of test content. Native executables remain unchanged.
