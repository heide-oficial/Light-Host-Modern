# Build and release

Light Host Modern has two native build systems: CMake builds the JUCE host, while MSBuild builds and packages the C++/WinRT WinUI shell. The release script combines both outputs.

## Requirements

- Windows 10 version 1809 or newer; Windows 11 is recommended.
- Visual Studio 2022 or newer with **Desktop development with C++**.
- MSVC x64 toolchain and MSBuild.
- Windows SDK `10.0.22621.0` or newer; the WinUI project uses a newer installed SDK when configured.
- CMake 3.22 or newer.
- Git and network access for CMake dependency retrieval.
- Windows App SDK / WinUI 3 tooling available through Visual Studio and NuGet restore.

Default fetched dependencies are JUCE 8.0.13, Steinberg VST3 SDK `v3.8.0_build_66`, the ASIO SDK, and Xaymar VST2 headers when that provider is enabled.

## Build the host

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1"
```

Useful variants:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 OFF
```

The default Release host output is:

```text
out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe
```

## Build the WinUI shell

Build `WinUI\LightHost.WinUI\LightHost.WinUI.vcxproj` for x64 Debug or Release with MSBuild or Visual Studio. The release workflow copies the self-contained WinUI payload beside the JUCE host.

The WinUI shell is a packaged desktop app. During development, launch it with its package registration or `winapp run`; do not treat the packaged executable as a standalone unpackaged app.

## VST2 configuration

`LIGHTHOST_ENABLE_VST2` accepts:

- `AUTO` - enables VST2 when usable headers are available;
- `ON` - requires VST2 and fails configuration if headers are unavailable;
- `OFF` - builds without VST2 hosting.

`LIGHTHOST_VST2_PROVIDER` accepts `XAYMAR` or `LEGACY`. A legacy SDK root can be supplied through `LIGHTHOST_VST2_SDK_DIR` or the matching environment variable.

VST2 availability at compile time is separate from the runtime **Enable VST2 plugins** setting.

## Run and recover locally

```powershell
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe"
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --debug
& ".\out\build\windows-vs2022\LightHost_artefacts\Release\Light Host Modern.exe" --safe-mode
```

See [Persistence and recovery](persistence-and-recovery.md) for every recovery option.

## Create release artifacts

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Release.ps1"
```

Outputs:

```text
out\release\LightHostModern-Setup.msi
out\release\LightHostModern-Portable.exe
```

The MSI installs per user under `%LOCALAPPDATA%\Programs\Light Host Modern`. The portable launcher contains the self-contained payload, extracts it to a temporary directory, and starts the host.

## Repository layout

```text
Source\                         JUCE host, audio engine, IPC, tray, plugins
WinUI\LightHost.WinUI\          Native C++/WinRT WinUI 3 shell
WinUI\LightHost.WinUI\Locales\ Runtime JSON translation catalogues
Icon\                           Host and application icon assets
ThirdParty\                     Compatibility shims and third-party notices
Utilities\Build Windows.ps1     Host build helper
Utilities\Build Release.ps1     MSI and portable release builder
docs\                           User and internal architecture documentation
```
