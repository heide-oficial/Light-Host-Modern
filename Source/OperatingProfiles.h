#pragma once
#include "PluginInstances.h"
#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace lightHostModern
{
struct OperatingProfile
{
    juce::String id, name, description, created, modified;
    bool includeAudio = false;
    juce::String audioXml;
    bool monoInput = false, monoOutput = false, muted = false, bypassed = false;
    PluginInstances session;
};

// Saved profiles are independent from the crash-recovery working session.
// Mutations are committed atomically before the in-memory catalogue is replaced.
class OperatingProfiles
{
public:
    explicit OperatingProfiles(const juce::File& preferences)
        : file(preferences.getSiblingFile(preferences.getFileName() + ".profiles.xml")) { load(); }
    static juce::String defaultId(const juce::String& mode) { return mode == "chain" ? "00000000000000000000000000000002" : "00000000000000000000000000000001"; }
    static bool isDefault(const juce::String& id) { return id == defaultId("list") || id == defaultId("chain"); }
    static OperatingProfile makeDefault(const juce::String& mode) {
        OperatingProfile p; p.id = defaultId(mode); p.name = "Default"; p.created = p.modified = juce::Time::getCurrentTime().toISO8601(true);
        p.session.mode = mode; p.session.profileId = p.id;
        if (mode == "chain") { p.session.graph = RoutingGraph::empty(); p.session.graph.edges.clear(); }
        return p;
    }
    bool ensureDefaults() {
        auto next = profiles; for (const auto* mode : {"list", "chain"}) if (!find(defaultId(mode))) next.push_back(makeDefault(mode));
        if (!writable) { profiles = std::move(next); return true; }
        return next.size() == profiles.size() || commit(std::move(next));
    }
    std::vector<OperatingProfile> profiles;
    juce::String error;
    bool writable = true;
    const OperatingProfile* find(const juce::String& id) const
    { for (const auto& p : profiles) if (p.id == id) return &p; return nullptr; }
    bool commit(std::vector<OperatingProfile> next, bool preserveBackup=false)
    {
        if (!writable) return false;
        juce::XmlElement root("LIGHTHOSTPROFILES"); root.setAttribute("version", 1);
        for (const auto& p : next)
        {
            auto* e = root.createNewChildElement("PROFILE"); e->setAttribute("id", p.id); e->setAttribute("name", p.name);
            e->setAttribute("description", p.description); e->setAttribute("created", p.created); e->setAttribute("modified", p.modified);
            e->setAttribute("includeAudio", p.includeAudio); e->setAttribute("monoInput", p.monoInput); e->setAttribute("monoOutput", p.monoOutput);
            e->setAttribute("muted", p.muted); e->setAttribute("bypassed", p.bypassed);
            e->createNewChildElement("AUDIO")->addTextElement(p.audioXml); e->addChildElement(p.session.serialize().release());
        }
        const auto bytes = root.toString();
        if (bytes.getNumBytesAsUTF8() > 256 * 1024 * 1024) { error = "Profile storage exceeds 256 MiB."; return false; }
        juce::TemporaryFile pending(file);
        {
            juce::FileOutputStream stream(pending.getFile());
            if (!stream.openedOk() || !stream.writeText(bytes, false, false, "\n")) { error = "Could not write profiles."; return false; }
            stream.flush(); if (stream.getStatus().failed()) { error = stream.getStatus().getErrorMessage(); return false; }
        }
        // ReplaceFile keeps the previous complete catalogue as the recovery
        // candidate. Never delete the primary before the replacement succeeds.
#if JUCE_WINDOWS
        const auto target = file.getFullPathName();
        const auto source = pending.getFile().getFullPathName();
        const auto backup = backupFile().getFullPathName();
        const bool replaced = file.existsAsFile()
            ? ReplaceFileW(target.toWideCharPointer(), source.toWideCharPointer(), preserveBackup?nullptr:backup.toWideCharPointer(), 0, nullptr, nullptr) != FALSE
            : MoveFileExW(source.toWideCharPointer(), target.toWideCharPointer(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!replaced) { error = "Could not replace the profiles file. The previous profiles were preserved."; return false; }
#else
        if (!preserveBackup && file.existsAsFile() && !file.copyFileTo(backupFile())) { error = "Could not back up profiles."; return false; }
        if (!pending.overwriteTargetFileWithTemporary()) { error = "Could not replace the profiles file."; return false; }
#endif
        profiles = std::move(next); error.clear(); return true;
    }
    bool recover()
    {
        if (writable) return true;
        // Recovery is explicit; retain damaged bytes even when only some entries
        // could be read. Do not turn a partial load into an automatic overwrite.
        const auto preserved = file.getSiblingFile(file.getFileName() + ".recovery-" + juce::Uuid().toString());
        if (file.existsAsFile() && !file.copyFileTo(preserved)) { error = "Could not preserve the damaged profile file."; return false; }
        writable = true;
        if (commit(profiles,true)) return true;
        writable = false; return false;
    }
private:
    juce::File backupFile() const { return file.getSiblingFile(file.getFileName() + ".bak"); }
    void load()
    {
        if (!file.existsAsFile()) {
            if (backupFile().existsAsFile()) { read(backupFile()); writable = false; error = "Profiles recovered from backup. Confirm recovery to save changes."; }
            return;
        }
        read(file);
        if (!writable && profiles.empty() && backupFile().existsAsFile()) {
            read(backupFile()); writable = false;
            error = "Profiles recovered from backup. The original file was preserved; confirm recovery to save changes.";
        }
        ensureDefaults();
    }
    void read(const juce::File& candidate)
    {
        if (candidate.getSize() > 256 * 1024 * 1024) { writable = false; error = "Profile storage exceeds 256 MiB."; return; }
        const auto root = parseBoundedXml(candidate);
        if (!root || !root->hasTagName("LIGHTHOSTPROFILES") || root->getIntAttribute("version") != 1)
        { writable = false; error = "The profiles file could not be read. The original file was preserved."; return; }
        std::set<juce::String> ids;
        for (const auto* e : root->getChildIterator())
        {
            OperatingProfile p; p.id = e->getStringAttribute("id"); p.name = e->getStringAttribute("name");
            const auto* session = e->getChildByName("LIGHTHOSTSESSION");
            if (!e->hasTagName("PROFILE") || p.id.length() != 32 || !p.id.containsOnly("0123456789abcdef")
                || !ids.insert(p.id).second || !session || !p.session.deserialize(*session))
            { writable = false; error = "Some saved profiles are invalid. Valid entries remain available; confirm recovery to save changes. The original file was preserved."; continue; }
            p.description = e->getStringAttribute("description"); p.created = e->getStringAttribute("created"); p.modified = e->getStringAttribute("modified");
            p.includeAudio = e->getBoolAttribute("includeAudio"); p.monoInput = e->getBoolAttribute("monoInput"); p.monoOutput = e->getBoolAttribute("monoOutput");
            p.muted = e->getBoolAttribute("muted"); p.bypassed = e->getBoolAttribute("bypassed");
            if (const auto* audio = e->getChildByName("AUDIO")) p.audioXml = audio->getAllSubText();
            profiles.push_back(std::move(p));
        }
    }
    juce::File file;
};
}
