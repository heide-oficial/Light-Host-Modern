# Light Host Modern

<p align="left">
<a href="https://github.com/heide-oficial/Light-Host-Modern/stargazers"><img src="https://img.shields.io/github/stars/heide-oficial/Light-Host-Modern?colorA=363a4f&colorB=e0ac00&style=for-the-badge" alt="GitHub star count"></a>
<a href="https://github.com/heide-oficial/Light-Host-Modern/releases"><img src="https://img.shields.io/github/downloads/heide-oficial/Light-Host-Modern/total?colorA=363a4f&colorB=d53984&style=for-the-badge" alt="GitHub release download count"></a>
<a href="https://github.com/heide-oficial/Light-Host-Modern/blob/main/license"><img src="https://img.shields.io/static/v1.svg?style=for-the-badge&label=License&message=GPL-2.0-or-later&colorA=363a4f&colorB=b7bdf8" alt="GPL-2.0-or-later license"></a>
</p>

Light Host Modern is a native Windows audio plugin host for running VST3 and optional VST2 effects outside a DAW. It combines a JUCE-based realtime host with a dedicated WinUI 3 interface for configuring audio devices, building a serial plugin chain, monitoring the stream, and keeping processing active from the notification area.

## ✨ Features

- Hosts VST3 and optional VST2 audio effects in a serial realtime processing chain.
- Supports Windows Audio, DirectSound, and ASIO backends exposed by JUCE.
- Configures audio devices, input/output channels, sample rate, and buffer size.
- Displays live input/output meters, CPU usage, latency, x-runs, and plugin counts.
- Scans configurable plugin folders and maintains a reusable installed-plugin database.
- Adds multiple instances of the same plugin and supports reorder, bypass, duplicate, editor, and remove actions.
- Preserves plugin state and compensates bypass latency to keep the chain aligned.
- Recovers preferred audio devices after startup, sleep, driver restarts, or temporary unavailability.
- Provides compact and expanded layouts, Windows 11 materials, icon variants, JSON localization, and Brazilian Portuguese.
- Runs from the notification area with close-to-tray and current-user startup options.
- Includes safe mode, settings reset, failed-plugin quarantine recovery, installer, and portable packages.

## 🖼️ Demo

![Light Host Modern dashboard](https://i.imgur.com/CPhkOgy.png)

![Audio device and stream configuration](https://i.imgur.com/EVMlX0A.png)

![Running and installed plugin management](https://i.imgur.com/Nl3Jsfu.png)

![Windows 11-style settings](https://i.imgur.com/XGLSiXE.png)

## 🚀 Usage

1. Start Light Host Modern and open its interface from the notification area if it is not already visible.
2. Open **Audio** and select the backend, device, channels, sample rate, and buffer size used by the host.
3. Open **Plugins > Installed**, configure **Scan paths**, and scan for VST3 or VST2 plugins.
4. Add plugins to **Running**, arrange the serial processing order, and open each plugin editor when needed.
5. Review **Settings** to configure device persistence, startup, close-to-tray, VST2 availability, language, layout, material, and icon.

For detailed descriptions of the screens, workflows, and internal implementation, see the [application documentation](docs/_index.md).

## ⚙️ Requirements

- Windows 10 version 1809 (`10.0.17763`) or newer; Windows 11 is recommended.
- Compatible 64-bit VST3 plugins, or VST2 plugins when VST2 support is included and enabled.

## ⬇️ Installation

### Recommended installation

Download `LightHostModern-Setup.msi` from the [latest GitHub release](https://github.com/heide-oficial/Light-Host-Modern/releases/latest), open it, and follow the Windows Installer steps. The application is installed under `%ProgramFiles%\Light Host Modern` and receives Start menu and desktop shortcuts. Newer MSI releases upgrade the existing installation; the installer also migrates installations created by the legacy per-user setup.

### Portable version

Download `LightHostModern-Portable.zip` from the [latest GitHub release](https://github.com/heide-oficial/Light-Host-Modern/releases/latest), extract it to a stable folder, and run `Light Host Modern.exe`. The ZIP contains the complete self-contained app and does not use an extraction launcher or PowerShell at runtime.

## 🔒 Privacy and disclosures

- The application does not include telemetry, analytics, advertising, authentication, or user accounts.
- Audio processing, plugin hosting, device enumeration, settings, and host/UI communication remain local to the computer.
- Host settings, plugin database entries, chain state, and plugin state are stored locally through JUCE application properties.
- WinUI preferences are stored in `%LOCALAPPDATA%\LightHostModern\ui-settings.ini`.
- Debug logs are created under `%APPDATA%\LightHostModern\Logs` only when the host is started with `--debug`.
- The updater sends an HTTPS request to the public GitHub Releases API for this repository. When an update is accepted, it downloads the release MSI, verifies its published SHA-256 digest, and starts Windows Installer. It does not upload audio, plugin state, device settings, or personal data.
- Enabling **Start with Windows** creates an entry for the current user under the Windows `Run` registry key.
- GitHub and Ko-fi pages open in the default browser only after the user activates their corresponding controls. The Support page displays the Ko-fi banner from Ko-fi's content delivery network.
- The host and WinUI shell are full-trust desktop processes so they can access audio drivers, plugins, local files, the notification area, named pipes, and startup registration.

## 🌐 Supported languages

- English (`1.0.0+`)
- Brazilian Portuguese (`1.2.0+`)

Want to translate Light Host Modern? Copy the [English JSON catalogue](WinUI/LightHost.WinUI/Locales/en-us.json), rename it with the appropriate language code, translate only the values, and submit the file through a pull request or GitHub issue. Missing keys automatically fall back to English. See [Contributing translations](docs/localization.md) for the complete format.

## ❤️ Support

You can support continued development by [donating on Ko-fi](https://ko-fi.com/heide_oficial), [starring the GitHub repository](https://github.com/heide-oficial/Light-Host-Modern), or publishing a video and [submitting it for showcase](https://github.com/heide-oficial/Light-Host-Modern/issues/new?title=%5BSHOWCASE%20VIDEO%5D%20Video%20title%20here&labels=showcase%20video&body=Here%27s%20my%20video%20showcasing%20or%20featuring%20the%20app%3A%20%5BINSERT%20LINK%20HERE%5D).

<a href="https://ko-fi.com/heide_oficial" target="_blank">
  <img src="https://storage.ko-fi.com/cdn/brandasset/v2/support_me_on_kofi_beige.png" alt="Support me on Ko-fi" width="200">
</a>

## 👥 Credits

- Modern Windows fork maintained by [Matheus Heidemann - heide-oficial](https://github.com/heide-oficial).
- Based on the original [Light Host](https://github.com/opencma/LightHost) by Rolando Islas / OpenCMA.
- Built with [JUCE](https://github.com/juce-framework/JUCE), the [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk), the [ASIO SDK](https://github.com/audiosdk/asio), and optional [Xaymar VST2 headers](https://github.com/Xaymar/vst2sdk).
- [multimattia](https://github.com/multimattia) contributed the [RNNoise VST3 loading fix for plugins with incomplete scan channel metadata](https://github.com/heide-oficial/Light-Host-Modern/pull/4).

⚠️ GitHub's automatic Contributors list is based on commit authorship and may not include every person credited above. This section is the project's complete attribution record, including contributions that were reviewed, adapted, or reimplemented before integration.

## 📄 License

Light Host Modern follows the original Light Host license lineage and is distributed under the [GNU General Public License version 2 or later](license). Third-party components remain subject to their respective licenses and notices.
