#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <set>
#include <windows.h>
#include "VerboseLog.h"
#include "ScanTiming.h"
#include "BoundedInput.h"

namespace lightHostModern::scan
{
inline constexpr int scannerProtocolVersion = 3;
inline constexpr int metadataCacheVersion = 3;
inline constexpr int batchItems = 64;
inline constexpr int maximumResponseBytes = 4 * 1024 * 1024;
template<class Input> inline std::unique_ptr<juce::XmlElement> parseScannerXml(const Input& input)
{
    return lightHostModern::parseBoundedXml(input, maximumResponseBytes, 32, 65536);
}

inline juce::String filesystemFailure(const std::error_code& error)
{
    const bool native = error.category() == std::system_category();
    // Windows maps some network errors to the generic missing-path condition;
    // preserve the more specific native reason before consulting that mapping.
    if (native && (error.value() == ERROR_BAD_NETPATH || error.value() == ERROR_BAD_NET_NAME
        || error.value() == ERROR_NETWORK_UNREACHABLE || error.value() == ERROR_CONNECTION_UNAVAIL
        || error.value() == ERROR_NETNAME_DELETED)) return "network_unavailable";
    if (error == std::errc::permission_denied || (native && (error.value() == ERROR_ACCESS_DENIED
        || error.value() == ERROR_NETWORK_ACCESS_DENIED))) return "access_denied";
    if (error == std::errc::no_such_file_or_directory || (native && (error.value() == ERROR_FILE_NOT_FOUND
        || error.value() == ERROR_PATH_NOT_FOUND))) return "missing";
    if (error == std::errc::filename_too_long) return "path_too_long";
    return "enumeration";
}

inline juce::File batchFile(const juce::File& response, int index)
{
    return response.getSiblingFile(response.getFileName() + ".batch-" + juce::String(index) + ".xml");
}

inline juce::String fingerprint(const juce::File& module, const std::function<void()>& progress = {})
{
    StageTiming timing("fingerprint", "path=" + module.getFullPathName().toStdString());
    uint64_t readBytes = 0;
    double readMs = 0, hashMs = 0;
    std::vector<std::filesystem::path> entries;
    std::error_code error;
    const std::filesystem::path path(module.getFullPathName().toWideCharPointer());
    if (std::filesystem::is_directory(path, error))
    {
        std::filesystem::recursive_directory_iterator cursor(path, std::filesystem::directory_options::none, error), end;
        if (error) throw std::runtime_error("metadata_unavailable");
        for (; cursor != end; cursor.increment(error))
        {
            if (progress) progress();
            if (error) throw std::runtime_error("metadata_unavailable");
            const auto attributes = GetFileAttributesW(cursor->path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("metadata_unavailable");
            // Never accept a cache fingerprint that silently omitted a linked
            // subtree containing executable code. Root links are supported by
            // enumeration; linked files here are read and hashed normally.
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                throw std::runtime_error("module_link_unsupported");
            if (cursor->is_regular_file(error)) entries.push_back(cursor->path());
            if (error) throw std::runtime_error("metadata_unavailable");
        }
        if (error) throw std::runtime_error("metadata_unavailable");
    }
    else
    {
        if (error || !std::filesystem::is_regular_file(path, error)) throw std::runtime_error("missing");
        entries.push_back(path);
    }
    std::sort(entries.begin(), entries.end());
    timing.add("walkMs", timing.elapsedMs());
    juce::MemoryOutputStream manifest;
    for (const auto& entry : entries)
    {
        const juce::File file(juce::String(entry.wstring().c_str()));
        manifest.writeString(juce::String(entry.lexically_relative(path).wstring().c_str()));
        manifest.writeInt64(file.getSize());
        manifest.writeInt64(file.getLastModificationTime().toMilliseconds());
        if (file.hasFileExtension("dll;vst3;json"))
        {
            auto stream = file.createInputStream();
            if (!stream) throw std::runtime_error("metadata_unavailable");
            const auto size = stream->getTotalLength();
            class ObservedInput final : public juce::InputStream {
            public:
                ObservedInput(juce::InputStream& input, const std::function<void()>& callback, uint64_t& bytes, double* readTime)
                    : source(input), tick(callback), bytesRead(bytes), readMilliseconds(readTime) {}
                juce::int64 getTotalLength() override { return source.getTotalLength(); }
                juce::int64 getPosition() override { return source.getPosition(); }
                bool setPosition(juce::int64 p) override { return source.setPosition(p); }
                bool isExhausted() override { return source.isExhausted(); }
                int read(void* p,int n) override {
                    const auto start = readMilliseconds ? StageTiming::Clock::now() : StageTiming::Clock::time_point{};
                    const auto result=source.read(p,n);
                    if (readMilliseconds) *readMilliseconds += StageTiming::milliseconds(StageTiming::Clock::now() - start);
                    if (result > 0) { bytesRead += static_cast<uint64_t>(result); if(tick)tick(); }
                    return result;
                }
                juce::InputStream& source; const std::function<void()>& tick;
                uint64_t& bytesRead; double* readMilliseconds;
            } observed(*stream,progress,readBytes,timing.active()?&readMs:nullptr);
            // JUCE's SHA256 reads 64 bytes per call. Buffer the file stream so
            // hashing does not issue one Windows file read for every SHA block.
            // The same complete byte stream is hashed; memory stays bounded.
            juce::BufferedInputStream buffered(observed, 256 * 1024);
            const auto beforeHash = timing.elapsedMs(), beforeRead = readMs;
            const juce::SHA256 digest(buffered);
            hashMs += juce::jmax(0.0, timing.elapsedMs() - beforeHash - (readMs - beforeRead));
            if (stream->getPosition() != size) throw std::runtime_error("changed");
            manifest.writeString(digest.toHexString());
        }
    }
    timing.add("files", static_cast<double>(entries.size()));
    timing.add("bytesRead", static_cast<double>(readBytes));
    timing.add("readMs", readMs); timing.add("hashAndProgressMs", hashMs);
    return juce::SHA256(manifest.getData(), manifest.getDataSize()).toHexString();
}

inline bool belongsToModule(const juce::String& identifier, const juce::String& module, const juce::String& format)
{
    if (identifier == module) return true;
    if (format != "VST3" || !juce::File::isAbsolutePath(identifier)) return false;
    const juce::File binary(identifier), root(module);
    // Lexical containment on the host; filesystem validation is done in worker.
    return binary.hasFileExtension("vst3") && binary.isAChildOf(root);
}

class BatchWriter
{
public:
    BatchWriter(const juce::XmlElement& request, juce::File result)
        : response(std::move(result)), id(request.getStringAttribute("id")) { reset(); }
    bool append(std::unique_ptr<juce::XmlElement> item)
    {
        bytes += item->toString().getNumBytesAsUTF8();
        batch->addChildElement(item.release());
        return batch->getNumChildElements() < batchItems && bytes < 1024 * 1024 && GetTickCount64()-lastFlush<100 ? true : flush();
    }
    bool flush()
    {
        if (batch->getNumChildElements() == 0) return true;
        const auto destination = batchFile(response, count);
        juce::TemporaryFile temporary(destination);
        if (!batch->writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary()) return false;
        ++count;
        lastFlush=GetTickCount64();
        reset();
        return true;
    }
    int finish()
    {
        if (!flush()) return 5;
        juce::XmlElement result("SCAN");
        result.setAttribute("version", scannerProtocolVersion);
        result.setAttribute("id", id);
        result.setAttribute("mode", "enumerate");
        result.setAttribute("batches", count);
        return result.writeTo(response) ? 0 : 5;
    }
private:
    void reset()
    {
        batch = std::make_unique<juce::XmlElement>("BATCH");
        batch->setAttribute("version", scannerProtocolVersion);
        batch->setAttribute("id", id);
        batch->setAttribute("sequence", count);
        bytes = 0;
    }
    juce::File response;
    juce::String id;
    int count = 0;
    size_t bytes = 0;
    uint64_t lastFlush=0;
    std::unique_ptr<juce::XmlElement> batch;
};

// Called only by the job-controlled scanner executable, including in tests.
inline int enumerate(const juce::XmlElement& request, const juce::File& response,
    const std::function<juce::String(const juce::String&)>& rootFailure = {})
{
    BatchWriter writer(request, response);
    uint64_t lastPulse=0, token=0;
    const auto pulse=[&] {
        if(GetTickCount64()-lastPulse<100)return;
        auto item=std::make_unique<juce::XmlElement>("PROGRESS");item->setAttribute("token",juce::String(++token));
        if(!writer.append(std::move(item))||!writer.flush())throw std::runtime_error("write_failed");lastPulse=GetTickCount64();
    };
    const auto format = request.getStringAttribute("format");
    const auto extension = format == "VST3" ? ".vst3" : ".dll";
    std::set<std::wstring> seen;
    const auto fail = [&](const juce::String& path, const juce::String& reason) {
        auto item = std::make_unique<juce::XmlElement>("FAILURE");
        item->setAttribute("path", path); item->setAttribute("reason", reason);
        if (!writer.append(std::move(item))) throw std::runtime_error("write_failed");
        // Publish failures promptly even if the next root blocks.
        if (!writer.flush()) throw std::runtime_error("write_failed");
    };
    const auto candidate = [&](const std::filesystem::path& entry) {
        std::error_code error;
        const auto canonical = std::filesystem::canonical(entry, error);
        if (error) { fail(juce::String(entry.wstring().c_str()), filesystemFailure(error)); return; }
        const juce::String canonicalPath(canonical.wstring().c_str());
        if (!seen.insert(canonicalPath.toLowerCase().toWideCharPointer()).second) return;
        // Canonical paths are for deduplication only. Pass the discovered path
        // to JUCE so its original (including legacy 8.3) identity stays intact.
        const juce::String path(entry.wstring().c_str());
        try
        {
            auto item = std::make_unique<juce::XmlElement>("CANDIDATE");
            item->setAttribute("path", path);
            item->setAttribute("canonicalPath", canonicalPath);
            item->setAttribute("fingerprint", fingerprint(juce::File(path),pulse));
            item->setAttribute("stamp", juce::String(juce::File(path).getLastModificationTime().toMilliseconds()));
            if (!writer.append(std::move(item))) throw std::runtime_error("write_failed");
            // Each completed candidate remains usable if a subsequent lookup hangs.
        }
        catch (const std::exception& exception) { fail(path, exception.what()); }
    };
    try
    {
        for (const auto* root : request.getChildIterator())
        {
            if (!root->hasTagName("ROOT")) continue;
            const auto rootName = root->getStringAttribute("path");
            verbose::log("scanner.enumerate", "root="+rootName.toStdString());
            auto started = std::make_unique<juce::XmlElement>("ROOT");
            started->setAttribute("path", rootName); started->setAttribute("index", root->getIntAttribute("index"));
            if (!writer.append(std::move(started)) || !writer.flush()) throw std::runtime_error("write_failed");
            if (rootFailure)
            {
                const auto reason = rootFailure(rootName);
                if (reason.isNotEmpty()) { fail(rootName, reason); continue; }
            }
            const std::filesystem::path path(rootName.toWideCharPointer());
            std::error_code error;
            if (!std::filesystem::exists(path, error)) {
                if(!error&&root->getBoolAttribute("optional")){
                    auto ignored=std::make_unique<juce::XmlElement>("IGNORED");ignored->setAttribute("path",rootName);ignored->setAttribute("reason","optional_missing");writer.append(std::move(ignored));
                } else fail(rootName, error ? filesystemFailure(error) : "missing"); continue;
            }
            const auto completed=[&] {auto done=std::make_unique<juce::XmlElement>("ROOT_DONE");done->setAttribute("path",rootName);done->setAttribute("index",root->getIntAttribute("index"));writer.append(std::move(done));writer.flush();};
            if (juce::String(path.extension().wstring().c_str()).equalsIgnoreCase(extension)) { candidate(path); completed(); continue; }
            std::set<std::wstring> directories;
            auto rootKey=std::filesystem::weakly_canonical(path,error).wstring();
            std::transform(rootKey.begin(),rootKey.end(),rootKey.begin(),towlower);directories.insert(rootKey);
            std::filesystem::recursive_directory_iterator cursor(path, std::filesystem::directory_options::follow_directory_symlink, error), end;
            if (error) { fail(rootName, filesystemFailure(error)); continue; }
            for (; cursor != end; cursor.increment(error))
            {
                pulse();
                if (error) { fail(rootName, filesystemFailure(error)); break; }
                const auto entry = cursor->path();
                const auto attributes = GetFileAttributesW(entry.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto code = GetLastError();
                    fail(juce::String(entry.wstring().c_str()), filesystemFailure(std::error_code(static_cast<int>(code), std::system_category())));
                    continue;
                }
                if((attributes&FILE_ATTRIBUTE_DIRECTORY)!=0){
                    auto canonical=std::filesystem::canonical(entry,error);
                    if(error){fail(juce::String(entry.wstring().c_str()),filesystemFailure(error));error.clear();cursor.disable_recursion_pending();continue;}
                    auto key=canonical.wstring();std::transform(key.begin(),key.end(),key.begin(),towlower);
                    if(cursor.depth()>64){fail(juce::String(entry.wstring().c_str()),"directory_depth_limit");cursor.disable_recursion_pending();continue;}
                    if(!directories.insert(key).second){
                        auto ignored=std::make_unique<juce::XmlElement>("IGNORED");ignored->setAttribute("path",juce::String(entry.wstring().c_str()));ignored->setAttribute("reason","duplicate_or_cycle");
                        writer.append(std::move(ignored));verbose::log("scanner.enumerate","ignored duplicate_or_cycle path="+juce::String(entry.wstring().c_str()).toStdString());
                        cursor.disable_recursion_pending();continue;
                    }
                }
                const juce::String suffix(entry.extension().wstring().c_str());
                if (suffix.equalsIgnoreCase(extension)) { cursor.disable_recursion_pending(); candidate(entry); }
                else if (suffix.equalsIgnoreCase(".vst3") || suffix.equalsIgnoreCase(".lv2")) cursor.disable_recursion_pending();
            }
            if (error) fail(rootName, filesystemFailure(error));
            else completed();
        }
        return writer.finish();
    }
    catch (...) { writer.flush(); return 6; }
}
}
