# Canvas selection, channel tabs and connection feedback

## Changes

- Empty-canvas right-click clears selection; right-drag panning preserves it.
  Right-clicking a selected card or its channels keeps bulk actions, with Delete
  selected elements last. An unselected card becomes the sole selection.
- The Chain plugin picker's enabled Add (N) button uses the accent color.
- Plugin audio channels has separate Input and Output tabs. Choices survive tab
  switches, including formats coupled by the plugin. Each side has bounded
  scrolling and a localized empty state.
- Connected cards use a solid foreground outline; disconnected cards use dots
  and retain subdued content. Selection, errors and bypass stripes are preserved.
- Wire dragging highlights the source and eligible target with a 180 ms opacity
  entrance and persistent outline. Performance mode and Windows reduced motion
  keep static feedback. Invalid, cyclic and duplicate targets are excluded.
- Preview wires render above cards. Connected bodies render behind cards; short
  segments of the original cubic and socket markers render above the cards.
  Coordinates use actual socket centers, including zoom, pan and DPI.
- Each XAML shape owns its dash collection, avoiding an invalid-argument failure
  when a collection is attached to more than one wire visual.

## Validation

- WinUI Release build completed. Native audio code was unchanged.
- ChainAuditUiTests -ExercisePointers passed against the final build, including
  keyboard connections, forward/backward dragging, wire-only menus, insertion
  into mixers and a real VST3 fixture, channel selection, cancellation and undo.
- Added checks for right-clicking a selected channel, clearing bulk selection on
  empty canvas, single-card menus afterward, and enabled Add (1).
- Plugin bus checks passed for Input/Output tab switching and coupled mono/stereo
  formats in both normal and isolated plugin hosting.
- Inspected scripted screenshots of drag feedback, connected endpoints, dotted
  borders, menus, accent Add and the Output channels tab. Evidence:
  out/test-profiles/chain-audit-ui-0606838d8b8d4bd99e4690145b0cfd8d.
- The test harness checks foreground-window ownership before pointer actions;
  keyboard actions still require the intended focused process. Captures during
  gestures preserve pointer capture.
- Only targeted validation was run, not the complete application suite.

## Development package

Version 2.0.0, portable build 0031, under dev-test/LightHostModern-build-0031.
Verified payload inventory, file hashes, launcher and current WinUI executable;
no test profiles or plugin fixtures are included.
