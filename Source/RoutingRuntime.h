#pragma once
#include "RealtimeHostProcessor.h"
#include <stdexcept>

// Everything here is allocated/compiled while processing is suspended. The audio
// callback walks a bounded DAG, with preallocated channel buffers and delay lines.
struct RoutingRuntime
{
    struct Node
    {
        lightHostModern::RouteNode layout;
        std::shared_ptr<PluginSlot> slot;
        AudioBuffer<float> storage, processorStorage, view;
        MidiBuffer midi;
        std::atomic<float> peak{0};
        std::array<std::atomic<float>, 256> channelPeaks{};
        std::array<std::atomic<float>, 128> targetGains{};
        std::array<float, 128> currentGains{};
        bool active = false, usesMidi = false;
        int arrival = 0, channels = 1, processingChannels = 1;
        std::vector<size_t> incoming;
        std::atomic<bool> midiNeedsRepair{false};
    };
    struct Edge
    {
        lightHostModern::RouteEdge layout;
        int from = 0, to = 0, delay = 0, cursor = 0;
        std::vector<float> ring;
    };
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Edge> edges;
    std::vector<int> order;
    lightHostModern::audioLimits::Reservation memory;
    int latency = 0;
    float gainStep = 0.001f;
    mutable std::atomic<int> telemetrySamples{0};
    std::vector<int> physicalInputs, physicalOutputs;

    static std::shared_ptr<RoutingRuntime> compile(ChainSnapshot& snapshot)
    {
        auto plan = std::make_shared<RoutingRuntime>();
        plan->gainStep = static_cast<float>(1.0 / jmax(1.0, snapshot.sampleRate * 0.005));
        plan->physicalInputs = snapshot.physicalInputs; plan->physicalOutputs = snapshot.physicalOutputs;
        auto& graph = snapshot.graph;
        const auto error = graph.validate();
        if (error.isNotEmpty()) throw std::invalid_argument(error.toStdString());
        plan->order = graph.order();
        size_t storageSamples = 0;
        for (const auto& layout : graph.nodes)
        {
            auto n = std::make_unique<Node>(); n->layout = layout;
            if (layout.kind == "mixer") for (size_t lane = 0; lane < layout.gains.size(); ++lane) {
                n->currentGains[lane] = layout.muted[lane] ? 0.0f : layout.gains[lane];
                n->targetGains[lane].store(n->currentGains[lane]);
            }
            for (const auto& slot : snapshot.slots) if (slot->instanceId == layout.id) n->slot = slot;
            n->channels = jmax(1, jmax(layout.inputs, layout.outputs));
            n->processingChannels = n->slot ? jmax(1, jmax(n->slot->inputChannels, n->slot->outputChannels)) : n->channels;
            n->channels = jmax(n->channels, n->processingChannels);
            n->active = layout.kind == "output";
            plan->nodes.push_back(std::move(n));
        }
        for (const auto& layout : graph.edges)
        {
            const auto* source = graph.find(layout.from); const auto* target = graph.find(layout.to);
            if (!lightHostModern::RoutingGraph::available(*source, true, layout.output, layout.sourceWidth)
                || !lightHostModern::RoutingGraph::available(*target, false, layout.input, layout.targetWidth)) continue;
            Edge e; e.layout = layout;
            for (size_t i = 0; i < graph.nodes.size(); ++i)
            { if (graph.nodes[i].id == layout.from) e.from = static_cast<int>(i); if (graph.nodes[i].id == layout.to) e.to = static_cast<int>(i); }
            plan->nodes[static_cast<size_t>(e.to)]->incoming.push_back(plan->edges.size());
            plan->edges.push_back(std::move(e));
        }
        // Only nodes leading to a physical output participate in processing.
        for (auto it = plan->order.rbegin(); it != plan->order.rend(); ++it)
            if (plan->nodes[static_cast<size_t>(*it)]->active)
                for (const auto& e : plan->edges) if (e.to == *it) plan->nodes[static_cast<size_t>(e.from)]->active = true;
        for (const int index : plan->order)
        {
            auto& node = *plan->nodes[static_cast<size_t>(index)];
            if (!node.active) continue;
            storageSamples += static_cast<size_t>(node.channels) * snapshot.blockSize;
            if (storageSamples > 16 * 1024 * 1024) throw std::length_error("Routing buffers exceed 64 MiB.");
            lightHostModern::audioLimits::format(node.channels, snapshot.blockSize, snapshot.sampleRate);
            plan->memory.grow(static_cast<size_t>(node.channels) * snapshot.blockSize * sizeof(float));
            node.storage.setSize(node.channels, snapshot.blockSize);
            if (node.slot) {
                const auto bytes = static_cast<size_t>(node.processingChannels) * snapshot.blockSize * sizeof(float);
                plan->memory.grow(bytes); storageSamples += bytes / sizeof(float);
                if (storageSamples > 16 * 1024 * 1024) throw std::length_error("Routing buffers exceed 64 MiB.");
                node.processorStorage.setSize(node.processingChannels, snapshot.blockSize);
                node.view.setDataToReferTo(node.processorStorage.getArrayOfWritePointers(), node.processingChannels, snapshot.blockSize);
            } else node.view.setDataToReferTo(node.storage.getArrayOfWritePointers(), node.processingChannels, snapshot.blockSize);
            node.usesMidi = node.slot && (node.slot->processor->acceptsMidi() || node.slot->processor->producesMidi());
            if (node.usesMidi) {
                // JUCE reserves 1.5x the requested byte count.
                plan->memory.grow(lightHostModern::midiCapacityBytes * 3 / 2 + 16);
                node.midi.ensureSize(lightHostModern::midiCapacityBytes);
            }

            int arrival = 0;
            for (const auto& e : plan->edges) if (e.to == index) arrival = jmax(arrival, plan->nodes[static_cast<size_t>(e.from)]->arrival);
            for (auto& e : plan->edges) if (e.to == index)
            {
                e.delay = arrival - plan->nodes[static_cast<size_t>(e.from)]->arrival;
                if (e.delay > static_cast<int>(snapshot.sampleRate * 10)) throw std::length_error("Routing delay exceeds ten seconds.");
                storageSamples += static_cast<size_t>(e.delay) * e.layout.targetWidth;
                if (storageSamples > 16 * 1024 * 1024) throw std::length_error("Routing buffers exceed 64 MiB.");
                plan->memory.grow(static_cast<size_t>(e.delay) * e.layout.targetWidth * sizeof(float));
                e.ring.resize(static_cast<size_t>(e.delay) * e.layout.targetWidth, 0.0f);
            }
            const auto total = static_cast<int64>(arrival) + (node.slot ? node.slot->getLatencySamples() : 0);
            if (total > snapshot.sampleRate * 10) throw std::length_error("Routing delay exceeds ten seconds.");
            node.arrival = static_cast<int>(total);
            if (node.layout.kind == "output") plan->latency = node.arrival;
        }
        plan->order.erase(std::remove_if(plan->order.begin(), plan->order.end(), [&](int index) { return !plan->nodes[static_cast<size_t>(index)]->active; }), plan->order.end());
        return plan;
    }

    void process(AudioBuffer<float>& host, int hostInputs, int hostOutputs, std::atomic<uint64>& failures) noexcept
    {
        const int samples = host.getNumSamples();
        const bool measure = telemetrySamples.load(std::memory_order_relaxed) > 0;
        if (measure) telemetrySamples.fetch_sub(samples, std::memory_order_relaxed);
        for (const int index : order)
        {
            auto& n = *nodes[static_cast<size_t>(index)];
            if (!n.active) continue;
            n.storage.clear(0, samples);
            n.view.setDataToReferTo((n.slot ? n.processorStorage : n.storage).getArrayOfWritePointers(), n.processingChannels, samples);
            if (n.layout.kind == "input")
                for (int c = 0; c < jmin(hostInputs, host.getNumChannels()); ++c)
                {
                    const int physical = c < static_cast<int>(physicalInputs.size()) ? physicalInputs[static_cast<size_t>(c)] : c;
                    if (physical < n.layout.outputs) n.storage.copyFrom(physical, 0, host, c, 0, samples);
                }
        }
        host.clear();
        for (const int index : order)
        {
            auto& n = *nodes[static_cast<size_t>(index)];
            if (!n.active) continue;
            for (auto edgeIndex : n.incoming)
            {
                auto& e = edges[edgeIndex];
                const auto& src = *nodes[static_cast<size_t>(e.from)];
                for (int s = 0; s < samples; ++s)
                {
                    for (int ch = 0; ch < e.layout.targetWidth; ++ch)
                    {
                        float value = src.storage.getSample(e.layout.output + (e.layout.sourceWidth == 1 ? 0 : ch), s);
                        if (e.layout.sourceWidth == 2 && e.layout.targetWidth == 1)
                            value = .5f * (src.storage.getSample(e.layout.output, s) + src.storage.getSample(e.layout.output + 1, s));
                        if (e.delay > 0)
                        {
                            auto& delayed = e.ring[static_cast<size_t>(ch * e.delay + e.cursor)];
                            std::swap(value, delayed);
                        }
                        n.storage.addSample(e.layout.input + ch, s, value);
                    }
                    if (e.delay > 0 && ++e.cursor == e.delay) e.cursor = 0;
                }
            }
            if (n.layout.kind == "mixer")
            {
                std::array<float, 128> targets;
                for (size_t lane = 0; lane < n.layout.gains.size(); ++lane) targets[lane] = n.targetGains[lane].load(std::memory_order_relaxed);
                for (int s = 0; s < samples; ++s)
                {
                    float left = 0, right = 0;
                    for (size_t lane = 0; lane < n.layout.gains.size(); ++lane)
                    {
                        auto& gain = n.currentGains[lane];
                        gain += jlimit(-gainStep, gainStep, targets[lane] - gain);
                        left += n.storage.getSample(static_cast<int>(lane) * 2, s) * gain;
                        right += n.storage.getSample(static_cast<int>(lane) * 2 + 1, s) * gain;
                    }
                    for (int output = 0; output < n.layout.outputs; output += 2) { n.storage.setSample(output, s, left); n.storage.setSample(output + 1, s, right); }
                }
            }
            else if (n.layout.kind == "plugin")
            {
                if (!n.slot) { n.storage.clear(); continue; }
                const ScopedTryLock callbackLock(n.slot->processor->getCallbackLock());
                if (!callbackLock.isLocked()) { n.storage.clear(); continue; }
                if (!n.slot->layoutMatches()) n.slot->layoutPending.store(true);
                if (!n.slot || !n.slot->prepared || n.slot->processDisabled.load() || n.slot->layoutPending.load()) n.storage.clear();
                else
                {
                    n.view.clear();
                    for (int c = 0; c < n.layout.inputs; ++c) {
                        const int physical = n.layout.inputPorts.empty() ? c : n.layout.inputPorts[static_cast<size_t>(c)].physical;
                        if (physical >= 0 && physical < n.slot->inputChannels) n.view.copyFrom(physical, 0, n.storage, c, 0, samples);
                    }
                    n.slot->captureDry(n.view); n.midi.clear();
                    try {
                        n.slot->processor->processBlock(n.view, n.midi);
                        if (lightHostModern::audioLimits::sanitize(n.view)) {
                            n.slot->processDisabled.store(true); n.slot->processFailed.store(true);
                            failures.fetch_add(1, std::memory_order_relaxed); n.storage.clear();
                        }
                        if (n.usesMidi
                            && n.midi.data.getAllocatedCapacity() < lightHostModern::midiCapacityBytes)
                            n.midiNeedsRepair.store(true, std::memory_order_relaxed);
                    }
                    catch (...) { n.slot->processDisabled.store(true); n.slot->processFailed.store(true); failures.fetch_add(1); n.storage.clear(); }
                    n.slot->mixDry(n.view, n.slot->bypassed.load(std::memory_order_relaxed));
                    n.storage.clear();
                    if (!n.slot->layoutMatches()) n.slot->layoutPending.store(true);
                    if (!n.slot->layoutPending.load()) for (int c = 0; c < n.layout.outputs; ++c) {
                        const int physical = n.layout.outputPorts.empty() ? c : n.layout.outputPorts[static_cast<size_t>(c)].physical;
                        if (physical >= 0 && physical < n.slot->outputChannels) n.storage.copyFrom(c, 0, n.view, physical, 0, samples);
                    }
                }
            }
            if (lightHostModern::audioLimits::sanitize(n.view)) failures.fetch_add(1, std::memory_order_relaxed);
            if (n.layout.kind == "output")
                for (int c = 0; c < jmin(hostOutputs, host.getNumChannels()); ++c)
                {
                    const int physical = c < static_cast<int>(physicalOutputs.size()) ? physicalOutputs[static_cast<size_t>(c)] : c;
                    if (physical < n.layout.inputs) host.copyFrom(c, 0, n.storage, physical, 0, samples);
                }
            if (!measure) continue;
            float peak = 0;
            for (int c = 0; c < (n.layout.kind == "output" ? n.layout.inputs : n.layout.outputs); ++c)
            {
                const auto value = n.storage.getMagnitude(c, 0, samples);
                n.channelPeaks[static_cast<size_t>(c)].store(std::isfinite(value) ? value : 0.0f, std::memory_order_relaxed);
                peak = jmax(peak, value);
            }
            n.peak.store(std::isfinite(peak) ? peak : 0.0f, std::memory_order_relaxed);
        }
    }
};
