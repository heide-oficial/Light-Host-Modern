#include "BoundedInput.h"
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>
#include <windows.h>

namespace lightHostModern
{
// Copy a frozen preferences family before opening it. Originals are never
// removed. A durable manifest makes partial publication resumable; canonical
// files without a pending migration always take precedence, even if damaged.
inline juce::Result migratePreferences(const juce::File& legacy, const juce::File& current)
{
    using namespace juce;
    if (current.getSiblingFile(current.getFileName() + ".factory-reset-completed").existsAsFile()) return Result::ok();
    const auto stage = current.getSiblingFile(current.getFileName() + ".identity-migration");
    const auto manifest = stage.getChildFile("manifest.json");
    // An intentional later reset must not import the preserved legacy copy again.
    if (!manifest.existsAsFile() && stage.getChildFile("completed.json").existsAsFile()) return Result::ok();
    const auto digest = [](const File& file) { FileInputStream input(file); return input.openedOk() ? SHA256(input).toHexString() : String(); };
    const auto copy = [&](const File& from, const File& to) -> Result {
        const auto pending = to.getSiblingFile(to.getFileName() + ".copy-pending");
        if (!CopyFileW(from.getFullPathName().toWideCharPointer(), pending.getFullPathName().toWideCharPointer(), FALSE))
            return Result::fail("Could not copy migration file: " + to.getFullPathName());
        const auto h = CreateFileW(pending.getFullPathName().toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        const bool flushed = h != INVALID_HANDLE_VALUE && FlushFileBuffers(h);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!flushed || digest(from) != digest(pending)) return Result::fail("Migration copy verification failed: " + to.getFullPathName());
        return MoveFileExW(pending.getFullPathName().toWideCharPointer(), to.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH)
            ? Result::ok() : Result::fail("Could not publish migration file: " + to.getFullPathName());
    };
    if (!manifest.existsAsFile())
    {
        if (current.existsAsFile() || current.getSiblingFile(current.getFileName() + ".session.json").existsAsFile()
            || current.getSiblingFile(current.getFileName() + ".session.json.bak").existsAsFile()
            || current.getSiblingFile(current.getFileName() + ".session.json.pending").existsAsFile()
            || current.getSiblingFile(current.getFileName() + ".session.json.backup-pending").existsAsFile()) return Result::ok();
        auto files = legacy.getParentDirectory().findChildFiles(File::findFiles, false, legacy.getFileName() + "*");
        if (files.isEmpty()) return Result::ok();
        if (auto result = stage.createDirectory(); result.failed()) return result;
        Array<var> entries;
        // Preferences are published last; every recoverable session candidate
        // and damaged archive is retained with its exact bytes.
        files.removeFirstMatchingValue(legacy); if (legacy.existsAsFile()) files.add(legacy);
        const auto crashed = legacy.getSiblingFile("RecentlyCrashedPluginsList");
        if (crashed.existsAsFile()) files.insert(0, crashed);
        for (const auto& file : files)
        {
            if (file.getSize() > 256LL * 1024 * 1024) return Result::fail("Migration source exceeds 256 MiB; original retained");
            const auto originalHash = digest(file);
            const auto name = file == crashed ? file.getFileName() : current.getFileName() + file.getFileName().substring(legacy.getFileName().length());
            const auto frozen = stage.getChildFile(name);
            if (!frozen.existsAsFile()) { if (auto result = copy(file, frozen); result.failed()) return result; }
            if (originalHash.isEmpty() || originalHash != digest(frozen) || originalHash != digest(file))
                return Result::fail("Preferences changed during migration; close the older app and retain recovery files");
            auto* entry = new DynamicObject; entry->setProperty("name", name); entry->setProperty("sha256", originalHash);
            entries.add(var(entry));
        }
        const auto pending = stage.getChildFile("manifest.pending");
        { FileOutputStream output(pending); if (!output.openedOk()) return Result::fail("Cannot prepare migration manifest");
          output.setPosition(0); output.truncate();
          const auto text = JSON::toString(var(entries)); output.writeText(text, false, false, nullptr); output.flush();
          if (output.getStatus().failed()) return output.getStatus(); }
        if (!MoveFileExW(pending.getFullPathName().toWideCharPointer(), manifest.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH))
            return Result::fail("Cannot commit migration manifest");
    }
    const auto entries = lightHostModern::parseBoundedJson(manifest);
    if (!entries.isArray()) return Result::fail("Invalid migration manifest; original files retained");
    for (const auto& entry : *entries.getArray())
    {
        const auto name = entry["name"].toString();
        if (name.isEmpty() || name.containsAnyOf("/\\:") || name == "." || name == "..") return Result::fail("Invalid migration filename");
        const auto frozen = stage.getChildFile(name), destination = current.getSiblingFile(name);
        const auto expected = entry["sha256"].toString();
        if (!frozen.existsAsFile() || expected.isEmpty() || digest(frozen) != expected) return Result::fail("Migration backup is invalid; originals retained");
        if (!destination.existsAsFile()) { if (auto result = copy(frozen, destination); result.failed()) return result; }
        if (digest(destination) != expected) return Result::fail("Canonical data conflicts with pending migration; no files overwritten");
    }
    if (!MoveFileExW(manifest.getFullPathName().toWideCharPointer(), stage.getChildFile("completed.json").getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH))
        return Result::fail("Could not complete preference migration");
    return Result::ok();
}
}
