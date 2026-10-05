# Primary modal action accent

The shared ContentDialog template always styles the primary action on the right
with AccentButtonStyle. Secondary/cancel actions remain neutral even when they
are the default keyboard action. A lone Close action, or a secondary action used
without a primary action, gets the accent in its right-hand position. Disabled
actions retain WinUI's disabled appearance.

Release UI compilation succeeded. No test suites were run for this styling
change. Portable build 0033 was generated and all 251 inventory entries verified;
its DialogStyles.xbf matches the new build and differs from build 0032.

Portable: dev-test/LightHostModern-build-0033/LightHostModern.exe.
