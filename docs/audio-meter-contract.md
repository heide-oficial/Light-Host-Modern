# Audio measurement contract (IPC 5)

Input is measured before plugins. Output is measured after individual/global
wet/dry selection, mute, optional main-output mono and the coordinated resume ramp. No clipping decision
uses a value clamped to unity. Host processing continues while meters are hidden.

LightHostModern 2.0.0 uses IPC 5 for both the command and dedicated meter endpoint.
Detailed histories and diagnostic counters are collected while Diagnostics is
enabled. Dashboard peak levels remain available when it is disabled; toggling
diagnostic collection clears the detailed meter histories.

`meters` contains `input` and `output`. Each direction contains `channels` and
`aggregate`; each measurement contains linear `rms`, `peak`, `peakHold` and a
persistent `clipped` flag. Channel records also contain a zero-based stream
channel ID and its displayed device-channel label. The Dashboard presents only
two level bars with 28 segments on a logarithmic -60..0 dBFS scale and
green/yellow/red levels. Each has an unclamped, one-decimal dBFS reading in a
fixed-width field to its left, with smooth decay and short peak retention. RMS, hold and clipping details remain in
the IPC contract but are no longer shown
as labels or a channel/clipping menu in the UI.

RMS uses a prepared sliding window of 300 ms, initially padded with silence.
Peak is the greatest absolute sample in the latest callback. Peak hold retains
the highest observed sample for 1.5 seconds of processed audio after that peak.
The aggregate independently selects the maximum channel RMS, peak and hold;
its clipping flag is true if any channel has clipped. A sample at or above unity
latches clipping. Nonfinite samples also latch clipping and are excluded from
the RMS accumulator, so a malformed plugin cannot poison later measurements.

`reset-clipping` takes one object: `{direction: "input" | "output" | "all",
channel: <zero-based integer, optional>}`. Omitting the channel resets that
direction; `all` has no channel. Reset works even when the audio device is closed.
New clipped samples may immediately relatch it. Reset never alters audio.

All histories and accumulators are prepared on the controller with callback
exclusion. Channel capacity follows the effective stream (maximum 256), not
the number of installed plugins. The callback publishes only fixed-capacity
atomic measurement data. It never allocates strings, JSON, histories or vectors.

Diagnostics distinguish DSP deadline load, host process CPU, UI process CPU,
application worker CPU, xruns, failed plugin calls, dropped MIDI events and
chain latency. Requested device settings are separate from effective driver
values. Unknown driver values are null, never fabricated zeroes.
Diagnostics has a dedicated sidebar page after Profiles and before Support me,
with non-collapsible cards. When diagnostic collection is enabled, the UI requests
telemetry at most once per second while either that page or the Dashboard resource
cards are visible. Dashboard resource cards combine available host, UI and worker
measurements; the two level meters retain their independent maximum presentation
rate of 20 Hz, including when Diagnostics is disabled.

The native tray Performance submenu also reads local resource measurements at
approximately 1 Hz while visible, independently of the WinUI window and its IPC
requests. Both consumers renew the host's two-second memory-sampling lease. A
minimized or closed interface therefore does not prevent an open tray submenu
from displaying current resource readings. Turning Diagnostics off disables
these resource readings while retaining Dashboard peak meters.

The Dashboard reads `meter-levels` from the independent `<host-pipe>-meters`
endpoint. Its response contains `inputPeak`, `outputPeak` and the standard
protocol/session envelope. This endpoint reads only the latest atomic peaks;
it does not query a driver, wait for the controller, or serialize diagnostics.
Other commands are rejected on the meter endpoint. `transport-diagnostics`
reports these reads separately as `meterRequests`.

The UI allows one meter request in flight, with a 150 ms deadline, and discards
late results or results from an obsolete host session. Structural snapshots
and one-second diagnostic updates do not overwrite newer meter readings.
Hidden/minimized meters make no new meter requests. Preparing or releasing
the audio processor clears its published peaks so stopped audio cannot leave
a stale level on screen. The 50 ms presentation interval is not an audio
latency guarantee; it excludes device, plugin and display latency.
