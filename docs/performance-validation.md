# Release performance validation

Use these tools to measure CPU, memory, rendering and audio callback behavior.
Results apply to the tested build, workload, device and configuration; keep that
information with each report.

## Measurement workflow

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
lost work, xruns, callback allocations and minimized visual telemetry. It also
checks unavailable values, measurement duration, mismatched window sizes and GPU
work after a zero baseline. Smoke reports remain explicitly incomplete. These
measurements do not replace hardware, accessibility or installer checks.

## Focused benchmarks

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

The `-Variant baseline` option requires separately prepared baseline artifacts
at the paths expected by the runner. They are not included in a fresh checkout.
Verify the executable hashes and configuration before using an existing baseline
report for comparison.

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
