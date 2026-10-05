#pragma once
#include "PluginBusLayout.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_cryptography/juce_cryptography.h>
#include "PluginInstanceId.h"
#include "BoundedInput.h"
#include "RoutingGraph.h"
#include <algorithm>
#include <set>
#include <vector>

namespace lightHostModern
{
inline bool normalizeInstanceName(const juce::String& input, juce::String& normalized)
{
    // JUCE counts Unicode code points, including supplementary characters.
    for (const auto character : input)
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f)
            || character == 0x2028 || character == 0x2029) return false;
    normalized = input.trim();
    return normalized.length() <= 128;
}

// A class identity never contains instance position, display name, version or a
// modified vendor UID. Length prefixes make even unusual identifiers unambiguous.
inline juce::String knownPluginId(const juce::PluginDescription& description)
{
    const auto field = [](const juce::String& text) { return juce::String(text.length()) + ":" + text; };
    const auto identity = field(description.pluginFormatName) + field(description.fileOrIdentifier)
        + field(juce::String::toHexString(description.uniqueId != 0 ? description.uniqueId : description.deprecatedUid));
    return juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
}

struct PluginInstanceRecord
{
    PluginInstanceId id;
    juce::String originalIdentity;
    juce::PluginDescription description;
    juce::String legacyDescription; // exact imported description, including a possibly synthetic UID
    juce::String customName, cardColor;
    juce::String lastValidState;
    juce::String recoveryState; // undecodable/failed state must remain recoverable
    juce::String loading = "unloaded";
    juce::String error;
    bool bypassed = false;
    juce::var busLayout;
    bool isolated = false; // Existing profiles retain direct execution.
    bool identityResolved = true;
    bool stateCaptureAllowed = true;

    juce::String stateContentDigest(bool recovery = false) const
    {
        const auto& value = recovery ? recoveryState : lastValidState;
        auto& previous = recovery ? hashedRecovery : hashedState;
        auto& digest = recovery ? recoveryDigest : stateDigest;
        // Shared String storage makes the unchanged fast path constant time.
        if (digest.isEmpty() || previous.toRawUTF8() != value.toRawUTF8()) {
            digest = juce::SHA256(value.toRawUTF8(), value.getNumBytesAsUTF8()).toHexString();
            previous = value;
        }
        return digest;
    }
    mutable juce::String hashedState, hashedRecovery, stateDigest, recoveryDigest;

    juce::String displayName() const { return customName.isEmpty() ? description.name : customName; }
};

class PluginInstances
{
public:
    std::vector<PluginInstanceRecord> records;
    juce::String recoveryError;
    bool writable = true;
    juce::String mode = "list", profileId;
    RoutingGraph graph;

    // A flat presentation of the same dependency order used by RoutingRuntime.
    // List mode retains its explicit instance order. Include unavailable or
    // temporarily unrepresented instances so tray actions never lose an entry.
    std::vector<size_t> processingOrder() const
    {
        std::vector<size_t> result;
        std::set<size_t> included;
        if (mode == "chain") for (const auto index : graph.order()) {
            const auto& node = graph.nodes[static_cast<size_t>(index)];
            if (node.kind != "plugin") continue;
            for (size_t i = 0; i < records.size(); ++i)
                if (records[i].id == node.id && included.insert(i).second) { result.push_back(i); break; }
        }
        for (size_t i = 0; i < records.size(); ++i) if (included.insert(i).second) result.push_back(i);
        return result;
    }

    static juce::String legacyBaseKey(const juce::String& type, const juce::PluginDescription& plugin)
    {
        return "plugin-" + type.toLowerCase() + "-" + juce::String::toHexString(plugin.createIdentifierString().hashCode64());
    }
    static juce::String legacyKey(const juce::String& type, const juce::PluginDescription& plugin)
    {
        return legacyBaseKey(type, plugin) + "-" + juce::String::toHexString(plugin.deprecatedUid);
    }

    // Called before any description normalization. Never deduplicate the XML.
    void migrate(const juce::XmlElement& legacy, juce::PropertySet& settings,
                 const juce::Array<juce::PluginDescription>& known, const juce::String& migrationId = {})
    {
        records.clear();
        std::vector<juce::PluginDescription> descriptions;
        for (const auto* child : legacy.getChildIterator())
        {
            juce::PluginDescription description;
            if (description.loadFromXml(*child)) descriptions.push_back(std::move(description));
            else { recoveryError = "Unrecognized legacy session entry"; writable = false; }
        }
        juce::String importStateError;
        const auto read = [&](const juce::String& type, const juce::PluginDescription& description) {
            const auto exact = legacyKey(type, description);
            if (settings.containsKey(exact)) return settings.getValue(exact);
            const auto base = legacyBaseKey(type, description);
            if (settings.containsKey(base))
            {
                const auto count = std::count_if(descriptions.begin(), descriptions.end(), [&](const auto& other) {
                    return legacyBaseKey(type, other) == base;
                });
                if (type != "state" || count == 1) return settings.getValue(base);
                recoveryError = "Ambiguous legacy state retained in the original settings";
                importStateError = recoveryError;
                return juce::String();
            }
            const auto oldKey = "plugin-" + type.toLowerCase() + "-" + description.name + description.version + description.pluginFormatName;
            const auto count = std::count_if(descriptions.begin(), descriptions.end(), [&](const auto& other) {
                return other.name + other.version + other.pluginFormatName == description.name + description.version + description.pluginFormatName;
            });
            if (settings.containsKey(oldKey) && type == "state" && count > 1)
            {
                recoveryError = "Ambiguous legacy state retained in the original settings";
                importStateError = recoveryError;
                return juce::String();
            }
            return settings.getValue(oldKey);
        };
        std::stable_sort(descriptions.begin(), descriptions.end(), [&](const auto& a, const auto& b) {
            const auto orderA = read("order", a).getIntValue(), orderB = read("order", b).getIntValue();
            if (orderA == orderB) return a.name.compareNatural(b.name) < 0;
            return orderA > 0 && (orderB <= 0 || orderA < orderB);
        });
        juce::StringArray assigned;
        for (const auto& legacyDescription : descriptions)
        {
            PluginInstanceRecord record;
            const auto idKey = legacyKey("instance-id", legacyDescription);
            auto id = settings.getValue(idKey);
            if (migrationId.isNotEmpty() && (id.length() != 32 || !id.containsOnly("0123456789abcdef")
                || juce::Uuid(id).isNull() || assigned.contains(id)))
            {
                const auto seed = migrationId + ":" + juce::String(static_cast<int>(records.size())) + ":" + legacyDescription.createXml()->toString();
                id = juce::SHA256(seed.toRawUTF8(), seed.getNumBytesAsUTF8()).toHexString().substring(0, 32);
                settings.setValue(idKey, id);
            }
            record.id = ensurePluginInstanceId(settings, idKey, assigned);
            record.bypassed = read("bypass", legacyDescription).getIntValue() != 0;
            importStateError.clear();
            record.lastValidState = read("state", legacyDescription);
            record.error = read("failed", legacyDescription);
            if (importStateError.isNotEmpty()) { record.error = importStateError; record.stateCaptureAllowed = false; }
            record.legacyDescription = legacyDescription.createXml()->toString();
            record.description = legacyDescription;
            const juce::PluginDescription* original = nullptr;
            int matches = 0;
            for (const auto& candidate : known)
                if (candidate.pluginFormatName == legacyDescription.pluginFormatName
                    && candidate.fileOrIdentifier == legacyDescription.fileOrIdentifier
                    && (legacyDescription.uniqueId != 0 ? candidate.uniqueId == legacyDescription.uniqueId
                        : candidate.uniqueId == 0 && candidate.deprecatedUid == legacyDescription.deprecatedUid))
                { original = &candidate; ++matches; }
            record.identityResolved = matches == 1;
            if (record.identityResolved) record.description = *original;
            record.originalIdentity = knownPluginId(record.description);
            validateState(record);
            if (record.error.isNotEmpty()) record.loading = "failed";
            records.push_back(std::move(record));
        }
    }

    std::unique_ptr<juce::XmlElement> serialize(juce::uint64 revision = 0) const
    {
        auto root = std::make_unique<juce::XmlElement>("LIGHTHOSTSESSION");
        // Older hosts must refuse isolated sessions instead of silently loading
        // the same native plugin back into their own process after rollback.
        root->setAttribute("version", std::any_of(records.begin(), records.end(), [](const auto& r) { return r.busLayout.isObject(); }) ? 3 : std::any_of(records.begin(), records.end(), [](const auto& r) { return r.isolated; }) ? 2 : 1);
        root->setAttribute("revision", juce::String(revision));
        root->setAttribute("recoveryError", recoveryError);
        root->setAttribute("mode", mode);
        root->setAttribute("profileId", profileId);
        if (mode == "chain") root->createNewChildElement("ROUTING")->addTextElement(juce::JSON::toString(graph.json(), true));
        for (const auto& record : records)
        {
            auto* item = root->createNewChildElement("INSTANCE");
            item->setAttribute("id", record.id);
            item->setAttribute("identity", record.originalIdentity);
            item->setAttribute("identityResolved", record.identityResolved);
            item->setAttribute("bypassed", record.bypassed);
            item->setAttribute("isolated", record.isolated);
            if (record.busLayout.isObject()) item->createNewChildElement("BUSES")->addTextElement(juce::JSON::toString(record.busLayout, true));
            item->setAttribute("customName", record.customName);
            item->setAttribute("cardColor", record.cardColor);
            item->setAttribute("error", record.error);
            item->setAttribute("loading", record.loading);
            item->setAttribute("stateCaptureAllowed", record.stateCaptureAllowed);
            item->addChildElement(record.description.createXml().release());
            item->createNewChildElement("STATE")->addTextElement(record.lastValidState);
            item->createNewChildElement("RECOVERYSTATE")->addTextElement(record.recoveryState);
            item->createNewChildElement("LEGACY")->addTextElement(record.legacyDescription);
        }
        return root;
    }

    bool deserialize(const juce::XmlElement& root)
    {
        // Transactional: malformed data cannot turn a previously loaded session into an empty one.
        if (!root.hasTagName("LIGHTHOSTSESSION") || (root.getIntAttribute("version") < 1 || root.getIntAttribute("version") > 3)) return false;
        std::vector<PluginInstanceRecord> loaded;
        std::set<juce::String> ids;
        size_t decodedTotal = 0;
        const auto nextMode = root.getStringAttribute("mode", "list");
        RoutingGraph nextGraph;
        if (nextMode != "list" && nextMode != "chain") return false;
        if (nextMode == "chain")
        {
            const auto* routing = root.getChildByName("ROUTING"); juce::String error;
            if (!routing || !RoutingGraph::parse(parseBoundedJson(routing->getAllSubText()), nextGraph, error)) return false;
        }
        for (const auto* item : root.getChildIterator())
        {
            if (item->hasTagName("ROUTING")) continue;
            PluginInstanceRecord record;
            record.id = item->getStringAttribute("id");
            const auto* description = item->getChildByName("PLUGIN");
            if (!item->hasTagName("INSTANCE") || record.id.length() != 32
                || !record.id.containsOnly("0123456789abcdef") || juce::Uuid(record.id).isNull()
                || !ids.insert(record.id).second || !description || !record.description.loadFromXml(*description)) return false;
            record.originalIdentity = item->getStringAttribute("identity");
            record.identityResolved = item->getBoolAttribute("identityResolved", false);
            if (record.originalIdentity != knownPluginId(record.description)) return false;
            record.bypassed = item->getBoolAttribute("bypassed");
            record.isolated = item->getBoolAttribute("isolated", false);
            if (const auto* buses = item->getChildByName("BUSES")) {
                record.busLayout = parseBoundedJson(buses->getAllSubText()); juce::AudioProcessor::BusesLayout parsed;
                if (!pluginBuses::decode(record.busLayout, parsed)) return false;
            }
            record.customName = item->getStringAttribute("customName");
            record.cardColor = item->getStringAttribute("cardColor");
            if (!validVisualColor(record.cardColor)) record.cardColor.clear();
            record.error = item->getStringAttribute("error");
            record.stateCaptureAllowed = item->getBoolAttribute("stateCaptureAllowed", true);
            if (auto* state = item->getChildByName("STATE")) record.lastValidState = state->getAllSubText();
            if (auto* state = item->getChildByName("RECOVERYSTATE")) record.recoveryState = state->getAllSubText();
            if (auto* legacy = item->getChildByName("LEGACY")) record.legacyDescription = legacy->getAllSubText();
            validateState(record);
            size_t stateBytes = 0;
            if (!validPluginState(record.lastValidState, stateBytes) || stateBytes > maximumSessionStateBytes - decodedTotal) return false;
            decodedTotal += stateBytes;
            if (record.error.isNotEmpty()) record.loading = item->getStringAttribute("loading") == "missing" ? "missing" : "failed";
            loaded.push_back(std::move(record));
        }
        if (nextMode == "chain")
        {
            for (const auto& n : nextGraph.nodes) if (n.kind == "plugin" && ids.count(n.id) == 0) return false;
            for (const auto& r : loaded) { const auto* n = nextGraph.find(r.id); if (!n || n->kind != "plugin") return false; }
        }
        records = std::move(loaded);
        mode = nextMode; graph = std::move(nextGraph); profileId = root.getStringAttribute("profileId");
        recoveryError = root.getStringAttribute("recoveryError");
        return true;
    }

    int indexOf(const PluginInstanceId& id) const
    {
        for (size_t i = 0; i < records.size(); ++i) if (records[i].id == id) return static_cast<int>(i);
        return -1;
    }

private:
    static void validateState(PluginInstanceRecord& record)
    {
        size_t bytes = 0;
        if (!validPluginState(record.lastValidState, bytes))
        {
            record.recoveryState = record.lastValidState;
            record.lastValidState.clear();
            record.error = "Invalid saved plugin state preserved for recovery";
            record.stateCaptureAllowed = false;
        }
    }
};
}
