# Checkbox alignment and settings layout - 2026-09-12

> **Historical record.** The behavior, version, test results and pending work below belong to the original checkpoint. For current behavior see the [2.0.0 guides](_index.md); for current release status see [release preparation](release-2.0.0-validation.md). See [historical records](historical-records.md) for context. Generated evidence under `out/` is local and may have been removed; its paths are retained for traceability, not as release downloads.

Version remains 1.2.2.

- Input/output channels and enabled backend/device options use the same native CheckBox content layout: centered tight text bounds, eight-DIP content spacing, and wrapped labels. Device labels are part of the checkbox hit target.
- The scan paths dialog scrolls its description, Add new path card and saved paths together. The title and footer actions remain accessible.
- Preferred device keeps the picker beside its heading in Compact and Expanded. It remains 360 DIPs wide at normal page widths, with narrower limits when space requires it; long labels retain ellipsis.
- Diagnostics follows Settings in the sidebar. Its existing toggle is in General; its persistence, confirmation and collection behavior are unchanged.

Release build passed. All 18 CTest suites passed (9.07 seconds). Five focused UI scenarios passed, including measured checkbox text alignment/spacing, both preferred-device layouts, Diagnostics placement and complete scan scrolling. Visual inspection also found that the enabled-device dialog retained the system theme while the window used Light. The dialog now explicitly follows the window theme; its corrected light appearance was captured and inspected in a fresh UI process.

Evidence: `out/ui-polish-20260912/screenshots/results.json` and the adjacent screenshots. The corrected light-dialog capture is `out/ui-polish-20260912/theme-check/enabled-devices-light.png`. Portable artifact verification is stored under `out/release-ui-layout-fixes/`.
