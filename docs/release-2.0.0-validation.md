# LightHostModern 2.0.0 release validation

Validation: **local release checks recorded below; background-release checks passed**. The earlier candidate passed bounded VM installer validation; the latest host revision has not been installed in that VM. The maintainer authorized the final commit, tag, release uploads and public release on **2026-10-05**. SignPath/Authenticode remains deferred. Issues are left for the maintainer to answer and close manually.

## Documentation checkpoint: 2026-10-05

The latest manual-use portable is **build 0040**, with the recording fixtures
removed and only the normal built-in clean profiles supplied by the app. The
README showcase is the maintainer's video. The sections below are chronological:
earlier references to screenshots, GIFs, build 0037/0038 or a "final" package
describe that revision, not the latest delivery.

The latest binary packages were prepared during the showcase cleanup. Their
49 executable/library entries match the preceding candidate, and read-only
portable/MSI inspection found no demonstration plugins or user profiles. The
optional clean-start launch was not executed: automatic approval review rejected
that command before execution. `out/clean-release-20261005/fresh-startup.json`
records that limit. No user preferences were reset.

The documentation review described below updates the corresponding-source
archive and its checksums, without rebuilding the MSI, portable or application.
It does not extend the earlier test results to new scenarios. Use
`releases/v2.0.0/candidate-verification.json` and `source-verification.json` for
the current source-archive identity; use the artifact hashes attached to each
earlier result when assessing its coverage.

## Scope and source identity

This release includes the accumulated 2.0.0 work since public release 1.4.1.
The final source is committed and identified by the `v2.0.0` tag. The matching
Source ZIP and its per-file manifest record the actual project and native
dependency sources used, including the applied JUCE patches. The archive builder
reads every entry back and checks its size and SHA-256 before writing
`source-verification.json`; that record identifies the delivered source ZIP.

The maintainer selected the open-source licensing path and confirmed an encrypted
backup of the update-signing key. See [licensing](licensing.md) and the
[update contract](update-contract.md). Only the public update key is distributed.

## Prepared changes

- Release x64 host, WinUI, scanner, isolated worker, helper and launcher rebuilt.
- The installer and portable now include the original license and component
  notices, with explicit filename casing for archives and case-sensitive hosts.
- Portable extraction permits only the intended notice file and text/Markdown
  files inside `Licenses/`; executable files or path traversal through that
  directory remain rejected.
- An update test uses the real helper source with a synthetic earlier version
  gate, the production public trust key and the exact candidate packages. This
  fixture is excluded from distributed binaries.
- The MSI lifecycle test is reserved for a disposable Windows environment.
- Release preparation also corrected two reproducible UI defects: clicking a
  partially enabled channel pair disabled both channels, and asynchronous work
  performed after window destruction could leave the UI process alive. Normal
  close, restart, reset and update now share cleanup before destroying the window.
- Sixty corrupted Brazilian Portuguese strings were corrected and visually
  checked in the final extracted portable.

## Validation record

Preparation evidence is under `out/release-prep-20261002-214523/`.

| Check | Result and evidence |
| --- | --- |
| Release x64 build | Host, UI, scanner, worker, update helper, launcher and native tests built. `build.log`, `build-signing-fixtures.log`; UI corrections have separate final build logs. |
| Native regression suite | **24/24 passed**, 125.21 seconds. `ctest.log` and `package-build.log`. |
| Initial integration run | **12 passed / 5 failed**, preserved in `integration/results.json`. The failures were investigated individually; the original report is not rewritten as green. |
| Host integration coverage | Profiles, instances, session persistence, state synchronization, scanning, profile-edit isolation, mixers, isolated hosts and plugin preferences passed. |
| Hardware integration | Windows Audio, Exclusive, Low Latency, DirectSound and ASIO passed opening/processing and available configuration/reopen scenarios. Unsupported choices are recorded as unavailable. `integration/hardware/results.json`. |
| Isolated audio, Windows Audio | **60 seconds passed**; 2 missed worker blocks out of 6,239, below the explicit 0.1% threshold. `integration/isolated-audio/results.json`. |
| Isolated audio, ASIO | The original Voicemeeter Insert attempt could not open the selected driver. A targeted **60-second Focusrite USB ASIO run passed** at 48 kHz / 64 samples: 1 missed worker block out of 23,436, no processing failure or x-run. `isolated-asio-focusrite/results.json`. |
| Real plugin smoke | **4/4 passed**: Renegate, RoughRider3, T-De-Esser and kHs Pitch Shifter. Isolated loading, editor opening, synthetic-buffer processing, capture and restoration. `real-plugin-results.json`. This is not prolonged real-device/plugin compatibility certification. |
| Signed update, first package | **7/7 passed** with production trust roots and the exact candidate ZIP. `portable-integration-1/result.json`. |
| Package inspection, first package | **6/6 passed**; a subsequent notice review identified an unnecessary `.appxrecipe`, now excluded from staging and rejected by inspection. Final package verification is recorded separately. |
| MSI test preparation | Parser and default/custom dry plans passed. `vm-plan-startup-smoke-review/`. **No MSI lifecycle execution on the development computer.** |

The four UI failures were channel-pair behavior, normal close, logging restart and
keyboard focus. Besides the two product fixes, the harness now tolerates atomic
replacement of `profile.json`, invokes buttons without racing popup animations,
and records/retries externally interrupted focus trials without accepting a real
in-app focus loss. Final targeted UI results and package rechecks are recorded
below.

### Final status of the five initial failures

The **12 passed / 5 failed** count describes the first run, not five unresolved
product defects in the final candidate. The evidence below preserves that
distinction; passing a different audio driver does not clear the failed one.

| Initial failure | Final status | Evidence and remaining limit |
| --- | --- | --- |
| `ModernizationUiSmoke.ps1`: clicking a partially enabled channel pair did not enable both channels | **Product defect corrected; recheck passed.** | Final English/Portuguese, Compact/Expanded checks passed in `integration/ModernizationUiSmoke.final.log`. |
| `UiLifetimeIntegrationTests.ps1`: close-with-quit left a process alive | **Product defect corrected; recheck passed.** | Cleanup now finishes before window destruction. All four lifetime scenarios passed in `integration/ui-lifetime-final-1/results.json`; the six later VM startup/shutdown checks also exited normally. |
| `VerboseLogUiIntegrationTests.ps1`: reading `profile.json` failed while another process replaced it | **Harness race corrected; recheck passed.** | The harness retries transient metadata reads and avoids popup timing races. All four log/restart/export scenarios passed in `integration/logs-ui-final-3/results.json`, using the corrected close lifecycle. |
| `ChainAuditUiTests.ps1`: keyboard movement appeared to lose card focus | **Desktop interference/harness handling corrected; uninterrupted recheck passed.** | The initial log records another application taking focus. The final trial retained focus with `externalFocus=false` on attempt 1; see `integration/ChainAuditUiTests.final.log` and its profile's `keyboard-focus-trials.json`. No separate product focus fix is claimed. |
| `IsolatedAudioIntegrationTests.ps1`: ASIO driver open failure with Voicemeeter Insert | **Voicemeeter Insert remains unvalidated; a different ASIO driver passed.** | The 60-second Focusrite USB ASIO recheck passed at 48 kHz / 64 samples in `isolated-asio-focusrite/results.json`. This does not establish the cause of the Voicemeeter failure or certify that driver. |

The isolated-audio passes also do **not** mean every worker deadline was met:
Windows Audio recorded two missed blocks (about 0.0321%) and Focusrite one
(about 0.00427%), both below the explicit 0.1% test threshold, with no reported
processing failure or x-run. These were 60-second checks, not prolonged-session
or broad driver/plugin validation; process isolation remains experimental.

### Final candidate checks

| Check | Result and evidence |
| --- | --- |
| UI lifetime | **4/4 passed** after the fix: close-to-tray, sidebar reopen, forced UI termination and full close. `integration/ui-lifetime-final-1/results.json`. |
| Detailed logs UI | **4/4 passed**: arm, restart, cancel export and save TXT from both processes without another restart. `integration/logs-ui-final-3/results.json`; screenshot reviewed. |
| Modernization UI | **Passed** in English and Brazilian Portuguese, Compact and Expanded, at 192 DPI with 21 metrics. Partial input/output pairs now enable correctly. `integration/ModernizationUiSmoke.final.log`. Visual review also found corrupted Portuguese accents, corrected before final packaging. |
| Chain UI | **Passed** keyboard movement with focus retained on the first uninterrupted trial, keyboard connection/recovery and 20 device-modal cycles. Private memory grew 1.48 MiB from cycle 4 to 20, below the 8 MiB assertion. `integration/ChainAuditUiTests.final.log`; captures reviewed. |
| Final Portuguese capture | **Passed** in development build **0037** after the locale-only rebuild. Accents and audio/channel layout reviewed. `pt-br-final-727e9864eb1649809b40f2cd8b1755c1/result.json` and captures. |
| Final portable update | **7/7 passed**, 82.97 seconds, using the final ZIP and production trust key. `portable-integration-2/result.json`. The synthetic baseline limitation below still applies. |
| Final package inspection | **6/6 passed**, 21.16 seconds. MSI/ZIP identities, signed helper verification, stale staging rejection and portable apply/rollback. `packages-final/package-inspection-results.json`. |
| Final payload and notices | **Passed**: 82 root/payload document comparisons in the portable and 41 installed documents; all 288 MSI payload files match the portable payload. No PDB, `.appxrecipe`, signing key or test fixture is distributed. `final-package-review/`. |
| Installed-startup harness preflight | **Passed** against the final portable payload without invoking MSI: host/UI identity, IPC, a visible UI window, audio suspended and normal shutdown. The seven VM-bundle input hashes also passed. `final-package-review/startup-smoke-preflight/` and `runner-inputs-review.json`. This is not proof of installed startup in the VM. |
| Source build preparation | **Passed** restoration of all nine pinned NuGet packages into an initially empty cache and validation of the project's package imports. The build helpers now restore the expected directory layout explicitly. `nuget-restore-review-67b6541760ef4d72aa878462a48ff008/results.json`. Required source/resource references were reviewed; a full fresh-machine source build was not run. |
| Windows 11 VM installer lifecycle | **14/14 scenarios passed** across default/custom installation paths. All 12 MSI operations returned 0 without a reboot; six installed host/UI startup checks passed with normal shutdown. The MSI hash matches the final candidate. `VM-RESULTS/results-20261002-230335-0d979978/`. |
| Script hygiene | PowerShell syntax, source-archive Python syntax and `git diff --check` passed. |

The packages used for the VM and end-to-end update checks are in `packages-final/`.
The later documentation-only repackaging is in `packages-readme-review/` and is
the delivery under `releases/v2.0.0/`, with current SHA-256 sums and verification
metadata. Build **0037** remains the extracted portable used for manual UI review
under `dev-test/LightHostModern-build-0037/`.

### Documentation-only package revision

After the checks above, the README and release notes were simplified. The release
notes compare against **v1.4.1**, excluding changes already shipped in that version;
fixes to features introduced only during 2.0.0 development are excluded from the
public changelog. The internal validation history remains intact.

The installer and portable were regenerated to include the updated README,
without recompiling the app. The 295-entry portable payload comparison found only
README and generated release/inventory metadata changes. In the 288-file MSI
payload, only README and the release-info timestamp changed; all 48 EXE/DLL files
and embedded binaries/icons are identical. ProductCode, UpgradeCode, installer
actions, paths and shortcuts are unchanged. The MSI PackageCode and build
timestamps changed as expected. See `readme-package-comparison.json` and
`readme-msi-comparison.json` under the preparation evidence directory.

The revised packages passed all **6/6 package inspection checks**, including
verification of their new signed update manifests. The corresponding source ZIP
and delivery checksums were refreshed. The VM lifecycle and 7-scenario update
run were **not repeated** for this documentation-only revision, so those records
remain evidence for the preceding packages, not execution of the new MSI bytes.

| MSI revision | SHA-256 |
| --- | --- |
| Tested in the VM | `66512f7d54e8e36242cba55791a2563f8b6f88f6edc499470b001a0c82d4d1f0` |
| Delivered with revised README | `a671dc206f501efe95ac05033222bd49a4627c0e92f3770f9332b49d7ff5dfc4` |

### Background-release revision and documentation review (2026-10-03)

The changelog now compares against **v1.4.1**, and the README contains seven new
captures of the real 2.0.0 interface using isolated, illustrative demo profiles.
The UI executable is unchanged from the captured build. The local issue drafts
are under `out/issue-replies/v2.0.0/`; no reply was posted and no issue was closed.

The native host was rebuilt to check for new stable releases with the GUI closed
to the tray. It respects the Windows-notification preference, checks at startup
and every six hours, retries transport failures after five minutes, and shares
per-session notification deduplication with UI requests. Closing the host cancels
and joins its worker. This is a **code revision**, not documentation-only repackaging.

| Focused check | Result and evidence |
| --- | --- |
| Build | WinUI staging stamp refreshed and native host/update tests rebuilt successfully. `out/background-release-ui-build.log`, `out/background-release-native-build.log`. |
| Native background-release scenarios | **8/8 passed**: metadata validation, scheduling, retry, preferences, deduplication, cancellation and shutdown. `out/background-release-unit.log`. |
| Real-host background integration | **6/6 passed**, with no GUI, real audio, external network or actual Windows banner. Local metadata fixtures exercise the notification delivery path, restart, live preferences and clean shutdown. `out/background-release-20261003-2/results.json`. |
| Initial focused integration failure | The harness polled IPC after a successful host shutdown. Its log recorded orderly exit in 10 ms. The harness now waits for process exit after the quit receipt; no product correction was needed. Original evidence remains in `out/background-release-20261003-1/`. |
| Revised package inspection | **6/6 passed**, including signed-manifest verification and preparation/application of the new portable ZIP. `out/background-release-package-inspection.log`. |
| Manual development delivery | Extracted portable: `dev-test/LightHostModern-build-0038`. |

The refreshed packages are in `packages-background-release/` under the preparation
evidence directory. Their identities are:

- Host executable SHA-256: `ff1c9bb4daf01db68482f1c80367aeb38543ecaf338accbd22c6a7ee57ca6c33`.
- MSI SHA-256: `a6e1ca03766cf27ae89d0c8ed99fddc2194be6c0b88217c29b56314205131005`.
- Portable ZIP SHA-256: `09221e18724d92246c464fae189564cdc50fdaef8b103bf49ba027a0e0e2cea2`.

The earlier full regression suite, seven-scenario signed-update run and VM
installer lifecycle results remain evidence for their recorded builds. They were
not rerun for this native-host revision. The latest MSI has not been executed in
the VM; the local inspection does not replace that lifecycle check. The focused
notification tests do not certify live GitHub availability or Windows banner
visibility under the user's notification policy.

### README animation revision (2026-10-03)

The README now includes a 13-second Chain connection demo and an 8-second tray
Quick Access demo, alongside the still captures. Both were recorded from the real
build 0038 interface in isolated profiles using scripts. The Chain uses illustrative
routing/meter data; the tray opens the real RoughRider3 editor without an audio
device. All recording processes were closed. GIF frame decoding, dimensions,
timings and local Markdown asset paths were checked; no full app test suite was
run for this documentation-only change.

MSI and portable packages were regenerated with the new README and freshly signed
update manifests. All **49 EXE/DLL entries in the portable are byte-identical** to
the preceding package. Only README and generated release/inventory/selection
metadata changed. Evidence: `out/readme-gifs-20261003/package-comparison.json`.
The corresponding-source ZIP and final checksums were refreshed as well. The
development executable remains build 0038; application sources were not changed.

- Revised MSI SHA-256: `13efb5bd9d0ae8923747aac4aa2cedbd9d592bc71b933e124bf14fb7b109cc99`.
- Revised portable SHA-256: `ea85ca2513b5def35673d94356aaea7da7e761a3264f0e34aa739bcc58a57f73`.

The earlier integration/package/VM records retain their original artifact hashes.
No installer lifecycle test was repeated for this README revision, and no remote
publication took place.

### Update test limits

The earlier update baseline is a synthetic 1.9.9 inventory with unchanged current
binaries and a test-only helper compiled with an earlier version gate. It uses
the actual production helper code and public trust key; no production private key
is exported and no trust root is bypassed. The test checks authenticated prepare,
cancel, apply after host/UI shutdown, launcher confirmation, profile preservation
and recovery from an interrupted candidate descriptor. It does not prove a real
1.x portable migration or recovery from physical power loss.

### Disposable Windows validation

The maintainer ran the guarded bundle inside the Windows 11 VMware VM after
creating a snapshot and returned the complete results. The report records a
different guest hostname, execution enabled and successful verification of the
bundle's input hashes. No MSI was installed on the development computer.

For each default/custom install path, the runner checks clean installation of
2.0.0, installation of 1.4.1 and upgrade to 2.0.0, repair, complete recorded-payload
and shortcut removal, and preservation of a test preferences file. Installed host
and UI startup are checked after clean install, upgrade and repair in isolated
profiles with audio suspended. Test profiles and logs remain as evidence; cleanup
is restricted to products/processes/files created by that run. Both executed
rounds passed. The 288-file installation inventories match the candidate payload,
the repaired scanner hash matches, and the test preferences were preserved and
then safely cleaned. All twelve MSI operations returned 0; their logs contain no
`Return value 3`. The redirected stderr files contain only PowerShell information
and progress records, not errors.

Pending beyond these bounded checks: abrupt power-loss/cancellation/file-in-use
installation cases, legacy EXE installer
migration, physical hotplug, prolonged audio sessions, additional Windows
versions and a broader plugin/device matrix. Plugin process isolation remains
experimental. Windows publisher signing through SignPath is explicitly deferred.

The previous audit results remain historical evidence in
[the remediation log](../devlog/2.0.0/2026-10-01-audit-remediation.md), not a
substitute for this candidate's package checks.

## Showcase and recording cleanup (2026-10-05)

The README showcase now links to the maintainer's YouTube video:
[LightHostModern 2.0.0 showcase](https://www.youtube.com/watch?v=FT8IO7-vbjo).
The seven former showcase screenshots were removed from the project. Historical
capture and animation verification records above describe earlier revisions.

The dedicated recording build 0039, its synthetic audio host, six fictional
Aster Audio plugins, six demonstration profiles, reset seeds and local recording
source/build tree were removed. Temporary runtime profiles used for the earlier
README captures were removed as well. Normal application preferences were not
reset. Production sources and binaries never included the recording backend or
those demonstration profiles; ordinary empty built-in profiles remain available.

The local installer, portable and corresponding-source archive were refreshed
for this documentation change. Package checks verify that no recording plugins,
profiles or user settings are distributed and that production executable/library
hashes match the preceding candidate. The update manifest and artifact hashes
were regenerated. Evidence is under `out/clean-release-20261005` and the current
artifact identities are in `releases/v2.0.0/candidate-verification.json`.
The complete application suite and VM installer lifecycle were not rerun for
this documentation and demo-data cleanup. Nothing was published.

## Documentation review (2026-10-05)

The current user guides and internal contracts were checked against the local
2.0.0 source. Corrections cover List versus Chain processing and latency, IPC 5,
scanner/cache 3, profile-restored mute/bypass, device selection, diagnostics and
tray measurements, VST2 restart behavior, factory-reset scope and build/test
instructions. The documentation index now separates current guides from
[historical records](historical-records.md).

Old validation results remain dated and unchanged. Unavailable generated-evidence
links were replaced with their original paths and an explanation that these
files are local, ignored work products. Local documentation links and Markdown
formatting are checked separately from application tests. No audio session, UI
test, installer lifecycle or full regression suite is run for this text revision.

The corresponding-source ZIP is refreshed from the final documentation and
unchanged application/dependency sources. Its read-back verification and delivery
checksums identify this revision. Evidence is under `out/docs-review-20261005/`;
the installer, portable ZIP and signed update manifest retain their previous
identities at that checkpoint. Publication approval was received afterward,
as recorded below.

## Publication approval and final media

The maintainer supplied `docs/videos/lighthostmodern-v2.0.0-showcase.mp4` and
authorized publishing version 2.0.0 on 2026-10-05. The file is 1920 x 1080 at
60 fps, H.264 video with AAC stereo audio, 64.37 seconds long, with its MP4 index
at the beginning for progressive playback. Its SHA-256 is
`b1161e240a6e44b3583a8ff55f9eae4586e61599bb76781a74d26fbe7cad05bc`.
The original file is kept in the project. The README uses its uploaded GitHub
media attachment as a native player and retains a YouTube link. Uploading the
attachment did not create or modify an issue or pull request.

Final preparation repackages the updated README with the previously built
application, refreshes the signed update manifest and corresponding-source
archive, and records the checks under `out/publication-20261005/`. Earlier VM
results retain their original artifact hashes; repackaging does not imply that
the new MSI was executed in that VM. The public target is
[v2.0.0](https://github.com/heide-oficial/Light-Host-Modern/releases/tag/v2.0.0).

The final native regression run passed **24/24 tests** in **133.38 seconds**.
Final package inspection passed **6/6 scenarios**, covering artifact identities,
portable contents, MSI migration metadata, signed update verification, stale
staging rejection and application of the real ZIP by the portable update engine.
All 14,430 dependency-source files in the source cache matched the current local
trees, including the patched JUCE build copy. The GitHub Markdown renderer
confirmed that the showcase attachment becomes a native video player. Evidence
is in `package-build.log`, `readme-render.html` and the final source/package
verification records under `out/publication-20261005/`. The repack uses existing
verified application binaries; it does not claim a new compilation or a repeat
of the VM installation tests.

## Publication handoff

The final local release directory contains the MSI, its compatibility alias,
portable ZIP, corresponding-source ZIP, signed update manifest/signature and
verification metadata. The numbered `dev-test` delivery contains an extracted
portable ready for manual use. Build intermediates, test profiles and detailed
logs remain under `out`.

The separate `LightHostModern-2.0.0-VM-Validation.zip` contains the two MSI versions,
the guarded lifecycle runner, startup/IPC helpers, an input hash inventory and
Portuguese instructions. It is a test kit, not a release asset. The returned guest
results were reviewed successfully. Publication approval was subsequently given
on 2026-10-05; the earlier preparation restrictions above are historical.

At publication time, review the final results and approved release notes, commit
the intended source files, create the version tag and upload the exact verified
assets. Replacing or re-signing any binary package requires recalculating its
hashes and update signature. The local `candidate-verification.json` records
the final source commit, artifact identities and confirmed publication result.
