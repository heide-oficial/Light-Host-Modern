#pragma once
#include "ScannerProtocol.h"
#include "ScanTiming.h"
#include <map>

namespace lightHostModern::scan
{
// The XML snapshot remains compatible with cache v3. During validation, append
// one checksummed class delta instead of rewriting every preceding class.
// A new checkpoint ID binds the journal to its atomically published snapshot.
class CacheJournal
{
public:
    explicit CacheJournal(juce::File snapshotFile)
        : snapshot(std::move(snapshotFile)), journal(snapshot.withFileExtension("journal")) {}

    static std::unique_ptr<juce::XmlElement> load(const juce::File& file)
    {
        StageTiming timing("cache_read", "path=" + file.getFullPathName().toStdString());
        auto result = parseScannerXml(file);
        if (!result || result->getBoolAttribute("complete", true)
            || result->getIntAttribute("cacheVersion") != metadataCacheVersion) return result;
        const auto id = result->getStringAttribute("checkpointId");
        if (id.isEmpty()) return result; // Old partial snapshot, without a journal.
        const auto journalFile = file.withFileExtension("journal");
        if (journalFile.getSize() > journalLimit) return result;
        auto input = journalFile.createInputStream();
        if (!input) return result;
        std::map<juce::String, std::unique_ptr<juce::XmlElement>> entries;
        size_t restoredBytes = 0;
        for (const auto* entry : result->getChildIterator()) {
            if (!entry->hasTagName("ENTRY")) continue;
            restoredBytes += entry->toString().getNumBytesAsUTF8();
            entries[entry->getStringAttribute("knownId")] = std::make_unique<juce::XmlElement>(*entry);
        }
        int records = 0;
        while (input->getPosition() <= journalLimit - 4 && input->getTotalLength() - input->getPosition() >= 4) {
            const auto length = input->readInt();
            if (length <= 0 || length > maximumResponseBytes || input->getPosition() > journalLimit - length - 64
                || input->getTotalLength() - input->getPosition() < length + 64LL) break;
            juce::MemoryBlock data(static_cast<size_t>(length));
            char checksum[64];
            if (input->read(data.getData(), length) != length || input->read(checksum, 64) != 64) break;
            if (juce::SHA256(data).toHexString() != juce::String::fromUTF8(checksum, 64)) break;
            auto record = parseScannerXml(juce::String::fromUTF8(static_cast<const char*>(data.getData()), length));
            if (!record || !record->hasTagName("CHECKPOINT") || record->getStringAttribute("id") != id
                || record->getNumChildElements() != 1) break;
            const auto* entry = record->getFirstChildElement();
            const auto key = entry->getStringAttribute("knownId");
            if (!entry->hasTagName("ENTRY") || key.length() != 64 || !key.containsOnly("0123456789abcdef")) break;
            const auto existing = entries.find(key);
            const auto oldBytes = existing == entries.end() ? size_t(0) : existing->second->toString().getNumBytesAsUTF8();
            const auto newBytes = entry->toString().getNumBytesAsUTF8();
            if (restoredBytes - oldBytes + newBytes > maximumResponseBytes) break;
            restoredBytes = restoredBytes - oldBytes + newBytes;
            entries[key] = std::make_unique<juce::XmlElement>(*entry);
            ++records;
        }
        // A torn/corrupt trailing frame never discards earlier complete records.
        result->deleteAllChildElements();
        for (auto& entry : entries) result->addChildElement(entry.second.release());
        timing.add("records", records);
        timing.add("bytesRead", static_cast<double>(input->getPosition()));
        return result;
    }

    bool begin(const juce::XmlElement& baseline)
    {
        StageTiming timing("cache_begin", "path=" + snapshot.getFullPathName().toStdString());
        juce::XmlElement copy(baseline);
        checkpointId = juce::Uuid().toString();
        copy.setAttribute("cacheVersion", metadataCacheVersion);
        copy.setAttribute("complete", false);
        copy.setAttribute("catalog", false);
        copy.setAttribute("checkpointId", checkpointId);
        // Keep the old snapshot+journal intact if publishing the merged baseline
        // fails. Delete the old journal only after its entries are in the snapshot.
        if (!writeSnapshot(copy) || !journal.deleteFile()) return false;
        output = journal.createOutputStream();
        return output != nullptr && output->openedOk();
    }

    bool append(const juce::XmlElement& entry)
    {
        if (!output) return false;
        StageTiming timing("cache_checkpoint", "class=" + entry.getStringAttribute("knownId").toStdString());
        juce::XmlElement record("CHECKPOINT");
        record.setAttribute("id", checkpointId);
        record.addChildElement(new juce::XmlElement(entry));
        const auto text = record.toString();
        const auto bytes = text.getNumBytesAsUTF8();
        if (bytes > maximumResponseBytes || output->getPosition() + bytes + 68 > journalLimit) {
            output.reset(); return false;
        }
        const auto checksum = juce::SHA256(text.toRawUTF8(), bytes).toHexString();
        const bool written = output->writeInt(static_cast<int>(bytes))
            && output->write(text.toRawUTF8(), bytes) && output->write(checksum.toRawUTF8(), 64);
        output->flush(); // Each complete class survives subsequent worker failure.
        const bool ok = written && output->getStatus().wasOk();
        timing.add("bytesWritten", static_cast<double>(bytes + 68));
        if (!ok) output.reset(); // Never append behind an incomplete frame.
        return ok;
    }

    bool compact(const juce::XmlElement& result)
    {
        StageTiming timing("cache_compact", "path=" + snapshot.getFullPathName().toStdString());
        output.reset();
        juce::XmlElement copy(result);
        copy.removeAttribute("checkpointId");
        if (!writeSnapshot(copy)) return false;
        journal.deleteFile(); // An old ID cannot attach to the new snapshot.
        return true;
    }

    void invalidate()
    { output.reset(); snapshot.deleteFile(); journal.deleteFile(); }

private:
    bool writeSnapshot(const juce::XmlElement& xml)
    {
        const auto text = xml.toString();
        if (text.getNumBytesAsUTF8() > maximumResponseBytes || snapshot.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary(snapshot);
        auto stream = temporary.getFile().createOutputStream();
        if (!stream || !stream->write(text.toRawUTF8(), text.getNumBytesAsUTF8())) return false;
        stream->flush();
        const bool ok = stream->getStatus().wasOk();
        stream.reset();
        return ok && temporary.overwriteTargetFileWithTemporary();
    }

    static constexpr juce::int64 journalLimit = 16 * 1024 * 1024;
    juce::File snapshot, journal;
    juce::String checkpointId;
    std::unique_ptr<juce::FileOutputStream> output;
};
}
