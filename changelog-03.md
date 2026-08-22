# Light Host Modern 1.2.0

## Added

- Added a completely redesigned native WinUI 3 interface based on Windows 11 Fluent Design.
- Added compact and expanded layout modes with consistent responsive behavior across every page.
- Added an in-app update notification backed by the latest GitHub release.
- Added a Support me page with Ko-fi, GitHub repository, and video showcase actions.
- Added JSON-based localization with automatic English fallback.
- Added Brazilian Portuguese translation and a runtime language selector.
- Added plugin search and contextual sorting for running and installed plugins.
- Added dedicated dialogs for preferred audio device selection, enabled-device management, and plugin scan paths.
- Added community translation instructions and modular user and internal architecture documentation.
- Added accessible labels, Automation IDs, and explicit On/Off state text for settings toggles.

## Removed

- Removed page-specific width limits and inconsistent legacy layouts.
- Removed hardcoded UI strings in favor of external JSON language catalogues.
- Removed inline preferred-device selectors in favor of a dedicated modal workflow.
- Removed separate Restore defaults and Add path footer actions from the scan-path dialog.
- Removed premature plugin rejection based only on channel metadata reported during scanning.
- Removed the behavior that forced the interface to open maximized.

## Improved

- Reworked Dashboard, Audio, Plugins, Support me, and Settings with a consistent Fluent card-based structure.
- Improved responsive behavior for narrow and wide windows.
- Improved Running and Installed plugin organization, status indicators, actions, search, and sorting.
- Improved Settings organization by separating retry interval and maximum attempts into individual cards.
- Improved preferred-device selection with explicit Save and Cancel actions.
- Improved scan-path management with an Add new path card that immediately focuses its editor.
- Improved enabled-device and scan-path dialogs with wider responsive layouts and native centered positioning.
- Improved menu and flyout positioning near window edges.
- Improved input focus behavior when clicking page or dialog backgrounds.
- Improved Windows backdrop handling for Mica, Mica Alt, Acrylic, and Solid.
- Improved collapsed-sidebar branding and accessibility.
- Improved release packaging so the current WinUI build and every locale catalogue are included.
- Improved README and project documentation for users, translators, contributors, and maintainers.

## Fixed

- Fixed RNNoise and other VST3 plugins being rejected when scan metadata incorrectly reported zero input and output channels.
- Fixed restored plugins with incomplete scan metadata being rejected before their instantiated audio buses could be inspected.
- Fixed responsive dialogs becoming anchored to the left edge.
- Fixed controls clipping or overflowing at smaller window sizes.
- Fixed plugin action buttons, hover surfaces, sorting menus, and flyouts being cut off.
- Fixed the Ko-fi banner being clipped on the Support me page.
- Fixed numeric and text inputs retaining keyboard focus after clicking outside them.
- Fixed inconsistent container widths between application pages.
- Fixed window material changes not producing the expected visible backdrop.
- Fixed release builds packaging an outdated WinUI shell or omitting locale files.
