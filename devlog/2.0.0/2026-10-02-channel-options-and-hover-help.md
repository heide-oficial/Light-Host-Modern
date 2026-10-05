# Channel menus, multi-selection and hover help

## Changes

- Restoring a pair's original name clears its explicit pair alias and both
  inherited individual aliases. Names on other pairs remain unchanged. This
  applies to device channels and canvas plugin/mixer channels.
- Split channel visibility menus into Enable channels and Disable channels.
  Entries contain only channel names; partially enabled hardware pairs can
  appear in both menus. Connected plugin channels remain protected from hiding.
- Show Pairs before Individual. Remove the divider between the Audio mode and
  grouping controls and shorten Main output mode to Output mode. DSP behavior
  and existing wire endpoints are unchanged.
- Allow multi-selection in the Chain plugin picker. Add (N) includes selections
  retained through filtering and sorting. Selected cards use the app accent
  color without a checkbox gutter. The context menu contains catalog actions.
- Add localized hover explanations for audio, channels, profiles, plugin and
  canvas controls. General > Enable tooltips when hovering options controls
  these and Diagnostics immediately; Settings cards keep their descriptions.
  The setting is independent of notifications and preserves screen-reader help.

## Validation

- Release native and WinUI builds completed.
- DeviceRecoveryTests passed, including a regression for individual aliases,
  restoration in pairs, and preservation of unrelated channel names.
- ChainAuditUiTests -ExerciseChannelOptions passed with two actual VST3 fixture
  entries: Add (N), selection through search/sort, adding both plugins, channel
  enable/disable menus, pair-name restoration, and Diagnostics tooltips on/off/on.
  Evidence: out/test-profiles/chain-audit-ui-398fce3a63f048f5b62e0e38ce8c71b5.
- ModernizationUiSmoke -SimulatedAudio -Layouts Expanded passed in English and
  Portuguese at 200% DPI. Verified audio modes, grouping, saved preferences,
  channel enablement and all 21 accessible diagnostic explanations. Evidence:
  out/test-profiles/modern-ui-85f63f18114b4733bc486a4b5fb62072.
- Inspected scripted captures of the selected cards, audio controls, General
  setting and live Diagnostics tooltip. The test harness avoids injecting Alt
  while hovering, and static picker captures preserve selection and focus.
- Locale JSON validation and git diff --check passed. Only targeted validation
  was run; the complete application suite was not rerun.

## Development package

Portable build 0030, version 2.0.0, is published under
dev-test/LightHostModern-build-0030. Payload and launcher hashes are checked
against the compiled artifacts; test profiles and fixtures are not packaged.
