# Tray and window behavior

The JUCE host owns application lifetime and can continue processing audio without keeping the WinUI window open.

## Notification-area icon

The icon is created by the host process. Its native context menu contains:

- **Open app UI** - launches, restores, or focuses the WinUI shell;
- **Quit** - saves state, closes the shell, stops audio processing, and exits the host.

The selected Color, White, or Black icon variant is applied to both host and interface assets where supported.

## Opening the interface

The host locates the WinUI shell beside its packaged payload and launches it with the unique host pipe name. If an existing `Light Host Modern` window is found, it is restored and activated instead of launching another shell.

The shell is a view of the host state. Closing or recreating it does not rebuild the audio engine by itself.

## Close to tray

When enabled, closing the WinUI window leaves the host, audio device, plugin instances, and notification-area icon active. Reopen the interface from the tray menu.

When disabled, the close flow requests host shutdown, which saves plugin state and flushes pending settings before exiting.

## Start with Windows

The setting creates a current-user Windows `Run` entry named `Light Host Modern`. It points to the host executable that enabled the option. Disabling the setting removes the value.

Portable users should keep the portable executable in a stable location before enabling startup because the registration follows that executable.

## Navigation and responsive layout

The sidebar contains Dashboard, Audio, Plugins, optional Support me, and Settings. Its collapsed state keeps the app logo and accessible navigation icons visible. The bottom control expands or collapses the pane.

All pages use one responsive content container. Compact mode limits its maximum width; Expanded mode uses the available space. Cards, plugin toolbars, and dialog content adapt to narrower windows rather than using page-specific fixed widths.
