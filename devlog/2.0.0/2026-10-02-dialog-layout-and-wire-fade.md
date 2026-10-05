# Dialog layout and wire endpoint fade

## Changes

- Foreground socket leads now fade into the wire body using alpha gradients on
  the existing cubic segments. Endpoint colors and the behind/above-card layers
  are preserved. Gradient geometry is updated only when a wire moves or changes
  color; no additional animation timer is introduced.
- Plugin audio channels follows the Plugins page: title, explanation, icon tab
  bar, and separate channel cards with format dropdowns. Each side scrolls within
  a bounded area, and coupled input/output choices survive tab changes.
- All ContentDialogs use one explicit shared template: cancel/back on the left,
  secondary action in the middle, primary action on the right. A lone close action
  stays on the right. Default buttons and keyboard behavior are retained.
- Dialog styles and materials are prepared before ShowAsync. Grid-backed surfaces
  receive their backdrop without removing/reinserting existing controls. This
  removes the live reparenting that could invalidate focus or pointer capture
  during the first click.

## Validation

- Release WinUI build succeeded. Native audio sources were unchanged by this work.
- Actual VST3 fixture checks passed for the new channel layout, tab switching,
  coupled mono/stereo changes, preservation/restoration of connections and the
  separate-process metadata path. Inspected modal and wire screenshots in
  `out/test-profiles/chain-audit-ui-fc8e85253987429b9b91f4b07aa07e39`.
- The packaged build passed `ChainAuditUiTests -ExerciseDialogs`: cancel, save,
  discard, both restart prompts, button order and a real isolated host/UI restart.
  The restart applied List mode and began verbose collection. Evidence:
  `out/test-profiles/chain-audit-ui-efb3dbf4ab4f4371970530f8ced690e2`.
- These final modal checks used one UI Automation invocation per action. Physical
  pointer checks could not be completed reliably while VMware received desktop
  focus/cursor input. `-PhysicalDialogClicks` remains available for that check;
  it rejects input interference rather than retrying clicks.
- No full application test suite was run. Temporary event instrumentation was
  removed before the final build.

## Development package

Version 2.0.0, extracted portable build 0032:
`dev-test/LightHostModern-build-0032/LightHostModern.exe`.
Verified all 251 payload files against their inventory, sizes and SHA-256 hashes,
plus the current UI executable, launcher and native executables. No test profiles,
fixtures or temporary dialog tracing are packaged.
