#include "AudioChannelAliases.h"
#include "BoundedInput.h"
#include "AudioEngine.h"
#include "PluginWindow.h"
#include <ctime>

namespace
{
String profileDisplayTime(const String& stored)
{
    // JUCE persists an explicit local offset. Send UTC to the Windows formatter
    // so it applies the current time zone exactly once.
    const auto seconds = static_cast<std::time_t>(Time::fromISO8601(stored).toMilliseconds() / 1000);
    std::tm utc{};
    if (gmtime_s(&utc, &seconds) != 0) return stored;
    return String::formatted("%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900,
        utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
}

String contentDigest(const lightHostModern::PluginInstances& source)
{
    auto session = source;
    for (size_t i = 0; i < session.records.size(); ++i) {
        session.records[i].lastValidState = source.records[i].stateContentDigest();
        session.records[i].recoveryState = source.records[i].stateContentDigest(true);
    }
    session.recoveryError.clear();
    session.graph.zoom = 1; session.graph.panX = session.graph.panY = 0;
    for (auto& record : session.records) { record.loading.clear(); record.error.clear(); }
    // Device/plugin labels are discovered metadata, not user edits.
    for (auto& node : session.graph.nodes) {
        node.name.clear(); node.inputNames.clear(); node.outputNames.clear();
        for (auto& p : node.inputPorts) p.physical = -1; for (auto& p : node.outputPorts) p.physical = -1;
        if (node.kind == "input" || node.kind == "output") node.inputs = node.outputs = 0;
    }
    const auto xml = session.serialize()->toString();
    return SHA256(xml.toRawUTF8(), xml.getNumBytesAsUTF8()).toHexString();
}
}

void AudioEngine::initializeOperatingProfiles()
{
    auto* settings = getAppProperties().getUserSettings();
    operatingProfiles = std::make_unique<lightHostModern::OperatingProfiles>(settings->getFile());
    if (sessionLoadSuppressed || !instances.writable) { operatingMode = instances.mode; profilesReady = true; return; }
    if (!settings->getBoolValue("operatingProfilesInitialized", false)
        && !instances.records.empty() && operatingProfiles->profiles.empty() && operatingProfiles->writable && instances.writable && !sessionLoadSuppressed)
    {
        lightHostModern::OperatingProfile initial;
        initial.id = Uuid().toString(); initial.name = "My plugins";
        initial.created = initial.modified = Time::getCurrentTime().toISO8601(true);
        initial.session = instances; initial.session.profileId = initial.id;
        initial.muted = isGlobalMuted(); initial.bypassed = isGlobalBypassed();
        if (operatingProfiles->commit({initial})) instances.profileId = initial.id;
    }
    operatingProfiles->ensureDefaults();
    if (!operatingProfiles->profiles.empty()) settings->setValue("operatingProfilesInitialized", true);
    operatingMode = settings->getValue("operatingMode", instances.mode);
    if (operatingMode != "chain") operatingMode = "list";
    const auto desired = settings->getValue("pendingOperatingMode", operatingMode);
    const bool switchMode = desired == "list" || desired == "chain";
    const auto targetMode = switchMode ? desired : operatingMode;
    const auto selected = settings->getValue("pendingOperatingProfile",
        settings->getValue("lastOperatingProfile_" + targetMode, lightHostModern::OperatingProfiles::defaultId(targetMode)));
    if (!sessionLoadSuppressed && (targetMode != instances.mode || settings->containsKey("pendingOperatingProfile")))
    {
        if (const auto* profile = operatingProfiles->find(selected); profile && profile->session.mode == targetMode)
        {
            instances = profile->session;
            hostProcessor.setGlobalMuted(profile->muted); hostProcessor.setGlobalBypassed(profile->bypassed);
        }
        else { instances = {}; instances.mode = targetMode; if (targetMode == "chain") instances.graph = lightHostModern::RoutingGraph::empty(); }
    }
    if (!operatingProfiles->find(instances.profileId)) instances.profileId = lightHostModern::OperatingProfiles::defaultId(instances.mode);
    operatingMode = instances.mode; // The running mode is immutable until restart.
    if (const auto* current = operatingProfiles->find(instances.profileId))
    { hostProcessor.setGlobalMuted(current->muted); hostProcessor.setGlobalBypassed(current->bypassed); }
    settings->setValue("operatingMode", operatingMode);
    settings->removeValue("pendingOperatingMode"); settings->removeValue("pendingOperatingProfile");
    settings->setValue("lastOperatingProfile_" + operatingMode, instances.profileId);
    profilesReady = true;
}

void AudioEngine::synchronizeGraph(ChainSnapshot& snapshot)
{
    snapshot.graphMode = isChainMode();
    if (!snapshot.graphMode) return;
    auto& graph = instances.graph;
    if (graph.nodes.empty()) graph = lightHostModern::RoutingGraph::empty(snapshot.inputChannels, snapshot.outputChannels);
    graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) {
        return n.kind == "plugin" && instances.indexOf(n.id) < 0;
    }), graph.nodes.end());
    graph.edges.erase(std::remove_if(graph.edges.begin(), graph.edges.end(), [&](const auto& e) {
        return !graph.find(e.from) || !graph.find(e.to);
    }), graph.edges.end());
    for (const auto& record : instances.records)
    {
        auto* node = graph.find(record.id);
        if (!node)
        {
            lightHostModern::RouteNode fresh; fresh.id = record.id; fresh.name = record.displayName();
            fresh.inputs = jlimit(0, 256, record.description.numInputChannels);
            fresh.outputs = jlimit(0, 256, record.description.numOutputChannels);
            fresh.x = 340 + static_cast<double>(graph.nodes.size() % 3) * 300;
            fresh.y = 80 + static_cast<double>(graph.nodes.size() / 3) * 230;
            graph.nodes.push_back(fresh); node = &graph.nodes.back();
        }
        node->name = record.displayName();
        for (auto& port : node->inputPorts) port.physical = -1;
        for (auto& port : node->outputPorts) port.physical = -1;
        for (const auto& slot : snapshot.slots) if (slot->instanceId == record.id)
        {
            try { lightHostModern::pluginBuses::synchronize(*node, *slot->processor); }
            catch (const std::exception& error) {
                slot->processDisabled.store(true); const auto i = instances.indexOf(record.id);
                if (i >= 0) { instances.records[static_cast<size_t>(i)].error = error.what(); instances.records[static_cast<size_t>(i)].loading = "failed"; }
            }
        }
    }
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto* in = graph.find("audio-in"); auto* out = graph.find("audio-out");
        const auto inputs = device->getInputChannelNames(), outputs = device->getOutputChannelNames();
        in->outputs = jmax(in->outputs, jmin(256, inputs.size())); out->inputs = jmax(out->inputs, jmin(256, outputs.size()));
        in->outputNames = inputs; out->inputNames = outputs;
        for (int c = 0; c < inputs.size(); ++c) if (device->getActiveInputChannels()[c]) snapshot.physicalInputs.push_back(c);
        for (int c = 0; c < outputs.size(); ++c) if (device->getActiveOutputChannels()[c]) snapshot.physicalOutputs.push_back(c);
    }
    snapshot.graph = graph;
}

String AudioEngine::currentProfileDigest() const { return contentDigest(instances); }

bool AudioEngine::profileHasChanges() const
{
    if (!profilesReady || !operatingProfiles) return false;
    const auto* saved = operatingProfiles->find(instances.profileId);
    if (!saved) return !instances.records.empty() || (isChainMode() && (instances.graph.nodes.size() > 2 || !instances.graph.edges.empty()));
    if (contentDigest(saved->session) != currentProfileDigest() || saved->muted != isGlobalMuted() || saved->bypassed != isGlobalBypassed()) return true;
    if (saved->includeAudio && (saved->monoInput != isMonoInputs() || saved->monoOutput != isMonoOutput()
        || saved->audioXml != JSON::toString(deviceController.selectionState()["configured"], true))) return true;
    return stateCaptureDue > 0 || !pendingStateCaptures.empty();
}

lightHostModern::OperatingProfile AudioEngine::captureProfile(const String& name, bool includeAudio, const String& description)
{
    savePluginStates();
    lightHostModern::OperatingProfile p; p.id = Uuid().toString(); p.name = name; p.description = description;
    p.created = p.modified = Time::getCurrentTime().toISO8601(true); p.includeAudio = includeAudio;
    p.session = instances; p.session.profileId = p.id;
    p.muted = isGlobalMuted(); p.bypassed = isGlobalBypassed();
    if (includeAudio)
    {
        p.audioXml = JSON::toString(deviceController.selectionState()["configured"], true);
        p.monoInput = isMonoInputs(); p.monoOutput = isMonoOutput();
    }
    return p;
}

void AudioEngine::applyProfile(const lightHostModern::OperatingProfile& profile)
{
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    if (const auto previous = hostProcessor.getActiveSnapshot())
        for (const auto& slot : previous->slots) if (slot) PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
    hostProcessor.publishSnapshot(nullptr);
    isolatedPlugins.clear(); isolatedCaptures.clear();
    ++operatingGeneration; pendingStateCaptures.clear(); stateCaptureDue = 0;
    instances = profile.session;
    if (profile.includeAudio)
    {
        auto audio = lightHostModern::parseBoundedJson(profile.audioXml);
        if (auto* object = audio.getDynamicObject()) object->setProperty("expectedGeneration", String(deviceController.getGeneration()));
        AudioDeviceSelection selection;
        if (lightHostModern::audioSelection::parse(audio, selection)) deviceController.restoreProfileConfiguration(selection);
        setMonoInputs(profile.monoInput); setMonoOutput(profile.monoOutput);
    }
    hostProcessor.setGlobalMuted(profile.muted); hostProcessor.setGlobalBypassed(profile.bypassed);
    routingUndo.clear(); routingRedo.clear();
    loadActivePlugins(); saveActivePluginList();
    getAppProperties().getUserSettings()->setValue("lastOperatingProfile_" + operatingMode, instances.profileId);
    markSettingsDirty();
}

var AudioEngine::getOperatingState() const
{
    auto* root = new DynamicObject();
    root->setProperty("generation", (operatingEpoch + ":" + String(operatingGeneration)));
    root->setProperty("mode", operatingMode); root->setProperty("activeProfile", instances.profileId);
    const auto* settings = getAppProperties().getUserSettings();
    root->setProperty("pendingMode", settings->getValue("pendingOperatingMode"));
    root->setProperty("pendingProfile", settings->getValue("pendingOperatingProfile"));
    root->setProperty("dirty", profileHasChanges()); root->setProperty("canUndo", !routingUndo.empty()); root->setProperty("canRedo", !routingRedo.empty());
    root->setProperty("writable", isSessionWritable());
    root->setProperty("catalogWritable", operatingProfiles && operatingProfiles->writable);
    auto error = operatingProfiles && operatingProfiles->error.isNotEmpty() ? operatingProfiles->error : profileError;
    if (error.isEmpty()) if (const auto snapshot = hostProcessor.getActiveSnapshot(); snapshot && snapshot->graphMode) error = snapshot->routingError;
    root->setProperty("error", error);
    Array<var> profiles;
    if (operatingProfiles) for (const auto& p : operatingProfiles->profiles)
    {
        auto* item = new DynamicObject(); item->setProperty("id", p.id); item->setProperty("name", p.name); item->setProperty("description", p.description);
        item->setProperty("mode", p.session.mode); item->setProperty("created", profileDisplayTime(p.created)); item->setProperty("modified", profileDisplayTime(p.modified));
        item->setProperty("hasUpdates", p.created != p.modified); item->setProperty("isDefault", lightHostModern::OperatingProfiles::isDefault(p.id)); item->setProperty("includeAudio", p.includeAudio); profiles.add(var(item));
    }
    root->setProperty("profiles", profiles);
    if (isChainMode()) root->setProperty("graph", instances.graph.json());
    return var(root);
}

var AudioEngine::getPluginBuses(const String& id)
{
    auto* slot = findActiveSlotFor(id);
    if (!slot) { auto* result = new DynamicObject; result->setProperty("error", "Load the plugin before configuring its channels."); return var(result); }
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    const auto remote = isolatedPlugins.find(id);
    var inventory;
    if (remote != isolatedPlugins.end()) inventory = remote->second->busInventory();
    else {
        // JUCE VST3 can only ask the plugin about supported arrangements while
        // inactive. Merely suspending the host callback still reports formats
        // that the underlying plugin may reject.
        const bool prepared = slot->prepared;
        const auto rate = slot->preparedSampleRate; const auto block = slot->preparedBlockSize;
        const int channels = isChainMode() ? jmax(1, jmax(slot->inputChannels, slot->outputChannels))
            : jlimit(1, RealtimeHostProcessor::maxScratchChannels, jmax(hostProcessor.getTotalNumInputChannels(), hostProcessor.getTotalNumOutputChannels()));
        slot->release();
        try {
            inventory = lightHostModern::pluginBuses::inventory(*slot->processor);
            if (prepared) slot->prepare(rate, block, channels);
            hostProcessor.refreshLatencies();
        } catch (...) {
            try { if (prepared && !slot->prepared) slot->prepare(rate, block, channels); }
            catch (...) { slot->processDisabled.store(true); slot->processFailed.store(true); }
            hostProcessor.refreshLatencies(true);
            throw;
        }
    }
    inventory.getDynamicObject()->setProperty("id", id);
    inventory.getDynamicObject()->setProperty("profileId", instances.profileId);
    inventory.getDynamicObject()->setProperty("generation", operatingEpoch + ":" + String(operatingGeneration));
    return inventory;
}

void AudioEngine::refreshPluginLayouts()
{
    const auto snapshot = hostProcessor.getActiveSnapshot(); if (!snapshot) return;
    if (std::none_of(snapshot->slots.begin(), snapshot->slots.end(), [](const auto& slot) { if (!slot) return false; const ScopedTryLock lock(slot->processor->getCallbackLock()); return lock.isLocked() && (!slot->layoutMatches() || slot->layoutPending.load()); })) return;
    RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
    bool changed = false;
    for (const auto& slot : snapshot->slots) {
        if (!slot) continue;
        const ScopedTryLock callbackLock(slot->processor->getCallbackLock());
        if (!callbackLock.isLocked() || (slot->layoutMatches() && !slot->layoutPending.load())) continue;
        try {
            slot->release(); slot->refreshLayout();
            if (jmax(slot->inputChannels, slot->outputChannels) > 256) throw std::runtime_error("Plugin layout exceeds 256 channels");
            const int index = instances.indexOf(slot->instanceId);
            if (index >= 0) instances.records[static_cast<size_t>(index)].busLayout = lightHostModern::pluginBuses::encode(slot->busLayout);
            slot->processDisabled.store(false); slot->processFailed.store(false); changed = true;
        } catch (const std::exception& error) {
            slot->processDisabled.store(true); slot->layoutPending.store(false);
            const int index = instances.indexOf(slot->instanceId);
            if (index >= 0) { auto& r = instances.records[static_cast<size_t>(index)]; r.error = error.what(); r.loading = "failed"; }
            ++chainVersion;
        }
    }
    if (changed) {
        Logger::writeToLog("LightHostModern: plugin channel layout changed; rebuilding routes with stable channel identities");
        routingUndo.clear(); routingRedo.clear(); ++operatingGeneration;
        loadActivePlugins(); saveActivePluginList();
    }
}

String AudioEngine::handleOperatingCommand(const var& request)
{
    if (!isSessionWritable() || !operatingProfiles) return "The session is currently read-only.";
    const auto action = request["action"].toString(), id = request["id"].toString();
    if (action == "recover-profiles") { if (!operatingProfiles->recover()) return operatingProfiles->error; ++chainVersion; return {}; }
    const bool catalogueChange = action == "create" || action == "save" || action == "edit" || action == "overwrite" || action == "delete" || action == "duplicate" || action == "delete-all-profiles";
    if (catalogueChange && !operatingProfiles->writable) return "Confirm profile recovery before changing the saved catalogue.";
    const bool routingEdit = action == "graph" || action == "undo" || action == "redo" || action == "add" || action == "remove" || action == "batch" || action == "mixer-channels" || action == "mixer-gain" || action == "isolation" || action == "plugin-buses";
    if (routingEdit && (request["profileId"].toString() != instances.profileId || request["generation"].toString() != (operatingEpoch + ":" + String(operatingGeneration))))
        return "This edit belongs to an earlier profile session. Refresh before editing.";
    if (action == "mixer-gain") {
        if (!isChainMode() || !request["values"].isArray()) return "Invalid mixer controls.";
        if (request["revision"].toString() != String(chainVersion)) return "The chain changed. Refresh before editing it again.";
        auto preview = instances.graph;
        if (request["values"].getArray()->size() > 1024) return "Too many mixer controls.";
        for (const auto& value : *request["values"].getArray()) {
            auto* node = preview.find(value["id"].toString());
            if (!node || node->kind != "mixer" || !value["lane"].isInt()
                || !(value["gain"].isDouble() || value["gain"].isInt())) return "Invalid mixer control.";
            const int lane = static_cast<int>(value["lane"]); const double gain = static_cast<double>(value["gain"]);
            if (lane < 0 || lane >= static_cast<int>(node->gains.size()) || !std::isfinite(gain) || gain < 0 || gain > 2) return "Invalid mixer gain.";
            node->gains[static_cast<size_t>(lane)] = static_cast<float>(gain);
        }
        // Preview changes only the prepared controls. The final graph commit
        // creates one undo entry and persists the gesture as a single edit.
        hostProcessor.updateRoutingControls(preview);
        return {};
    }
    if ((action == "save" || action == "edit" || action == "overwrite" || action == "delete") && lightHostModern::OperatingProfiles::isDefault(id))
        return "The default profile is protected. Save your setup as a new profile.";
    if (action == "overwrite") {
        const auto* original = operatingProfiles->find(id);
        if (!original || original->session.mode != operatingMode) return "Choose a profile in the current mode.";
        auto replacement = captureProfile(original->name, original->includeAudio, original->description);
        if (!stateCaptureFailures.isEmpty()) return "Some plugin settings could not be captured. The previous profile was preserved.";
        replacement.id = id; replacement.created = original->created; replacement.session.profileId = id;
        auto next = operatingProfiles->profiles; for (auto& p : next) if (p.id == id) p = replacement;
        if (!operatingProfiles->commit(std::move(next))) return operatingProfiles->error;
        // Overwriting a saved slot does not activate a different profile.
        markSettingsDirty(); ++chainVersion; return {};
    }
    if (action == "delete-all-profiles") {
        std::vector<lightHostModern::OperatingProfile> next;
        for (const auto* mode : {"list", "chain"}) next.push_back(lightHostModern::OperatingProfiles::makeDefault(mode));
        if (!operatingProfiles->commit(std::move(next))) return operatingProfiles->error;
        auto* settings = getAppProperties().getUserSettings();
        for (const auto* mode : {"list", "chain"}) settings->setValue(String("lastOperatingProfile_") + mode, lightHostModern::OperatingProfiles::defaultId(mode));
        settings->removeValue("pendingOperatingProfile");
        const auto fallback = *operatingProfiles->find(lightHostModern::OperatingProfiles::defaultId(operatingMode)); applyProfile(fallback);
        ++chainVersion; return {};
    }
    if (action == "save" || action == "create" || action == "edit" || action == "duplicate")
    {
        String name;
        if (!lightHostModern::normalizeInstanceName(request["name"].toString(), name) || name.isEmpty()) return "Enter a name between 1 and 128 characters.";
        const auto description = request["description"].toString(); if (description.length() > 1024) return "The description is too long.";
        const auto* original = operatingProfiles->find(id);
        if (action != "create" && !original) return "This profile no longer exists.";
        const bool audio = static_cast<bool>(request["includeAudio"]);
        auto next = operatingProfiles->profiles;
        if (next.size() >= 256 && (action == "create" || action == "duplicate")) return "The profile limit has been reached (256).";
        lightHostModern::OperatingProfile p;
        if (action == "save" || action == "create")
        {
            if (action == "save" && id != instances.profileId) return "Activate the profile before saving its setup.";
            p = captureProfile(name, audio, description);
            if (!stateCaptureFailures.isEmpty()) return "Some plugin settings could not be captured. The previous profile was preserved.";
            if (action == "save") { p.id = original->id; p.created = original->created; p.session.profileId = p.id; }
        }
        else
        {
            p = *original; p.name = name; p.description = description; p.includeAudio = audio;
            if (audio && !original->includeAudio)
            {
                p.audioXml = JSON::toString(deviceController.selectionState()["configured"], true);
                p.monoInput = isMonoInputs(); p.monoOutput = isMonoOutput();
            }
            p.modified = Time::getCurrentTime().toISO8601(true);
            if (action == "duplicate") { p.id = Uuid().toString(); p.created = p.modified; p.session.profileId = p.id; }
        }
        for (const auto& existing : next) if (existing.id != p.id && existing.session.mode == p.session.mode && existing.name.equalsIgnoreCase(p.name))
            return "A profile with this name already exists in this mode.";
        if (action == "save" || action == "edit") for (auto& existing : next) { if (existing.id == p.id) { existing = p; break; } }
        else next.push_back(p);
        if (!operatingProfiles->commit(std::move(next))) return operatingProfiles->error;
        if (action == "save" || action == "create")
        {
            instances.profileId = p.id; saveActivePluginList();
            getAppProperties().getUserSettings()->setValue("lastOperatingProfile_" + operatingMode, p.id);
        }
    }
    else if (action == "delete")
    {
        if (!operatingProfiles->find(id)) return "This profile no longer exists.";
        auto next = operatingProfiles->profiles;
        next.erase(std::remove_if(next.begin(), next.end(), [&](const auto& p) { return p.id == id; }), next.end());
        if (!operatingProfiles->commit(std::move(next))) return operatingProfiles->error;
        if (instances.profileId == id) { const auto fallback = *operatingProfiles->find(lightHostModern::OperatingProfiles::defaultId(operatingMode)); applyProfile(fallback); }
        auto* settings = getAppProperties().getUserSettings();
        for (const auto* key : {"lastOperatingProfile_list", "lastOperatingProfile_chain", "pendingOperatingProfile"})
            if (settings->getValue(key) == id) settings->removeValue(key);
    }
    else if (action == "activate" || action == "mode")
    {
        if (profileHasChanges() && !static_cast<bool>(request["discard"])
            && !(action == "mode" && request["mode"].toString() == operatingMode)) return "Save or discard the current profile changes before switching.";
        const auto* profile = operatingProfiles->find(id);
        const String mode = action == "activate" && profile ? profile->session.mode : request["mode"].toString();
        if (action == "activate" && !profile) return "This profile no longer exists.";
        if (mode != "list" && mode != "chain") return "Invalid operating mode.";
        auto* settings = getAppProperties().getUserSettings();
        if (mode != operatingMode)
        {
            settings->setValue("pendingOperatingMode", mode);
            settings->setValue("pendingOperatingProfile", profile ? profile->id : settings->getValue("lastOperatingProfile_" + mode, lightHostModern::OperatingProfiles::defaultId(mode)));
        }
        else
        {
            settings->removeValue("pendingOperatingMode"); settings->removeValue("pendingOperatingProfile");
            if (profile) { const auto copy = *profile; applyProfile(copy); }
        }
        if (!settings->saveIfNeeded()) return "Could not save the pending operating mode.";
    }
    else if (action == "card-color")
    {
        const auto color=request["color"].toString();
        if(!lightHostModern::validVisualColor(color))return "Invalid card color.";
        const auto index=instances.indexOf(id);
        if(index<0)return "This plugin instance no longer exists.";
        instances.records[static_cast<size_t>(index)].cardColor=color;
        saveActivePluginList();
    }
    else if (action == "plugin-buses")
    {
        const int index = instances.indexOf(id); auto* slot = findActiveSlotFor(id);
        if (index < 0 || !slot) return "Load the plugin before configuring its channels.";
        AudioProcessor::BusesLayout layout;
        if (!lightHostModern::pluginBuses::decode(request["layout"], layout)) return "Invalid plugin channel configuration.";
        RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
        const auto remote = isolatedPlugins.find(id);
        const auto current = remote != isolatedPlugins.end() ? remote->second->busInventory()["layout"] : lightHostModern::pluginBuses::encode(slot->processor->getBusesLayout());
        if (JSON::toString(request["previousLayout"], true) != JSON::toString(current, true)) return "The plugin channels changed. Reopen the channel settings.";
        if (remote != isolatedPlugins.end()) {
            if (!remote->second->configureBuses(request["layout"], request["previousLayout"])) return "A plugin channel change is already in progress.";
            return {};
        }
        auto& processor = *slot->processor; const auto previousLayout = processor.getBusesLayout();
        slot->release();
        String failure;
        try {
            if (!processor.checkBusesLayoutSupported(layout) || !processor.setBusesLayout(layout)) failure = "This plugin rejected the channel configuration.";
            slot->refreshLayout();
            slot->prepare(hostProcessor.getCurrentSampleRateForPlugins(), hostProcessor.getCurrentBlockSizeForPlugins(),
                isChainMode() ? jmax(1, jmax(slot->inputChannels, slot->outputChannels)) : jlimit(1, RealtimeHostProcessor::maxScratchChannels, jmax(hostProcessor.getTotalNumInputChannels(), hostProcessor.getTotalNumOutputChannels())));
        } catch (const std::exception& error) { failure = error.what(); }
        catch (...) { failure = "The plugin failed while configuring its channels."; }
        if (failure.isNotEmpty()) {
            slot->release();
            if (!processor.setBusesLayout(previousLayout)) { slot->processDisabled.store(true); return "The plugin could not restore its previous channels. Reload the plugin."; }
            slot->refreshLayout();
        }
        instances.records[static_cast<size_t>(index)].busLayout = lightHostModern::pluginBuses::encode(processor.getBusesLayout());
        routingUndo.clear(); routingRedo.clear(); ++operatingGeneration;
        loadActivePlugins(); saveActivePluginList();
        if (failure.isNotEmpty()) return failure;
    }
    else if (action == "isolation")
    {
        const auto index = instances.indexOf(id); if (index < 0) return "This plugin instance no longer exists.";
        auto& record = instances.records[static_cast<size_t>(index)];
        const bool enabled = static_cast<bool>(request["enabled"]);
        if (record.isolated == enabled) return {};
        const auto worker = isolatedPlugins.find(id);
        const bool failedWorker = record.isolated && worker != isolatedPlugins.end()
            && (worker->second->failure().isNotEmpty() || worker->second->captureFailure().isNotEmpty());
        if (!failedWorker) {
            savePluginStates();
            if (stateCaptureFailures.contains(id)) return "Save the current plugin settings before changing its execution mode.";
        }
        record.isolated = enabled; record.error.clear(); record.loading = "unloaded";
        isolatedPlugins.erase(id); isolatedCaptures.erase(id);
        loadActivePlugins(); saveActivePluginList();
    }
    else if (action == "retry")
    {
        const auto index = instances.indexOf(id);
        if (index < 0) return "This plugin instance no longer exists.";
        auto& record = instances.records[static_cast<size_t>(index)];
        if (record.error.isEmpty() && (record.loading == "loaded" || record.loading == "loading")) return {};
        isolatedPlugins.erase(id); isolatedCaptures.erase(id);
        record.error.clear(); record.loading = "unloaded";
        loadActivePlugins(); saveActivePluginList();
    }
    else if (action == "graph" || action == "undo" || action == "redo" || action == "add" || action == "remove" || action == "batch" || action == "mixer-channels")
    {
        if (!isChainMode()) return "Restart in Chain mode before editing connections.";
        if (request["revision"].toString() != String(chainVersion)) return "The chain changed. Refresh before editing it again.";
        if (action != "graph") savePluginStates();
        auto previous = instances;
        const auto routingOnly = [](lightHostModern::RoutingGraph graph) {
            graph.zoom = 1; graph.panX = graph.panY = 0; graph.animate = false;
            for (auto& n : graph.nodes) { n.zOrder = 0; n.x = n.y = n.cardHeight = 0; n.cardWidth = 360; n.horizontalPorts = false; n.splitInputs = n.splitOutputs = false; n.hiddenInputs.clear(); n.hiddenOutputs.clear(); n.customName.clear(); n.cardColor.clear(); n.inputColors=var(new DynamicObject); n.outputColors=var(new DynamicObject); n.inputAliases = var(new DynamicObject); n.outputAliases = var(new DynamicObject); n.gains.assign(n.gains.size(), 1.0f); n.muted.assign(n.muted.size(), false); }
            return JSON::toString(graph.json(), true);
        };
        if (action == "undo" || action == "redo")
        {
            auto& source = action == "undo" ? routingUndo : routingRedo;
            auto& target = action == "undo" ? routingRedo : routingUndo;
            if (source.empty()) return "There is no action to undo or redo.";
            auto restored = source.back(); source.pop_back(); target.push_back(previous);
            restored.profileId = instances.profileId;
            bool sameInstances = instances.records.size() == restored.records.size();
            if (sameInstances) for (const auto& record : instances.records) if (restored.indexOf(record.id) < 0) sameInstances = false;
            if (sameInstances) for (auto& record : restored.records) {
                const bool bypassed = record.bypassed;
                record = instances.records[static_cast<size_t>(instances.indexOf(record.id))];
                record.bypassed = bypassed; // Preserve live parameters while undoing explicit bypass edits.
            }
            const bool sameBypass = sameInstances && std::all_of(instances.records.begin(), instances.records.end(), [&](const auto& record) {
                return record.bypassed == restored.records[static_cast<size_t>(restored.indexOf(record.id))].bypassed;
            });
            if (sameBypass && routingOnly(instances.graph) == routingOnly(restored.graph)) {
                // Undoing a layer/layout edit must not interrupt the audio path.
                instances = std::move(restored);
                hostProcessor.updateRoutingControls(instances.graph);
            } else {
                RealtimeHostProcessor::ScopedSuspension suspension(hostProcessor);
                if (!sameInstances) {
                    if (const auto old = hostProcessor.getActiveSnapshot()) for (const auto& slot : old->slots) if (slot) PluginWindow::closeCurrentlyOpenWindowsFor(*slot->processor);
                    hostProcessor.publishSnapshot(nullptr);
                }
                instances = std::move(restored); loadActivePlugins();
            }
        }
        else
        {
            if (action == "graph")
            {
                lightHostModern::RoutingGraph next; String error;
                if (!lightHostModern::RoutingGraph::parse(request["graph"], next, error)) return error;
                for (auto& node : next.nodes) if (node.kind == "plugin") if (const auto* old = instances.graph.find(node.id)) {
                    node.inputPorts = old->inputPorts; node.outputPorts = old->outputPorts;
                    node.inputNames = old->inputNames; node.outputNames = old->outputNames;
                }
                for (const auto& edge : next.edges) {
                    const bool existed = std::any_of(instances.graph.edges.begin(), instances.graph.edges.end(), [&](const auto& old) {
                        return old.id == edge.id && old.from == edge.from && old.to == edge.to && old.output == edge.output && old.input == edge.input
                            && old.sourceWidth == edge.sourceWidth && old.targetWidth == edge.targetWidth;
                    });
                    if (!existed && (!lightHostModern::RoutingGraph::available(*next.find(edge.from), true, edge.output, edge.sourceWidth)
                        || !lightHostModern::RoutingGraph::available(*next.find(edge.to), false, edge.input, edge.targetWidth))) return "This plugin channel is unavailable.";
                }
                for (const auto& node : next.nodes) if (node.kind == "plugin" && instances.indexOf(node.id) < 0) return "Unknown plugin card.";
                for (const auto& n : next.nodes) if (const auto* existing = instances.graph.find(n.id))
                    if (existing->kind != n.kind || existing->inputs != n.inputs || existing->outputs != n.outputs)
                        return "Channel layouts are owned by the audio device or plugin.";
                for (const auto& record : instances.records) if (!next.find(record.id)) return "Remove the plugin before deleting its card.";
                // View/port-visibility edits must not suspend or rebuild audio.
                if (routingOnly(next) == routingOnly(instances.graph))
                {
                    instances.graph = std::move(next); routingUndo.push_back(std::move(previous));
                    hostProcessor.updateRoutingControls(instances.graph);
                    if (routingUndo.size() > 20) routingUndo.erase(routingUndo.begin()); routingRedo.clear();
                    saveActivePluginList(); ++chainVersion; return {};
                }
                instances.graph = std::move(next);
            }
            if (action == "mixer-channels")
            {
                if (!request["output"].isBool() || !request["removePair"].isInt() || static_cast<int>(request["removePair"]) < -1) return "Invalid mixer channel action.";
                const auto error = instances.graph.editMixerPair(id, static_cast<bool>(request["output"]), static_cast<int>(request["removePair"]));
                if (error.isNotEmpty()) return error;
            }
            if (action == "add")
            {
                if (instances.graph.nodes.size() >= lightHostModern::RoutingGraph::maxNodes) return "The canvas is full.";
                if (request["kind"].toString() == "mixer")
                {
                    lightHostModern::RouteNode node; node.id = Uuid().toString(); node.kind = "mixer"; node.name = "Mixer";
                    node.inputs = 8; node.outputs = 2; node.x = static_cast<double>(request["x"]); node.y = static_cast<double>(request["y"]);
                    instances.graph.nodes.push_back(node);
                }
                else
                {
                    const auto known = getKnownPluginsSorted(); const int index = findKnownPluginIndexById(request["knownId"].toString());
                    if (index < 0) return "This plugin is no longer installed.";
                    auto record = lightHostModern::newKnownPluginInstance(*getAppProperties().getUserSettings(), known[static_cast<size_t>(index)]);
                    lightHostModern::RouteNode node; node.id = record.id; node.name = record.displayName();
                    node.inputs = jlimit(0, 256, record.description.numInputChannels); node.outputs = jlimit(0, 256, record.description.numOutputChannels);
                    node.x = static_cast<double>(request["x"]); node.y = static_cast<double>(request["y"]);
                    instances.graph.nodes.push_back(node); instances.records.push_back(std::move(record));
                }
            }
            if (action == "batch")
            {
                const auto* ids=request["ids"].getArray();const auto operation=request["operation"].toString();
                if(!ids||ids->size()>lightHostModern::RoutingGraph::maxNodes)return "Invalid selection.";
                if(operation!="remove"&&operation!="bypass"&&operation!="enable")return "Invalid selection action.";
                std::set<String> selected;
                for(const auto& value:*ids){const auto id=value.toString();if(!instances.graph.find(id))return "Unknown canvas card.";selected.insert(id);}
                if(operation=="remove"){
                    for(auto it=selected.begin();it!=selected.end();){const auto* n=instances.graph.find(*it);if(n->kind=="input"||n->kind=="output")it=selected.erase(it);else ++it;}
                    instances.records.erase(std::remove_if(instances.records.begin(),instances.records.end(),[&](const auto& n){return selected.count(n.id)!=0;}),instances.records.end());
                    instances.graph.nodes.erase(std::remove_if(instances.graph.nodes.begin(),instances.graph.nodes.end(),[&](const auto& n){return selected.count(n.id)!=0;}),instances.graph.nodes.end());
                    instances.graph.edges.erase(std::remove_if(instances.graph.edges.begin(),instances.graph.edges.end(),[&](const auto& e){return selected.count(e.from)||selected.count(e.to);}),instances.graph.edges.end());
                }else{
                    for(auto& record:instances.records)if(selected.count(record.id))record.bypassed=operation=="bypass";
                }
            }
            if (action == "remove")
            {
                const auto* node = instances.graph.find(id); if (!node || node->kind == "input" || node->kind == "output") return "This card cannot be removed.";
                instances.records.erase(std::remove_if(instances.records.begin(), instances.records.end(), [&](const auto& r) { return r.id == id; }), instances.records.end());
                instances.graph.nodes.erase(std::remove_if(instances.graph.nodes.begin(), instances.graph.nodes.end(), [&](const auto& n) { return n.id == id; }), instances.graph.nodes.end());
                instances.graph.edges.erase(std::remove_if(instances.graph.edges.begin(), instances.graph.edges.end(), [&](const auto& e) { return e.from == id || e.to == id; }), instances.graph.edges.end());
            }
            try { loadActivePlugins(); }
            catch (const std::exception& e) { instances = previous; loadActivePlugins(); return String(e.what()); }
            if (const auto snapshot = hostProcessor.getActiveSnapshot(); snapshot && snapshot->routingError.isNotEmpty()) {
                const auto error = snapshot->routingError;
                instances = previous; loadActivePlugins(); return error;
            }
            routingUndo.push_back(std::move(previous)); if (routingUndo.size() > 20) routingUndo.erase(routingUndo.begin()); routingRedo.clear();
        }
        saveActivePluginList();
    }
    else return "Unknown profile or routing action.";
    ++chainVersion; markSettingsDirty(); return {};
}


namespace {
String channelAliasDeviceKey(const var& selection, bool input)
{
    const auto setup = selection["editable"];
    const auto identity = setup["backend"].toString() + "\n" + setup[input ? "input" : "output"].toString() + (input ? "\nin" : "\nout");
    return SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
}
}

var AudioEngine::getAudioChannelAliases() const
{
    const auto stored = lightHostModern::parseBoundedJson(getAppProperties().getUserSettings()->getValue("audioChannelAliases", "{}"));
    const auto selection = getAudioSelectionState();
    auto* result = new DynamicObject;
    for (bool input : {true, false}) {
        auto names = stored.getProperty(Identifier(channelAliasDeviceKey(selection, input)), var());
        result->setProperty(input ? "input" : "output", names.isObject() ? names : var(new DynamicObject));
    }
    return var(result);
}

String AudioEngine::renameAudioChannel(const var& request)
{
    uint64 generation = 0; String name;
    const auto selection = getAudioSelectionState();
    const auto direction = request["direction"].toString();
    if (!request.isObject() || (direction != "input" && direction != "output")
        || !lightHostModern::audioSelection::generation(request["expectedGeneration"], generation)
        || generation != static_cast<uint64>(selection["generation"].toString().getLargeIntValue()))
        return "The audio device changed. Open Rename again.";
    if (!request["channel"].isInt() || !request["width"].isInt()
        || !request["name"].isString() || !lightHostModern::normalizeInstanceName(request["name"].toString(), name))
        return "Use a single-line name with at most 128 Unicode characters.";
    const bool input = direction == "input";
    const int channel = request["channel"], width = request["width"];
    const auto config = getAudioDeviceConfiguration();
    const auto count = input ? config.inputChannelNames.size() : config.outputChannelNames.size();
    if (channel < 0 || (width != 1 && width != 2) || channel + width > static_cast<int>(count)
        || (width == 2 && channel % 2 != 0)) return "This audio channel is no longer available.";
    auto* settings = getAppProperties().getUserSettings();
    auto stored = lightHostModern::parseBoundedJson(settings->getValue("audioChannelAliases", "{}"));
    if (!stored.isObject()) stored = var(new DynamicObject);
    const auto deviceKey = channelAliasDeviceKey(selection, input);
    auto names = stored.getProperty(Identifier(deviceKey), var()); if (!names.isObject()) names = var(new DynamicObject);
    lightHostModern::editAudioChannelAlias(*names.getDynamicObject(), channel, width, name);
    stored.getDynamicObject()->setProperty(deviceKey, names);
    settings->setValue("audioChannelAliases", JSON::toString(stored, true));
    markSettingsDirty(); ++chainVersion;
    return {};
}

var AudioEngine::getAudioDeviceAliases() const
{
    auto value = lightHostModern::parseBoundedJson(getAppProperties().getUserSettings()->getValue("audioDeviceAliases", "{}"));
    return value.isObject() ? value : var(new DynamicObject);
}

String AudioEngine::renameAudioDevices(const var& request)
{
    const auto* edits = request.getDynamicObject(); if (!edits || edits->getProperties().size() > 1024) return "Invalid device names.";
    auto aliases = getAudioDeviceAliases(); const auto choices = getAvailableAudioChoicesConfiguration();
    for (const auto& edit : edits->getProperties()) {
        bool found = false; for (const auto& choice : choices.deviceChoices)
            if (edit.name.toString() == choice.backendName + "|" + choice.role + "|" + choice.deviceName) { found = true; break; }
        String name; if (!found || !edit.value.isString() || !lightHostModern::normalizeInstanceName(edit.value.toString(), name)) return "Invalid device name.";
        if (name.isEmpty()) aliases.getDynamicObject()->removeProperty(edit.name); else aliases.getDynamicObject()->setProperty(edit.name, name);
    }
    getAppProperties().getUserSettings()->setValue("audioDeviceAliases", JSON::toString(aliases, true)); markSettingsDirty(); ++chainVersion; return {};
}

String AudioEngine::restoreAllOriginalNames()
{
    if (!isSessionWritable() || !operatingProfiles || !operatingProfiles->writable) return "The session or profiles are currently read-only.";
    const auto reset = [](lightHostModern::PluginInstances& session) {
        for (auto& record : session.records) record.customName.clear();
        for (auto& n : session.graph.nodes) { n.customName.clear(); n.inputAliases = var(new DynamicObject); n.outputAliases = var(new DynamicObject);
            if (n.kind == "plugin") for (const auto& record : session.records) if (record.id == n.id) n.name = record.description.name; }
    };
    auto next = operatingProfiles->profiles; for (auto& profile : next) reset(profile.session);
    if (!operatingProfiles->commit(std::move(next))) return operatingProfiles->error;
    reset(instances); routingUndo.clear(); routingRedo.clear();
    auto* settings = getAppProperties().getUserSettings();
    const auto keys = settings->getAllProperties().getAllKeys();
    for (const auto& key : keys) if (key.startsWith("knownPluginName-") || key == "audioChannelAliases" || key == "audioDeviceAliases") settings->removeValue(key);
    markSettingsDirty(); ++chainVersion; ++pluginDatabaseVersion; saveActivePluginList(); return {};
}
