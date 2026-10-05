# Durable session contract

Application version 2.0.0 uses IPC version 5. Session storage is separate from preferences and is scoped by the preferences file, including test profiles and separate instances. Chain mutations carry the active profile ID and generation; stale edits are rejected even when node IDs are shared between profiles.

## File and model

The preferences file's sibling `<preferences filename>.session.json` is an envelope with `formatVersion: 1`, a decimal-string `revision`, `intentionalEmpty`, `migrationId`, `contentHash` (SHA-256), and `sessionXml`. The XML uses the strict, ordered `LIGHTHOSTSESSION` adapter (schemas 1 through 3). UUID, original class identity and description, bypass, custom name, last valid state and recovery material remain independent. XML serialization uses revision zero; the envelope owns the storage revision. Timestamps and revision are excluded from the content digest and deduplication. No fuzzy matching of legacy plugin identities is permitted.

A stored empty collection must explicitly have `intentionalEmpty: true`. Suppressed loading (safe mode/startup preference), missing devices, missing plugins and invalid source data cannot authorize writing an empty replacement. Invalid/unsupported files retain their original bytes and make recovery visible. Files larger than 256 MiB are rejected for automatic loading/writing and retained for manual recovery.

An explicitly requested [factory reset](persistence-and-recovery.md#factory-reset) removes the active preferences/session recovery family and profile catalogue before startup. That deletion is separate from normal session saving and from safe mode, which retains the saved session.

## Migration and recovery

Load and validate main, backup and completed temporary files before considering legacy preferences. Select the valid candidate with the highest revision; ties prefer main. Preserve damaged originals before repairing the primary file. If session files exist but none validates, do not silently reimport stale legacy data or create an empty session.

When no session exists, back up the exact previous preferences before importing the version-1 adapter or legacy exact keys. A migration identity derived from the recovery copy makes generated legacy UUIDs deterministic. Once any valid session exists, it is authoritative; repeated migration never appends records. General preferences, device settings, language, appearance and original legacy recovery keys stay in their existing files.

## Capture and commit

The controller queues dirty instances without consuming newer revisions. For directly hosted plugins, only the plugin's state callback runs on the JUCE message thread behind processing suspension/drain. Encoding, comparison and persistence run after audio resumes. Plugin state hashes are reused until content changes. Isolated plugins capture in their own worker; the mutation queue waits asynchronously while reads and other audio paths remain usable. A failed or oversized capture retains that instance's previous valid state and reports an error. A copied model is immutable after submission. One storage worker serializes it, computes the digest, coalesces changes with a one-second debounce and skips writes for identical content. At most one pending snapshot and one in-flight snapshot are retained.

Before replacing a pending file, the worker copies the newest valid candidate to a flushed backup temporary and atomically renames it over the backup. This also protects recovery when a pending file is the only valid copy. The worker then writes the new session temporary in the same directory, flushes it to disk and atomically renames it over the primary. The primary is never truncated in place. Errors keep the requested revision pending and are exposed through session status. A new submission or explicit retry may retry the pending revision.

Normal shutdown captures and waits for the final write before processors are destroyed. An explicit `flush-session` operation provides the same durability barrier to the updater and returns a structured error on failure. Saved revisions advance only after a successful commit or verified content deduplication against an already successful commit. Recovery must be tested at each write/flush/backup/replace interruption point, including disk-full and replacement failures.

The ordered XML adapter writes schema 3 when plugin channel layouts are captured, schema 2 for isolated instances without captured layouts, and schema 1 otherwise. All three are accepted by 2.0.0. Earlier hosts reject unsupported schemas so they cannot silently reinterpret logical channel identities or execute isolated plugins directly. The BUSES element stores typed input/output bus layouts; routing port inventories preserve bus identity, channel type and unavailable saved endpoints. Runtime buffer mappings are refreshed from the loaded plugin and cannot be overridden by a canvas edit. IDs, isolation, names, colors, routing and state are preserved. The JUCE MemoryBlock encoding (decimal size prefix plus its own alphabet) is validated before allocating, with a 192 MiB individual decoded limit and a 256 MiB file/aggregate ceiling. JSON nesting and XML structure are bounded before parsing.

The profile catalogue has its own validated backup and atomic replacement. Partially invalid catalogues preserve their original files and require explicit recovery before catalogue changes; this does not prevent editing the current valid session. UI recovery files carry profile/generation and are presented for review, never silently applied over another session. Normal close flushes pending edits before stopping IPC; forced termination only preserves edits that reached a durable recovery point.

Each saved profile includes global Mute and Bypass alongside its session model. The active-session XML has no equivalent global-control fields: saving a profile captures the current flags, and applying or initializing that profile restores them. Optional profile audio settings additionally include the configured device selection and input/output mono preferences.

Storage progress publishes an operation-domain event (`session-save`) without changing `chainVersion`. Starting or finishing a disk write must not invalidate an otherwise current canvas edit. Actual graph/instance mutations still advance the edit revision, and profile/generation checks remain mandatory. `ProfileEditIsolationTests.ps1` covers both delayed edits across profiles and edits spanning an autosave.
