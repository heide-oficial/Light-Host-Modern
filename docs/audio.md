# Audio

The Audio page configures the stream owned by the host process. Changes are sent to the host over IPC and are reflected back through the next state snapshot.

## Backend and device selection

Select an available backend first. The list is provided by JUCE and commonly includes Windows Audio, DirectSound, and ASIO when those drivers are available.

- For Windows Audio and DirectSound, input and output devices can be selected independently.
- For ASIO, **Device** represents one driver. Light Host Modern validates that the driver actually opens and avoids saving an invalid mixed input/output pair.

If a selection fails, the host keeps or restores the previous working setup and returns a descriptive error to the UI.

## Input and output channels

The input and output channel sections list the channels exposed by the opened device. Each channel can be enabled independently, and **Check all** or **Uncheck all** changes the complete group.

Channel masks are saved for the combination of backend, input device, and output device. Returning to the same configuration restores its previous channel choices when possible.

## Sample rate and buffer size

The available sample rates and buffer sizes come from the active driver. Changing either value rebuilds the device setup and prepares the running plugin chain for the new stream configuration.

A smaller buffer can reduce latency but gives the realtime thread less time to finish each block. A larger buffer is normally more tolerant of expensive plugins but increases monitoring latency.

## Signal flow

The enabled input channels enter the serial running chain. Each plugin receives the output of the preceding slot, and the result is routed to the enabled output channels. Empty and fully bypassed chains preserve direct input-to-output routing where the channel configuration allows it.

See [Audio processing](audio-processing.md) for the realtime implementation.

## Disabled choices

Settings can block specific backends or device choices. Disabled choices are excluded from automatic recovery and cannot be selected manually until they are enabled again through **Manage enabled devices**.
