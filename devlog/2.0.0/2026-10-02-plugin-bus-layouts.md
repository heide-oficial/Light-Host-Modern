# Plugin channel identities and configuration

## Changes

- Read channel types from actual plugin buses. Pair Left/Right only when they
  belong to the same bus; keep discrete channels distinct and retain exact types
  through the isolated-worker protocol.
- Add Configure plugin channels to Chain channel submenus and List running-plugin
  menus. Offer supported current/default, mono, stereo and common surround layouts,
  with disabled buses where supported. Display related input/output changes when
  the plugin requires them together. Apply with profile/generation and previous
  layout guards, and restore the previous layout if a plugin rejects the change.
- Query VST3 capabilities only while the processor is released. JUCE's active
  instance check otherwise accepts representable layouts without asking the
  plugin. Restore prepared audio after an in-host query; worker metadata queries
  already stop processing and await a fresh shared-buffer preparation.
- Store logical ports independently of processor buffer offsets. Preserve channel
  identities, aliases, colors and wires when optional buses shift offsets. Retained
  unavailable ports are labeled, cannot receive new wires, and have dashed silent
  connections. Restoring the same channels restores their routes.
- Persist typed layouts in session schema 3 and profiles; refuse reinterpretation
  by older hosts. Adopt legacy labels conservatively when matching saved ports.
- Detect plugin-initiated layout changes, pause affected processing and rebuild
  the prepared routing. Use nonblocking callback locks and allocation-free layout
  comparisons. The array-reference comparison avoids copying large channel sets
  in the audio callback.
- Worker protocol 2 carries exact bus layouts and configuration choices. Recreate
  proxies/shared buffers after changes. Prepare requests bind to the expected
  layout, including changes requested before the first mapping is ready.
- Correct the wire conversion label to Stereo -> mono / Mono -> stereo. Downmix
  remains the L/R average; upmix duplicates mono. Keep unavailable status out of
  rename dialogs, and handle pair aliases when logical L/R indices start odd.

## Validation

- Release WinUI and native host/worker builds completed.
- Plugin-instance persistence, realtime routing/allocation audit and IPC protocol
  regressions passed. The realtime scenarios cover shifted optional buses,
  unavailable retained endpoints, stereo/mono conversion and legacy migration.
- Isolated-worker regressions passed, including early channel configuration,
  plugin-initiated layout changes, exact channel types, persisted optional buses,
  latency/MIDI alignment, allocation audit and crash/hang containment.
- `Tests/ChainAuditUiTests.ps1 -ExercisePluginBuses` passed with the actual
  Scenario Fixture VST3, both directly and in a separate worker. Unsupported
  layouts are absent; coupled mono changes, preserved unavailable wires and
  stereo restoration were confirmed. Captures were visually inspected in
  `out/test-profiles/chain-audit-ui-b1b58b6141a041228b95ff833f942905/captures`.
- Broader pointer passes were interrupted by desktop cursor/focus changes. They
  are not counted as complete passes; the dedicated channel UI regression uses
  control automation and completed successfully.
- English/Portuguese JSON keys and conversion-arrow text verified;
  `git diff --check` passed.
- Extracted development portable generated at
  `dev-test/LightHostModern-build-0028/LightHostModern.exe` (2.0.0).
  All 250 payload files matched their manifest hashes; launcher, host, worker,
  scanner, update helper and WinUI matched the current build. Test fixtures are
  absent from the portable.
