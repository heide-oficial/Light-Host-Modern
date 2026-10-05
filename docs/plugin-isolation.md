# Optional plugin isolation

The plugin menu in Running and in Chain offers **Run in a separate process (experimental)**. Confirming preserves the instance ID, saved settings, names, colors and connections. Existing profiles use direct execution unless the user opts in. **Run inside the host** returns that instance to direct execution. Isolation is crash containment, not a security sandbox for untrusted native plugins.

Scanner isolation is separate: discovering a plugin in `LightHostModernScanner.exe` does not opt its active instances into worker processing. The choice is saved per instance, so two instances of the same plugin can use different execution modes.

## Process and audio behavior

Each isolated instance owns a `LightHostModernWorker.exe` process. Its plugin, native or generic editor, preparation and state calls stay in that process. A kill-on-close Windows job owns the worker and its descendants. Loading, control messages and state capture use a controller thread with bounded deadlines. The host's message thread continues serving reads while a mutation waits for state capture. Normal UI/tray shutdown and duplication use the same mutation queue.

Audio and MIDI use preallocated shared memory, three owned slots and sequenced blocks. The host callback does not wait for the worker, allocate buffers or access files. A sample-count accumulator supports callbacks smaller than the worker block. Each worker block is the larger of the device block and 2 ms of samples rounded up to a power of two. The pipeline adds **two worker blocks per isolated plugin**, in addition to that plugin's own reported latency. At 48 kHz, a 64-sample device therefore adds 256 samples (5.33 ms); a 480-sample device adds 960 samples (20 ms). Chain compensation includes that latency. Several isolated plugins in series add their delays. Heavy scheduling pressure can still miss deadlines; isolation is not a way to reduce latency or CPU/memory use. The extra completed block is retained until its declared output time instead of being discarded early.

Only the worker's processing thread registers with Windows MMCSS as `Pro Audio`, using its critical relative priority. Registration is reverted when that thread exits; the controller and editor keep their ordinary scheduling. If MMCSS is unavailable, the thread retains the high-priority fallback and missed blocks remain visible in Diagnostics. See Microsoft's [task registration](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw) and [relative priority](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadpriority) contracts.

Missing/late blocks use delayed dry audio for effects, or silence for instruments. MIDI has a bounded per-block transport and validated lengths/offsets. Non-finite audio is rejected without clipping ordinary finite samples above unity. A failed worker affects its contribution, not other graph paths. The last valid state and routing stay available. **Retry loading** manually starts a fresh worker; there is no infinite restart loop. Disabled and auxiliary bus metadata remain distinct; a changed incompatible layout requires reloading the instance.

Diagnostics exposes each isolated instance's process ID, CPU, memory, total latency, worker block size, processed/missed blocks and dropped MIDI. Detailed logs record worker control stages and errors without logging state payloads. Process health checks remain active without the visual UI. State capture can pause the contribution of that worker while leaving other plugins running. Captures respect both the 192 MiB individual state limit and available space within the 256 MiB session budget, including encoding overhead. A rejected capture keeps the previous state.

Host/worker startup and state capture use bounded waits; the current controller allows 15 seconds for the initial pipe connection and for a capture reply. A processing slot that remains busy without progress for more than two seconds faults that worker. Audio-buffer dimensions and allocations are checked before preparation; an excessive plugin latency or memory request is an error rather than an unbounded allocation.

## State and channel contract

Session XML containing isolated instances uses at least schema 2, or schema 3 when channel configurations are captured. Older hosts must refuse unsupported session schemas rather than silently execute plugins directly or reinterpret channel mappings. User settings remain separate from portable payload versions; rollback does not overwrite newer user edits with a settings backup.

Worker protocol 3 uses 64-bit state revisions and associates each captured buffer
with the revision observed before capture. Disk writes and hashing run after the
audio lock is released; a newer edit remains pending for another capture. Diagnostics
separates capture time from persistence time. Host and worker must be rebuilt together.

The protocol also carries exact channel types, current/disabled bus layouts and
supported configuration choices. It no longer reconstructs every bus as discrete
channels. Channel changes are applied with processing stopped, then the host
recreates its proxy and prepares matching shared buffers. Changes initiated by a
plugin are detected before processing a block and reported through the worker
heartbeat; affected audio is silent while the new layout is prepared. Failed
configuration requests report their error and retain the previous layout when
the plugin accepts restoration. Restarted workers receive the saved bus layout.

## Testing

`Tests/IsolatedPluginTests.cpp` exercises real worker processes with controlled crashes and hangs, pipeline audio/MIDI, fragmented and tiny callbacks, serial instances, bus metadata and state-capture limits. Instrumented callbacks check host allocations and frees. The failure fixtures are compiled into `LightHostModernWorkerFixture.exe`; the production worker uses `LIGHTHOST_WORKER_TEST_FIXTURES=0`, and release packages exclude the fixture executable.

`Tests/IsolatedAudioIntegrationTests.ps1` checks worker processing with an explicitly selected real audio device. `Tests/IsolatedRealPluginTests.ps1` covers loading, editor lifecycle, synthetic audio, state capture and restore with supplied plugins; see [Real plugin validation](real-plugin-validation.md) for commands. Reports must retain the device settings, plugin versions and build hashes used for each run.

The feature remains experimental. Controlled fixtures and short integration checks do not establish long-session stability or compatibility with every plugin, driver and device. Review missed blocks, added latency and editor behavior for the intended setup.
