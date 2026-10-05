# Release performance validation

This guide describes the current measurement tools, not a claim that every
performance scenario passed for 2.0.0. Current candidate results and limitations
are in [release preparation](release-2.0.0-validation.md). Historical 1.2.2 results
and their old baseline paths are retained at the end of this page.

## Current measurement workflow

Build the Release host and WinUI first; see [Build and release](build-and-release.md).
Run from the repository root using a disposable test setup and an exact output
device name. The script opens that real output device, so schedule the measurement
when it will not disturb another audio application. It uses isolated app profiles,
no input, muted output and hashes of the normal user's preference/session files.
UI scenarios require the `winapp ui` automation CLI.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tests/ReleasePerformance.ps1 -Variant current -OutputDevice "YOUR OUTPUT DEVICE" -UiState dashboard -OutputDirectory out/performance-current
```

`Tests/ReleasePerformance.ps1` runs a named shared-WASAPI output at 48 kHz / 480
samples in fresh profiles, with output muted and no input. It records process
CPU, private/working-set memory, UI GPU counters (when available), callback
quantiles/work/audit and driver diagnostics. Defaults are 30 seconds of warmup,
five minutes of measurement and five repetitions; short overrides are explicitly
marked as smoke tests. Dashboard, minimized and closed UI modes are available.
Each report keeps artifact hashes, actual device settings and the original
preferences hash check. A completed measurement does not automatically approve
performance; comparison, processing continuity and xrun review are still required.

`Tests/AnalyzeReleasePerformance.py BASELINE_JSON CURRENT_JSON --output REPORT`
compares interval-weighted process CPU, median memory, endpoint memory growth,
delivered work and per-run callback quantiles. It flags changes above five percent,
lost work, xruns, callback allocations and minimized visual telemetry. Eight historical local
tests passed for weighted samples, unavailable values, duration checks, regressions,
work loss, memory/telemetry checks, mismatched window sizes and new GPU work after a zero baseline. Smoke reports remain explicitly incomplete.
The script does not approve the overall application or substitute for the
outstanding hardware, accessibility and installer scenarios.

## Focused 2.0.0 benchmarks

- `Tests/CanvasResourceBenchmark.ps1` accepts `-HostExecutable`, defaults to IPC 5,
  128 nodes and 15 seconds per observation, and exercises canvas resources in an
  isolated host/UI setup. Use it for canvas rendering and minimized-state costs,
  not for audible audio latency.
- `Tests/ProfileDigestBenchmark.ps1` compares profile inspection with small and
  16 MiB synthetic plugin states, using the current `out/build/windows-vs2022`
  host and plugin-instance fixture target. It writes measurements under `out`.
- `Tests/AnalyzeReleasePerformance.py` compares compatible baseline/current
  reports. Check device, buffer, workload, instrumentation, measurement duration
  and executable hashes before interpreting a percentage as an improvement.

The `-Variant baseline` option belongs to the historical 1.2.2 comparison and
requires its separately reconstructed artifacts. A clean checkout does not
contain those ignored files. Do not substitute an arbitrary executable or
represent an unavailable baseline as a completed comparison.

## Callback measurement contract

IPC 5 exposes two commands in an explicit temporary test profile:

- `measure-callbacks` takes two integers: warmup seconds (0–300) and measurement
  seconds (1–1,800). Submit it as an operation, once per host, while no driver is
  open. Opening the selected device starts the clock at its first callback.
- `callback-measurement` returns `disabled`, `armed`, `warmingUp`, `measuring`,
  `completed`, or `interrupted`. Stopping the device during the run does not count
  as completion. A failed open before any callback leaves the measurement armed.

The measured interval includes callbacks whose start is in the requested window;
each included callback contributes its complete duration and sample count. The
clock frequency, interval endpoints, delivered callback/sample counts, total
duration and exact maximum are returned as decimal strings. QPC ticks are used,
so convert a duration to milliseconds with `ticks * 1000 / frequency`.

The fixed histogram has 256 subdivisions per power of two and never grows in the
callback. P95/P99 are upper bounds with relative error at most 1/256 (0.390625%).
They are available only after completion or interruption, when the audio writer
has stopped changing the histogram. Polling progress cannot race a live histogram
read or block the audio writer. The maximum and work counters are not quantized.

The window measures the complete host audio adapter, including input/output
copies, segmentation, metering and plugin calls. Compare its delivered counts
with `diagnostics.processedBlocks`, `processedSamples`, MIDI counters and failures;
reduced work is not a performance improvement.

## Allocation attribution

Configure with `-DLIGHTHOST_REALTIME_AUDIT=ON` for an instrumented Release host.
The same implementation is always linked into the offline processing tests. It
intercepts this executable's C allocation imports and C++ allocation operators;
no plugin DLL or system module is patched. JUCE's format bridge remains in the
host scope during calls into plugins. Simulated plugins explicitly mark their
own scope in tests, so deliberate MIDI growth remains distinguishable.

Diagnostics report `hostAllocationAuditAvailable`. The callback allocation/free
values are null when the audit is absent or its installation failed, rather than
presenting an unmeasured zero. `thirdPartyAllocationAuditAvailable` is false:
plugin DLL internals are outside this audit's coverage. A zero host count does
not establish that third-party code is allocation-free.

The audit is opt-in for normal distribution builds. Record whether it is enabled
for both comparison variants; do not compare instrumented and uninstrumented CPU
measurements as if their configurations matched.

## Historical baseline and results: September 2026

The following paths and results belong to the 1.2.2 investigation, not the current
release. Generated artifacts may no longer be present after workspace cleanup.
No historical test was rerun for this documentation update.

The frozen preparation build is in `out/baselines/completion-20260908-release`.
It has not been timed. Do not launch that binary with the production user's
preferences: it predates complete test-profile isolation. A comparison copy must
preserve its DSP and UI implementation and record any isolation/instrumentation
adaptations separately. The original frozen files remain unchanged.

`Tests/PrepareComparisonBaseline.py` verifies all 518 frozen artifacts and
reconstructs the source from revision `93e7dd1097ea2730bb91ad73741972354c5c1d88`,
the captured working-tree patch and captured untracked sources.
`Tests/AdaptComparisonBaseline.py` records a separate diff/hash manifest for
profile isolation, a timer forwarding to the original JUCE player, work counters
and the same allocation auditor used by the final host. It does not replace the
old DSP implementation or UI. The verified comparison build is under
`out/bcmp1`; its UI is a copy of the original frozen executable. The incomplete
first reconstruction under `out/bcmp` must not be used.

### Recorded checks

The deterministic callback suite passed warmup/end boundaries, exact work counts,
histogram precision up to the maximum integer value, interrupted runs and
concurrent progress readers. The realtime suite passed after including the JUCE
format bridge in host attribution. The actual shared-WASAPI integration passed
with a one-second warmup and two-second measured window: 209 delivered callbacks,
100,320 samples, zero host allocations/frees and zero reported xruns. The report
is `out/callback-measurement/results.json`. This short test validates the
measurement path; it is not a performance acceptance run. The complete paired
five-by-five-minute scenario comparison was not executed before the user ended
additional testing on 2026-09-08.

Both reconstructed-baseline and current-host two-second smoke runs passed with
the exact same output configuration and 209 callbacks / 100,320 samples each,
zero host allocations/frees and zero xruns. The baseline no-audio watchdog stayed
closed for six seconds before explicit selection. Production preferences were
unchanged. Reports: `out/performance-baseline-smoke` and
`out/performance-current-smoke`. The old protocol can disconnect before replying
to quit; the harness records that separately and confirms the process actually
exited. These reports are not a five-by-five-minute comparison.

The reconstructed baseline's full Dashboard series completed in
`out/performance-full/baseline-dashboard/results.json`, with eight simulated VST3
processors, 30 seconds of warmup and five repetitions of 300 seconds. The five
runs delivered 30,046 / 30,031 / 29,999 / 30,000 / 30,038 callbacks and reported
2 / 3 / 0 / 0 / 1 xruns. Every run recorded zero host allocations/frees and no
processing failures; production preferences were unchanged. The xrun results
are retained without an assumed cause.

The matching full current-build Dashboard series and both full minimized series
were not started. Earlier short Dashboard/minimized runs of both variants passed;
the current minimized smoke kept its visual-telemetry count unchanged at 82.
These short runs do not establish long-term memory/queue stability or compliance
with the five-percent regression threshold. Performance acceptance remains
incomplete, and no additional runs are scheduled as part of this delivery.

The final local packages in `out/release-test-final-audit` retain the explicit
Release host allocation audit. Their artifact manifest identifies the delivered
files; they must not be described as an uninstrumented performance build.
