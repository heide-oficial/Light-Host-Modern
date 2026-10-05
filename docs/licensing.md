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

The build modifies its own JUCE source copy using the tracked
`Utilities/PatchJuce.cmake`. GitHub's source download includes that patch recipe,
and CMake applies it after obtaining the pinned JUCE source. The tracked
`ThirdParty/XaymarVST2JuceShim` supplies the VST2 adapter headers. These project
changes are part of the source needed to rebuild the application.

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

The release uses GitHub's automatic **Source code (zip)** and **Source code
(tar.gz)** downloads for the `v2.0.0` tag. The two application packages are the
versioned MSI and portable ZIP; no separately uploaded source package or
source-verification file is part of this layout. GitHub describes these downloads
as [snapshots of the selected tag or commit](https://docs.github.com/en/repositories/working-with-files/using-files/downloading-source-code-archives).

The tagged repository contains application and UI source, resources, build scripts,
dependency declarations, license texts, `PatchJuce.cmake` and the VST2 compatibility
headers. Its CMake configuration fetches the public dependency revisions listed
above, including VST3 submodules, and applies the JUCE patches automatically. NuGet
restores the pinned Microsoft build/runtime packages separately under their own
terms. Follow [Building from GitHub source downloads](build-and-release.md#building-from-github-source-downloads)
for the required tools and build command.

The automatic archive does not bundle the downloaded SDK trees or local dependency
caches. Rebuilding requires access to the pinned public sources unless those caches
have already been populated. Keep their source locations and revisions documented,
and retain the applicable source-access and notice obligations when distributing
binaries. This packaging choice does not change any component's license terms.

The release tag must contain every project change and dependency patch used by the
binary. Uncommitted local fixes are absent from GitHub's archive. Any additional
dependency changes must be represented by tracked source or a tracked patch recipe;
a dependency URL alone does not preserve local modifications. Record the resolved
release commit when matching source and binaries, and compare the actual dependency
overrides used by the build with the documented pins. An archive or notice review
alone does not prove a successful rebuild or identical output bytes.

Private signing keys, credentials, user profiles, test-plugin binaries and
build products must stay out of the tagged project source.

`ThirdParty/Licenses/SOURCES.txt` records the copied-text paths, source locations
and SHA-256 hashes. Compare those hashes when updating the inventory, and update
the build pins, notices and tracked patches together when a dependency changes.
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
