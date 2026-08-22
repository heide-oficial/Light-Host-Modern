# Audio processing

Light Host Modern uses JUCE's `AudioDeviceManager` and `AudioProcessorPlayer` with a custom `RealtimeHostProcessor`. The processor runs installed plugin instances in one serial chain.

## Chain snapshots

The message thread builds a `ChainSnapshot` containing:

- current sample rate and block size;
- enabled input/output channel counts;
- maximum plugin channel requirement;
- total latency;
- ordered shared plugin slots.

The completed snapshot is published atomically to the realtime processor. The audio callback reads one immutable snapshot for the entire block, so reorder, duplicate, removal, bypass, or device changes do not mutate its structure halfway through processing.

Old snapshots are retired and collected outside the callback. Compatible plugin slots are reused during rebuilds to avoid destroying and recreating processors unnecessarily.

## Processing a block

For every audio block:

1. The input peak is calculated.
2. Channel data is adapted to the scratch buffer and chain requirements.
3. Each slot is processed in list order.
4. Bypassed slots pass audio through their compensation path.
5. Failed slots are disabled for later blocks.
6. The result is copied to the configured output channels.
7. The output peak is calculated.

The empty-chain path still routes compatible inputs to outputs instead of producing silence.

## Channel handling

Plugins can expose mono, stereo, or other channel layouts. The scratch buffer supports up to 64 channels, and each slot records its input/output capabilities. The host adapts between the opened device and plugin requirements rather than assuming every processor is stereo.

Plugins that expose no usable audio input/output configuration can be rejected when added to the running chain.

## Bypass and latency

Each slot stores the latency reported by its processor. When bypassed, a delay buffer passes the dry signal with equivalent latency. This keeps downstream timing aligned and avoids changing total chain latency merely because an effect was bypassed.

The Dashboard reports the sum of active slot latencies in samples.

## Realtime safety

The callback avoids settings writes, UI work, plugin database mutation, and normal logging. Process exceptions are caught at the slot boundary, recorded atomically, and reported later from a non-realtime timer. Rebuilds, state saves, retired snapshot cleanup, and diagnostic logging run away from the callback.

This design reduces realtime-thread risk, but plugin code executes in-process. A plugin that performs an unrecoverable native fault can still crash the host.
