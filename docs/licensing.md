# Licensing and corresponding source

LightHostModern 2.0.0 uses the open-source licensing path selected by the maintainer.
The original Light Host notice in [LICENSE](../license) remains **GPL-2.0-or-later**.
For this build, its GPLv3 option is used when combining the code with JUCE 8 under
**AGPLv3** and ASIO under **GPLv3**. This does not replace the original grant,
relicense upstream components, or assign contributors' rights to another person.

This inventory describes the source pins, copied texts and packaging choices for
LightHostModern 2.0.0. It preserves the upstream terms.

The full component notices are in [THIRD-PARTY-NOTICES.txt](../THIRD-PARTY-NOTICES.txt).
The source-tree directory [ThirdParty/Licenses](../ThirdParty/Licenses) is copied
to `Licenses/` in the distributed application. The GPLv2, GPLv3 and AGPLv3 texts
are included; GNU GPLv3 and AGPLv3 section 13 describe their combination while
retaining the terms of the respective parts.

## Components in this build

| Component | Pinned source / version | Licensing path used |
|---|---|---|
| Original Light Host code | Original notice retained in LICENSE | GPLv3 option of GPL-2.0-or-later |
| JUCE | 8.0.13, `7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2` | AGPLv3 |
| ASIO SDK | `496a0765b8bb9c26f764f22f9a9712a937177db2` | GPLv3 |
| VST3 SDK | 3.8.0 build 66, `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` | MIT |
| Xaymar VST2 SDK with the XAYMAR provider | v0.4.0, archive SHA-256 `de2bad3db73f5e062f3b9a258207a7eacec396ecf069f2f029f7c6424f85896b` | BSD-3-Clause |
| JUCE bundled image/font/audio libraries | Versions contained in the pinned JUCE tree | Their preserved notices in `Licenses/JUCE-8.0.13/` |
| Microsoft Windows App SDK, C++/WinRT, WebView2 loader, WIL and runtime files | NuGet versions in `WinUI/LightHostModern.WinUI/packages.config`; runtime versions in the release inventory | Their own preserved Microsoft/third-party terms |

JUCE and ASIO publish alternative commercial/proprietary terms. This build selects
their stated open-source options; it does not claim that a commercial agreement
has been purchased. No proprietary VST2 SDK is included with the XAYMAR provider.
Selecting the separate `LEGACY` provider requires a fresh licensing and notice review.

The build modifies its own JUCE source copy using `Utilities/PatchJuce.cmake`.
`Utilities/Build Source.py` includes that script and the patched JUCE files from
the selected build directory. This preserves the actual adapter changes used in
the 2.0.0 build. An unmodified upstream archive by itself does not describe the
code used in the application.

Microsoft libraries are not covered by the application's GNU grant. Their notices
must stay with the distributed binaries, which must remain unmodified except where
the vendor's own terms permit modification. The Visual C++ runtime is sourced from
the vendor's redistributable directory. Version 2.0.0 uses DLL version
`14.44.35211.0` from the Visual Studio 2022 `Microsoft.VC143.CRT` redist directory.
Its runtime terms and redistribution pointer are included in
`Licenses/Microsoft/VC-Runtime-2022/`. The retained 2026 texts describe the
separately installed build tools, not the shipped runtime version. Presence in a
developer installation does not by itself grant redistribution rights; see Microsoft's
[2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)
and [runtime redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).

The package list and the shipped file inventory serve different purposes.
`packages.config` also pins build tooling; it does not mean every restored file
ships in the application. Likewise, the WebView2 SDK/loader entry does not claim
that a complete WebView2 browser runtime is bundled. Use the final payload
inventory to identify deployed files and retain the applicable copied notices.

## Source delivery for a release

The source download accompanying the binary release must match the actual build,
including local fixes that were not in the preceding public tag. The release
preparation records that identity and creates a source archive before publication.
It must contain:

- Application and UI source, resources, build scripts, dependency declarations,
  license texts and the JUCE patch script.
- The pinned open-source dependencies used by the build, including required SDK
  submodules, or another source-delivery arrangement that meets the applicable
  license terms. A complete source bundle avoids depending solely on upstream
  links remaining available.
- Build instructions and the tool/dependency versions used. Any local dependency
  override must be identified and its actual corresponding source included.

The current source helper writes `LightHostModern-2.0.0-Source.zip` and
`source-verification.json` to a selected workspace output directory. Its embedded
`source-manifest.json` identifies every included file by size and SHA-256 and
records the base Git commit without treating that commit as the complete source
identity. The script rereads and verifies the ZIP before finalizing it. See
[Build and release](build-and-release.md#corresponding-source-archive) for the command.

The helper expects the documented dependency trees under `out/deps` and the
patched JUCE copy under its selected `--build-directory`. It does not discover
arbitrary dependency overrides automatically. Verify those inputs against the
actual build before using its output as corresponding source. Later source or
documentation edits require a refreshed snapshot; creating the source archive
does not compile application binaries or prove that their object files came
from that snapshot.

The matching source archive must be made available with the binaries, with a clear
download link and without an additional charge for access to the corresponding
source. GitHub's automatically generated source archive alone does not include
fetched dependencies, external submodules or uncommitted local fixes. A URL to the
default branch is not a substitute for identifying the exact release source.

A notice inventory alone does not validate a binary/source pair. Before publication, verify the
source archive, final package inventory and retained notices together. Do not
include private signing keys, credentials, user profiles or test-plugin binaries
in either public artifact. The source ZIP excludes generated application binaries
and build intermediates. Dependency source under the archive's `out/deps` is
intentional. Test source and regression scripts are also retained
for rebuilding and verification; runtime test profiles and recording fixtures
are excluded by the project-path allowlist.

`ThirdParty/Licenses/SOURCES.txt` records the copied-text paths, source locations
and SHA-256 hashes. Compare those hashes when updating the inventory, and update
the build pins, notices and source snapshot together when a dependency changes.
The documented Microsoft Word-to-text extraction and separate SheenBidi
attribution retain their recorded provenance.

## Primary license sources

- [GNU GPLv2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html),
  [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
  [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html).
- [JUCE at the pinned revision](https://github.com/juce-framework/JUCE/blob/7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2/LICENSE.md).
- [ASIO at the pinned revision](https://github.com/audiosdk/asio/blob/496a0765b8bb9c26f764f22f9a9712a937177db2/LICENSE.txt).
- [VST3 at the pinned revision](https://github.com/steinbergmedia/vst3sdk/blob/9fad9770f2ae8542ab1a548a68c1ad1ac690abe0/LICENSE.txt).
- [Xaymar v0.4.0](https://github.com/Xaymar/vst2sdk/tree/v0.4.0).
- [Copied-text provenance and hashes](../ThirdParty/Licenses/SOURCES.txt).

The component texts govern their respective works. This inventory explains the
chosen build and preserves notices; it does not create additional rights or
replace those license terms.
