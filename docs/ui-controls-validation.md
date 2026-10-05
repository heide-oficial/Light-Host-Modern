# UI controls and plugin catalogue names — 2026-09-12

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Release remains 1.2.2. Work preserves the existing audio/session architecture and incorporates the latest UI corrections.

## Behavior

- Failures use the current WinUI ListViewItem template, with accent selection, visible unchecked borders, and native Light/Dark/HighContrast resources.
- Installed has the same separator before Sort as Running. Running's drag instruction is removed.
- Compact centers the header, toolbars and cards while keeping scroll viewports at the window edge. Virtualized plugin containers receive the same horizontal inset, including when first realized or recycled.
- Preferred device retains a 360-DIP width in both layouts, reduced only if the viewport requires it. Long labels use the existing ellipsis.
- Plugin menus separate editing, processing, names, order and removal. Restore is disabled when the displayed name is original. Details retains the native dialog and makes Close span the footer.
- The enabled-device modal scrolls its backend selector and device sections together. Its backend checkbox removes the native five-DIP content offset so the label is centered.
- Installed supports Unicode aliases using stable `knownId` keys in preferences. JUCE descriptions and plugin identity remain original. New instances inherit the alias; changing or restoring a catalogue alias does not rename existing instances.
- Diagnostics is enabled by default and stored in the host profile. Disabling requires a localized confirmation. The host stops DSP clock sampling, CPU/worker accounting, detailed audio measurements, diagnostic counters and periodic diagnostic logging; the UI stops diagnostic polling and CPU sampling. Functional device recovery, state persistence, plugin processing and independent dashboard peaks continue. Re-enabling starts fresh measurement history.

## Verification

- `PluginInstanceTests`: catalogue alias persistence, rescan/metadata independence, Unicode validation, class isolation, original-name restoration, independent UUIDs, and inheritance without modifying existing instances.
- `RealtimeTests`: diagnostic counters stop while gain processing, sample delivery and dashboard peaks continue; counters resume when enabled. Existing channel, bus, MIDI, latency, lifecycle and allocation checks remain in this suite.
- `PluginPreferencesIntegrationTests.ps1`: actual Release host in an isolated profile, no audio opened. Default enabled state, rename/details, invalid-name rejection, disabled CPU sampling, timing rejection, independent meter endpoint, and alias/diagnostic/restore persistence across normal host restarts.
- `ui-tests-controls.ps1`: compact picker and scrollbar geometry, confirmation cancellation, disabled UI polling, installed rename/add/restore, running menu state, full-width Close, toolbar widths, failure selection, and scrolling the complete enabled-device content.
- Screenshots inspected in English/dark and Portuguese/light. Initial test-script selector/fixture issues were corrected; the first-realization plugin-card alignment issue found visually was fixed and rechecked in a fresh UI process.

Evidence and screenshots are under `out/ui-controls-20260912/`. Release artifact verification is stored beside the portable archive in `out/release-ui-layout-fixes/`.

## Final delivery

- Release host and WinUI builds succeeded. All 18 CTest suites passed (10.47 seconds).
- The final Release host passed the preferences integration scenarios. Seven UI scenarios passed across the focused runs, with screenshots reviewed in dark/English and light/Portuguese.
- The portable archive was rebuilt and its 249 extracted files verified. The host launched the UI from that extracted portable folder; its executable matches both the canonical build and the package manifest. Settings and scan controls passed the packaged-app smoke check.
- The isolated test opened no audio and exited normally. Production UI preferences were unchanged. The production host resumed with the same ordered instance UUIDs and global controls.
- ZIP SHA-256: `D619E9602D1E138E2076982BA46071831E9E79690C51651C3BDE454863093CFD`.
- Consolidated evidence: `out/ui-controls-20260912/verification.json`.
