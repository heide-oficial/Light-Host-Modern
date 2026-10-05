# Live resource usage in the tray

- Added a Performance submenu with CPU usage, VRAM usage, and RAM usage, refreshed once per second while visible.
- Reuses the Dashboard resource definitions and shared GPU sampler. Includes the interface only while its monitored process is running, and works when the window is closed.
- Uses fixed columns with the tray's existing theme, DPI, accessibility text size, and translations. Readings update in place without dismissing the menu.
- Honors the Diagnostics setting and reports missing readings as Unavailable. GPU collection runs outside the message/audio threads, with no menu or engine references in background work.
- Validation: Release compilation completed; build 0035 was packaged with matching host/UI hashes. Tests were not run at the user's request.
- Portable: `dev-test/LightHostModern-build-0035/LightHostModern.exe`.
