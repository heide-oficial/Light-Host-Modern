#pragma once
#include <juce_core/juce_core.h>

namespace lightHostModern {
// Called after validating device generation, channel bounds and display name.
inline void editAudioChannelAlias(juce::DynamicObject& names, int channel, int width, const juce::String& name)
{
    if (width == 1) names.removeProperty(juce::String(channel - channel % 2) + ":2");
    const auto key = juce::String(channel) + ":" + juce::String(width);
    if (name.isEmpty()) {
        names.removeProperty(key);
        for (int lane = channel; lane < channel + width; ++lane)
            names.removeProperty(juce::String(lane) + ":1");
    } else names.setProperty(key, name);
}
}
