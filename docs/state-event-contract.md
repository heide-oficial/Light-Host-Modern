# Protocol 5 state and event contract

LightHostModern 2.0.0 requires IPC version 5 for command, event and meter requests.
Other protocol versions are rejected before dispatch; host and UI must be rebuilt
and restarted together.

The command pipe keeps operation admission and result queries. A separate pipe,
formed by appending `-events` to the profile's command pipe, handles `events`
requests independently. Both pipes use the same host session and 4-MiB limit.

`events` accepts `{hostSession, afterSequence, waitMs}` in its argument object.
`waitMs` is 0–4000. Each response identifies the host session, latest sequence,
entity revisions, changed entity IDs and whether a full resynchronization is
required. The host coalesces structural changes at 100-ms intervals. It retains
at most 256 events, bounded to 32 MiB; a stale/future cursor or truncated change
set requires a snapshot. Empty responses are connectivity heartbeats, not state
changes. Operation IDs identify completions whose results remain in the ledger.

`snapshot-manifest` captures one logical state on the controller thread, after
publishing pending structural changes. It returns an opaque snapshot ID,
event sequence, entity revisions, ordinary state fields and collection counts.
`snapshot-page` takes `{snapshotId, collection, offset, limit}`. Collections are
`activePlugins` and `knownPluginList`; pages contain at most 100 entries and stay
below 4 MiB. All pages retain the same immutable snapshot ID/session. Snapshots
are retained for 60 seconds, up to eight entries and 32 MiB in total. An expired
snapshot returns `stale_snapshot`; consumers restart the read without modifying
their displayed state. Oversized snapshots fail explicitly.

The UI applies a snapshot only after receiving every page. It then resumes
events after the snapshot's sequence, so changes occurring during pagination
are not lost. Host-session changes invalidate both cursors and pending display
deltas, but never authorize replaying a mutation. Collection reconciliation
uses UUIDs/class IDs and updates observable properties without rebuilding rows.

The WinUI shell requests visual telemetry only while visible: Dashboard meters at most 20 Hz
and diagnostic telemetry at most 1 Hz. With diagnostic collection enabled, either
the Diagnostics page or the Dashboard resource cards can request that telemetry.
Meter reads use a third, independent pipe, formed by
appending `-meters`, so pending commands and diagnostics cannot queue in front
of a meter update. See [the meter contract](audio-meter-contract.md).
A minimized window keeps structural events and a 5-second command heartbeat,
with no visual telemetry requests from that window. The native tray Performance
submenu can still read local resource measurements independently, without these
IPC transports. Closing the UI cancels all three transports and exits that UI
process. A new UI attaches to the live host.
