#pragma once
#include "BoundedInput.h"
#include <juce_data_structures/juce_data_structures.h>

namespace lightHostModern
{
inline void useBoundedPreferences(juce::PropertiesFile::Options& options)
{
    // The build-owned JUCE adapter consumes the returned tree, not the original
    // file again. Invalid input cannot fall back to its unbounded binary reader.
    options.xmlFileReader = [](const juce::File& file) { return parseBoundedXml(file); };
    options.xmlValueParser = [](const juce::String& text) { return parseBoundedXml(text); };
}
}
