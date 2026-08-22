# Plugins

The Plugins page separates the active processing chain from the installed plugin database.

## Running

**Running** contains plugin instances in processing order. Each card shows the plugin name, manufacturer, format, and current status.

Available actions include:

- open the native or generic plugin editor;
- bypass or enable processing;
- duplicate the instance;
- remove the instance from the chain;
- reorder the chain by drag and drop or the available move actions.

Duplicating a plugin creates another independent running instance. Reordering changes signal flow immediately because audio is processed from the first card to the last.

Bypass keeps the slot in the chain. The host compensates for the plugin's reported latency while passing audio around its processor, which avoids shifting the remaining chain whenever bypass is toggled.

## Installed

**Installed** is the known-plugin database produced by scanning. It can be searched and sorted by plugin name, manufacturer, format, availability, or running status.

Each installed entry can be:

- added to the running chain;
- opened in File Explorer;
- removed from the database.

Removing an installed entry that is currently running also removes its running instances after confirmation. **Remove missing** deletes database entries whose plugin files no longer exist. Clearing the database also clears the running chain.

## Scan paths

**Scan paths** opens a wide modal containing the directories searched by VST3 and VST2 scanning. **Add new path** inserts a new editable entry and selects its text immediately. Folder buttons open the Windows folder picker, and delete buttons remove individual entries.

Default Windows locations include common system and per-user VST3 folders and conventional VST2 folders under Program Files. Paths are saved in WinUI preferences and sent to the host when scanning starts.

## Scanning and quarantine

Scanning is performed by the host with JUCE plugin formats. VST2 scanning occurs only when support was compiled into the host and **Enable VST2 plugins** is enabled.

Plugins that fail to load can be quarantined so one broken binary does not repeatedly crash startup or chain restoration. Use `--clear-failed-plugins` to clear that quarantine, or `--safe-mode` to start without restoring the saved chain.

## State persistence

The host saves the installed database, running order, bypass state, individual processor state, and plugin editor positions. Stable slot keys and compatibility with older keys help preserve state across reorder and removal operations.

See [Persistence and recovery](persistence-and-recovery.md) for recovery commands and storage behavior.
