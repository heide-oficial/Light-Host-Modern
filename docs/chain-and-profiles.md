# Operating modes and profiles

Settings > **Operating mode > Plugin mode** selects **List** or **Chain**. Profiles are created,
saved and activated from the **Profiles** sidebar page. The Plugins page has a
single toolbar. Settings > Appearance > **Always expand Chain** lets the Chain
page use the full window width even when the application's layout is Compact;
this option is shown in Chain mode and is enabled by default.

A mode change is staged until the host restarts. The confirmation offers Restart
now or Restart later; **Waiting for restart** reopens the confirmation. Selecting
the currently running mode cancels the staged change. Closing/reopening only the
interface does not apply a mode change.

## List

Running and Installed retain their existing behavior. Running order defines a
serial audio path. Profiles add named, independently saved configurations without
changing this processing model.

## Chain

Plugins shows a canvas instead of Running and Installed. A new Chain setup starts
with Audio input and Audio output cards and no connection, so its output is silent.

- Add installed plugins or mixers using the toolbar or canvas context menu. The picker
  can open the existing scanner when a plugin has not been discovered yet.
- Drag from an output port to an input port to connect them. An output can feed
  several paths. Each destination channel accepts one connection; combine paths
  with a Mixer or separate inputs on a suitable plugin.
- The built-in Mixer starts with four stereo inputs, independent gain/mute, and one stereo
  output. Its menus can add or delete stereo pairs, retaining at least one and at most 128 per side. Mixer gain/mute controls change smoothly without rebuilding the processing graph.
- Plugin buses retain the layout negotiated by the plugin. Enabled stereo buses
  have grouped L/R ports by default. A card's **…** menu (also available by right-click) can separate L/R or hide
  unused channels. Hiding does not disable or reconfigure a plugin bus. Connected
  channels must be disconnected before they can be hidden.
- Hardware ports keep physical channel numbers even when Audio enables only a
  subset. Only channels selected in Audio actually receive/send device audio.
  Disabled hardware ports and their wires are hidden from the canvas. Saved
  connections are retained and remain silent; channels are never renumbered or
  redirected to another physical channel.
- **Configure plugin channels**, above Input channels in a plugin's context menu
  (or its Running menu in List mode), opens the audio-bus configuration dialog.
  Querying formats briefly releases and restores the plugin's prepared audio:
  VST3 plugins can only confirm their supported arrangements while inactive.
  It offers the current/default format and supported mono, stereo and common
  surround formats, plus Disabled when supported. The dropdown lists Stereo,
  Mono and Disabled first (only supported entries), followed by other formats. A change can also adjust a
  related bus when required by the plugin. List mode still carries the main bus;
  use Chain mode to explicitly route sidechains and auxiliary outputs.
- Channel identities include the bus index/name and speaker type. Only actual
  Left/Right channels on the same bus are paired. Two discrete channels are not
  presented as stereo. Pairs/Individual and Hide/Show remain visual choices.
- Disabled, missing or replaced plugin ports retain their saved identities,
  aliases, colors and wires. They are labeled Unavailable, cannot receive new
  connections, and their retained wires are dashed and silent. Re-enabling the
  same channels restores their connections. Channel-format changes clear routing
  undo history so an earlier graph cannot restore an obsolete mapping.
- Plugin channel formats persist in sessions/profiles and isolated workers.
  Session schema 3 prevents earlier hosts from interpreting stable logical port
  numbers as processor buffer indices. Existing schema 1/2 sessions are adopted
  on first load without renumbering their existing ports.
- Stereo to mono averages L/R. Mono to stereo duplicates the mono signal. These
  conversions are labeled on the wire. Changing Individual/Pairs preserves the
  stored channel endpoints and widths: a stereo-to-one-channel wire remains
  stereo-to-mono even when that channel is displayed inside a pair. Reconnect
  to the pair to route audio to both channels.
- Right-drag to pan and scroll to zoom around the pointer. A right-click without
  dragging opens the context menu. Zoom out/in, percentage and Fit to screen are
  in the bottom-right corner of the canvas. Camera preferences are saved separately
  from chain edits and do not dirty a profile or consume undo history. Organize
  asks for confirmation, places the graph in horizontal routing columns and fits it.
- Left-drag the background to select multiple cards. Ctrl/Shift add to selection.
  Left-drag a selected card to move the group, or click an unselected card to move
  only that card. Escape clears selection. The selection context menu can
  disconnect input/output wires, bypass/enable plugins or delete processing
  elements. Hardware input/output cards cannot be deleted.
- Use the toolbar search to locate an existing plugin, mixer, audio input or
  audio output. Choosing a result centers and highlights that card until clicked.
- Open editor, bypass and other plugin actions are in the card context menu;
  double-clicking a plugin header also opens its editor.
- Rename a card or a port from its context menu. Hardware aliases are shared with the
  rename buttons in Audio and are specific to the backend/device/direction.
  Individual channels and stereo pairs have independent aliases. Plugin/mixer
  channel aliases and processing-card names are saved with the chain.
- Bypassed and disconnected cards have distinct outlines/opacity and text labels.
  A sustained signal arriving without output is flagged after 1.5 seconds;
  temporary silence is not treated as proof of a plugin failure.
- Undo/Redo keeps the last 20 canvas edits, including add/remove. It does not rewind
  live plugin parameter edits. Switching profiles clears history; external tray
  additions/removals/duplications clear it to avoid restoring stale plugin sets.
- Wire animation follows measured audio activity and respects the Windows motion
  preference. The bottom-right controls can disable animation separately.
- Missing or failed plugins retain their cards, state, and connections. Retry
  loading is available in the card menu. Unloaded graph nodes output silence;
  an already prepared isolated effect retains its delayed dry fallback.

Processing follows an acyclic graph with latency compensation where parallel
paths meet. Feedback loops and overlapping destination channels are rejected.
Disconnected plugins retain their state but do not process audio. Canvas
connections carry audio; MIDI connections are not exposed. Bypassing a plugin
keeps its existing connections. Global **Bypass chain** selects the host's direct
dry path, independently of those connections; **Mute output** still silences it.

The graph allows 128 cards (including hardware), 512 connections, and 256 channels
per card. Routing buffers are bounded to 64 MiB and total compensation to ten
seconds. A graph that cannot be prepared produces silence and an error rather
than falling back to a different signal path. Canvas position/visibility changes
do not suspend audio. Connection changes use the host's processing suspension and
resume transition.

Tray Quick Access remains available. In Chain mode, its Plugins submenu omits
Move up/down because connections define the signal order. Plugins added through
the tray appear as unconnected cards.

## Saved profiles

The **Profiles** sidebar page lists only profiles for the current operating mode,
with an **Active profile** section followed by **Other Profiles**. Search filters
the latter; the active profile remains visible. The toolbar groups search and
**Save current settings as a new profile** inside a card. Saving a new profile
activates it. A three-dot menu contains Activate, Overwrite with current settings,
Edit, Duplicate and Delete. Created is replaced by Last updated after the first
update; protected defaults display Built-in clean profile. Editing metadata does
not activate a profile. Duplicating creates an independent saved copy. Deleting
the active profile switches to the protected empty default for its mode.

Profiles store plugin instances and their state, optional process isolation, bypass/order or graph connections,
canvas layout and channel visibility, mixer controls, and global mute/bypass.
Mute and bypass are restored when the profile is activated and on host startup.
Canvas pan/zoom is kept as a separate local view preference for each profile and
does not mark a saved profile as changed.
The **Save current audio settings** toggle captures the current devices/backend, sample rate, buffer, selected
physical channels, and input/output mono options. The setting is presented as a card in the profile dialog; its description explicitly refers to the current audio configuration. When a selected
profile's device is unavailable, the previous device is closed and the requested
device remains the recovery target.

Changing profiles or modes prompts Save/Discard/Cancel for unsaved changes.
Activating a profile of the other mode also requires restart. The last profile of
each mode is remembered; both modes have a protected, empty **Default** profile. Existing users'
current plugin session is migrated into an initial **My plugins** profile.

Profiles expose actions through a three-dot menu. **Overwrite with current settings**
replaces a saved setup after confirmation, keeping its metadata and audio-capture
preference. It does not activate another profile. Default profiles cannot be edited,
overwritten or deleted; save a new profile to retain edits made while a default is
active. Deleting an active profile switches to its mode's default.

Connections can be dragged from either an input or an output. Hardware channel
selection (Individual/Pairs) follows the same per-device preference as Audio;
the hardware card context menu also exposes mono processing. Rename individual
ports with their context menu. Resize card width from the bottom-right corner and
use **Horizontal channels** to place ports side by side with wrapping. Card sizes,
orientation and plugin port display preferences are saved with the session/profile.
Height follows the content automatically. Width stops once the visible channels
fit on one row in horizontal mode, or their complete labels fit in vertical mode.
Hardware channel enablement, grouping and mono settings are shared with Audio.
Pair labels inherit individual custom names unless a pair has its own custom name;
editing an individual name removes that pair override.

Bypassed cards use reduced opacity and diagonal stripes, and muted output dims
the output card. Subtitles identify the kind of element; status details remain in
tooltips. Wires brighten while fresh audio levels are present. Add plugin opens a
searchable catalogue with manufacturer grouping, sorting and scan access.

Settings places **Danger zone** before About. Its actions are initially hidden
behind **Show Danger zone options**. Each action requires confirmation, with the
accept button disabled for three seconds: remove missing plugins, clear the plugin
database, restore original display names, delete profiles, or reset the entire app.
Full reset restarts the app and retains installed plugin files and exported logs.

Saved profiles live in a `.profiles.xml` file beside the host preferences. Updates
use an atomic replacement and a validated backup. Damaged entries are preserved;
readable profiles remain available, while catalogue mutations require explicit
recovery. A read-only catalogue does not make the current session uneditable.
The existing crash-recovery session remains separate from explicitly saved
profiles. Up to 256 profiles and 256 MiB of profile storage are supported.

Graph commands carry the profile ID and session generation. A delayed edit from
another profile is rejected. Mixer previews update live controls while the final
gesture is persisted separately. Normal close flushes pending edits before
disconnecting; failed transmission retains a per-profile recovery file for
explicit review. Forced termination can only preserve data already received.

Cards and ports support keyboard actions: focus the card's options button and
move with arrows (Shift for larger steps), or use a port's **Connect to** menu.
Visual timers stop when the canvas is hidden/minimized without stopping pending
edit delivery. Camera values are validated before drawing the dotted background.

## Manual review

Manual review should cover both modes, save/discard/cancel and restart-later,
profile duplication/deletion, mono/stereo and physical channel mapping, parallel
latency compensation, mixer controls, missing plugins/devices, undo/redo, and
theme/DPI/text scaling in the canvas.


## Canvas navigation and catalogue consistency

Entering the Plugins page in Chain mode fits every card in the visible canvas.
The fit button uses a frame icon; zoom supports 0.1%–300%, with decimal percentages
below 10%. Navigation changes remain local view preferences, not session edits.

Cards resize horizontally through one pointer-capture gesture. Their maximum
width still follows the visible channel content. Bypass stripes cover the entire
rounded card, including its padding. The top-left summary counts plugin instances,
bypassed plugins, and plugins with no connections (hardware and mixers excluded).

Disconnect input/output actions are individually enabled only when the selected
elements have corresponding connections. Physical or visible channel toggles are
inside Input/Output channels → Channels; grouping and mono stay one level above.

Settings → Operating mode → Maximum space between elements controls the maximum
gap allowed when dropping a card or a selection away from the rest of the graph.
The default is 2,400 canvas units, adjustable from 600 to 12,000. The correction
moves the selection together and leaves its wires intact. Reducing this setting
also brings existing distant cards within the new limit, including while the
canvas page is hidden. The setting is shown only in Chain mode. The switch beside Fit enables
blue edge arrows for completely offscreen elements and remembers the preference.

Add plugin hosts the same Installed page components and data controller as List
mode: search, scan, Sort (including manufacturer grouping), manufacturer cards,
connected plugin cards and their action menus. Rename/details temporarily replace
the picker and then return to it with search and sorting preserved.

Canvas cards and corner overlays follow the material selected in Settings, with
solid theme-aware backgrounds for Solid mode and system colors in high contrast. The two
bottom-right panels separate zoom/fit from offscreen indicators/audio animation.
Context menu actions use changing labels instead of checkmarks, and channel
actions follow the current Individual/Pairs grouping. Keyboard search previews
keep the suggestion list open until selection is committed or dismissed.

## Card layers

A card or selection's context menu offers **Arrange layers → Bring to front,
Bring forward, Send backward, Send to back**. The selected cards keep their
relative order. Layers are saved in profiles and support undo/redo; they do not
change plugin processing order, connections or the audio graph. Dragging a wire
to overlapping cards targets only the visible top card.

Performance mode in Settings temporarily stops decorative wire motion and uses
solid canvas surfaces. It preserves the chain's animation preference and does not
change connections or audio processing.

The wire-animation toggle uses a flow icon, not a playback symbol. Its tooltip
states that it only changes the visual effect; it never pauses audio.

The built-in mixer initially sums four stereo inputs into one stereo output. **Pairs**
shows each Left/Right pair together, while **Individual** exposes each side for
separate wiring. Input labels are `1 Left`, `1 Right`, through `4 Left`, `4 Right`;
the output is `Left` / `Right`. Custom names are preserved. The volume/mute controls remain linked per stereo input and are labelled `1 L/R`, `2 L/R`, and so on.

## Visual colors and profile dates

Use **Card color** in a canvas card's context menu or a Running plugin's menu.
Choose a preset or use **Custom color** for the native spectrum/HEX/RGB/HSV and
opacity controls, or choose **Default color** to clear the override. Card colors
are soft overlays so labels remain readable and the selected material remains visible.

Right-click a canvas port and choose **Channel color**. Colors belong to physical
channel indices: Individual changes one channel, Pairs changes both members. Changing
grouping retains existing individual overrides; a paired port shows the first member's
color. Wires use their source/destination channel colors and blend between different
colors. The moving audio indicator follows that same gradient. These are visual-only
changes; they do not rebuild audio routing. Chain colors support undo/redo.

Session/profile serialization retains card and channel colors, including currently
hidden channels. Duplicating a Running instance retains its card color. Older sessions
without color fields use the existing appearance. Invalid serialized colors are rejected
for graph payloads, or ignored for legacy instance attributes.

Profiles show **Created** until their saved definition is changed, then **Last updated**.
A duplicate starts with its own creation date. Built-in profiles retain their clean-profile
label. Unsaved live edits do not change the saved profile's date.

**Settings > Appearance > Dotted canvas background** enables a subtle theme-aware
grid that follows pan/zoom. It is an app preference, disabled by default, shown only in
Chain mode, and temporarily hidden in Performance mode.

### Mixer channel pairs and saved palettes (2.0.0)

Mixer cards offer **Add new input channel** and **Add new output channel**. Each action
adds a stereo pair; right-click a mixer port for **Delete channel pair**, even in
Individual view. At least one input and output pair remains, up to 128 pairs per side.
Every output pair carries the same stereo sum. Deleting a pair removes its connected
wires and shifts the indices of later pairs while retaining their connections, names,
colors, gain and mute settings. Changes support undo/redo and profile persistence.
Hardware and plugin channel layouts remain owned by their device/plugin.

The color dialog always shows **Custom color**, without a collapse control. **Save custom color**, aligned to the right, adds the current
color to the app palette. Right-click any saved swatch, including a supplied preset,
to delete it. Palette changes persist independently of dialog confirmation and profiles;
deleting a swatch does not alter colors already assigned to cards or ports. Up to 128
colors can be saved. Hover preserves the swatch color.

A dragged wire immediately uses its starting port's color. Active audio paths increase
in thickness and gain a subtle halo plus traveling segments. Animation still respects
the canvas flow toggle, Performance mode and the Windows animation preference; the
static active/inactive indication remains available.

### Connecting and inserting elements

Drag from an output to an input, or from an input back to an output. The drop
target uses the visible port bounds, including canvas zoom, pan and Windows DPI.
The existing keyboard connection menu remains available.

Hover near a wire to highlight it and show **Connection · Right-click for options**.
The hit area stays the same size on screen as the canvas zoom changes. Right-click
the highlighted wire to disconnect it.

Drag a disconnected plugin or mixer across a wire to preview insertion. The wire
can cross any part of the card's inner area; it does not need to touch the center.
Release, then choose **Insert** to replace the wire with connections through the
element's selected input and output. The confirmation dialog shows the existing
endpoints and selectors for the element's visible channels. Pairs offers a pair
selector; when a stereo wire reaches channels displayed individually, Left and
Right have independent selectors on that side. Duplicate Left/Right input choices
are blocked. The first visible channels are selected initially. **Keep position only** moves the card without
changing routing. Insertion is one undoable edit; already-connected elements are
not automatically rewired.

Wire context menus contain only **Disconnect**. Empty-canvas actions are grouped
as **Add plugin / Add mixer**, **Organize**, and **Mute output / Bypass chain**.
Card menus group plugin operations, visual/name actions, channels and removal;
input/output disconnection lives inside the corresponding channel submenu.
Multi-selection follows the same grouping for applicable bulk actions, including
card color, channel orientation, layers and directional disconnection.

The tray remains a flat plugin list in Chain mode. It follows the graph's
dependency order, the same order used by the routing runtime, including paths
through mixers. Parallel paths use the graph's stable traversal order. Moving
cards or changing layers does not change this order. Unavailable/unrepresented
instances remain accessible, and List mode retains its explicit plugin order.

The Chain Add plugin dialog supports multiple selections. Selected cards use the app accent color, without a checkbox gutter; clicking a selected card deselects it. Add (N) shows the total, including selections retained while filtering or sorting. Manufacturer headings cannot be selected. Add inserts each chosen plugin once, with separate canvas positions and profile/generation guards. Addition stops on a failed command. The context menu is reserved for details, folder, rename and database removal.

The enabled **Add (N)** action uses the app accent color. **Plugin audio channels**
has separate Input channels and Output channels tabs with a bounded scrolling
area. Switching tabs preserves choices, including input/output formats that the
plugin requires to change together. The dialog follows the Plugins page layout:
title and explanation, an icon tab bar, then one card per audio group with a
format dropdown. The plugin still controls which formats are available.

Right-clicking empty canvas clears the selection and opens the canvas menu;
right-dragging to pan preserves it. Right-clicking any part of a selected card,
including a channel, opens bulk actions when multiple cards are selected.
**Delete selected elements** is the last bulk action. Right-clicking an unselected
card selects that card alone.

Connected cards have a solid foreground-colored outline (white in dark mode).
Disconnected cards use a dotted outline and keep their subdued content. Selection,
search and errors retain their distinct outlines; bypass stripes remain independent
of connection state.

During a wire drag, the source and an eligible target receive a short highlight
animation and remain highlighted until the drag ends. Performance mode and Windows
reduced-animation settings use static feedback. The preview stays above cards and
snaps to the target socket. Completed wire bodies stay behind cards, while their
short socket leads remain above them. Endpoints use each rendered socket's exact
center at any zoom, layout or DPI. Preview eligibility and drop validation share
the same check, excluding unavailable channels, duplicates and feedback loops.
The foreground leads fade into the wire body instead of ending with a hard cut,
preserving each endpoint's custom color.

App dialogs place cancel/back on the left, secondary actions in the middle and
the primary action on the right. A dialog with only a close action places it on
the right. The right-hand primary action uses the accent color independently of
the default keyboard action; disabled buttons retain their disabled appearance.
The shared template is applied before opening; material preparation
keeps existing controls attached so opening a dialog cannot discard their input
capture by rebuilding the content tree.
