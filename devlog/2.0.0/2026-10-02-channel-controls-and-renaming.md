# Channel controls and consistent rename actions

## Changes

- Replace the Audio input/output mono switches with Input mode and Main output
  mode dropdowns (Stereo / Mono). Keep existing per-device preferences, guarded
  commands and DSP behavior. Place both above Channel selection with a divider,
  and use the same names and choices in hardware card context menus.
- Move Configure plugin channels into the plugin card's main context menu,
  directly above Input channels. Order supported formats Stereo, Mono, Disabled,
  followed by other supported formats without losing their exact layouts.
- Explain in the grouping tooltip and documentation that Individual/Pairs changes
  presentation only. Existing stereo-to-one-channel wires remain stereo-to-mono;
  reconnect to the pair to use both channels.
- Focus Name when opening the new-profile dialog. Preserve the currently focused
  control when applying a material reparents dialog content, and wait for layout
  before assigning initial focus to name fields.
- Keep only Rename in plugin context menus. Move Restore original name into the
  rename modal, with sufficient width for its buttons. Channel/device/card rename
  dialogs retain the same pattern.

## Validation

- Release WinUI build completed. Native routing and worker binaries are unchanged
  from development build 0028.
- `Tests/ChainAuditUiTests.ps1 -ExercisePluginBuses` passed: profile Name focus,
  menu placement, format order, rename/restore through the plugin catalog, actual
  VST3 coupled mono/stereo changes, retained wires and isolated-worker behavior.
  Captures were inspected in
  `out/test-profiles/chain-audit-ui-aac3bbd48b4d445ab1a152834fdd09d4/captures`.
- English and Portuguese locale JSON validated without duplicate keys;
  `git diff --check` passed.
- `Tests/ModernizationUiSmoke.ps1 -SimulatedAudio -Layouts Expanded` passed in
  English and Portuguese at 200% DPI. Verified repeated Stereo/Mono changes,
  input/output independence, pending commands, grouping persistence and channel
  enablement. The simulated device leaves the user's audio untouched. Captures
  were inspected in
  `out/test-profiles/modern-ui-712939cc99b44bbeb81bcd004687d45c/profiles/redesign-4e8ae0d7e83645939629f1e01cb73e03`.
- Generated `dev-test/LightHostModern-build-0029/LightHostModern.exe` (2.0.0).
  Verified all 250 payload hashes, manifest hash, launcher and native/UI binary
  identity against the validated build. No test fixtures are packaged.
