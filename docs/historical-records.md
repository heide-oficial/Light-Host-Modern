# Historical implementation and validation records

These documents preserve decisions and results from earlier development checkpoints. A statement such as "current", "pending" or "passed" in a record applies to its recorded version, date and artifact, not to the current 2.0.0 candidate. Do not use an old test pass as evidence that a later binary was tested.

For present behavior, start with the [documentation index](_index.md). For the current release status and test limits, use [2.0.0 preparation and validation](release-2.0.0-validation.md).

## Early implementation and investigation

- [Technical comparison, 2026-09-06](lighthost-comparison-2026-09-06.md).
- [Stability checkpoint, 1.2.2](stability-implementation-status.md).
- [Completion work, 1.2.2](completion-progress.md).
- [Local delivery, 2026-09-08](implementation-delivery.md).

## Interface and packaging revisions, September 2026

- [UI redesign](ui-redesign-validation.md).
- [Toolbars and meters](ui-toolbar-validation.md).
- [Settings and Plugins layout](ui-layout-fixes-validation.md).
- [Plugin database, search and status](ui-database-validation.md).
- [Navigation and scan dialog](ui-scan-dialog-validation.md).
- [Scan feedback and meters](ui-scan-feedback-validation.md).
- [Control alignment](ui-alignment-validation.md).
- [Controls and catalogue names](ui-controls-validation.md).
- [Settings crash verification](settings-crash-verification.md).
- [Portable packaging correction](portable-package-verification.md).

## 1.4.0 and 1.4.1 work

- [Issue 6 and PR 5 implementation plan](issue-6-implementation-plan.md).
- [Individual/Pairs selection and mono output plan](audio-channels-followup-plan.md).
- [Issue 7 scanner and detailed-log plan](issue-7-scanner-and-verbose-logs-plan.md).
- [Issue 7 validation](issue-7-validation.md).
- [UI lifetime and sidebar validation](ui-lifetime-and-sidebar-validation.md).
- [1.4.1 release validation](release-1.4.1-validation.md).

## 2.0.0 planning and design

- [Audit remediation plan, 2026-10-01](2.0.0-audit-remediation-plan.md). The original acceptance plan is retained; its introductory note links to later execution and release results.
- Chain design concepts: [horizontal](concepts/plugin-chain/01-horizontal.png), [vertical](concepts/plugin-chain/02-vertical.png) and [details](concepts/plugin-chain/03-details.png), with [generation prompts](concepts/plugin-chain/prompts.txt). These are early concepts, not screenshots of the final interface. The current showcase is linked from the [README](../README.md).

The [performance](performance-validation.md) and [real-plugin](real-plugin-validation.md) guides contain usable test instructions followed by explicitly dated historical results. Their current procedures and old results are kept separate.

## Local evidence

Old reports mention packages, screenshots and logs under `out/`. These generated files are excluded from Git; many were removed during workspace cleanup. Their original paths are preserved as text for traceability, without presenting unavailable files as downloadable links. The 2026-10-05 documentation review found 105 missing local-evidence links across nine historical reports and converted those references to paths. Links between the maintained documentation pages remain navigable.

No old result was rerun or changed from failed/pending to passed during this documentation update. Later results supersede only the checks and artifacts they explicitly identify.
