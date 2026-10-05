# LightHostModern documentation

These guides describe **LightHostModern 2.0.0**, reviewed on **2026-10-05**. See the [release validation record](release-2.0.0-validation.md) for package revisions, completed checks and validation limits; a documentation review is not a new application test run.

LightHostModern is a Windows audio plugin host. A JUCE host process owns the audio stream, serial List or routed Chain, persistence and notification-area lifetime. A native WinUI 3 process presents the interface and exchanges commands and snapshots with the host through a local named pipe.

## Using the app

| Guide | Contents |
| --- | --- |
| [Dashboard](dashboard.md) | Audio devices and meters, operating mode, active profile, plugin counts, CPU, RAM, VRAM and app version. |
| [Audio](audio.md) | Backends, devices, sample rate, buffer size, Stereo/Mono and Individual/Pairs channel selection. |
| [Plugins](plugins.md) | Plugin discovery, the installed catalogue, running List instances, plugin channels and editors. |
| [Chain and profiles](chain-and-profiles.md) | Canvas routing, parallel paths, mixers, colors, selection, keyboard controls and saved setups. |
| [Settings](settings.md) | General behavior, operating mode, appearance, notifications, device policy and confirmed maintenance actions. |
| [Diagnostics](diagnostics.md) | Local measurements, driver information and detailed logs for troubleshooting. |
| [Tray and window](tray-and-window.md) | Quick Access, live Performance, background lifetime, startup and sidebar behavior. |
| [Support me](support.md) | Donation, repository and showcase submission actions. |
| [Experimental plugin isolation](plugin-isolation.md) | Per-plugin workers, added latency, failure recovery and compatibility limits. |

### Typical workflow

1. Start the host and open its interface from the tray. Configure the stream on **Audio**.
2. Choose **List** or **Chain** in **Settings > Operating mode**. Changing mode requires a host restart, not just reopening the window.
3. Scan plugin folders from **Installed** in List, or **Add plugin** in Chain.
4. In List, add and reorder running plugins. In Chain, connect Audio input, plugins/mixers and Audio output. A new unconnected graph is silent.
5. Use **Profiles** to save the setup, optionally including its current audio settings. Profiles belong to one mode; changing profiles can prompt to save or discard edits.
6. Use Dashboard, Diagnostics or tray Performance to inspect the app. Enable a detailed-log capture in Diagnostics when investigating a problem.

Audio processing needs a running host and an open, usable device. Closing the interface normally can leave processing active when **Close to tray** is enabled. Forced termination of the interface is handled differently; see [window lifetime](tray-and-window.md).

## Development and contracts

| Document | Contents |
| --- | --- |
| [Architecture and IPC](architecture-and-ipc.md) | Processes, startup, IPC 5, operations, snapshots and scanner protocol. |
| [State and events](state-event-contract.md) | Generations, revisions, events and command completion. |
| [Device selection](device-selection-contract.md) | Device identity, selection, policies and recovery. |
| [Audio processing](audio-processing.md) | Serial and graph processing, latency compensation, meters and failure handling. |
| [Audio measurement](audio-meter-contract.md) | Peak meters, diagnostic counters and collection cadence. |
| [Session contract](session-contract.md) | State capture, bounds, atomic saves and profile recovery. |
| [Persistence and recovery](persistence-and-recovery.md) | File locations, restore behavior, reset scope and command-line recovery. |
| [Update contract](update-contract.md) | Background checks, package authentication, portable launcher, rollback and MSI boundaries. |
| [Build and release](build-and-release.md) | Dependencies, build commands, numbered development portables and release packaging. |
| [Localization](localization.md) | Translation catalogues and fallback behavior. |
| [Licensing and corresponding source](licensing.md) | Component notices and source-distribution requirements. |
| [Performance validation](performance-validation.md) | Measurement tools, interpretation and separately dated historical results. |
| [Real plugin validation](real-plugin-validation.md) | Opt-in scanner/processing fixtures and the limits of recorded results. |

## Releases and history

- [2.0.0 release notes](release-2.0.0-notes.md): changes since **v1.4.1**, migration and experimental isolation limits.
- [2.0.0 release validation](release-2.0.0-validation.md): artifact history, completed checks, validation limits and publication details.
- [Historical records](historical-records.md): earlier plans, investigations and validation reports, including the 2.0.0 audit plan. Their dates, versions and original results are retained; they do not describe today's release status.

Paths under `out/`, `dev-test/`, `releases/` and `VM-RESULTS/` in validation records refer to local work products. They are not committed documentation assets and are not guaranteed to exist in a fresh checkout. Current guides and source links are relative to the repository; historical local evidence is written as a path rather than a broken download link.
