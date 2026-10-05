# Version 1.4.1 validation

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Date: 2026-09-23. Windows x64 Release build, native build directory `out/build/modern-validation`, WinUI x64 Release. Tests use isolated profiles; production audio preferences are preserved. Local logs, screenshots, JSON results and commands are retained under ignored `out/release-1.4.1-work/`.

## Native and host integration

- CTest: **23/23 passed**, including scanner crash/timeout isolation, productive-worker deadlines, class checkpoints, journal recovery, session/state persistence, audio routing, updater validation, UI process lifetime and verbose capture/export. Packaging reran the complete suite successfully.
- The real 60-second scanner watchdog test passed with a productive worker running longer than its inactivity budget.
- Profile isolation, plugin instances, sessions, scanner integration, plugin preferences and state events passed. State-event tests now allow an earlier session-save event before requiring the requested UUID delta.
- Real discovery/cache tests passed for four Dragonfly VST2 modules and four VST3 modules. Missing/valid/invalid/stale VST3 manifest checks and full-class-ID checks passed.
- PurestGain identity and saved-instance recovery passed using a freshly scanned fixture at the current workspace path.
- Hardware integration passed for Windows Audio shared, exclusive and low-latency modes, DirectSound and ASIO. Supported setting transitions and recovery were exercised; unavailable hardware capabilities remain explicitly recorded as unavailable. This is not a zero-xrun or physical-hotplug claim.
- Callback measurement integration passed against the real shared Windows Audio driver. A separate `out/build/release-audit` build also passed with allocation auditing required: zero detected host allocations/frees. Published packages keep this instrumentation disabled.
- Instrumented performance smoke checks passed with the UI on Dashboard, minimized and closed: one 10-second window per state after 2 seconds of warmup, 1,000 callbacks per window, zero reported xruns and zero detected host callback allocations/frees. These short checks are not the full five-run comparative benchmark.

## Interface and logging

- Actual host-launched UI tests passed normal close-to-tray/reopen, forced UI termination stopping the host, collapsed/expanded sidebar defaults, preference persistence through restart, and close-with-quit.
- Detailed logs passed arming without early collection, restart-later, reopening restart confirmation, restarting both processes, stopping collection, cancelling Windows Save As, retrying export and saving TXT. Export included host/UI events; logging switched off without restarting either process.
- Modernization smoke tests passed English and Brazilian Portuguese, Compact and Expanded, with both actual suspended audio and simulated channel/meter input. Coverage includes mono, Individual/Pairs selection, partial pairs, fixed dBFS labels and Diagnostics tooltips at 192 DPI.
- Actual UI tests passed global mute/bypass, audio-selection transactions, lazy pages, meter polling suspension, themes/Diagnostics, instance rename/swap/details/restore, manufacturer grouping, and keyboard/language navigation.
- Virtualized list tests passed 100, 500 and 1,000 catalog/chain entries, including search, selection/focus retention, grouping, visual sorting without changing processing order, and UUID-based drag reordering. These synthetic entries do not represent 1,000 loaded audio processors.
- Actual scanner UI tests passed incremental loading of 151 failures, retained multi-selection, selected retry, retry-all and preservation of the catalog and chain. Session-save failure/retry and reopening the UI with live global controls passed.
- Mock-host UI cases covered materials, channel layout, Unicode scan paths, plugin toolbars, aliases, maintenance confirmations, failure checkboxes and translated dialogs. Cases affected by obsolete selectors, animation timing or foreground changes were corrected and rerun individually; evidence is retained across the `fake-*` directories rather than only the last aggregate result.
- Meter transport stayed responsive with the command pipe blocked and shut down with a pending reader. Rising-edge response passed with a simulated 900 ms Diagnostics delay; hidden meters stopped requesting frames. Downward readings intentionally use the app's smooth decay.
- Portable update UI tests passed cancellation cleanup, successful ZIP validation, checksum rejection and closing the UI during transfer while preserving the host.

## Packages

- MSI and portable generated at `out/release-1.4.1/` with version 1.4.1.
- All three artifact hashes/sizes, portable contents, machine-scope MSI identity/upgrade mapping, shortcuts and legacy migration entries passed inspection.
- Staging correctly rejected a stale WinUI build record before touching the destination.
- The versioned MSI and `LightHostModern-Setup.msi` compatibility alias have identical content.
- The final extracted portable passed an additional English/Compact UI smoke run at 192 DPI. PowerShell/Python syntax checks and matching English/Portuguese localization keys passed.

## Remaining environment-dependent limits

- **Native third-party editor:** 8 of 9 offline real-plugin processing/state/editor cases passed. Dragonfly Early Reflections VST2 exceeded the 60-second deadline in its native editor. The same module passed processing/state and the generic editor. This does not establish universal native-editor compatibility or identify the cause of that timeout.
- The installed Xvox modules previously failed Windows loading with error 126, and the user independently reproduced failure in another host. Version 1.4.1 improves diagnosis and scanning; it does not claim to repair a missing third-party dependency.
- Full MSI install/upgrade/repair/uninstall was **not executed** on this user's machine. Package inspection and the current/previous MSI lifecycle plan passed; an isolated disposable Windows machine is still required for execution.
- No physical device unplug/replug, additional monitors at 96/144 DPI, every third-party plugin, or long-duration comparative performance certification is claimed.
- Packages are unsigned, matching the available release environment; no trusted code-signing certificate was configured.

These tests provide evidence for the exercised scenarios, not a guarantee for every driver/plugin combination.
