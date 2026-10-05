# Audio processing

LightHostModern 2.0.0 uses JUCE's `AudioDeviceManager` and `AudioProcessorPlayer` with a custom `RealtimeHostProcessor`. List mode runs plugin instances in list order. Chain mode runs the connected graph, supporting branches, mixers and explicit channel routing; the list position does not determine its signal order.

## Chain snapshots

The message thread builds a `ChainSnapshot` containing:

- current sample rate and block size;
- enabled input/output channel counts;
- maximum plugin channel requirement;
- total latency;
- ordered shared plugin slots;
- the operating mode, routing graph, physical channel mappings and compiled routing plan.

The completed snapshot is published atomically to the realtime processor. The audio callback reads one immutable snapshot for the entire block, so reorder, duplicate, removal, bypass, or device changes do not mutate its structure halfway through processing.

Old snapshots are retired and collected outside the callback. Compatible plugin slots are reused during rebuilds to avoid destroying and recreating processors unnecessarily.

## Processing a block

For every audio block:

1. The input peak is calculated.
2. Enabled inputs are packed by JUCE. If mono mixing is enabled for the current device pair, their unity-gain sum is faded into the main stereo pair over 5 ms, before dry/bypass capture and plugins.
3. List mode processes slots in list order. Chain mode executes the prepared graph in dependency order, processing only nodes that lead to a physical output.
4. Bypassed slots pass audio through their compensation path.
5. Failed slots are disabled for later blocks.
6. Global latency-compensated bypass and mute are applied.
7. If enabled and both physical principal outputs are active, output mono blends their signals toward their average over 5 ms. Auxiliary outputs and lone principal outputs retain their signal.
8. The stream-resume gain is applied, and output meters measure only actual output channels before JUCE sends them to the device.

An empty List passes compatible inputs to outputs. In Chain mode, the graph's connections determine which inputs and plugin outputs reach the physical outputs; a disconnected route contributes silence. A routing plan that cannot be prepared leaves graph output silent and exposes an error.

## Graph routing

The controller validates the graph and compiles its processing order, incoming edges, channel buffers and compensation delays before publication. Cycles are rejected. The graph supports at most 128 nodes, 512 connections and 256 channels per node. Mixer gains and mutes use prepared targets, ramping by one unit of gain per 5 ms. Nodes disconnected from every physical output are omitted from the processing order. See [Chain and profiles](chain-and-profiles.md) for editing behavior.

## Channel handling

Plugins can expose mono, stereo, or other channel layouts. The scratch buffer supports up to 256 channels, and each slot records its input/output capabilities. List mode carries the main bus through successive plugins; auxiliary inputs receive silence and auxiliary outputs are not carried into the next slot. With mono input mixing enabled, List mode centers a mono plugin's main output in the main stereo pair. Chain mode instead follows each connection's saved source/target channels and widths, including explicitly routed auxiliary buses: mono-to-stereo duplicates the signal, while stereo-to-mono averages the pair. Changing Individual/Pairs presentation does not rewrite those connections. Summing inputs or mixer lanes can exceed 0 dBFS; there is no automatic normalization or limiter.

Plugins that expose no usable audio input/output configuration can be rejected when added to the running chain.

At device start, the active physical output mask maps outputs 1/2 to JUCE's packed buffer positions. This map and the output count are prepared while the callback is excluded. Output mono reads an atomic preference; it adds no callback allocation, device enumeration, settings access or lock. The transition continues from its current blend when toggled rapidly. Global bypass keeps this final monitoring transformation active; global mute still silences it. Input meters remain ahead of input mixing, and output meters include output mono even with Diagnostics disabled.

## Bypass and latency

Each slot stores the latency reported by its processor. When bypassed, a delay buffer passes the dry signal with equivalent latency. This keeps downstream timing aligned and avoids changing total chain latency merely because an effect was bypassed.

Global bypass blends to the latency-compensated direct device-input path, independently of List order or Chain connections, while active plugins continue processing. Global mute applies after that blend. Both flags can be saved and restored with an operating profile; see [Persistence and recovery](persistence-and-recovery.md).

Diagnostics reports the prepared processing latency in samples. In List mode this is the sum of prepared slot latencies. In Chain mode it follows the longest connected path to the output; shorter incoming paths receive compensation delays before they are combined. Optional isolated instances include their worker pipeline delay in the reported slot latency.

## Realtime safety

The callback avoids settings writes, UI work, plugin database mutation, and normal logging. Process exceptions are caught at the slot boundary, recorded atomically, and reported later from a non-realtime timer. Rebuilds, state saves, retired snapshot cleanup, and diagnostic logging run away from the callback.

Directly hosted plugin code can still crash or hang the host after an unrecoverable native fault. [Optional isolation](plugin-isolation.md) contains such failures in a separate process, with a buffered delay per isolated instance and additional memory use. The exact delay is reported and included in routing compensation. It is not a security sandbox.

Sample rate, channels, block and latency are checked before buffer allocation. Compensation is limited to ten seconds, the routing plan to 64 MiB and host-owned audio reservations to 256 MiB including replacement buffers. Preparation publishes a complete valid plan; an invalid new plugin latency quarantines that processor rather than allocating from an unchecked value. These limits do not control arbitrary allocations made by a directly hosted third-party plugin.

NaN/Infinity samples are contained at plugin and output boundaries. Finite samples above unity remain valid; this guard is not a limiter. Fully bypassed paths copy dry samples directly, avoiding contamination by invalid wet samples. MIDI is bounded and only reserved on nodes that use it. Active processing order and incoming edges are compiled ahead of time; visual telemetry is reduced when it has no consumer.
