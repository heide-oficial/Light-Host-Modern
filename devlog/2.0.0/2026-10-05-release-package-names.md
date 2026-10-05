# Versioned release packages and GitHub source downloads

The 2026-10-05 packaging correction keeps version **2.0.0** and defines two
application packages: `LightHostModern-2.0.0-Setup.msi` and
`LightHostModern-v2.0.0-Portable.zip`. Unversioned aliases and the separately
uploaded source ZIP/source-verification asset are excluded. The signed update
manifest pair, `release-artifacts.json` and `SHA256SUMS.txt` remain technical
release files.

README and the current build, update and licensing guides describe GitHub's
automatic source downloads for the release tag. The build fetches public pinned
dependencies and applies the tracked `Utilities/PatchJuce.cmake`; no dependency
tree needs to be copied into the repository. Historical package-validation records
retain their original filenames, inventories and results.

Read-only source review found three modified files in the JUCE source cache and
seven additional differences in the build-owned JUCE copy, with no tracked source
files missing. Replaying the patch recipe in memory from upstream Git blobs
reproduced all ten modified files after normalizing line endings. ASIO and VST3
source caches had no local changes. Evidence is recorded locally in
`out/release-naming-20261005/dependency-source-review.json`.

This review did not compile or launch the application, download dependencies or
publish files. Package rebuilds, checksums and remote asset replacement are
separate release work; this entry does not claim their completion.

The subsequent Release build succeeded. All 24 native tests and six package
inspection scenarios passed. The helper verified the signed manifests against
the versioned MSI and portable, and the portable engine exercised the actual
ZIP's preparation, application and rollback. The packaging script now generates
checksums for the public assets and removes obsolete unversioned aliases from its
validated output directory. Evidence is under `out/release-naming-20261005/`.
