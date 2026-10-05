# Stability implementation status

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

This records the implementation checkpoint for the A–G plan. The overall plan is **not complete**. Version remains 1.2.2. No commit, push, tag, or release was created.

## Baseline — 2026-09-06

- Existing host Release build succeeded using the configured VS2022 CMake build.
- Existing WinUI x64 Release build succeeded using VS18 MSBuild, with two generated-header `GetCurrentTime` macro warnings and the existing APPX0006 `runFullTrust` packaging warning.
- The initial sandboxed host build failed in MSBuild FileTracker with access denied; the same build passed after the user changed chat permissions.
- No real audio device was opened or reconfigured for testing. Runtime CPU/GPU/memory and callback percentile baselines have not been measured.

## A — partially implemented

Implemented:

- Owned queued operations and a server lifetime gate; shutdown cancels pending operations and overlapped pipe I/O.
- Complete message-mode reads, 4 MiB limits, deadlines, and response receipt before disconnect.
- Asynchronous WinUI FIFO transport, with copied coroutine inputs and retained window lifetime.
- JSON version/ID/typed-argument validation with JUCE and Windows parsers. No automatic mutation resend; later snapshots reconcile visible state after connectivity returns.
- Driver-creation policy adapter around every default JUCE device type, including internal fallback/restart creation paths. Settings can still enumerate disabled devices.
- Direct complete backend setup application through DeviceController, permitted previous-setup restoration, and preservation of the saved audio-device key on initialization/recovery errors.
- Updated protocol documentation, integration script, and localized transport errors.
- Commands wait for their post-command snapshot before accepting another mutation; channel/scan loops own copies of data used across awaits. Closing cancels the UI queue before a separate, bounded quit attempt.

Still required before A can be considered complete:

- Full simulated disappearance/return and manual-selection-during-retry scenarios; explicit generation invalidation and exhaustive recovery-state verification across all three modes.
- Verify the no-permitted-candidate state stops application attempts and displays None in all UI paths while retaining the configured target and chain.
- Explicit retrieval of late command results by request ID beyond snapshot reconciliation; bounded server result retention if introduced.
- End-to-end host shutdown tests with actual queued engine commands and WinUI window close/reopen tests. Current tests exercise the production primitives and transport against simulated peers.

## B — DSP and lifecycle increment

Implemented in the next-stage increment:

- Prepared audio views for 1–256 channels, removing temporary expanded-buffer pointer allocation at 32+ channels. Only additional scratch channels are cleared.
- Segmented processing for callbacks larger than the prepared block, with MIDI positions translated into and out of each segment.
- Individual bypass keeps the plugin processing, continuously records latency-compensated dry audio, and mixes over 5 ms.
- Controller-side suspension drains in-flight callbacks before prepare, state capture, or incompatible publication; callbacks return silence during suspension and resume with a 5 ms fade.
- Latency notifications carry an atomic requested value to the controller, which rebuilds delay storage while suspended.
- Reused processors retain their editors; only editors belonging to removed/rebuilt slots close during publication.
- Layouts exceeding 256 channels are rejected before publication and recorded as plugin load failures by the engine.

Instance-identity increment (2026-09-07):

- Active records receive persistent UUIDs before loading, including quarantined/missing modules. Exact legacy keys disambiguate duplicates without falling back to shared identity keys. Invalid/colliding IDs are repaired.
- Snapshots expose IDs; protocol 2 carries IDs for all running-chain commands, and unknown IDs produce localized structured errors. Protocol 1 is rejected. Internal legacy controller functions still resolve to processing-order indices on the serialized message thread.
- Running menu actions own IDs in their tags. Drag-and-drop keeps IDs separately from visual positions. Row comparison includes IDs so otherwise identical duplicates do not retain the wrong action target.
- Processors are reused by ID. New additions and duplicates get new UUIDs; duplicates copy source state and bypass. An explicit empty state masks stale legacy fallback values.
- Protocol/property regressions cover UUID persistence across serialized restart and reorder, collisions, distinct legacy states, rejection of numeric instance arguments, and WinUI encoding of ID pairs.
- Verification: host and WinUI x64 Release builds succeeded; all five CTest suites passed (0.90 seconds). Both locale JSON files parsed successfully, and the bundled UI executable hash matches the updated solution build. The existing WinUI APPX0006 packaging warning remains. No real audio devices were configured, no live UI interaction tests were run, and version remains 1.2.2.

Still required before B can be considered complete: complete duplicate/missing-plugin migration and normalization of original plugin descriptors (the legacy deprecatedUid workaround remains), exhaustive allocation instrumentation (including MIDI capacity exhaustion and plugin-generated MIDI), and callback performance measurements. The current MIDI staging buffers reserve 1 MiB; this is not a proven allocation-free bound for arbitrary plugin output. Dynamic latency updates reset the dry delay history and use a resume fade; uninterrupted compensation across arbitrary latency changes still requires validation.

Global-controls increment (2026-09-07):

- Prepared global dry history tracks total chain latency continuously. Global bypass mixes dry/wet over 5 ms; a separate 5 ms mute gain follows that selection. Both modes keep all prepared plugins processing.
- Dashboard and Running use native, keyboard-accessible ToggleButtons with shared snapshot/telemetry state. The tray uses the same host flags and embedded English/PT-BR JSON catalogs, reading the existing UI language preference.
- Flags start false in a new host and are never persisted. UI recreation and audio reconfiguration preserve the live host flags.
- Host and WinUI Release builds succeeded. All five CTest suites passed (0.70 seconds), including global controls across the channel matrix, mute precedence, continuous plugin processing, delayed dry impulse, and startup/reconfiguration semantics.
- A live WinUI test against a simulated IPC server passed Dashboard/Running synchronization, keyboard Space activation, process exit on close, and state retention after UI recreation. A Dashboard screenshot was inspected in the current dark theme. The UI and helper processes were closed afterward; no real audio devices were opened. Reusable scripts are `Tests/GlobalControlFakeHost.ps1` and `WinUI/ui-tests-global-controls.ps1`.
- PT-BR runtime interaction, high-contrast/all-DPI combinations, and real tray interaction remain unverified. Dry history still resets on latency changes; exhaustive continuity/performance and MIDI allocation bounds remain pending.

The remaining A recovery scenarios above are still open; this increment does not certify A or B complete.

## C — isolated discovery increment (2026-09-07)

- Added a dedicated scanner executable without host devices, tray, or session initialization. A background controller launches one module at a time under a kill-on-close Windows Job Object, with a 60-second timeout and cancellation.
- Validates correlated, size-limited results as complete module batches, including multiple identities and channel counts. Existing modification-time cache entries are reused. Validated batches are collected and saved on the host message thread; interrupted scans do not remove known plugins.
- Protocol 3 acknowledges scan queue acceptance and adds begin/status/cancel/retry commands. Both host and UI must be rebuilt together; protocol 1/2 peers are rejected. WinUI reports progress at 1 Hz, provides cancellation and explicit retry, and uses English/PT-BR catalog entries.
- Release staging requires the scanner and includes it in the existing optional Authenticode signing targets. No package was published or signed during this work.
- Host, scanner and WinUI x64 Release builds passed. All seven CTest suites passed in 2.82 seconds. Tests cover process job membership, Unicode argument quoting, launch failure, crash, timeout, cancellation, malformed results, multiple plugin identities, cache hits, retry and completed-result preservation. They use fake modules/workers and do not open audio devices.

- Live WinUI validation against a simulated IPC peer passed scan progress, cancellation, retry and normal UI exit. The current dark-theme screenshot was inspected. Reusable fixtures are `Tests/ScannerUiFakeHost.ps1` and `WinUI/ui-tests-scanner.ps1`; initial physical-click attempts were rejected by the foreground-window guard, so the successful test uses UI Automation InvokePattern. No real host was launched. High contrast, PT-BR interaction and other DPI settings remain unverified.

Still required before C is complete: static metadata acceleration and full bus details; complete failure browsing (the endpoint returns the first 100 entries and UI shows eight, with the full count and retry covering all failures); a broader real-plugin compatibility corpus and MSI installation checks. Filesystem enumeration checks cancellation between entries but individual filesystem calls have no hard deadline, particularly on network shares. Active plugins remain in the host process.

Scanner follow-up verification (2026-09-07): missing roots and traversal errors are now recorded explicitly; cancelled enumeration/cache work cannot publish stale progress. Status snapshots copy at most 100 failures and preserve a separate complete count, while retry uses every recorded failure. A 151-path regression covers missing roots, a regular file used as a directory, bounded snapshots and complete retry coverage. A separate process test abruptly terminates a simulated host owner and verifies both its worker and worker descendant exit. Host/scanner Release builds and all seven CTest suites passed (3.72 seconds). The existing WinUI payload was copied and hash-verified; no WinUI source changed in this increment. PowerShell syntax and locale validation passed. Tests did not open real audio devices or load real plugins. Network-share deadlines and permission-denied ACL scenarios remain unverified.

Real-plugin increment (2026-09-07): user-authorized validation downloaded the official Dragonfly Reverb 3.2.10 Windows x64 archive into ignored `out/real-plugin-test`. All four VST2 and four VST3 effects passed isolated scan, stereo channel/identity validation, Unicode-path handling and serialized cache restoration. Real VST3 testing exposed JUCE descriptions pointing to inner bundle binaries; validation/cache lookup now accepts an existing VST3 binary inside the requested bundle while retaining its original identity, and rejects paths into another bundle. VST2 discovery skips DLLs inside LV2/VST3 bundles. Simulated regressions cover both fixes. The opt-in Python fixture pins the official archive hash and saves scanner/runner hashes and results; no third-party binary is installed or shipped. See `docs/real-plugin-validation.md`. This does not validate audible processing, editors or real-device behavior.

## D–G — pending

Structural event channel, page/controller extraction, virtualization, richer meters/plugin actions, atomic session writer, and authenticated updater/build signing remain pending.

Keep the requested order A → B → C → D → E → F → G. Private signing-key provisioning remains maintainer work; no private keys are present or generated.

## Validation

CTest suites:

1. `ipc-lifetime-and-transport`: timeout/late completion, cancellation, lifetime closure, partial large Unicode messages, message limits, disconnect, and cancelled connect/read.
2. `winui-json-and-async-transport`: Windows JSON escaping/arrays, typed requests, FIFO ordering, complete large responses, acknowledgement, timeout without mutation resend, pending-read cancellation, and closed-client cancellation.
3. `ipc-versioned-protocol`: JUCE JSON, malformed/legacy/version-mismatched requests, IDs, types, integer bounds, argument counts, and structured errors.
4. `device-creation-policy`: simulated driver boundary, both channel roles, policy changes, default selection, and JUCE restore/fallback/restart creation attempts.
5. `realtime-channels-segments-and-bypass`: simulated processor across a 2/32/64/256 host/plugin channel-count matrix, oversized blocks, MIDI boundary offsets, continued processing while bypassed, dry impulse continuity, dynamic latency, rejected oversized layouts, reconfiguration waiting for an in-flight callback, and failed preparation invalidating old buffers. Host and plugin channel counts differ in these cases; asymmetric plugin input/output buses still need dedicated coverage.

6. `scanner-process-lifetime`: job membership, argument quoting, crash, timeout, cancellation, missing worker, and abrupt owner termination killing both worker and descendant.
7. `scanner-controller-results`: multiple identities, corrupt correlation, crash/hang, incremental cache, cancellation preserving validated results, explicit retry, unavailable paths and bounded failure snapshots with complete retry coverage.

All tests are isolated from the user's audio host and audio configuration. The registry-changing `WinUI/host-integration-tests.ps1` was updated for protocol v3 but was not executed.

All five CTest suites passed after the final DSP increment, including the failed-preparation regression (0.51 seconds). The host Release build succeeded, and the WinUI executable copied into its output was verified against the existing updated UI build. The earlier static checks also confirmed the original 50 host commands have matching schemas and that the new IPC messages exist in both locale JSON files.

Final host and WinUI Release builds succeeded. The final WinUI build retained the existing APPX0006 warning. Solution builds place WinUI under `WinUI/x64/Release/LightHost.WinUI`, while the release script builds the project directly and uses its project-relative output directory. The freshly built solution output was copied into the host's development output tree so its UI payload matches protocol v3; no release package was published.

The WinUI code review found and fixed borrowed channel-row references across awaits, accepting index-based mutations before their refresh completed, waiting behind long commands when closing, and an incorrect dirty-state branch after preserving unavailable-device settings. Existing page construction, list virtualization, and broader UI validation remain outstanding under D/E.

`.gitignore` was reviewed: build outputs, test binaries, temporary patch scripts under `out/`, runtime logs, and signing secrets are already excluded; no new ignore rules were necessary.

Remaining validation includes DSP regression/allocation tests, scanner real-module tests, session migration/write-failure tests, accessibility/DPI and large-list UI tests, signed updater tests, and comparable runtime performance measurements.
