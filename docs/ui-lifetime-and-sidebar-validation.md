# UI lifetime and sidebar preference

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Validated for 1.4.1 with native process-handle tests and `Tests/UiLifetimeIntegrationTests.ps1`: normal close/reopen, forced UI termination, saved sidebar preference, full restart and close-with-quit passed. Detailed-log restart also passed end to end. See [release validation](release-1.4.1-validation.md) for evidence and limits.

## Behavior and extended manual checklist

- Enable Close to tray, close the window normally, and confirm audio continues. Reopen from the tray and confirm there is only one UI process.
- Use Windows taskbar **End task** while the UI is open. Confirm the UI, audio host, tray icon and owned scanner workers exit. Repeat with Close to tray enabled and disabled.
- Repeat forced UI termination while a scan is active and while plugin code is stalled. The host should request normal shutdown immediately and cannot remain running beyond the ten-second fallback deadline.
- Abrupt UI failure, including a crash or a forced exit with code zero, has the same host-shutdown policy. Exit codes are not used to guess the user's intent.
- Confirm tray Quit, normal close with Close to tray disabled, detailed-log restart and update-driven shutdown still complete normally.
- Verify a separate test profile is unaffected by ending another profile's UI. The monitor watches the exact launched process handle and a unique close event, not an executable name.
- Set **Settings > Appearance > Sidebar on open** to Expanded. Close to tray and reopen, then fully quit/restart. Confirm it opens expanded in both cases. Repeat for Collapsed.
- Toggle the sidebar manually. Confirm it stays in that state while navigating, changing language or focusing the existing window, but returns to the configured default on the next opening.
- Confirm missing/invalid `Appearance/SidebarOnOpen` values use Collapsed, and both English and Portuguese labels follow the Settings card layout.

## Limits

Forced termination cannot guarantee that a plugin's latest unsaved state is preserved. The host gets an opportunity for normal cleanup, but stalled cleanup is bounded. The sidebar preference affects subsequent openings, without changing the current sidebar immediately.
