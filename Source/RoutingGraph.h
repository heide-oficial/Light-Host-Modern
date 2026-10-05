#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace lightHostModern
{
inline bool validVisualColor(const juce::String& value)
{ return value.isEmpty() || (value.length()==9 && value[0]=='#' && value.substring(1).containsOnly("0123456789abcdefABCDEF")); }

struct RoutePort
{
    juce::String key;
    int bus = -1, type = 0, physical = -1;
};
struct RouteNode
{
    juce::String id, kind = "plugin", name;
    double x = 100, y = 100, cardWidth = 360, cardHeight = 0;
    bool horizontalPorts = false;
    int zOrder = 0;
    int inputs = 2, outputs = 2;
    bool splitInputs = false, splitOutputs = false;
    juce::StringArray inputNames, outputNames;
    std::vector<RoutePort> inputPorts, outputPorts;
    juce::String customName, cardColor;
    juce::var inputColors = juce::var(new juce::DynamicObject), outputColors = juce::var(new juce::DynamicObject);
    juce::var inputAliases = juce::var(new juce::DynamicObject), outputAliases = juce::var(new juce::DynamicObject);
    std::vector<int> hiddenInputs, hiddenOutputs;
    std::vector<float> gains = std::vector<float>(4, 1.0f);
    std::vector<bool> muted = std::vector<bool>(4, false);
};
struct RouteEdge
{
    juce::String id, from, to;
    int output = 0, input = 0, sourceWidth = 2, targetWidth = 2;
};
struct RoutingGraph
{
    std::vector<RouteNode> nodes;
    std::vector<RouteEdge> edges;
    double zoom = 1, panX = 0, panY = 0;
    bool animate = true;
    static constexpr int maxNodes = 128, maxEdges = 512, maxChannels = 256;
    const RouteNode* find(const juce::String& id) const
    { for (const auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    RouteNode* find(const juce::String& id)
    { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    static bool hidden(const std::vector<int>& channels, int start, int width)
    { for (int i = start; i < start + width; ++i) if (std::find(channels.begin(), channels.end(), i) != channels.end()) return true; return false; }
    std::vector<int> order() const
    {
        std::vector<int> result, degree(nodes.size(), 0);
        for (size_t i = 0; i < nodes.size(); ++i)
            for (const auto& e : edges) if (e.to == nodes[i].id) ++degree[i];
        for (size_t i = 0; i < nodes.size(); ++i) if (degree[i] == 0) result.push_back(static_cast<int>(i));
        for (size_t p = 0; p < result.size(); ++p)
            for (const auto& e : edges) if (e.from == nodes[static_cast<size_t>(result[p])].id)
                for (size_t j = 0; j < nodes.size(); ++j) if (nodes[j].id == e.to && --degree[j] == 0) result.push_back(static_cast<int>(j));
        return result;
    }
    juce::String validate() const
    {
        if (nodes.size() > maxNodes || edges.size() > maxEdges) return "The canvas has reached its routing capacity.";
        if (!std::isfinite(zoom) || zoom < .001 || zoom > 3 || !std::isfinite(panX) || !std::isfinite(panY)
            || std::abs(panX) > 100000 || std::abs(panY) > 100000) return "Invalid canvas view.";
        std::set<juce::String> ids;
        int hardwareInputs = 0, hardwareOutputs = 0;
        for (const auto& n : nodes)
        {
            if (n.id.isEmpty() || n.id.length() > 64 || !ids.insert(n.id).second
                || (n.kind != "input" && n.kind != "output" && n.kind != "plugin" && n.kind != "mixer")
                || !std::isfinite(n.x) || !std::isfinite(n.y) || std::abs(n.x) > 50000 || std::abs(n.y) > 50000
                || n.zOrder < 0 || n.zOrder >= maxNodes
                || n.inputs < 0 || n.inputs > maxChannels || n.outputs < 0 || n.outputs > maxChannels
                || !std::isfinite(n.cardWidth) || n.cardWidth < 360 || n.cardWidth > 262144
                || !std::isfinite(n.cardHeight) || n.cardHeight < 0 || n.cardHeight > 2400
                || n.name.length() > 128 || n.customName.length() > 128) return "Invalid canvas card.";
            if (n.kind == "input") { ++hardwareInputs; if (n.id != "audio-in" || n.inputs != 0) return "Invalid audio input."; }
            if (n.kind == "output") { ++hardwareOutputs; if (n.id != "audio-out" || n.outputs != 0) return "Invalid audio output."; }
            if (n.kind == "mixer" && (n.inputs < 2 || n.outputs < 2 || n.inputs % 2 || n.outputs % 2 || n.gains.size() != static_cast<size_t>(n.inputs / 2) || n.muted.size() != n.gains.size())) return "Invalid mixer layout.";
            for (auto gain : n.gains) if (!std::isfinite(gain) || gain < 0 || gain > 2) return "Invalid mixer volume.";
            for (auto channel : n.hiddenInputs) if (channel < 0 || channel >= n.inputs) return "Invalid hidden input.";
            for (auto channel : n.hiddenOutputs) if (channel < 0 || channel >= n.outputs) return "Invalid hidden output.";
        }
        if (hardwareInputs != 1 || hardwareOutputs != 1) return "The canvas requires one audio input and one audio output card.";
        ids.clear(); std::set<std::pair<juce::String, int>> destinations;
        for (const auto& e : edges)
        {
            const auto* a = find(e.from); const auto* b = find(e.to);
            if (!a || !b || e.from == e.to || e.id.isEmpty() || e.id.length() > 64 || !ids.insert(e.id).second
                || e.output < 0 || e.input < 0 || (e.sourceWidth != 1 && e.sourceWidth != 2)
                || (e.targetWidth != 1 && e.targetWidth != 2) || e.output + e.sourceWidth > a->outputs
                || e.input + e.targetWidth > b->inputs) return "Incompatible audio connection.";
            if (hidden(a->hiddenOutputs, e.output, e.sourceWidth) || hidden(b->hiddenInputs, e.input, e.targetWidth))
                return "Disconnect a channel before hiding it.";
            for (int c = e.input; c < e.input + e.targetWidth; ++c)
                if (!destinations.insert({e.to, c}).second) return "This input is already connected. Use a Mixer to combine signals.";
        }
        if (order().size() != nodes.size()) return "Feedback connections are not allowed.";
        return {};
    }
    // Mixer topology edits preserve physical channel identity outside the removed pair.
    juce::String editMixerPair(const juce::String& id, bool output, int removePair = -1)
    {
        auto* n = find(id);
        if (!n || n->kind != "mixer") return "Only mixers support adding or deleting channels.";
        int& count = output ? n->outputs : n->inputs;
        if (removePair < 0) {
            if (count >= maxChannels) return "The mixer channel limit has been reached.";
            count += 2;
            if (!output) { n->gains.push_back(1.0f); n->muted.push_back(false); }
            return {};
        }
        if (count <= 2) return "Keep at least one stereo pair.";
        if (removePair >= count / 2) return "This channel pair no longer exists.";
        const int start = removePair * 2;
        edges.erase(std::remove_if(edges.begin(), edges.end(), [&](const auto& e) {
            const bool matches = (output ? e.from : e.to) == id;
            const int c = output ? e.output : e.input, width = output ? e.sourceWidth : e.targetWidth;
            return matches && c < start + 2 && c + width > start;
        }), edges.end());
        for (auto& e : edges) if ((output ? e.from : e.to) == id) {
            int& c = output ? e.output : e.input; if (c >= start + 2) c -= 2;
        }
        auto& hidden = output ? n->hiddenOutputs : n->hiddenInputs;
        hidden.erase(std::remove_if(hidden.begin(), hidden.end(), [&](int c){ return c >= start && c < start + 2; }), hidden.end());
        for (auto& c : hidden) if (c >= start + 2) c -= 2;
        const auto remap = [&](juce::var& values, bool aliases) {
            auto* shifted = new juce::DynamicObject;
            if (const auto* object = values.getDynamicObject()) for (const auto& item : object->getProperties()) {
                const auto key = item.name.toString(); const int c = key.getIntValue();
                if (c >= start && c < start + 2) continue;
                shifted->setProperty(juce::Identifier(juce::String(c >= start + 2 ? c - 2 : c) + (aliases ? ":" + key.fromFirstOccurrenceOf(":", false, false) : "")), item.value);
            }
            values = juce::var(shifted);
        };
        remap(output ? n->outputColors : n->inputColors, false);
        remap(output ? n->outputAliases : n->inputAliases, true);
        auto& names = output ? n->outputNames : n->inputNames;
        if (start < names.size()) names.removeRange(start, juce::jmin(2, names.size() - start));
        if (!output) { n->gains.erase(n->gains.begin() + removePair); n->muted.erase(n->muted.begin() + removePair); }
        count -= 2;
        return {};
    }
    static bool available(const RouteNode& node, bool output, int channel, int width = 1)
    {
        const auto& ports = output ? node.outputPorts : node.inputPorts;
        if (node.kind != "plugin" || ports.empty()) return true;
        for (int c = channel; c < channel + width; ++c)
            if (c < 0 || c >= static_cast<int>(ports.size()) || ports[static_cast<size_t>(c)].physical < 0) return false;
        return true;
    }
    juce::var json() const
    {
        using namespace juce;
        auto* root = new DynamicObject(); Array<var> ns, es;
        for (const auto& n : nodes)
        {
            auto* o = new DynamicObject(); o->setProperty("id", n.id); o->setProperty("kind", n.kind); o->setProperty("name", n.name);
            o->setProperty("cardColor", n.cardColor); o->setProperty("inputColors", n.inputColors); o->setProperty("outputColors", n.outputColors);
            o->setProperty("customName", n.customName); o->setProperty("inputAliases", n.inputAliases); o->setProperty("outputAliases", n.outputAliases);
            o->setProperty("zOrder", n.zOrder); o->setProperty("cardWidth", n.cardWidth); o->setProperty("cardHeight", n.cardHeight); o->setProperty("horizontalPorts", n.horizontalPorts);
            o->setProperty("x", n.x); o->setProperty("y", n.y); o->setProperty("inputs", n.inputs); o->setProperty("outputs", n.outputs);
            o->setProperty("splitInputs", n.splitInputs); o->setProperty("splitOutputs", n.splitOutputs);
            for (bool output : {false, true}) {
                Array<var> ports;
                for (const auto& p : output ? n.outputPorts : n.inputPorts) {
                    auto* port = new DynamicObject; port->setProperty("key", p.key); port->setProperty("bus", p.bus);
                    port->setProperty("type", p.type); port->setProperty("physical", p.physical); ports.add(var(port));
                }
                o->setProperty(output ? "outputPorts" : "inputPorts", ports);
            }
            Array<var> hi, ho, gs, ms, ins, outs;
            for (auto v : n.hiddenInputs) hi.add(v); for (auto v : n.hiddenOutputs) ho.add(v);
            for (auto v : n.gains) gs.add(v); for (bool v : n.muted) ms.add(v);
            for (const auto& v : n.inputNames) ins.add(v); for (const auto& v : n.outputNames) outs.add(v);
            o->setProperty("hiddenInputs", hi); o->setProperty("hiddenOutputs", ho); o->setProperty("gains", gs); o->setProperty("muted", ms);
            o->setProperty("inputNames", ins); o->setProperty("outputNames", outs); ns.add(var(o));
        }
        for (const auto& e : edges)
        {
            auto* o = new DynamicObject(); o->setProperty("id", e.id); o->setProperty("from", e.from); o->setProperty("to", e.to);
            o->setProperty("output", e.output); o->setProperty("input", e.input); o->setProperty("sourceWidth", e.sourceWidth);
            o->setProperty("targetWidth", e.targetWidth); es.add(var(o));
        }
        root->setProperty("nodes", ns); root->setProperty("edges", es); root->setProperty("zoom", zoom);
        root->setProperty("panX", panX); root->setProperty("panY", panY); root->setProperty("animate", animate);
        return var(root);
    }
    static bool parse(const juce::var& value, RoutingGraph& target, juce::String& error)
    {
        RoutingGraph g;
        const auto* ns = value["nodes"].getArray(); const auto* es = value["edges"].getArray();
        if (!ns || !es || ns->size() > maxNodes || es->size() > maxEdges) { error = "Invalid routing document."; return false; }
        g.zoom = value.getProperty("zoom", 1.0); g.panX = value.getProperty("panX", 0.0); g.panY = value.getProperty("panY", 0.0);
        g.animate = value.getProperty("animate", true);
        const auto channelNumber = [](const juce::var& v) {
            if (!v.isInt() && !v.isInt64() && !v.isDouble()) return false;
            const auto number = static_cast<double>(v);
            return std::isfinite(number) && number >= 0 && number <= maxChannels && std::floor(number) == number;
        };
        for (const auto& v : *ns)
        {
            if (!channelNumber(v["inputs"]) || !channelNumber(v["outputs"])) { error = "Invalid channel count."; return false; }
            for (const auto* key : {"hiddenInputs", "hiddenOutputs"})
                if (const auto* channels = v[key].getArray()) {
                    if (channels->size() > maxChannels) { error = "Invalid hidden channels."; return false; }
                    for (const auto& channel : *channels) if (!channelNumber(channel)) { error = "Invalid hidden channels."; return false; }
                }
            RouteNode n; n.id = v["id"].toString(); n.kind = v["kind"].toString(); n.name = v["name"].toString();
            n.customName = v["customName"].toString();
            n.cardColor=v.getProperty("cardColor", "").toString();
            if(!validVisualColor(n.cardColor)){error="Invalid card color.";return false;}
            for(const auto* key:{"inputColors","outputColors"}) {
                if(!v.hasProperty(key))continue;
                const auto* colors=v[key].getDynamicObject();
                if(!colors||colors->getProperties().size()>maxChannels){error="Invalid channel colors.";return false;}
                auto& target=juce::String(key)=="inputColors"?n.inputColors:n.outputColors;
                for(const auto& property:colors->getProperties()) {
                    const auto channel=property.name.toString();
                    if(channel.isEmpty()||!channel.containsOnly("0123456789")||channel.length()>3||channel.getIntValue()>=maxChannels
                        ||!property.value.isString()||!validVisualColor(property.value.toString())){error="Invalid channel color.";return false;}
                    target.getDynamicObject()->setProperty(property.name,property.value);
                }
            }
            const auto aliases = [&](const char* key, juce::var& out) {
                if (!v.hasProperty(key)) return true;
                const auto* object = v[key].getDynamicObject(); if (!object || object->getProperties().size() > maxChannels * 2) return false;
                out = juce::var(new juce::DynamicObject);
                for (const auto& property : object->getProperties()) {
                    const auto label = property.value.toString();
                    if (!property.value.isString() || label.length() > 128 || label.containsAnyOf("\r\n") || property.name.toString().length() > 10) return false;
                    out.getDynamicObject()->setProperty(property.name, label);
                }
                return true;
            };
            if (!aliases("inputAliases", n.inputAliases) || !aliases("outputAliases", n.outputAliases)) { error = "Invalid channel names."; return false; }
            const auto layer = v.getProperty("zOrder", 0);
            const auto layerNumber = static_cast<double>(layer);
            if ((!layer.isInt() && !layer.isInt64() && !layer.isDouble()) || !std::isfinite(layerNumber) || std::floor(layerNumber) != layerNumber || layerNumber < 0 || layerNumber >= maxNodes) { error = "Invalid card layer."; return false; }
            n.zOrder = static_cast<int>(layerNumber);
            n.cardWidth = v.getProperty("cardWidth", 360.0); n.cardHeight = v.getProperty("cardHeight", 0.0); n.horizontalPorts = v.getProperty("horizontalPorts", false);
            n.x = v["x"]; n.y = v["y"]; n.inputs = v["inputs"]; n.outputs = v["outputs"];
            n.splitInputs = v["splitInputs"]; n.splitOutputs = v["splitOutputs"];
            for (bool output : {false, true}) if (v.hasProperty(output ? "outputPorts" : "inputPorts")) {
                const auto* ports = v[output ? "outputPorts" : "inputPorts"].getArray();
                const int count = output ? n.outputs : n.inputs;
                if (!ports || (ports->size() != 0 && ports->size() != count)) { error = "Invalid plugin port inventory."; return false; }
                std::set<juce::String> keys;
                for (const auto& port : *ports) {
                    const auto key = port["key"].toString();
                    if (!port.isObject() || key.isEmpty() || key.length() > 32 || !keys.insert(key).second
                        || !port["bus"].isInt() || !port["type"].isInt() || !port["physical"].isInt()
                        || static_cast<int>(port["bus"]) < -1 || static_cast<int>(port["bus"]) >= 64
                        || static_cast<int>(port["type"]) < 0 || static_cast<int>(port["type"]) > 383
                        || static_cast<int>(port["physical"]) < -1 || static_cast<int>(port["physical"]) >= 256) { error = "Invalid plugin port."; return false; }
                    (output ? n.outputPorts : n.inputPorts).push_back({key, static_cast<int>(port["bus"]), static_cast<int>(port["type"]), static_cast<int>(port["physical"])});
                }
            }
            if (const auto* a = v["hiddenInputs"].getArray()) for (const auto& x : *a) n.hiddenInputs.push_back(static_cast<int>(x));
            if (const auto* a = v["hiddenOutputs"].getArray()) for (const auto& x : *a) n.hiddenOutputs.push_back(static_cast<int>(x));
            // Hardware visibility now follows Audio's enabled physical channels.
            // Old canvas-only masks must not hide an enabled port or reject a wire.
            if (n.kind == "input" || n.kind == "output") { n.hiddenInputs.clear(); n.hiddenOutputs.clear(); }
            if (const auto* a = v["inputNames"].getArray()) for (const auto& x : *a) n.inputNames.add(x.toString());
            if (const auto* a = v["outputNames"].getArray()) for (const auto& x : *a) n.outputNames.add(x.toString());
            if (const auto* a = v["gains"].getArray()) { n.gains.clear(); for (const auto& x : *a) n.gains.push_back(static_cast<float>(x)); }
            if (const auto* a = v["muted"].getArray()) { n.muted.clear(); for (const auto& x : *a) n.muted.push_back(static_cast<bool>(x)); }
            g.nodes.push_back(std::move(n));
        }
        for (const auto& v : *es)
        {
            for (const auto* key : {"output", "input", "sourceWidth", "targetWidth"})
                if (!channelNumber(v[key])) { error = "Invalid connection channel."; return false; }
            RouteEdge e; e.id = v["id"].toString(); e.from = v["from"].toString(); e.to = v["to"].toString();
            e.output = v["output"]; e.input = v["input"]; e.sourceWidth = v["sourceWidth"]; e.targetWidth = v["targetWidth"];
            g.edges.push_back(std::move(e));
        }
        error = g.validate(); if (error.isNotEmpty()) return false;
        target = std::move(g); return true;
    }
    static RoutingGraph empty(int inputs = 2, int outputs = 2)
    {
        RoutingGraph g;
        RouteNode in; in.id = "audio-in"; in.kind = "input"; in.name = "Audio input"; in.inputs = 0; in.outputs = inputs; in.x = 40; in.y = 120;
        RouteNode out; out.id = "audio-out"; out.kind = "output"; out.name = "Audio output"; out.inputs = outputs; out.outputs = 0; out.x = 780; out.y = 120;
        g.nodes = {in, out}; return g;
    }
};
}
