# LightHostModern

<p align="left">
<a href="https://github.com/heide-oficial/Light-Host-Modern/stargazers"><img src="https://img.shields.io/github/stars/heide-oficial/Light-Host-Modern?colorA=363a4f&colorB=e0ac00&style=for-the-badge" alt="GitHub star count"></a>
<a href="https://github.com/heide-oficial/Light-Host-Modern/releases"><img src="https://img.shields.io/github/downloads/heide-oficial/Light-Host-Modern/total?colorA=363a4f&colorB=d53984&style=for-the-badge" alt="GitHub release download count"></a>
<a href="docs/licensing.md"><img src="https://img.shields.io/static/v1.svg?style=for-the-badge&label=License&message=Open%20source&colorA=363a4f&colorB=b7bdf8" alt="Open-source component licensing"></a>
</p>

LightHostModern is a native Windows audio plugin host for running VST3 and optional VST2 effects outside a DAW. It combines a JUCE-based realtime host with a dedicated WinUI 3 interface. Use **List** for a serial plugin chain or **Chain** for visual routing through plugins, mixers and parallel paths, save your setups as profiles, and keep processing active from the notification area.

See the [2.0.0 release notes](docs/release-2.0.0-notes.md) for what's new and how to migrate from 1.x.

## ✨ Features

- Hosts VST3 and optional VST2 audio effects in a serial List or an explicitly connected Chain, with latency compensation where parallel paths meet.
- Provides a routing canvas with plugin and hardware ports, configurable stereo mixers, live gain/mute controls, mono/stereo conversions, undo/redo, multi-selection, search, zoom, pan and keyboard connections.
- Saves independent profiles for each mode, including plugin state, routing, names, colors and optional audio-device settings. Save/Discard/Cancel protects unsaved changes when switching profiles.
- Supports Windows Audio, DirectSound, and ASIO backends exposed by JUCE.
- Configures audio devices, input/output channels, sample rate, and buffer size.
- Selects input and output channels individually or in consecutive pairs, with separate Stereo/Mono input and output modes saved per device.
- Configures supported plugin bus layouts, including mono, stereo, sidechains and auxiliary outputs. Chain retains unavailable ports and their connections when a bus is disabled, without redirecting them to another channel.
- Supports device/channel aliases, card and port colors, saved color palettes, custom card sizes and layers, and optional animated signal flow.
- Displays logarithmic input/output meters, the active profile and CPU/RAM/VRAM readings on Dashboard. Diagnostics adds latency, x-runs, stream details and explanatory tooltips; tray Quick Access includes live resource readings.
- Supports detailed log capture in Diagnostics, with restart confirmation and manual TXT export for troubleshooting.
- Scans configurable folders in isolated workers, with caching, progress, cancellation, readable failures, and retries.
- Manages Running and Installed plugins through searchable cards, action toolbars, visual status badges, and optional manufacturer grouping.
- Supports multiple instances, reorder, bypass, duplicate, custom names, original-name restoration, details, and editor actions.
- Offers **Run in a separate process (experimental)** per plugin, with manual retry and per-instance diagnostics. This adds latency and resources; it contains failures in opted-in instances and is not a security sandbox. See [plugin isolation](docs/plugin-isolation.md).
- Provides global output mute and latency-compensated chain bypass from List, Chain and the notification area.
- Preserves independent plugin state and names in versioned sessions with atomic writes, backups, and recovery from legacy settings.
- Compensates bypass latency and uses short transitions when toggling bypass or mute.
- Recovers preferred audio devices after startup, sleep, driver restarts, or temporary unavailability, with bounded retries and clear notices when the selected device cannot run. Disabled recovery never substitutes another device.
- Provides compact and expanded layouts, Windows materials, high-contrast support, icon variants, English and Brazilian Portuguese. Performance mode reduces decorative work while retaining audio and status information.
- Runs from the notification area with close-to-tray, current-user startup, and Quick Access menus for plugin editors, instance actions, installed plugins and performance.
- Separates action, warning/error, in-app update and Windows update notifications, with an independent hover-help preference. Update checks and Windows notifications continue while the app is in the tray.
- Includes safe mode, confirmed reset actions, failed-plugin quarantine recovery, an MSI installer and a portable distribution with a stable launcher and versioned payloads.

## 🎬 Showcase

https://github.com/user-attachments/assets/536eecbe-43b7-4f03-a0e6-58e18fa91b35

[Watch on YouTube](https://www.youtube.com/watch?v=FT8IO7-vbjo).

## 🚀 Usage

1. Start LightHostModern and open its interface from the notification area if it is not already visible.
2. Open **Audio** and select the backend, device, channels, sample rate, and buffer size used by the host.
3. Choose **List** or **Chain** in **Settings > Operating mode**. A mode change takes effect after restarting the host; reopening only the interface is not enough.
4. In List, open **Plugins > Installed > Scan for plugins**. In Chain, open **Add plugin** to access the catalogue and scanner. Add your plugin folders and start a scan; cancellation and failure retries are available.
5. In List, add installed plugins and arrange their order in **Running**. In Chain, add plugins or mixers and connect their ports from Audio input to Audio output. A new unconnected Chain is silent. Open an editor from the card menu or double-click its header.
6. Use **Profiles** to save the current setup, optionally including its audio settings, and switch between saved configurations. Keep List and Chain profiles separate; choose Save, Discard or Cancel when prompted about edits.
7. Review **Settings** for recovery, startup, appearance, notifications and database maintenance. Use **Dashboard**, **Diagnostics**, or the tray **Performance** submenu to monitor the app.

For detailed descriptions of the screens, workflows, and internal implementation, see the [application documentation](docs/_index.md).

## ⚙️ Requirements

- Windows 10 version 1809 (`10.0.17763`) or newer; Windows 11 is recommended.
- A 64-bit (x64) Windows environment and a compatible audio device/driver.
- Compatible 64-bit VST3 plugins, or VST2 plugins when VST2 support is included and enabled.

## ⬇️ Installation

### Recommended installation

Download `LightHostModern-2.0.0-Setup.msi` from the [2.0.0 release](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v2.0.0), open it, and follow the Windows Installer steps. The application is installed under `%ProgramFiles%\LightHostModern` and receives Start menu and desktop shortcuts. Newer MSI releases upgrade the existing installation; the installer also migrates installations created by the legacy per-user setup.

### Portable version

Download `LightHostModern-v2.0.0-Portable.zip` from the [2.0.0 release](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v2.0.0), extract it to a stable folder, and run `LightHostModern.exe`. The complete self-contained app does not extract itself on every launch or require PowerShell at runtime.

Starting with 2.0.0, the root executable is a stable launcher and the app lives under `versions/`. Extract the **complete** ZIP and always start the root executable. To move from a 1.x flat portable, extract 2.0.0 into a new folder once; do not copy only the new executable over the old folder. Preferences remain in the existing user profile. Internal portable updates require NTFS; other file systems use manual extraction.

### Building from source

Use GitHub's **Source code (zip)** or **Source code (tar.gz)** downloads for the `v2.0.0` tag. Follow [Build and release](docs/build-and-release.md#building-from-github-source-downloads) to install the tools and fetch the pinned public dependencies. The archive includes the application's dependency patches.

## 🔒 Privacy and disclosures

- The application does not include telemetry, analytics, advertising, authentication, or user accounts.
- Audio processing, plugin hosting, device enumeration, settings, and host/UI communication remain local to the computer.
- Host preferences and the plugin database are stored locally through JUCE application properties. Chain/plugin state and saved profiles use separate files beside those preferences, with atomic writes and recoverable backups. Portable app files do not move these settings into the portable folder.
- WinUI preferences are stored in `%LOCALAPPDATA%\LightHostModern\ui-settings.ini`.
- Debug logs are created under `%APPDATA%\LightHostModern\Logs` only when the host is started with `--debug`.
- The updater contacts this repository’s public GitHub Releases API over HTTPS. Downloads begin after an explicit choice and support cancellation. Applying an update waits for session saving and orderly shutdown; manual browser download remains available. No audio, plugin state, device settings, or personal data is uploaded.
- Diagnostics are local and enabled by default. Disabling **Settings > General > Diagnostics** hides the page and stops diagnostics collection; audio processing and dashboard peak meters continue.
- Enabling **Start with Windows** creates an entry for the current user under the Windows `Run` registry key.
- GitHub and Ko-fi pages open in the default browser only after the user activates their corresponding controls. The app's Support page uses a bundled local Ko-fi image. The donation image in this GitHub README is hosted by Ko-fi.
- The host and WinUI shell are full-trust desktop processes so they can access audio drivers, plugins, local files, the notification area, named pipes, and startup registration.

## 🌐 Supported languages

- English (`1.0.0+`)
- Brazilian Portuguese (`1.2.0+`)

Want to translate LightHostModern? Copy the [English JSON catalogue](WinUI/LightHostModern.WinUI/Locales/en-us.json), rename it with the appropriate language code, translate only the values, and submit the file through a pull request or GitHub issue. Missing keys automatically fall back to English. See [Contributing translations](docs/localization.md) for the complete format.

## ❤️ Support

Please consider supporting my work. There are many hours of work, thinking and effort behind it. You can support the application by [donating any amount on Ko-fi](https://ko-fi.com/heide_oficial), [starring the GitHub repository](https://github.com/heide-oficial/Light-Host-Modern), or publishing a video about the application and [submitting it for showcase](https://github.com/heide-oficial/Light-Host-Modern/issues/new?title=%5BSHOWCASE%20VIDEO%5D%20Video%20title%20here&labels=showcase%20video&body=Here%27s%20my%20video%20showcasing%20or%20featuring%20the%20app%3A%20%5BINSERT%20LINK%20HERE%5D).

Thank you!

<a href="https://ko-fi.com/heide_oficial" target="_blank">
  <img src="https://storage.ko-fi.com/cdn/brandasset/v2/support_me_on_kofi_beige.png" alt="Support me on Ko-fi" width="200">
</a>

## 👥 Credits

WARNING: GitHub's automatic Contributors list is based on commit authorship and may not include every person credited below. This section is the project's complete attribution record, including contributions that were reviewed, adapted, or reimplemented before integration.

- Created by [Matheus Heidemann - heide-oficial](https://github.com/heide-oficial).
- Based on the original [Light Host](https://github.com/opencma/LightHost) by [Rolando Islas / OpenCMA](https://github.com/opencma/).
- Built with [JUCE](https://github.com/juce-framework/JUCE), the [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk), the [ASIO SDK](https://github.com/audiosdk/asio), and optional [Xaymar VST2 headers](https://github.com/Xaymar/vst2sdk).
- [multimattia](https://github.com/multimattia) contributed the [RNNoise VST3 loading fix for plugins with incomplete scan channel metadata](https://github.com/heide-oficial/Light-Host-Modern/pull/4).
- [k-ross](https://github.com/k-ross) contributed the [individual input selection and mono input mixing proposal](https://github.com/heide-oficial/Light-Host-Modern/pull/5), adapted with smooth transitions, device-specific preferences and revised routing.
- [Log1cFX](https://github.com/Log1cFX) contributed [feedback on naming consistency, upgrade cleanup, audio device handling, diagnostics and meter responsiveness](https://github.com/heide-oficial/Light-Host-Modern/issues/6).

## 📄 License

Original Light Host source remains under GPL-2.0-or-later. This release uses the GPLv3 option for that code when combined with JUCE 8 under AGPLv3 and ASIO under GPLv3; other components retain their own terms. See [LICENSE](license), [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) and [Licensing and corresponding source](docs/licensing.md) for the component notices and corresponding-source information.
