# Plugins

In List mode, the Plugins page separates the active processing chain from the installed plugin database. Chain mode replaces both tabs with a canvas. See [Operating modes and profiles](chain-and-profiles.md) for connections, parallel paths, mixers and saved setups.

The tray's [Quick Access menu](tray-and-window.md) exposes **Running** in List mode, **Plugins** in Chain mode, and **Installed** in both. Open editors, add instances, bypass, duplicate, or remove plugins without opening the main window. List mode also offers Move up/down. The tray uses the same host state as this page; in Chain mode, added plugins start unconnected.

## Running

**Running** contains plugin instances in processing order. Each card shows the plugin name, manufacturer, format, a status badge, and an action menu. The toolbar combines search, output mute, chain bypass, and sorting.

Available actions include:

- open the native or generic plugin editor;
- bypass or enable processing;
- duplicate the instance;
- rename the instance or restore its original name;
- inspect plugin identity and input/output buses;
- configure supported plugin input/output channel formats;
- run inside the host or in a separate process (experimental);
- retry loading an unavailable instance;
- choose a card color;
- swap positions with another instance;
- remove the instance from the chain;
- reorder the chain by drag and drop or the available move actions.

Duplicating a plugin creates another independent running instance. Reordering changes signal flow immediately because audio is processed from the first card to the last.

Search and Sort change the visible list without changing processing order. Drag reordering is available in the unfiltered chain-order view. **Configure plugin channels** opens separate Input channels and Output channels tabs; List mode carries the main bus, while Chain supports explicit sidechain and auxiliary-output routing.

Bypass keeps the slot in the chain and the processor running while selecting latency-compensated dry audio. Individual bypass, global chain bypass, and output mute use short transitions. While global bypass is active, the card's bypass action offers **Disable bypass chain** first. Saved profiles also capture global mute and bypass and restore their saved values when activated or loaded at startup.

## Installed

**Installed** is the known-plugin database produced by scanning. Its toolbar combines search, **Scan for plugins**, and sorting. **Group by manufacturer**, at the top of the Sort menu, displays manufacturer headings connected to their plugin cards. Status badges distinguish available, running, bypassed, and unavailable entries.

Each installed entry can be:

- added to the running chain;
- opened in File Explorer;
- renamed, restored to its original name, or inspected in Plugin details;
- removed from the database.

These actions are in each card's **…** menu. A custom installed name is saved and used when adding a new running instance; running instances can then be renamed independently. The menu exposes only Rename; **Restore original name** is a button inside that dialog, consistently with card, channel and device renaming.

Removing an installed entry that is currently running also removes its running instances after confirmation. **Settings > Danger zone > Remove missing plugins** deletes entries whose plugin files no longer exist. **Clear plugin database** clears the database and running chain after confirmation.

## Scan paths

**Scan for plugins** opens one dialog for folders and scanning. Under **Add new path**, type a full folder path or use the folder picker, then save it with the save icon. Saved paths appear below in individual cards with open-folder and delete actions. The editor and saved paths scroll together, with their headings outside the cards.

Default Windows locations include common system and per-user VST3 folders and conventional VST2 folders under Program Files. Changes are saved automatically in WinUI preferences and sent to the host when scanning starts. Duplicate and invalid paths show inline feedback.

## Scanning and quarantine

**Start scan** opens a small progress dialog with cancellation. Completion shows results and offers **Retry failures**, **View failures**, and **Close**; retry and failure details are enabled when failures exist. Failure cards separate the path, readable error, format, and attempt count, with **Retry selected** for checked entries. Cancelling keeps plugins already found.

Scanning uses `LightHostModernScanner.exe` for directory enumeration, module catalogs and individual class validation. A class crash or timeout does not discard other verified classes in its module. Partial cache checkpoints let retries reuse successful classes. The worker job also terminates descendants on cancellation or owner exit. Active effects run inside the audio host by default. Their Running/Chain menu offers optional execution in a separate process; see [Plugin isolation](plugin-isolation.md) for the extra block latency, containment limits and validation status. VST2 scanning requires a build with VST2 support and **Enable VST2 plugins** enabled at host startup. Restart the app after changing that setting.

The initial inactivity timeout is 60 seconds; real filesystem/hash progress renews it. A separate 30-minute processing limit prevents unbounded enumeration; consumer backpressure is excluded. Enumeration overlaps one validator through a queue of at most 128 candidates. Fingerprints are computed at discovery and after validation; warm scans avoid plugin instantiation. Worker protocol/cache version 3 invalidates older metadata caches without deleting the installed database or saved sessions.

VST3 validation compares the module and class, including the complete CID when available. Bundle and corresponding inner-binary paths are equivalent, while persisted IDs/aliases remain stable. The isolated catalog checks the real factory instead of trusting stale manifest data; channel/bus data comes from the instantiated effect. JUCE adaptations are limited to the build-owned dependency copy.

The manifest-only pass never falls through to a second factory enumeration when metadata is missing or invalid. A single-class VST3 is instantiated, checked and fingerprinted in its existing isolated catalog worker; multi-class modules retain separate validation workers per class. Hash input is buffered in 256 KiB blocks while retaining the same full-content SHA-256 fingerprint. Known-class/ID lookups use indexes rather than rescanning the full installed list for each result.

Partial scans append checksummed class records to a bounded cache journal. The complete XML catalog is written at the start/end boundaries rather than after every class. Recovery keeps complete preceding records when a trailing record is interrupted, and checkpoint IDs prevent replaying a journal from another snapshot. This additive cache-v3 format does not change plugin/session identities.

Verbose captures include `scan.timing` events for module/catalog/instantiation, fingerprint reads and hashing, cache operations, and child-process launch/CPU/I/O/peak working set. Use `python Utilities/summarize-scan-timings.py <capture-folder-or-export.txt> --output report.json`. Nested stage durations and overlapping workers are not additive, and process I/O counters are not physical-disk transfer measurements.

Default missing folders are skipped. User-added unavailable paths remain actionable failures; default VST2-only folders are not traversed with VST2 disabled. Directory links are deduplicated canonically, cycles are reported as skipped, and traversal depth is limited to 64. Incompatible PE architectures are identified before instantiation. Load failures can include a Windows error code, such as 126 for an unavailable module/dependency; retry alone does not repair a plugin installation.

Linked subdirectories **inside a plugin bundle** are reported as `module_link_unsupported`; they are not silently omitted from module verification. Regular bundles and linked scan roots remain supported.

Progress distinguishes examined modules, cached modules, recognized classes, skipped items and current failures. Enumeration is indeterminate; incomplete roots cannot report complete success. A successful root retry resolves its old enumeration failure. Failure details include the technical reason and stage, with selectable text.

Plugins that fail to load can be quarantined so one broken binary does not repeatedly crash startup or chain restoration. Use `--clear-failed-plugins` to clear that quarantine, or `--safe-mode` to start without restoring the saved chain.

## State persistence

The host saves the installed database, aliases, running order, per-instance names and bypass state, individual processor state, and plugin editor positions. Persistent instance IDs keep duplicate plugins distinct. Versioned session files use atomic writes, backups, and migration of legacy keys to preserve state across reorder, restart, and recovery.

See [Persistence and recovery](persistence-and-recovery.md) for recovery commands and storage behavior.

### Instance card colors

The Running plugin menu includes **Card color**, with presets, a custom color picker,
and **Default color** to remove the override. Color applies to that instance and is
saved with its session/profile, including duplicates. It does not change the Installed
catalogue or other instances of the same plugin. Chain cards and ports expose equivalent
color actions in their context menus. The color dialog also saves and removes app-wide palette swatches; see [Operating modes and profiles](chain-and-profiles.md#visual-colors-and-profile-dates).
