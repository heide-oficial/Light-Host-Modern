#pragma once
#include "PluginInstances.h"
#include "PluginIdentity.h"
#include <stdexcept>

namespace lightHostModern
{
// Validate the instance returned by the format before passing it saved bytes.
// A replaced single-class VST2 DLL can instantiate despite a stale requested
// vendor ID. Metadata changes are harmless; a different class is not.
template<class Restore>
void restorePluginState(PluginInstanceRecord& record, const juce::PluginDescription& actual, Restore&& restore)
{
    if (!samePluginClass(record.description, actual))
        throw std::runtime_error("plugin_identity_mismatch");
    if (record.lastValidState.isEmpty()) return;
    juce::MemoryBlock binary;
    if (!decodePluginState(record.lastValidState, binary)) throw std::runtime_error("invalid_saved_state");
    try { restore(binary.getData(), static_cast<int>(binary.getSize())); }
    catch (...)
    {
        record.stateCaptureAllowed = false;
        record.recoveryState = record.lastValidState;
        throw std::runtime_error("plugin_state_restore_failed");
    }
}

// The controller provides the appropriate plugin thread and callback exclusion.
// Assignment is last, so an exception (including encoding/allocation failure)
// cannot erase the previously captured, valid state.
template<class Capture>
bool capturePluginState(PluginInstanceRecord& record, Capture&& capture, size_t maximumEncodedBytes=maximumSessionStateBytes)
{
    try
    {
        juce::MemoryBlock binary;
        capture(binary);
        if (binary.getSize() > maximumPluginStateBytes) return false;
        // Include the decimal size and separator before allocating its encoding.
        if ((binary.getSize()*8+5)/6+12>maximumEncodedBytes) return false;
        auto encoded = binary.toBase64Encoding();
        record.lastValidState = std::move(encoded);
        return true;
    }
    catch (...) { return false; }
}
}
