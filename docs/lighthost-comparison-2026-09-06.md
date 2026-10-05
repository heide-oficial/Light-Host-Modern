# Light Host Modern and ReLightHost: Technical Investigation

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Date: 2026-09-06. Investigation only; no application changes or release actions.

Archive note (2026-09-30): this report was preserved during workspace cleanup.
The temporary research checkouts and generated artifacts mentioned below were removed.

## Contents

1. [Scope and conclusion](#1-scope-and-conclusion)
2. [Architecture and dependencies](#2-architecture-and-dependencies)
3. [Feature comparison](#3-feature-comparison)
4. [Performance assessment](#4-performance-assessment)
5. [Local stability findings](#5-local-stability-findings)
6. [Ideas worth adapting](#6-ideas-worth-adapting)
7. [Choices not worth copying](#7-choices-not-worth-copying)
8. [Suggested decision order](#8-suggested-decision-order)
9. [Validation needed before implementation](#9-validation-needed-before-implementation)
10. [Licensing and evidence](#10-licensing-and-evidence)

## 1. Scope and conclusion

Sources examined:

| Project | Inspected revision | Declared version |
| --- | --- | --- |
| Light Host Modern, local working tree | `93e7dd1097ea2730bb91ad73741972354c5c1d88` | 1.2.2 |
| ReLightHost, independent project | `4a9c6d16f333cfa830f02e0c707563eefa66330d` | 2.3.5 |
| Original LightHost | `03cb4d0d711b6a5789c295d770f9b7de363779b7` | Historical master revision, 2016-09-22 |

ReLightHost's latest published release at inspection was v2.3.5, published on 2026-08-13. The GitHub issues query returned no issues; it therefore supplied no independent reproduction cases. Release notes were checked against implementation rather than treated as proof of performance. [Release](https://github.com/hiimgyn/ReLightHost/releases/tag/v2.3.5)

**Recommendation: retain C++/JUCE and native WinUI 3. Adapt selected engineering practices and features, not the other application's whole architecture.**

The local snapshot-based processing design already avoids several compromises found in ReLightHost's stereo processing path. The most useful opportunities are asynchronous IPC, visibility-aware UI updates, safer scanning, clearer module ownership, and richer plugin management and metering.

This was a source-level investigation, not a controlled audio benchmark. No driver was switched, plugin executed, installer run, or running audio session changed. CPU, RAM, GPU memory, audible glitches, and end-to-end latency were not measured. Findings below distinguish code facts, conditional risks inferred from those facts, and proposed improvements.

## 2. Architecture and dependencies

### 2.1 Light Host Modern

The application has two principal executables:

```text
LightHostWinUI.exe
  WinUI 3 views, settings, localization, GitHub updater
        |
        | named-pipe commands and JSON snapshots/telemetry
        v
Light Host Modern.exe
  tray + HostIpcServer + LightHostController + AudioEngine
        |
        v
  JUCE AudioDeviceManager -> AudioProcessorPlayer
        -> RealtimeHostProcessor -> serial PluginSlot chain -> output
```

The UI does not perform audio DSP. Third-party plugin instances and their editor windows belong to the native host process. Closing the WinUI window clears its content/backdrop, stops its timer, and exits the UI process; the host remains when close-to-tray is selected. This is a useful separation, but **not plugin-process isolation**. A native plugin fault can still terminate the audio host. [Window shutdown][lm-close] [Engine][lm-engine]

Declared build stack:

| Area | Local configuration |
| --- | --- |
| Language | C++17; C++/WinRT for UI, not C#/.NET |
| Audio framework | JUCE 8.0.13 |
| VST3 SDK | v3.8.0_build_66 |
| VST2 | Optional; Xaymar v0.4.0 headers plus project shim, or legacy SDK provider |
| ASIO SDK | Pinned audiosdk/asio revision |
| UI | Windows App SDK 2.2.0; WinUI package 2.2.1; CppWinRT 2.0.250303.1 |
| Native build | CMake; WinUI MSBuild project uses v145 and Windows SDK 10.0.28000.0 |
| Packaging | Native MSI and complete portable ZIP in the current project |

These are the versions declared in the inspected files, not a claim about the latest upstream versions or every binary previously distributed. WebView2 appears in the UI package dependency list, but the application UI itself is native XAML, not React in a WebView. [CMake][lm-cmake] [Packages][lm-packages] [Project][lm-project]

The native engine has useful internal boundaries already: a controller facade, plugin-state store, device-control helpers, and a separate real-time processor. However, much of device policy, persistence, plugin lifecycle, and recovery still lives in the roughly 3,200-line `AudioEngine.cpp`. `MainWindow.xaml.cpp` is 5,183 lines and includes transport, parsing, rendering, settings, localization traversal, and updates. These are maintainability pressure points, not proof that the application is slow.

### 2.2 ReLightHost

```text
React / TypeScript / Ant Design / Zustand
  Tauri invokes, events, visibility-aware polling
        |
        v
Rust / Tauri core
  commands, device management, plugin instances, session/autosave
        |
CPAL input -> SPSC stereo ring buffer -> plugin chain
        -> main output, optionally mirrored to another output
```

The declared stack includes Tauri 2, CPAL 0.15 with ASIO, `ringbuf`, `parking_lot`, `vst`/`vst3` bindings, manually implemented CLAP hosting, `nnnoiseless`, Rayon, and Serde. Its frontend uses React 18, TypeScript, Vite, Ant Design, and Zustand. These versions in manifests may be semver ranges, not exact resolved versions. [Cargo][rh-cargo] [Frontend manifest][rh-package]

Core concerns are divided into `audio`, `plugins`, `commands`, `domain`, `core`, and `bootstrap`. The frontend separates chain cards, library, audio settings, and focused hooks. This separation is worth adapting without adopting Rust or React.

CPAL's Windows backends for the relevant series are WASAPI and optional ASIO. Generic comments mentioning DirectSound in ReLightHost are not evidence of a DirectSound implementation. Nor do cross-platform path branches prove feature parity outside Windows. [CPAL](https://github.com/RustAudio/cpal/tree/v0.15.3)

### 2.3 What changed from original LightHost

The original combines tray handling, audio configuration, plugin persistence, and graph rebuilding in `IconMenu`. Its chain reconstruction clears the graph, creates plugin instances again, and wires channel 1/2 serially. Light Host Modern already replaces that rebuilding pattern with reusable slots and published snapshots. That is an existing improvement, not something that needs to be imported from ReLightHost. [Original chain][original-chain] [Modern reconstruction][lm-load]

## 3. Feature comparison

| Capability | Light Host Modern | ReLightHost | Assessment |
| --- | --- | --- | --- |
| VST2 / VST3 | Yes; VST2 optional | Yes | No new feature to import |
| CLAP | No host format enabled | Host implementation exists | Useful compatibility expansion; substantial work |
| Audio channels | Driver channel names/masks and variable channel counts | Main DSP path takes the first stereo pair | Preserve local behavior; do not replace with stereo-only routing |
| Windows Audio variants | Shared, exclusive, low-latency variants through JUCE, plus DirectSound and ASIO | CPAL Windows path | No reason to replace current backend layer |
| Preferred/enabled devices | Configurable persistence, retry policy, allowed choices | Defaults and startup-specific retries | Local policy is more suitable for this user's requirements |
| Chain editing | Add, duplicate, reorder, bypass, remove, native editor | Add, reorder, swap, rename, bypass, native editor | Rename and optional explicit swap are useful additions |
| Search/sort | Already available in running/installed lists | Search and format filters; collapsible manufacturer groups | Grouping/details are new value; basic search is not |
| Plugin details | Identity, manufacturer, format, state and file location | Dedicated details dialog | Useful low-risk UX extension |
| Metering | Input/output aggregate peaks, callback load, xruns, latency diagnostics | L/R peak, RMS, peak hold, clipping indication | Better meters are worthwhile |
| Global output mute | No dedicated global mute command found | Dedicated mute plus tray synchronization | Useful; must be separate from global effect bypass |
| Built-in processors | No comparable bundled effects found | Compressor, voice processor, RNNoise suppressor | Optional product feature, not a host performance optimization |
| Secondary monitor output | No equivalent dedicated output path found | Processed signal can be mirrored to another output | Useful in some setups; clock/safety complexity is high |
| Session restoration | Active chain and plugin state persisted | Session configuration and autosave snapshot | Improve implementation details, not duplicate the feature |
| Named preset library | No complete user-facing workflow found | Snapshot/preset data structures, but no complete named preset UI found | Future idea, not an established ReLightHost differential |
| Localization | JSON catalogs, English and Brazilian Portuguese | Hardcoded English across inspected React views | Keep current localization system |
| Updates | GitHub MSI download, trusted URL checks, SHA-256 digest check | Tauri signed updater artifacts and release workflow | Independent signatures and CI are useful enhancements |
| UI closed | Separate UI process exits | Close-to-tray hides the WebView window | Keep local teardown; hidden does not imply zero GPU memory |

Evidence: [Local feature surface][lm-readme], [ReLightHost chain][rh-chain], [Library][rh-library], [Plugin commands][rh-plugin-commands], [Meters][rh-meter], [Audio path][rh-audio], [Built-ins][rh-builtins], [Session model][rh-preset], [App lifecycle][rh-app], [Update configuration][rh-config].

Neither inspected project is a full DAW with a general-purpose routing matrix, tracks, automation timeline, and a complete MIDI-input workflow. In the local engine, passing a `MidiBuffer` between processors does not itself establish external MIDI-device support. The local multi-channel handling also does not create independent chains per stereo pair: after a stereo-output processor, channels beyond that processor's outputs are cleared. This deserves explicit routing tests rather than broader capability claims. [Local processing][lm-process]

## 4. Performance assessment

### 4.1 Existing local strengths

- Plugin bypass is an atomic flag change, not a graph rebuild.
- Unchanged plugins can be reused during chain reconstruction instead of recreated.
- Scratch audio storage is preallocated; matched-channel processing is in-place.
- Retired snapshots are retained and normally collected away from the audio callback, reducing real-time teardown work.
- UI telemetry is already separate from full state, and version counters prevent rebuilding complete plugin lists on every timer tick.
- Settings writes have a dirty/debounce mechanism.
- The WinUI process can exit while the host continues audio processing.

Do not describe the entire callback as guaranteed lock-free: atomic operations on `shared_ptr` are not guaranteed lock-free by C++. The ownership design is useful, but a new reclamation mechanism should only be introduced if measurements justify its complexity. [Realtime processor][lm-rt] [Telemetry][lm-telemetry] [Settings persistence][lm-saves]

### 4.2 Where the UI still does unnecessary work

The WinUI dispatcher ticks every 50 ms. It performs synchronous pipe I/O, parses telemetry, and sets multiple controls. The guard skips updates during dropdowns, commands and drags, but does not select update rate by active page or minimize state. Even a Settings or Support view leads to dashboard-control updates. List rendering avoids unchanged rebuilds, but the lists are `StackPanel`s named `ListView`, not virtualized controls. [Timer][lm-timer] [Refresh][lm-refresh] [XAML lists][lm-lists]

ReLightHost supplies better patterns here: visibility-aware intervals, scoped subscriptions, lazy components, and chain-change events. Its VU polling also avoids overlapping requests. These concepts map naturally to a native IPC client, observable state, lazy page creation, and virtualized WinUI lists. The web framework itself is not required. [Visibility hook][rh-visible] [VU polling][rh-vu-ui] [Chain subscriptions][rh-chain]

Expected benefits are lower background CPU/IPC traffic and better UI responsiveness. No percentage saving is established yet. GPU allocations also depend on window lifetime and composition surfaces; merely reducing polling does not release those resources.

### 4.3 Why the other DSP path is not an automatic upgrade

ReLightHost moves samples through a stereo FIFO and bypasses processing on some lock contention. Its chain lock can wait up to 80 microseconds. VST2/VST3 adapters can resize scratch vectors for unexpectedly large blocks. These choices can reduce blocking relative to older versions, but they are not preferable to a properly bounded snapshot callback by default. [Chain processing][rh-instances] [VST3 processor][rh-vst3] [VST2 processor][rh-vst2]

The FIFO's allocated capacity is not its measured latency. The important quantities are actual occupancy, device scheduling, plugin latency, and clock drift. Likewise, the displayed buffer/rate cannot substitute for the driver's effective settings.

### 4.4 Metrics are not directly comparable

Light Host Modern obtains audio CPU load and xruns from JUCE's device manager. ReLightHost measures plugin-chain elapsed time as a smoothed DSP percentage; its ring underrun counter increments for missing samples, not necessarily one unit per callback failure. Its separate system-stat command reads the current process, not every WebView child process. Comparing their displayed percentages or memory values directly would be misleading. [Local diagnostics][lm-diagnostics] [Remote DSP][rh-audio] [System statistics][rh-system]

## 5. Local stability findings

These are source-level findings in the current local revision, not reproduced failures in this session. They are worth addressing before a broad feature expansion.

### 5.1 High: IPC timeout leaves asynchronous work with stack references

`HostIpcServer::processRequestOnMessageThread` captures `result` and `done` by reference in a message-thread callback, then waits up to 120 seconds. On timeout the function returns and destroys both locals, but the queued/running callback is not cancelled. If it later writes the result or signals completion, it uses invalid references. Host shutdown also needs a lifetime-safe handling policy for queued requests. [Source][lm-ipc-timeout]

Suggested direction: a request object with explicit shared lifetime, completion state, cancellation/shutdown handling, and well-defined ownership. Increasing the timeout does not repair this issue. Do not abort arbitrary driver/plugin calls or move them indiscriminately to worker threads.

### 5.2 High: synchronous transport and incomplete message framing

The UI's `requestHost` does blocking reads on the UI thread. Its 50 ms `WaitNamedPipe` wait only limits the initial availability check, not the subsequent response read. Slow driver initialization or scanning can therefore stall the whole interface. [Client][lm-client]

The server reads only 255 request bytes once. The client uses a 64 KiB buffer and stops on a successful read, but never changes the handle from its default byte-read mode to message-read mode. Long scan paths or large state snapshots can be truncated. This matters before introducing large plugin libraries or richer state. Windows documents those byte-read semantics explicitly. [Server framing][lm-ipc-run] [Microsoft named pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-client)

Suggested direction: one asynchronous transport owner, bounded message sizes, complete framing, structured errors and request IDs. Initially keep serial command ordering; a concurrency rewrite could introduce new device-selection races.

### 5.3 High: audio selection can open an intermediate device

Preferred-device code validates the requested target, then calls JUCE's `setCurrentAudioDeviceType` before applying the requested device setup. JUCE 8.0.13's implementation inserts default/previous device names and calls `setAudioDeviceSetup` internally. That can touch an intermediate device before the preferred target is selected. Some startup/fallback paths likewise initialize defaults and only afterwards call `closeCurrentAudioDeviceIfBlocked`. [Preferred selection][lm-preferred] [Initial/fallback handling][lm-device-init] [JUCE backend switch][juce-switch]

For this user's Focusrite/Voicemeeter setup, opening and then closing an unwanted driver is not equivalent to never opening it. This does not prove a presently observed hardware failure, but the sequencing risk exists in the inspected code.

Suggested direction: test and centralize allowed-target selection **before any device-open side effect**, including framework convenience APIs. Preserve the current user-facing persistence modes and retry policy. Avoid adding another independent watchdog.

### 5.4 Medium: a hidden real-time allocation and silent capacity limits

The expanded-channel branch constructs an external-storage `AudioBuffer` each block. JUCE's constructor allocates its channel-pointer array at 32 or more channels, despite using already allocated sample storage. Thus an expanded plugin layout with 32-64 channels can allocate/free in the callback. The usual stereo path does not trigger this. [Expanded buffer][lm-process] [JUCE allocation][juce-buffer]

Also, `neededChannels > 64` returns without processing or reporting an unsupported layout. The 64-channel limit is checked before the in-place branch, not only before scratch usage.

Suggested direction: prepare reusable buffer views/capacity outside the callback, clear only the necessary channels, and report unsupported layouts during chain preparation. Add an allocation-instrumented test with synthetic multichannel processors.

### 5.5 Medium: bypass transition and latency handling need tests

Latency is captured during prepare, while the bypass delay history is advanced only when bypassed. Switching from active processing can therefore expose zero/old delay contents, and no explicit crossfade is present. If the engine buffer has more channels than the delay buffer, bypass delay processing returns without compensation. Dynamic plugin-latency changes also need an explicit refresh policy. [Bypass delay][lm-bypass]

Suggested direction: retain latency-aware bypass but validate impulse response, active/bypass transitions, channel-count changes and dynamic latency. Use a bounded fade and correct delay history where appropriate. ReLightHost's immediate pass-through is not a solution to these cases.

### 5.6 Medium: scanning and plugin calls share host responsiveness/failure risk

Scanning runs synchronous `PluginDirectoryScanner::scanNextFile` loops on the host message thread. The crash-marker file helps remember bad plugins after a failure; it does not sandbox their DLLs or prevent a hung scan from blocking command completion. Plugin construction and state restoration are also in the host process. [Scanning][lm-scan] [Loading][lm-load]

A scan worker **process** with per-plugin time limits is a better first isolation boundary than a full out-of-process DSP bridge. It can protect discovery without adding inter-process audio latency.

### 5.7 Medium: UI state and editor lifecycle are more coupled than necessary

The UI manually searches JSON strings even though a real JSON parser is already used for localization and updates. The transport uses sorted indexes for many operations. Async state changes and future grouping make stale index commands harder to reason about. The local chain distinguishes instances by modifying plugin-description identity fields instead of using a separate application instance ID. [Parser][lm-parser] [Instance creation][lm-add]

Additionally, every `loadActivePlugins` call closes all plugin editor windows even when slots are reusable. That is avoidable editor churn. Prefer preserving windows for unchanged instances and closing only those whose processor is destroyed/reconfigured unsafely. [Reload entry][lm-load]

Suggested direction: typed DTOs, real JSON parsing, explicit application instance IDs with persistence migration, and incremental view updates. Split modules by responsibility while preserving behavior and native WinUI controls.

## 6. Ideas worth adapting

### 6.1 Event-driven state with adaptive telemetry

Priority: high. Expected effort: medium. Primary benefit: responsiveness and lower idle work.

ReLightHost emits chain-change events and uses slower/visibility-aware polling for other data. In Light Host Modern, retain full-state version counters; make structural refresh event-driven or coalesced, and use fast meters only where visible. A slow heartbeat can still detect host disconnection. Start with asynchronous requests and stable lifetimes before introducing a new event stream. [Events][rh-events] [Visibility][rh-visible]

No high-level UI event emission, JSON serialization or allocation should move into the audio callback. Publish counters/flags there and dispatch notifications from a non-real-time owner.

### 6.2 Metadata-first, incremental, isolated plugin discovery

Priority: high. Effort: medium to high. Benefit: scan safety and startup/library responsiveness.

ReLightHost tries VST3 static metadata and Windows version resources before loading a factory. That reduces unnecessary code execution. Its scan cache fingerprints directories, but still walks their contents, and fallback discovery loads plugin code in-process. [Scanner][rh-scanner]

The local JUCE scanner already skips unchanged known plugins, so a second generic cache is not automatically useful. Build on that mechanism. Metadata is useful for presentation, but should not replace validated plugin class IDs, bus information, or enumeration of multiple plugins within one module. A separate worker can produce authoritative descriptions and return progress, cancellation and timeout results. [Existing JUCE cache][juce-scan]

### 6.3 Modular UI, lazy pages and virtualized libraries

Priority: high for maintainability; performance priority rises with library size. Effort: medium.

Adapt the separation of chain, library, settings and focused state controllers. In WinUI, use native page/control boundaries, observable collections and bounded virtualized scrolling, not a translation of React components. Keep search, sort, selection, focus, localization, and accessibility behavior. Test 100, 500 and 1,000 installed entries before selecting a threshold or claiming a saving. [Remote library][rh-library] [Current XAML][lm-lists]

### 6.4 Rich meters and actionable diagnostics

Priority: medium. Effort: low to medium.

Add optional per-channel/stereo peak, RMS, peak hold, and clipping indication. The current aggregate peak is overwritten every block and clamped to 1, so it cannot reliably preserve a short overload for a much slower UI reader. Accumulate/hold the relevant values without locks or allocations, and use time-based decay rather than a coefficient tied to a particular block size. [Remote meter][rh-meter] [Local peak][lm-process]

Separately label callback DSP load, total application CPU, xruns, effective driver format, and plugin-chain latency. Low average CPU alone is not proof of reliable low-latency audio.

### 6.5 Rename instances, group the library and expose plugin details

Priority: medium. Effort: low to medium, except identity migration.

Instance labels help distinguish repeated plugins by purpose. Collapsible manufacturer groups and a details dialog improve large-library navigation. Explicit swap may help chain editing, but should not replace existing reorder. Store a user label separately from the real plugin identity and preserve it with the chain. [Rename/swap commands][rh-plugin-commands] [Library grouping][rh-library]

### 6.6 Global mute and optional whole-chain bypass

Priority: medium. Effort: low to medium.

ReLightHost has global output mute. This is useful at the tray as well as in the main UI. A whole-chain bypass is a separate potential feature: mute produces silence; bypass leaves dry audio. Define both carefully, use short ramps to avoid clicks, and decide whether mute keeps processors running to retain tails/state. Do not implement mute by reopening or changing the audio device. [Mute control][rh-audio]

### 6.7 Deferred persistence with a clear commit boundary

Priority: medium. Effort: medium; threading correctness matters.

ReLightHost queues autosave requests, debounces them and builds one snapshot rather than repeatedly searching the chain. Light Host Modern already debounces settings, but some plugin-state saves serialize and flush on the host message thread. Separate plugin-state capture, serialization, and disk commit without calling arbitrary plugin state APIs on unsupported threads. [Worker][rh-autosave] [Snapshot construction][rh-snapshot] [Local persistence][lm-saves]

Use a content hash excluding timestamps, atomic replacement/backups as appropriate, a schema version, and a shutdown flush that waits for the last committed snapshot. This is an enhancement of existing persistence, not evidence that local session restoration is absent.

### 6.8 Signed update artifacts and build automation

Priority: medium. Effort: medium, including key handling.

ReLightHost configures a public key for its updater and a GitHub Actions signing workflow. The local updater already validates a GitHub-provided digest. An independent signature adds publisher authentication beyond a checksum obtained from the same distribution endpoint. Adapt the principle to the existing MSI/ZIP pipeline rather than adopting Tauri. Own keys, protected CI secrets, rotation policy and clear verification failures are required. [Updater configuration][rh-config] [Release workflow][rh-workflow] [Local updater][lm-update] [Tauri signature documentation](https://v2.tauri.app/plugin/updater/#signing-updates)

### 6.9 CLAP support

Priority: optional roadmap item. Effort: high. Benefit: plugin compatibility, not automatic CPU savings.

ReLightHost shows that CLAP hosting, state and editor integration are feasible. Its adapter is small and manual, so the existence of `.clap` loading is not proof of comprehensive host-extension compliance. A local implementation must cover discovery, multiple descriptors, bus layouts, thread rules, parameters/events, state, editor lifecycle, latency and recovery. Evaluate a maintained compatible adapter or specification-based implementation before choosing an integration route. [CLAP adapter][rh-clap] [Official API](https://github.com/free-audio/clap)

### 6.10 Built-in processors and secondary monitoring

Priority: optional. Effort: medium to high for DSP; high for cross-device output.

Bundled voice processing reduces setup steps for new users, but duplicates capabilities available through plugins. ReLightHost's RNNoise wrapper explicitly requires 48 kHz and otherwise declines creation; it also buffers frames. Supporting this feature properly means reporting latency, rate restrictions and CPU cost, and making it optional. A basic gain/utility processor is a smaller first step. [Noise suppressor][rh-noise]

The secondary-output feature can aid headphone monitoring while sending audio to a virtual cable. It should not be copied as a generic loopback implementation: it mirrors processed audio to an output, rather than capturing all Windows playback. Cross-device clocks need drift/rate handling, channel policy, feedback protection, and independent disconnect handling. For the current user, Voicemeeter may already satisfy this need. [Output routing][rh-audio]

## 7. Choices not worth copying

### 7.1 Automatic driver fallback and fixed startup delays

The remote application can fall back to OS default devices, and its frontend schedules ASIO retries including a forced restart. These are not a substitute for the local preferred/enabled-device policy. Keep recovery in a single backend owner, not partially in UI timers. [Startup logic][rh-app]

### 7.2 Treating panic catching as native plugin sandboxing

Remote crash protection uses Rust `catch_unwind`; recovery resets the status after a cooldown and does not reconstruct the damaged processor. It does not establish containment for access violations, process aborts or hangs inside native plugins. The local C++ exception catches also do not provide process isolation. Use a worker process when that failure boundary is required. [Crash wrapper][rh-crash] [Rust documentation](https://doc.rust-lang.org/std/panic/fn.catch_unwind.html)

### 7.3 Silent dry-audio fallback on lock contention

A limiter, denoiser or other important processor unexpectedly becoming dry for a block is a functional change, even if it avoids waiting. ReLightHost's bounded lock wait is an improvement relative to its earlier immediate fallback, not a generally superior real-time architecture. Do not introduce this behavior into the local engine. [Processing locks][rh-instances]

### 7.4 Parallelizing serial DSP or all plugin initialization

In a serial chain, each effect needs the previous effect's output. Parallel work introduces scheduling/merging cost unless there are actually independent branches. ReLightHost also treats VST3 startup specially because of thread-affinity constraints; generic parallel loading is not a safe assumption for VST2 or CLAP either. Restrict parallelism to independent work that does not call thread-affine plugins. [Instance loading][rh-instances]

### 7.5 Copying autosave literally

The remote hash includes `created_at`, rebuilt from the current time, which defeats unchanged-content deduplication across timestamps. `get_state_binary` uses try-locks and can return empty when busy; snapshot creation does not preserve a previous blob in that case. The worker's shutdown path exits on the stop message without a mandatory final save/join. Its file save directly overwrites JSON. Preserve the idea of coalesced background persistence, not these details. [Snapshot][rh-snapshot] [Preset format][rh-preset] [Worker][rh-autosave] [State extraction][rh-instances]

### 7.6 Mistaking configuration or a Windows beep for verified audio

Remote stream setup uses `BufferSize::Default`; settings update the requested configuration and restart streams without an equivalent plugin re-prepare path in those commands. Its test sound launches a Windows system beep, not a signal injected through the chosen plugin chain. Neither operation by itself proves that a requested ASIO buffer, route or plugin chain is functioning. [Audio configuration][rh-audio] [Audio commands][rh-audio-commands]

## 8. Suggested decision order

| Stage | Scope | Why first/next | Main guardrail |
| --- | --- | --- | --- |
| A | IPC lifetime/framing fixes; asynchronous UI transport; pre-open device-policy validation | Direct stability and responsiveness risks | Preserve command ordering and existing routing semantics |
| B | Real-time allocation/capacity tests; bypass/channel tests; safe scan subprocess | Protect uninterrupted processing and discovery | No new waits, disk work or high-level events in callback |
| C | Adaptive telemetry, lazy views, virtualized library, focused UI/controller modules | Reduce background work and regression surface | Keep native WinUI design/localization and stable selection |
| D | Rich meters, instance labels, grouped library/details, global mute | Useful features with limited routing impact | Stable IDs and click-free output control |
| E | Persistence refinements; signed-update/build automation | More predictable maintenance and delivery | Do not lose existing saved state or keys |
| F | CLAP; optional built-ins; optional monitor output; user-facing preset library | Broader product roadmap | Separate proposals and compatibility matrices |

Stages are recommendations for discussion, not an approved implementation plan. There is no need for a wholesale refactor before every small feature. Extract a boundary when it supports a concrete change and add regression coverage at that boundary.

## 9. Validation needed before implementation

### 9.1 Controlled performance comparison

Use identical Release builds, physical device, driver/backend, sample rate, **effective** buffer size, enabled channel count, plugin binaries, preset/state and input signal. Start with 48 kHz at 64/128/256/512 frames, selecting only settings both drivers truly support. Do not compare a stereo route to a multichannel route as equivalent work.

Test empty chains, identical one/five/ten-plugin chains, bypass transitions and editor-open/closed states. Compare UI visible, minimized and closed. Sum all relevant processes, including WinUI or WebView children. Record warm and cold startup separately.

Measure callback duration distribution (median, p95/p99 and maximum), missed deadlines/xruns, callback heap allocations, CPU, committed/private memory, GPU dedicated/shared memory, and measured round-trip delay. At 48 kHz, 128 frames provide about 2.67 ms per block; this is a processing deadline, not total round-trip latency. Record all underrun definitions so counters are comparable.

Test enough sustained runtime and repeat trials to expose clock drift and scheduling spikes. Do not infer audio reliability from average CPU alone. Disable verbose debug logging for comparative performance measurements, then repeat selected failure cases with diagnostics enabled.

### 9.2 Focused stability tests

| Area | Cases |
| --- | --- |
| IPC | Delayed completion past timeout; shutdown with queued request; disconnected client; request over 255 bytes; reply over 64 KiB; Unicode and escaped JSON |
| Device policy | Missing preferred target on startup; disabled default device; backend change; Voicemeeter restart; retry exhaustion; user selection during retry; verify no forbidden driver is opened |
| Plugin processing | Stereo and 32/64/>64-channel layouts; variable blocks; bypass impulse response; latency changes; plugin C++ exception; process/scan crash and hang in an isolated fixture |
| Persistence | Multiple instances with different states; rename/reorder; abrupt exit during save; corrupt file; missing plugin; save while editor is changing state; version migration |
| UI | 100/500/1,000 entries; fast search/sort; unchanged telemetry; minimize/close/reopen; keyboard navigation; screen-reader names; PT-BR; high DPI |
| Updates | Missing digest/signature; altered download; interrupted download; install failure; existing MSI upgrade; portable-user behavior |

The local tracked tests currently emphasize startup registration and responsive/UI behavior. ReLightHost contains eight unit tests found in crash-protection and meter modules. Neither inspected set establishes an end-to-end audio benchmark or full driver/plugin compatibility matrix. Those existing tests were inspected, not executed, because the local integration tests can launch the host and change settings/Windows startup registration. [Local host tests][lm-host-tests] [Local UI tests][lm-ui-tests] [Remote crash tests][rh-crash] [Remote meter tests][rh-meter]

## 10. Licensing and evidence

Light Host Modern declares GPL-2.0-or-later. ReLightHost declares CC-BY-NC-SA-4.0, including NonCommercial and ShareAlike conditions. Do not assume that source being public or sharing the same original inspiration permits direct code/asset integration under the local license. The conservative route is independently implementing the selected concepts, or obtaining explicit compatible licensing permission before copying. This is a reuse-risk flag, not a legal opinion on either project's history. [Local license declaration][lm-readme] [ReLightHost license][rh-license] [Creative Commons FAQ](https://creativecommons.org/faq/#can-i-apply-a-creative-commons-license-to-software)

No code from ReLightHost was incorporated. Research checkouts and this report are under `out/research`, which is ignored by the local repository. The application's tracked files were not modified, and no commit, tag, push or release was performed.

### Source references

Local references below point to the files inspected in the shared workspace. Remote references are pinned to the inspected commit rather than a moving branch.

[lm-cmake]: C:/Users/matheus-heidemann/Files/Programming/LightHost/CMakeLists.txt:3
[lm-packages]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/packages.config:3
[lm-project]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/LightHost.WinUI.vcxproj:17
[lm-readme]: C:/Users/matheus-heidemann/Files/Programming/LightHost/README.md:9
[lm-engine]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.h
[lm-rt]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/RealtimeHostProcessor.cpp:123
[lm-process]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/RealtimeHostProcessor.cpp:255
[lm-bypass]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/RealtimeHostProcessor.cpp:61
[lm-load]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:2189
[lm-add]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:2423
[lm-saves]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:2773
[lm-diagnostics]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:389
[lm-scan]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:2115
[lm-preferred]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:903
[lm-device-init]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/AudioEngine.cpp:328
[lm-ipc-run]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/HostIpcServer.cpp:164
[lm-ipc-timeout]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/HostIpcServer.cpp:229
[lm-telemetry]: C:/Users/matheus-heidemann/Files/Programming/LightHost/Source/HostIpcServer.cpp:689
[lm-client]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:79
[lm-parser]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:181
[lm-timer]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:2141
[lm-refresh]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:3519
[lm-lists]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml:381
[lm-close]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:5165
[lm-update]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/LightHost.WinUI/MainWindow.xaml.cpp:2959
[lm-host-tests]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/host-integration-tests.ps1
[lm-ui-tests]: C:/Users/matheus-heidemann/Files/Programming/LightHost/WinUI/ui-tests-responsive.ps1
[juce-switch]: https://github.com/juce-framework/JUCE/blob/8.0.13/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp#L750
[juce-buffer]: https://github.com/juce-framework/JUCE/blob/8.0.13/modules/juce_audio_basics/buffers/juce_AudioSampleBuffer.h#L1209
[juce-scan]: https://github.com/juce-framework/JUCE/blob/8.0.13/modules/juce_audio_processors/scanning/juce_PluginDirectoryScanner.cpp#L92
[original-chain]: https://github.com/opencma/LightHost/blob/03cb4d0d711b6a5789c295d770f9b7de363779b7/Source/IconMenu.cpp#L123
[rh-cargo]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/Cargo.toml
[rh-package]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/package.json
[rh-audio]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/audio/manager.rs
[rh-audio-commands]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/commands/audio.rs#L137
[rh-instances]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/core/instance.rs
[rh-crash]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/core/crash_protection.rs#L93
[rh-scanner]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/core/scanner.rs
[rh-events]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/core/app_events.rs
[rh-autosave]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/core/autosave.rs
[rh-snapshot]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/core/snapshot.rs
[rh-preset]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/domain/preset.rs
[rh-plugin-commands]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/commands/plugin.rs
[rh-vst3]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/processor/vst3.rs#L389
[rh-vst2]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/processor/vst2.rs#L283
[rh-clap]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/processor/clap.rs
[rh-builtins]: https://github.com/hiimgyn/ReLightHost/tree/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/builtin
[rh-noise]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/plugins/builtin/noise_suppressor.rs#L51
[rh-meter]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/audio/vu_meter.rs
[rh-system]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/src/commands/system.rs
[rh-app]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src/App.tsx
[rh-chain]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src/components/chain/PluginChain.tsx
[rh-library]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src/components/plugin/PluginLibrary.tsx
[rh-visible]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src/lib/useVisibleInterval.ts
[rh-vu-ui]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src/components/layout/VUMeter.tsx#L94
[rh-config]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/src-tauri/tauri.conf.json
[rh-workflow]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/.github/workflows/release.yml
[rh-license]: https://github.com/hiimgyn/ReLightHost/blob/4a9c6d16f333cfa830f02e0c707563eefa66330d/LICENSE
