# Chain wire interactions

## Changes

- Transfer pointer capture from the port button to the canvas. Observe handled
  move/release events and resolve the drop against rendered port bounds so zoom,
  pan and Windows DPI do not prevent completing a connection.
- Preview insertion when a wire crosses the inner area of a disconnected plugin
  or mixer card, instead of requiring its exact center to touch the wire.
- Insert through the first visible input/output and preserve stereo spans in
  Individual view. Confirmation replaces the original edge with two edges in one
  undoable operation. Cancel keeps only the card position. Guard delayed dialog
  completion against profile/generation changes and retain pending edit recovery.
- Highlight hovered/insertion-target wires with a thicker line and halo, plus a
  localized context hint. Proximity uses screen-space tolerance at every zoom.
  Clear feedback on exit, cancellation and edge removal; measure hint text
  independently of its position to avoid moving/jumping feedback.

## Validation

`Tests/ChainAuditUiTests.ps1 -ExercisePointers` exercises real mouse gestures and
Windows UI Automation in an isolated profile. `Tests/ChainPointerScenarios.ps1`
covers forward and reverse port drags, a three-second hold before release,
wire hover/context disconnect, mixer and real fixture-VST3 insertion, cancellation,
undo, and stereo preservation while plugin ports are displayed individually.
The original keyboard and pending-edit recovery checks remain in the parent suite.

The same forward-drag scenario fails on development build 0025 (no connection
persisted), without taking any screenshot during the drag:
`out/chain-pointer-baseline-final.log`.

The corrected build passes the gesture scenarios, with screenshots and JSON
results under
`out/test-profiles/chain-audit-ui-e21dc0f5b5fb4b5eb6f405e8fce6bb28`.
`out/chain-pointer-hold.log` records the full run.

The capture helper now supports passive interaction capture without activating
the window, injecting Alt, or sending WM_PRINT. Gesture assertions run without
capture during a held pointer: nested screenshot/automation collection was found
to disturb some synthetic release events. Screenshots are taken after release
and during hover. Synthetic pointer motion explicitly emits a mouse-move event;
SetCursorPos alone did not reliably notify WinUI of hover changes.

No audio-engine code or remote repository state was changed for these fixes.

## Development portable

Release WinUI compilation and development packaging succeeded. The extracted
portable is `dev-test/LightHostModern-build-0026/LightHostModern.exe`.
The complete targeted suite also passes when launched from that portable:
`out/chain-pointer-portable-0026.log`, with `pointer-result.json`, `ui-result.json`
and screenshots in
`out/test-profiles/chain-audit-ui-3d51e8dc17bc416bb6036be3e756a2ac`.
PowerShell parsing, both locale JSON files (including duplicate-key checks), and
`git diff --check` pass. The full audio/audit suite was not rerun for this UI change.
