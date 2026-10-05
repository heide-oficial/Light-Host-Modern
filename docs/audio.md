# Audio

The Audio page configures the stream owned by the host process. Changes are sent to the host over IPC and are reflected back through the next state snapshot.

## Backend and device selection

Select an available backend first. The list is provided by JUCE and commonly includes Windows Audio, DirectSound, and ASIO when those drivers are available.

- For Windows Audio and DirectSound, input and output devices can be selected independently.
- For ASIO, **Device** represents one driver. LightHostModern validates that the driver actually opens and avoids saving an invalid mixed input/output pair.

If a selection fails, the host keeps or restores the previous working setup and returns a descriptive error to the UI.

Choose **None** to clear a device selection. With no usable audio device selected, processing stays stopped. A missing selected device remains visible as the recovery target instead of silently selecting another device. Recovery preferences are under [Settings](settings.md#audio-recovery).

## Input and output channels

The page is ordered Devices, Format, Input/Output settings, then Input/Output channels. With sufficient width, input stays on the left and output on the right in both rows. Each settings card contains an **Input mode** or **Output mode** dropdown (Stereo / Mono), followed by the Pairs/Individual selector without a divider. Controls are aligned to the right. Each channel card contains its title, Check all / Uncheck all action and list. Narrow windows stack each direction's settings and channels.

The input and output sections each offer **Individual / Pairs** selection. Inputs default to Individual and outputs to Pairs. Switching this view preserves enabled channels and does not restart audio. Pairs follow physical channel order (1 + 2, 3 + 4); an odd last channel remains individual. The selector is hidden when fewer than two channels exist.

A partially checked pair has only one channel enabled. Clicking it enables both channels in one transaction; clicking a fully checked pair disables both. **Check all / Uncheck all** still changes the complete section. Presentation preferences are saved separately for inputs and outputs for each backend/input/output device combination.

Channel masks are saved for the combination of backend, input device, and output device. Returning to the same configuration restores its previous channel choices when possible.

## Sample rate and buffer size

The available sample rates and buffer sizes come from the active driver. Changing either value rebuilds the device setup and prepares the running plugin chain for the new stream configuration.

A smaller buffer can reduce latency but gives the realtime thread less time to finish each block. A larger buffer is normally more tolerant of expensive plugins but increases monitoring latency.

## Signal flow

In List mode the enabled input channels enter the serial running chain. An empty List chain, or one with every plugin bypassed, preserves direct input-to-output routing where the channel configuration allows it.

In Chain mode, explicit wires route channels through plugins and mixers, including parallel paths. A new Chain has no wires and produces silence until an input-to-output path is connected. To pass audio without a plugin, connect Audio input directly to Audio output. Bypassing a plugin preserves its existing path; it does not create missing connections. The canvas reflects the enabled physical channels, aliases, Individual/Pairs selection and mono controls from Audio. Global **Bypass chain** selects the host's direct dry path in either mode, independently of the canvas connections.

See [Audio processing](audio-processing.md) for the realtime implementation.

## Disabled choices

Settings can block specific backends or device choices. Disabled choices are excluded from automatic recovery and cannot be selected manually until they are enabled again through **Manage enabled devices**.

The management dialog uses a fixed inventory for one edit. Save validates the inventory token and every device identity before applying the batch; hotplug or ambiguous names require refreshing the dialog. Checkbox positions are never used as device identity. A connected/open driver, active processing, mute and an unconnected graph path are distinct states.

## Input mode

The **Stereo / Mono** dropdown sits in **Input settings**, defaults to Stereo and is saved for the exact backend/input-device/output-device combination. Existing saved choices are preserved. Stereo keeps the current separate-channel routing; Mono sums the enabled, packed input channels with unity gain before the plugins, routing that sum into the main stereo pair. Other host channels retain their route. The sum can exceed 0 dBFS or cancel signals of opposite polarity; there is no normalization or limiter.

A 5 ms input-matrix transition avoids a forced fade to silence. Both global and plugin dry paths receive the same transformed input. In List mode, a plugin with a mono main output is centered in the main stereo route while input mixing is enabled; real stereo output is preserved. Chain mode uses its explicit channel connections and mono/stereo wire conversions. Auxiliary plugin buses remain isolated.

Dashboard and Audio show a notice when processing is unavailable. Explicit None/safe mode and an unconfigured first start are distinguished from a missing selected device. Retry bounds remain 1–60 seconds and 1–100 attempts.

## Output mode

This independent **Stereo / Mono** dropdown sits in **Output settings**. In Mono mode, when physical outputs 1 and 2 are both enabled, each receives their average (`0.5 × L + 0.5 × R`) after plugins, global bypass and mute, before output metering. Switching uses a 5 ms transition. Equal signals keep their amplitude; opposite polarities can cancel. There is no limiter or normalization.

If only one principal output is enabled, its signal and level are preserved. Outputs 3 and higher are unchanged, even if they are the only enabled outputs. The tooltip explains when the main pair is incomplete. The preference defaults to Stereo, is saved per device combination and can be prepared while a configured device is temporarily unavailable. Both mode dropdowns are independent of channel grouping. The hardware card menus in Chain use the same names and Stereo / Mono choices, above Channel selection.

## Device and channel names

Use a channel's rename button to give an individual channel or displayed pair a custom name. Channel names also appear on the corresponding Chain hardware ports. Device names can be edited in **Settings > Audio recovery > Manage enabled devices** and appear in the device selectors. Chain hardware-card titles can be renamed separately on the canvas. Renaming does not change device selection or wiring.

**Restore original name** is inside the rename dialog. Restoring a pair's original name also clears custom names on its two individual channels. Other pairs keep their names. Editing a single channel clears any explicit pair caption that would hide that edit.
