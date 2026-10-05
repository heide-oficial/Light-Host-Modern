# Tray and window behavior

The JUCE host owns application lifetime and can continue processing audio without keeping the WinUI window open.

## Notification-area icon

The icon is created by the host process. Its context menu contains:

- **Open app UI** - launches, restores, or focuses the WinUI shell;
- **Running** in List mode or **Plugins** in Chain mode - lists plugin instances in processing order, each with its own action submenu;
- **Installed** - adds a new instance and opens its editor; List appends it to the serial chain, while Chain creates an unconnected card;
- **Mute output** - toggles the host-owned output mute;
- **Bypass chain** - toggles latency-compensated global bypass while processors continue running;
- **Performance** - opens live CPU, dedicated VRAM, and private RAM readings on hover;
- **Quit** - saves state, closes the shell, stops audio processing, and exits the host.

The selected Color, White, or Black icon variant is applied to both host and interface assets where supported.

Both left and right clicks open this Quick Access menu. Use **Open app UI** to open the main window. Plugin editors and tray actions work while the WinUI window is closed.

Running/Plugins uses saved instance names, format labels, processing positions and bypass/unavailable status. Each instance appears once with **Open editor**, **Bypass**, **Duplicate**, and **Remove from chain**. List mode also includes **Move up** and **Move down**. In Chain mode, the flat list follows graph dependency order, including paths through mixers; moving cards or changing layers does not alter that order.

Removal asks for confirmation and keeps the installed database entry. Duplication creates an independent instance and opens the new editor. In List mode it is inserted after the original; Chain duplicates appear without connections. Minimized editors are restored; plugins without a native editor use the existing generic editor fallback.

Installed uses the same host database and saved aliases as the Installed tab, ordered alphabetically by display name. It respects the saved **Group by manufacturer** preference; temporary search filters and visual sorting in the window do not hide or reorder tray entries. Selecting an already-running installed plugin creates another instance. Large lists are split into numbered ranges of at most 30 entries per page, without dropping plugins.

Menus are rebuilt on opening, not polled in the audio callback. Construction reads metadata only: it does not scan disks, load plugins or probe network paths. Actions carry persistent instance/class IDs and resolve the current target after selection (and again after removal confirmation), so changes while the menu is open cannot redirect an action to a different plugin. Checked bypass/mute actions apply the displayed intent. Read-only recovery sessions disable mutations. Unloaded instances cannot open an editor or be duplicated; failed additions retain their recoverable host record and show an error. Host version counters synchronize changes back to an open interface.

The popup uses a local JUCE appearance so its complete menu tree follows **Settings > Appearance > Theme**: Light, Dark, or the Windows app theme for System. The saved preference and current Windows appearance are read on each opening, including while WinUI is closed. Windows contrast themes take priority and use the system menu, text, highlight and disabled-text colors. Changing the tray palette does not change plugin editor themes.

The menu's invisible anchor follows the monitor where the tray is clicked. JUCE's per-monitor DPI support scales the popup and its submenus; physical coordinates are converted to logical coordinates before placement. The independent Windows **Accessibility > Text size** setting is read through `UISettings.TextScaleFactor` on each opening. Font size and row height grow together, and menu widths are measured from the scaled font. DPI is not multiplied into the font a second time. Detailed logs include the text factor, monitor scale and logical row height. These settings also apply with the main window closed.

The popup runs asynchronously on the host and is dismissed during host shutdown. Its appearance stays alive through submenu teardown, and actions resolve after the menu closes. English and Brazilian Portuguese labels follow the current interface language. No extra UI process is launched for plugin access.

Performance refreshes once per second while its submenu is visible, without rebuilding or closing the menu. Its fixed columns follow the tray theme, monitor DPI, and Windows text size. CPU and RAM include the host, the interface when running, and helper processes, using the same definitions as the Dashboard. VRAM includes dedicated GPU memory attributed to the host, interface, and isolated plugins; shared GPU memory is excluded. Readings continue when the main window is closed. When Diagnostics is disabled, the submenu displays **Disabled** and does not collect resources; unsupported readings display **Unavailable**. GPU queries run in the background with at most one outstanding request per panel and retain no references to the engine or menu. Closing the submenu stops new queries.

## Opening the interface

The host first locates the canonical `WinUI/x64/Release/LightHostModern.WinUI` shell inside its own payload and launches it with the unique host pipe name. Development fallbacks must identify the repository. An existing window for the same profile is restored and activated instead of launching another shell. Temporary profiles include their identifier in the window title and tray tooltip.

The shell is a view of the host state. Closing or recreating it does not rebuild the audio engine or change mute/global bypass. On host startup, those two controls are restored from the active saved profile; a clean default profile starts with both off. The initial window is sized for the current DPI and kept inside the monitor work area.

## Close to tray

When enabled, closing the WinUI window leaves the host, audio device, plugin instances, and notification-area icon active. Reopen the interface from the tray menu.

With **New release notifications on Windows** enabled, the host also checks for updates at startup and every six hours without opening the interface. Network failures retry after five minutes. The same release is announced at most once per host session; restarting the app permits another notification. Turning the setting off cancels an outstanding check, and turning it on starts a new one. Windows decides whether to show the banner according to its notification settings.

When disabled, the close flow requests host shutdown, which saves plugin state and flushes pending settings before exiting.

Windows taskbar **End task** is treated as ending the whole app, regardless of Close to tray. The host detects an abrupt UI exit and shuts down; this also applies to UI crashes. If plugin code blocks cleanup, a ten-second fallback terminates the host. Normal window closing acknowledges a separate per-launch event and continues to honor Close to tray.

## Start with Windows

The setting creates a current-user Windows `Run` entry named `LightHostModern`, with the `--startup` argument. Installed builds register the host executable. Versioned portable builds register the stable `LightHostModern.exe` launcher in the portable root, which selects the current payload in `versions/`. Disabling the setting removes the value.

Portable users should extract the complete ZIP to a stable folder before enabling startup and keep the root launcher, layout metadata, and `versions/` directory together. Updating the payload inside that folder preserves the launcher path. Moving or deleting the root folder invalidates the startup entry; reopening the app and toggling the option off and on registers its new location.

Temporary test profiles reject changes to Windows startup registration and do not open audio automatically.

## Navigation and responsive layout

The sidebar contains Dashboard, Audio, Plugins, Profiles, optional Diagnostics, optional Support me, and Settings, in that order. Diagnostics also remains available when a detailed-log capture needs attention. Its collapsed state keeps the app logo and accessible navigation icons visible. The bottom control expands or collapses the pane unless **Hide collapse/expand sidebar option** is enabled.

**Settings > Appearance > Sidebar on open** chooses Collapsed (default) or Expanded for each new window, including reopening from the tray. Manual toggling changes only the current window. Restoring or focusing an existing window preserves its current state.

Pages are created on first access and retained. Compact mode limits content width; Expanded mode uses the available space. **Always expand Chain**, enabled by default, lets the canvas use the full width in Compact mode. In List mode, Running and Installed each have a bounded virtualized list with its own scrolling. Search and visual sorting preserve the actual processing order; drag reordering is available only in the unfiltered chain-order view.
