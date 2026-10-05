#pragma once
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include "RoutingGraph.h"
#include <stdexcept>

namespace lightHostModern::pluginBuses
{
inline juce::var channels(const juce::AudioChannelSet& set)
{
    juce::Array<juce::var> result;
    for (int i = 0; i < set.size(); ++i) result.add(static_cast<int>(set.getTypeOfChannel(i)));
    return result;
}
inline bool readChannels(const juce::var& value, juce::AudioChannelSet& result)
{
    const auto* items = value.getArray();
    if (!items || items->size() > 256) return false;
    juce::AudioChannelSet next;
    for (const auto& v : *items) {
        if (!v.isInt() || static_cast<int>(v) <= 0 || static_cast<int>(v) > 383) return false;
        next.addChannel(static_cast<juce::AudioChannelSet::ChannelType>(static_cast<int>(v)));
    }
    if (next.size() != items->size() || juce::JSON::toString(channels(next), true) != juce::JSON::toString(value, true)) return false;
    result = next; return true;
}
inline juce::var encode(const juce::AudioProcessor::BusesLayout& layout)
{
    auto* result = new juce::DynamicObject;
    for (bool input : {true, false}) {
        juce::Array<juce::var> buses;
        for (const auto& bus : input ? layout.inputBuses : layout.outputBuses) buses.add(channels(bus));
        result->setProperty(input ? "inputs" : "outputs", buses);
    }
    return juce::var(result);
}
inline bool decode(const juce::var& value, juce::AudioProcessor::BusesLayout& result)
{
    juce::AudioProcessor::BusesLayout next;
    for (bool input : {true, false}) {
        const auto* buses = value[input ? "inputs" : "outputs"].getArray();
        if (!buses || buses->size() > 64) return false;
        int total = 0;
        for (const auto& bus : *buses) {
            juce::AudioChannelSet set;
            if (!readChannels(bus, set) || (total += set.size()) > 256) return false;
            (input ? next.inputBuses : next.outputBuses).add(set);
        }
    }
    result = next; return true;
}
inline bool matches(const juce::AudioProcessor& processor, const juce::AudioProcessor::BusesLayout& layout)
{
    // No allocation: also used as the last guard before processing a block.
    for (bool input : {true, false}) {
        const auto& buses = input ? layout.inputBuses : layout.outputBuses;
        if (processor.getBusCount(input) != buses.size()) return false;
        for (int b = 0; b < buses.size(); ++b)
            if (processor.getBus(input, b)->getCurrentLayout() != buses.getReference(b)) return false;
    }
    return true;
}
inline juce::var inventory(juce::AudioProcessor& processor)
{
    auto* result = new juce::DynamicObject;
    const auto current = processor.getBusesLayout();
    result->setProperty("layout", encode(current));
    for (bool input : {true, false}) {
        juce::Array<juce::var> buses;
        for (int b = 0; b < processor.getBusCount(input); ++b) {
            const auto* bus = processor.getBus(input, b);
            auto* item = new juce::DynamicObject;
            item->setProperty("name", bus->getName()); item->setProperty("main", bus->isMain());
            item->setProperty("types", channels(bus->getCurrentLayout()));
            item->setProperty("lastTypes", channels(bus->getLastEnabledLayout()));
            juce::Array<juce::var> choices; juce::StringArray seen;
            for (const auto& set : {bus->getCurrentLayout(), bus->getDefaultLayout(), bus->getLastEnabledLayout(),
                juce::AudioChannelSet::disabled(), juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo(),
                juce::AudioChannelSet::quadraphonic(), juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create7point1()}) {
                const auto key = juce::JSON::toString(channels(set), true);
                if (seen.contains(key)) continue;
                seen.add(key); auto candidate = current;
                (input ? candidate.inputBuses : candidate.outputBuses).set(b, set);
                if (set != bus->getCurrentLayout() && !processor.checkBusesLayoutSupported(candidate)) {
                    candidate = bus->getBusesLayoutForLayoutChangeOfBus(set);
                    if (candidate.getChannelSet(input, b) != set || !processor.checkBusesLayoutSupported(candidate)) continue;
                }
                auto* option = new juce::DynamicObject;
                option->setProperty("name", set.isDisabled() ? "Disabled" : set.getDescription());
                option->setProperty("types", channels(set)); option->setProperty("layout", encode(candidate)); choices.add(juce::var(option));
            }
            item->setProperty("choices", choices); buses.add(juce::var(item));
        }
        result->setProperty(input ? "inputs" : "outputs", buses);
    }
    return juce::var(result);
}
inline void synchronize(RouteNode& target, juce::AudioProcessor& processor)
{
    auto node = target;
    for (bool input : {true, false}) {
        auto& ports = input ? node.inputPorts : node.outputPorts;
        auto& names = input ? node.inputNames : node.outputNames;
        auto& count = input ? node.inputs : node.outputs;
        const bool legacy = ports.empty();
        const auto legacyNames = names;
        if (legacy && !legacyNames.isEmpty())
            for (int c = 0; c < count; ++c) ports.push_back({"legacy:" + juce::String(c), -1, 0, -1});
        for (auto& port : ports) port.physical = -1;
        int physical = 0;
        for (int b = 0; b < processor.getBusCount(input); ++b) {
            const auto* bus = processor.getBus(input, b); const auto& set = bus->getCurrentLayout();
            for (int c = 0; c < set.size(); ++c, ++physical) {
                const int type = static_cast<int>(set.getTypeOfChannel(c));
                const auto key = juce::String(b) + ":" + juce::String::toHexString(bus->getName().hashCode64()) + ":" + juce::String(type);
                auto found = std::find_if(ports.begin(), ports.end(), [&](const auto& p) { return p.key == key; });
                if (found == ports.end() && legacy && !legacyNames.isEmpty()) {
                    const auto prefix = bus->getName() + " ";
                    const auto oldName = prefix + (set.size() == 2 ? (c == 0 ? juce::String("L") : juce::String("R")) : juce::String(c + 1));
                    int match = -1, matches = 0, oldBusChannels = 0;
                    for (int i = 0; i < legacyNames.size(); ++i) {
                        if (legacyNames[i].startsWith(prefix)) ++oldBusChannels;
                        if (legacyNames[i] == oldName) { match = i; ++matches; }
                    }
                    if (matches == 1 && oldBusChannels == set.size() && match < count && ports[static_cast<size_t>(match)].bus < 0) {
                        found = ports.begin() + match; *found = {key, b, type, physical};
                    }
                }
                if (found == ports.end()) {
                    if (ports.size() >= 256) throw std::runtime_error("Plugin channel history exceeds 256. Remove and add this instance again.");
                    ports.push_back({key, b, type, physical}); found = ports.end() - 1;
                }
                found->physical = physical;
                const auto index = static_cast<int>(found - ports.begin());
                while (names.size() <= index) names.add("");
                auto channel = juce::AudioChannelSet::getAbbreviatedChannelTypeName(set.getTypeOfChannel(c));
                if (channel.isEmpty()) channel = juce::String(c + 1);
                names.set(index, bus->getName() + " " + channel);
            }
        }
        // Adopt legacy flat channels once. Unmatched saved ports remain unavailable.
        if (legacy) while (static_cast<int>(ports.size()) < count) ports.push_back({"legacy:" + juce::String(ports.size()), -1, 0, -1});
        count = static_cast<int>(ports.size());
    }
    target = std::move(node);
}
}
