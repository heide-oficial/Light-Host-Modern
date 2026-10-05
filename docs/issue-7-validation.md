# Issue 7 — local implementation validation

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Date: 2026-09-23. Baseline: published 1.4.0 (`2b7eea1`). The sections below preserve development-stage evidence. Current 1.4.1 results, including the completed restart/cancel/save UI test, are in [release-1.4.1-validation.md](release-1.4.1-validation.md).

## Implemented behavior

- VST3 bundle/binary equivalence is checked together with the complete class ID where available. Existing persisted plugin IDs remain stable. A manifest is checked against the isolated factory, and verified channel metadata comes from the instantiated class.
- Discovery renews its inactivity watchdog through actual filesystem/hash progress. Defaults are 60 seconds of inactivity and a separate 30-minute total worker budget. Time spent waiting for the controller's bounded queue is excluded from those budgets. A total-limit exit remains an explicit incomplete result.
- Retry reuses valid module/class checkpoints, clears resolved older root failures, and retains successful siblings when another class crashes or times out. Class checkpoints are persisted individually; publication to the host waits for final module-fingerprint verification. Cancelling a module can therefore leave reusable checkpoints that are not yet published.
- Root origin, architecture failures, ignored locations, link cycles and incomplete discovery have distinct handling. Linked directories inside a plugin bundle are explicitly unsupported rather than silently omitted from its fingerprint.
- Verbose capture is armed for the next complete startup, shares a session across host/UI/scanner, stops without restarting audio, and exports a UTF-8 TXT through Windows Save As. Successful export disables capture and cleans its own intermediates; cancellation/failure leaves a stopped capture available.
- Logging uses bounded asynchronous queues and a shared storage budget. Existing non-audio-thread events and periodic resource/counter summaries are collected; no per-block file writes or string formatting were introduced in the audio callback.

## Automated and reference-plugin checks

The native CTest suite passed all 21 tests. Focused scanner and capture tests cover root recovery, successful siblings around class crash/timeout, cache reuse on retry, full-CID mismatch, a rejected x86 PE fixture, cross-process capture stop, Unicode export, export failure preserving the capture, user-folder masking, idempotent completion and the shared storage cap.

`LightHostModernScanProcessTests --long-watchdog` passed with a productive worker running longer than 60 seconds under the real 60-second inactivity budget. The ordinary suite also exercises shorter inactivity/total-timeout cases.

The opt-in manifest regression passed for missing, valid, invalid and stale manifests using a private copy of Dragonfly Room Reverb. Full class IDs and final fingerprint verification were checked. All four Dragonfly VST2 modules and all four VST3 modules passed the real discovery/cache regression, including Unicode paths. These are discovery checks, not new audible-processing or native-editor compatibility claims.

Local evidence is retained under ignored `out/issue-7/validation`, `out/manifest-regression` and `out/real-plugin-test`. Reproduction instructions for the opt-in checks are in [real-plugin-validation.md](real-plugin-validation.md).

## Interface and export checks

In an isolated profile with audio suspended, the UI successfully armed capture and offered **Restart now / Restart later**. Restart later created no log segments. Restart now closed the old host/UI and reopened the same profile with a new host PID and active capture. A scan of the six installed Xvox modules was recorded.

Stopping opened native Windows Save As. Export completed and the UI displayed the disabled/saved state. The exported text contained host, UI and scanner events, including `load_failed:126`, and did not contain the user's personal-folder prefix. Intermediate capture files were removed after success. A local evidence copy is `out/issue-7/validation/ui-export.txt`.

The additional attempt to verify **Cancel** visually was inconclusive because the automation returned inconsistent window imagery/geometry; it was stopped. Backend stopped-state retention and failed-export retry were tested, but that is not a claim that every native-dialog action was manually verified.

## Scanner measurements

The table in this section predates the additional performance work described below. It is preserved as the initial measurement, not the timing of the latest portable.

Three independent isolated profiles per variant scanned the same four Dragonfly VST3 modules, followed by a warm scan. Audio was suspended. The final measurement ran after CTest and build completion; an earlier overlapping run is kept separately as `benchmark-with-concurrent-tests.json` and is not used below. Other user applications were not stopped.

| Build / capture | Cold median | Warm median | Host CPU time, cold median | Host peak working set, median |
|---|---:|---:|---:|---:|
| Published 1.4.0 | 3,123 ms | 962 ms | 62.5 ms | 60.8 MiB |
| Working build, logs off | 3,271 ms | 1,027 ms | 109.4 ms | 60.8 MiB |
| Working build, logs on | 3,374 ms | 961 ms | 125.0 ms | 61.0 MiB |

All cold runs examined four modules with zero failures. All warm runs examined zero modules and reused four cached results. CPU/memory columns cover the host process only, not aggregate scanner/UI usage. Sampling and IPC polling contribute to wall time. Raw records are in `out/issue-7/validation/benchmark.json`.

This small sample does **not** show a general speedup: the new logs-off cold/warm medians are approximately 4.7%/6.8% slower, with stronger factory, per-class isolation and final-content verification. The last successful class worker now performs final fingerprint verification itself, avoiding an extra process in that path; a separate verifier remains necessary after a last-class failure or cached final class. The warm cache remains effective. Larger-library and audible-load measurements are still needed before claiming broad performance improvements.

## Additional scanner performance work

The follow-up implementation adds:

- A 256 KiB buffered stream around full-content SHA-256 reads. JUCE's hasher requests 64-byte blocks; those requests now read from memory instead of reaching the file stream individually. The fingerprint algorithm and bytes covered remain unchanged.
- A manifest-only JUCE adapter mode that cannot fall through to factory loading. Missing/invalid metadata no longer causes an extra real factory enumeration.
- Combined catalog, instantiation and final fingerprint verification for single-class VST3 modules in their already isolated worker. Multi-class modules retain one validation process per class, and the host still does not load unknown plugins during discovery.
- A checksummed append-only class journal with atomic baseline/final snapshots, preserving per-class recovery without rewriting all earlier classes each time. Known-class and known-ID indexes also avoid repeatedly walking the entire installed list.
- `scan.timing` events and `Utilities/summarize-scan-timings.py` for catalog/instantiation/hash/cache/process analysis. Child-process launch, CPU, transfer counts and peak memory are measured when verbose capture is active. Nested/overlapping stage durations cannot be summed into wall time.

New regression cases cover buffer boundaries, journal compaction/recovery, stale/corrupt/truncated records, linear checkpoint write volume, and combined single-class manifest validation. These cases are prepared/compiled only: execution of the validation suite remains deferred at the user's request. The previous 21/21 result does not certify these additional changes.

Concurrent validation remains limited to one worker alongside enumeration. Increasing that limit remains conditional on representative library/audio-load measurements; it is not assumed to improve every plugin or licensing system.

### Follow-up measurements — 2026-09-23

Three alternating runs compared the previous `layout-update-4` portable with the optimized build, using four Dragonfly VST3 modules and fresh isolated profiles with audio suspended. Each cold metadata-cache scan was followed by a warm scan. OS filesystem caches were not cleared, and no compilation or validation suite ran concurrently.

| Build / capture | Cold metadata-cache median | Warm median | Host CPU, cold median | Host peak working set |
|---|---:|---:|---:|---:|
| Previous local portable | 3,287 ms | 937 ms | 78.1 ms | 60.9 MiB |
| Optimized, logs off | 1,915 ms | 443 ms | 31.3 ms | 60.8 MiB |
| Optimized, logs on | 1,950 ms | 473 ms | 31.3 ms | 61.0 MiB |

The logs-off medians decreased by approximately **42% cold and 53% warm in this sample**. Every cold scan recognized four modules without failures; every warm scan reused four cached modules with zero examinations. The four persisted fingerprints and class-ID sets match between the previous and optimized builds. This supports byte/identity preservation for these reference modules, not arbitrary commercial-library compatibility.

The final verbose pair of scans recorded six scanner processes (four combined single-class workers and two enumeration workers), four factory enumerations and twelve fingerprints. Fingerprint duration had a 17.0 ms median; the twelve calls read 41,051,136 bytes in total. Summed child CPU was 421.9 ms across the two scans. Cache finalization took a 36.9 ms median for four records. Stages overlap/nest, CPU counters are coarse, and these figures do not represent audible-load or large-library performance.

Raw local evidence: `out/issue-7/validation/benchmark-optimized.json`, `optimization-summary.json`, and `scan-timings-optimized.json`. The single-class benchmark does not exercise multi-class journal crash recovery; the added regression cases for that remain unexecuted. The full validation suite was not run.

The Release host, scanner, helper and WinUI builds succeeded, and the new/updated native regression targets compiled. Local packages for this round are under `out/issue-7/scan-optimized`. Version and remote repository remain unchanged.

## Xvox and commercial compatibility

All six locally installed Xvox 1.5.2 modules fail during Windows loading with error 126, before class-identity validation. The published 1.4.0 scanner also failed to load them. An independent native load reproduced the failure. On 2026-09-23, the user additionally confirmed that Xvox fails in another plugin host. This supports a problem beyond this host's scanner, but does not by itself identify the exact unavailable dependency or establish that every Xvox distribution has the same issue.

No installed plugin, dependency, licence or system setting was altered. Xvox processing/editor compatibility remains unverified until a working installation is available. Actual Waves/WaveShell and Townsend compatibility also remains unverified; synthetic multi-class tests are not a substitute for those products.

## Remaining validation limits

- The timing sample uses four real VST3 modules on this machine. Large commercial libraries, network storage and very large multi-class catalogs still need representative measurement. Per-class checkpoint persistence can be costly for a large catalog.
- The full manual matrix for physical disk exhaustion/disconnection, native-dialog overwrite/cancel, narrow-window/keyboard behavior in both languages, and restart with a populated commercial chain was not executed. Related error handling exists, but these scenarios should remain release-validation checks.
- Crash capture is best effort. Power loss or an abrupt process termination can lose buffered events. Private third-party logs are not automatically included.
- MSI creation validates packaging; installation/upgrade on a clean machine is a separate check. Local packages are development artifacts, not replacements for the published 1.4.0 release.

See the [approved plan](issue-7-scanner-and-verbose-logs-plan.md) for the complete intended validation matrix.

## Local delivery

Follow-up UI adjustments: the logs card now appears first in Diagnostics, follows the Settings icon/text/On-Off toggle layout, and uses the title **Detailed logs for troubleshooting**. The waiting-for-restart state is a blue link to the confirmation modal instead of a separate restart button. Settings no longer contains log status or a management link. At the user's request, validation tests were not rerun for these UI follow-ups; the test results below refer to the preceding implementation.

The visual simplifications remove Active, the start timestamp and the size from the card and present stop/save as a blue link. The latest adjustment also removes Disabled and Logs saved text: the Off toggle communicates the disabled state. Error feedback and pending-save actions remain available. The latest local packages are under `out/issue-7/layout-update-4`; earlier follow-up packages are preserved. Validation tests remain deferred at the user's request.

Release builds of WinUI, host, scanner and helper completed successfully. The final CTest run passed 21/21 in 30.48 seconds; its output is retained in `out/issue-7/validation/ctest-final.log`. Packaging reused those builds and skipped a redundant test run.

Development packages are in `out/issue-7/release/LightHostModern-Portable.zip` and `out/issue-7/release/LightHostModern-1.4.0-Setup.msi`. The portable directory was extracted from the delivered ZIP and compared against staging by file count and SHA-256 (`portable-verification.json`). These packages retain the existing version and are unsigned local test builds. No install, publication, remote commit, tag, issue comment or closure was performed.
