# Chain menus, insertion channels and tray order

## Changes

- Wire context menus contain only Disconnect, even when cards are selected.
- Group canvas actions as Add plugin / Add mixer, Organize, and Mute output /
  Bypass chain. Group card actions by plugin operations, appearance, channels and
  removal. Keep experimental process isolation after Duplicate. Move directional
  disconnection into the matching channel submenu, disabled when no wires exist.
- Apply the corresponding groups to multiple selected cards, including bulk card
  color, orientation, layering and directional disconnection.
- Place Add to chain before the Plugin details / Open folder group in installed
  plugin menus, both in List mode and the Chain plugin picker.
- Let insertion confirmation select visible input/output channels. Pairs uses a
  pair selector; stereo wires in Individual view offer separate Left/Right
  selectors. Block duplicate input assignments, retain the original endpoints,
  validate delayed selections and apply the complete insertion as one undoable edit.
- Flatten Chain tray entries using the same dependency traversal as the routing
  runtime, including paths through mixers. Parallel branches have stable ordering;
  card positions do not affect it. Keep unavailable instances accessible and
  preserve List mode order.

## Validation

Release native and WinUI builds passed. PluginInstanceTests passed, including
parallel paths, mixer traversal, unavailable instances, position independence and
List ordering (`out/chain-tray-order-tests.log`).

`Tests/ChainAuditUiTests.ps1 -ExercisePointers` passed against the final binaries
(`out/chain-menus-ui.log`). Scripted mouse/keyboard and UI Automation checks cover
wire-only menus, card/canvas/multiple-selection/installed menus, pair selection,
individual Left/Right mapping, duplicate input rejection, cancellation, undo,
port dragging and pending-edit recovery. Screenshots and result JSON are under
`out/test-profiles/chain-audit-ui-8399a3a0aaad415d9777f84bc9f4443a`.

The extracted development portable is
`dev-test/LightHostModern-build-0027/LightHostModern.exe`. Packaging verifies the
UI build stamp; packaged host/UI hashes match the tested binaries. These are
targeted checks, not a repeat of the complete audio/audit suite.
