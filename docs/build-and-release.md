# Build and release

LightHostModern has two native build systems: CMake builds the JUCE host and its helpers, while MSBuild builds the C++/WinRT WinUI shell. The build helpers stage the self-contained UI beside the host. This guide describes version 2.0.0; CMake, WinUI manifests, About, the updater and package metadata use that version.

## Requirements

- Windows 10 version 1809 or newer; Windows 11 is recommended.
- Visual Studio 2022 with **Desktop development with C++** for the default `windows-vs2022` preset, which selects the Visual Studio 17 2022 generator. A different generator requires a matching preset.
- MSVC v143 x64 toolchain and MSBuild, including UWP C++ x64 and base XAML build tools. Desktop C++ alone is insufficient. The WinUI project targets Windows SDK 10.0.26100.0.
- Install both Windows SDK **10.0.22621.0** (the native CMake preset) and **10.0.26100.0** (the WinUI project). Using other versions requires changing those target settings explicitly.
- CMake 3.22 or newer.
- Git and network access for CMake dependency retrieval.
- Windows App SDK / WinUI 3 tooling and the NuGet package manager component available through Visual Studio.

`Build UI.ps1` and `Build Release.ps1` restore the exact versions listed in
`packages.config` into `out/nuget/<package-id>/<version>` before compiling. They
use Visual Studio's NuGet/MSBuild restore targets with a temporary download-only
project, so rebuilding does not depend on an existing user cache or a separate
.NET SDK. Network access to NuGet.org is required when these packages are absent.
The helper checks the package metadata and every explicit NuGet import before
the UI build starts. This is separate from `packages.config`'s traditional
`ID.version` restore layout; a bare MSBuild `/restore` is insufficient here.

MSI packaging additionally needs the .NET SDK 8.0 or newer and
WiX Toolset 5.0.2 (the version used for 2.0.0 packaging). Install it from the
repository root, then run `Build Release.ps1`:

```powershell
dotnet tool install --tool-path tools/wix wix --version 5.0.2
```

The release script finds `tools/wix/wix.exe` and resolves its required UI
extension. WiX/.NET are packaging tools; building the native host and WinUI does
not require this tool installation.

Default fetched dependencies are JUCE 8.0.13, Steinberg VST3 SDK `v3.8.0_build_66`, the pinned ASIO SDK revision, and Xaymar VST2 headers v0.4.0 when the `XAYMAR` provider is selected. CMake fixes their revisions or archive hash. The helpers prefer available source caches under `out/deps`; see [Licensing and corresponding source](licensing.md) for the pins and notices. The current CMake configuration resolves the selected VST2 provider even when VST2 hosting is `OFF`.

## Build the host

For a runnable development copy, use the numbered portable workflow below. Native
intermediates stay under `out/build`; they are not manual-validation deliveries.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1"
```

This configures CMake, builds WinUI through `Build UI.ps1`, then builds the native targets. `-ApplicationOnly` limits the native build to the application and its dependencies; it does not build all regression executables. The helper compiles tests in a normal build but does not run CTest.

Useful variants:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 ON -Vst2Provider XAYMAR
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Windows.ps1" -EnableVst2 OFF
```

The default Release host output is:

```text
out\build\windows-vs2022\LightHostModern_artefacts\Release\LightHostModern.exe
```

## Build the WinUI shell

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build UI.ps1"
```

CTest regression executables are enabled by default (`BUILD_TESTING=ON`). After building Release:

```powershell
ctest --test-dir out/build/windows-vs2022 -C Release --output-on-failure
```

CTest covers IPC and transport, device policy/recovery, session and profile data, routing and realtime processing, scanner behavior, update validation and isolated-worker failures. Some tests start fixture worker processes or write temporary files; these are not UI automation or a real-device session. Passing CTest does not establish the results of the separate PowerShell integration, real-plugin, hardware or installer lifecycle suites. Build the full targets before running CTest; a build made only with `-ApplicationOnly` may not have current test executables.

Build `WinUI\LightHostModern.WinUI\LightHostModern.WinUI.vcxproj` for x64 Debug or Release with MSBuild or Visual Studio. The release workflow copies the self-contained WinUI payload beside the JUCE host.

The release WinUI payload is self-contained and is launched directly by the host. Isolated UI tests use the same executable through `Start-TestUi` in `Tests/HostProtocol.ps1`; `winapp ui` provides automation without registering a development package. Set `LIGHTHOST_TEST_BUILD_DIR` when testing a native build outside the default `out/build/windows-vs2022` directory.

### Isolated global-control UI check

After building the host and WinUI Release, run this from the repository root with `winapp` UI automation available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\WinUI\ui-tests-global-controls.ps1" -OutputDirectory "out\ui-global-controls"
```

The script accepts `OutputDirectory`; it starts its own real host and UI through `Tests/StartIsolatedProfile.ps1`. It uses a new profile directory under `out/test-profiles`, requires the audio driver to remain closed, checks Running mute/bypass controls and keyboard focus, and reopens the UI against the same host. It then restarts the host using that same isolated directory without saving an operating profile: both flags return to the empty Default profile's values. This does not test restoring mute/bypass from a saved custom operating profile. The runner writes `results.json`, `profile.json`, `running.png` and `reopened.png` below the output directory and calls `StopIsolatedProfile.ps1` in cleanup. Do not launch a fake host, pass `-AppPid`, or attach this runner to a personal session.

## VST2 configuration

`LIGHTHOST_ENABLE_VST2` accepts:

- `AUTO` - enables VST2 when usable headers are available;
- `ON` - requires VST2 and fails configuration if headers are unavailable;
- `OFF` - builds without VST2 hosting.

`LIGHTHOST_VST2_PROVIDER` accepts `XAYMAR` or `LEGACY`. A legacy SDK root can be supplied through `LIGHTHOST_VST2_SDK_DIR` or the matching environment variable.

VST2 availability at compile time is separate from the runtime **Enable VST2 plugins** setting.

## Run and recover locally

```powershell
$app = ".\dev-test\LightHostModern-build-0001\LightHostModern.exe"
& $app
& $app --debug
& $app --safe-mode
```

Use the number produced by your own development build. A normal launch uses the real user profile; `--safe-mode` is a recovery option, not profile isolation. Automated checks should use the isolated-profile helpers above. See [Persistence and recovery](persistence-and-recovery.md) for recovery options.

## Numbered development portables

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Dev.ps1"
```

This builds the host and self-contained UI without running tests, then creates
`dev-test/LightHostModern-build-0001`, `0002`, `0003`, and so on. Each folder contains
only the extracted portable and its required runtime files, ready to run through
`LightHostModern.exe`. No ZIP or installer is created for manual development checks.
Existing builds are never overwritten. The ignored `dev-test/.build-number` counter
keeps numbering even when older build folders are removed; preserve it during cleanup.
`-SkipBuild` packages the current native/UI outputs after checking their versions and
the UI source fingerprint. It does not prove that native sources were rebuilt.

Keep SDK source caches in `out/deps`, build tools in `out/tools` and `tools/wix`,
WiX extensions in `.wix`, and NuGet packages in `out/nuget`. The build helpers reuse
these when available; a fresh checkout can restore pinned dependencies normally.
Do not keep dependencies inside old build directories. Source, test scripts,
documentation, assets and compatibility shims remain part of development.

Historical validation reports identify the builds and paths used for those runs;
their temporary files may have been removed during later workspace cleanup. Keep
the recorded results distinct from checks performed on a new candidate. Concept
images are retained in `docs/concepts/plugin-chain`.

## Create release artifacts

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Utilities\Build Release.ps1"
```

Outputs:

```text
releases\v2.0.0\LightHostModern-2.0.0-Setup.msi
releases\v2.0.0\LightHostModern-Setup.msi  # identical compatibility alias
releases\v2.0.0\LightHostModern-Portable.zip
releases\v2.0.0\release-artifacts.json
releases\v2.0.0\portable-verification.json
releases\v2.0.0\update-manifest.json
releases\v2.0.0\update-manifest.sig
```

The signed manifest pair is generated only when manifest signing is configured. Without it, packages support manual installation. `-OutputDirectory` can select a candidate directory under the workspace's `out` or `releases` tree. The source ZIP is produced separately by `Build Source.py`; a checksum summary and candidate review record are also separate release-preparation outputs.

The MSI defaults to `%ProgramFiles%\LightHostModern` and supports an installer-selected destination. It uses a stable `UpgradeCode` and a major-upgrade relationship so newer MSI versions replace older ones. Its legacy per-user migration checks the registered location and backs up recognized payload files before removing them after installation commits; see the cleanup contract below.

Release builds configure `LIGHTHOST_REALTIME_AUDIT=OFF`; allocation auditing is reserved for explicit validation builds. `-SkipBuild` reuses already-built outputs, which must match the source and version being released. `-SkipTests` skips CTest for explicitly requested local packaging; normal release builds run it.

The portable ZIP contains a stable native `LightHostModern.exe` launcher and a complete self-contained payload under `versions/<version>-<inventory-hash>/`. Users extract it once and open the root launcher; no embedded PowerShell bootstrap or extraction on each launch is used. `Build Dev.ps1` and `Build Release.ps1` share `Portable Layout.ps1` for this layout.

Release staging copies an explicit application payload, notices and runtime files. It does not copy `dev-test`, recording builds or existing profiles. Plugin binaries, test executables and fixture paths are rejected by the release payload check. The distributed application starts with its built-in empty Default profiles when launched against a new user profile; packaging does not reset an existing user's data.

Reserve `releases/v<version>` for public release artifacts and their verification
metadata. Packaging intermediates and the temporary ZIP verification extraction stay
under `out/package-work` and are removed after a successful build, unless `-KeepStage`
is requested. The release script does not publish anything to GitHub. Use `Build Dev.ps1`
for routine layout/functionality checks; create a release folder when preparing that
public version.

Authenticode signing is optional and separate from update-manifest signing. Setting `LIGHTHOST_SIGNING_THUMBPRINT` selects a trusted Windows code-signing certificate; the build signs the configured application binaries and MSI with SHA-256 and a timestamp. Without it, the packages do not identify a verified application publisher to Windows. SignPath integration has not been configured.

The update manifest uses the independent `LIGHTHOST_MANIFEST_SIGNING_THUMBPRINT`. The maintainer's RSA public key is embedded in `UpdateTrustKeys.h`; the release script resolves the certificate from the Windows store, checks that it matches this public key, signs the exact final package hashes, and verifies them using the rebuilt update helper. The maintainer confirmed an encrypted private-key backup on 2026-10-02. Private keys and backup passwords never belong in the repository or release assets.

Publish `update-manifest.json` and `update-manifest.sig` together with the exact installer and ZIP whose hashes they contain. Repacking or signing a package afterward invalidates those hashes. `release-artifacts.json` and `portable-verification.json` record local verification; they do not replace the signed manifest. Initial migration from an older flat portable requires extracting the new distribution manually.

Both distributions include `LICENSE`, `THIRD-PARTY-NOTICES.txt` and the `Licenses` directory. The portable exposes these at its root and includes a versioned copy in its verified payload. See [Licensing and corresponding source](licensing.md) for the original grant, component inventory and source-delivery workflow.

For 2.0.0 package revisions, completed checks, validation limits and publication details, see the [release validation record](release-2.0.0-validation.md) and [release notes](release-2.0.0-notes.md).

## Corresponding-source archive

After building the final candidate, prepare its source delivery with Python 3.9+:

```powershell
python ".\Utilities\Build Source.py" --output-directory "out\source-release-2.0.0"
```

The script archives the actual working-tree source, resources, scripts and notices,
plus JUCE, ASIO, VST3 (including its submodules) and Xaymar sources. It takes JUCE
from the build-owned patched source directory and includes `PatchJuce.cmake` so
local changes are retained. `source-manifest.json` records every file's size and
SHA-256; the ZIP is reread and verified before it is finalized. The native sources
are placed under `out/deps` in the extracted archive for the build helpers to find.
Microsoft tools and NuGet packages are acquired separately under their own terms.

The source ZIP captures working-tree contents, including uncommitted changes; a Git
commit alone may not describe the packaged source. It excludes the local Git store,
application profiles, private-key files and generated application binaries. Build
timestamps/toolchains can affect output bytes, so this archive does not claim
bit-for-bit reproducible executables. Publish the matching source ZIP beside the
binary downloads and rebuild the snapshot if its source changes before publication.

`Build Source.py` records the current worktree, not the time at which each object file was compiled. Build the application from the intended source first, and account for any later documentation-only changes when comparing candidates. The script refuses an existing source ZIP or partial ZIP in the selected output directory; choose a new candidate directory for a new snapshot. Use `--build-directory` when the patched JUCE tree belongs to a non-default native build.

## Application updates

The native host can check for new releases and notify through Windows while the UI is closed. The WinUI shell separately retrieves release metadata for Settings. Internal downloads require a trusted signed update manifest as well as matching size, SHA-256, package identity, version and architecture. MSI updates use a helper that waits for durable session shutdown before starting Windows Installer. Portable updates stage a complete signed version and switch durable selection descriptors after shutdown; the stable launcher confirms startup or returns to a complete previous version. Old flat portables require manual migration to the launcher layout. The user can also choose a browser download. See [Update contract](update-contract.md).

## Repository layout

```text
Source\                         JUCE host, audio engine, IPC, tray, plugins
WinUI\LightHostModern.WinUI\          Native C++/WinRT WinUI 3 shell
WinUI\LightHostModern.WinUI\Locales\ Runtime JSON translation catalogues
Icon\                           Host and application icon assets
ThirdParty\                     Compatibility shims and third-party notices
Utilities\Build Windows.ps1     Host build helper
Utilities\Build UI.ps1          WinUI build helper
Utilities\Build Dev.ps1         Numbered, extracted development portable
Utilities\Build Release.ps1     MSI and portable release builder
dev-test\LightHostModern-build-0001\  Runnable manual-validation build
releases\v2.0.0\                Release packages and verification metadata
out\deps\                       Reusable SDK sources, separate from builds
docs\                           User and internal architecture documentation
```

## Legacy installer cleanup

MSI major upgrades run after InstallInitialize so rollback covers removal of the previous MSI. Cleanup of the old per-user EXE payload runs as the installing user only after commit. It verifies the HKCU product identity, the old executable version/product resource and non-reparse paths. A generated allowlist limits cleanup to known payload paths. Each file is copied and hash-verified under `%LOCALAPPDATA%/LightHostModern/Migrations` before removal. Unknown files and user settings are retained; directories are removed only when empty. Shortcut targets are checked, and startup registration is migrated only when it points to the exact old executable.

The helper does not guess orphan directories or remove another user's installation. Failures are recorded in the migration backup's log. Actual EXE-to-MSI upgrades, cancellation, files in use and MSI repair/uninstall must be exercised in a disposable Windows machine; package inspection alone does not prove that lifecycle.

The allowlist also includes `Uninstall-LightHostModern.ps1`, which the 1.2.x installer created after extracting its application ZIP. It is backed up and removed as an owned file, never executed.
