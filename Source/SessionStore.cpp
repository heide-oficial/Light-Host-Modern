#include "SessionStore.h"
#include <charconv>
#include <stdexcept>
#include <Windows.h>

namespace lightHostModern
{
namespace
{
using Slot = SessionStorage::Slot;
constexpr Slot candidates[]{Slot::primary, Slot::backup, Slot::pending, Slot::backupPending};
juce::String windowsError(const char* operation)
{
    return juce::String(operation) + " (Windows " + juce::String(static_cast<int>(GetLastError())) + ")";
}
std::wstring longPath(const juce::File& file)
{
    const std::wstring path(file.getFullPathName().toWideCharPointer());
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    return path.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
}
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::string contentForHash(const juce::String& xml, const juce::String& migration, bool empty)
{
    return (empty ? "empty:" : "chain:") + std::to_string(migration.getNumBytesAsUTF8()) + ":"
        + migration.toStdString() + xml.toStdString();
}
}

juce::String SessionCodec::digest(const std::string& bytes)
{
    return juce::SHA256(bytes.data(), bytes.size()).toHexString();
}

EncodedSession SessionCodec::encode(const SessionDocument& document)
{
    if (!document.instances.writable || (document.instances.records.empty() && !document.intentionalEmpty)
        || (!document.instances.records.empty() && document.intentionalEmpty))
        throw std::invalid_argument("Session is not authorized for writing");
    const auto xml = document.instances.serialize(0)->toString();
    const auto hash = digest(contentForHash(xml, document.migrationId, document.intentionalEmpty));
    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 1);
    root->setProperty("revision", juce::String(document.revision));
    root->setProperty("intentionalEmpty", document.intentionalEmpty);
    root->setProperty("migrationId", document.migrationId);
    root->setProperty("contentHash", hash);
    root->setProperty("sessionXml", xml);
    auto bytes = juce::JSON::toString(juce::var(root), true).toStdString();
    if (bytes.size() > maximumFileBytes) throw std::length_error("Session exceeds the 256 MiB storage capacity");
    return {std::move(bytes), hash};
}

std::optional<SessionDocument> SessionCodec::decode(const std::string& bytes, juce::String& error)
{
    error = "Invalid or unsupported session file";
    if (bytes.size() > maximumFileBytes) { error = "Session exceeds the 256 MiB storage capacity"; return {}; }
    const auto root = parseBoundedJson(juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size())), maximumFileBytes);
    if (!root.isObject() || !root["formatVersion"].isInt() || static_cast<int>(root["formatVersion"]) != 1
        || !root["revision"].isString() || !root["intentionalEmpty"].isBool() || !root["migrationId"].isString()
        || !root["contentHash"].isString() || !root["sessionXml"].isString()) return {};
    SessionDocument document;
    const auto revision = root["revision"].toString().toStdString();
    const auto parsed = std::from_chars(revision.data(), revision.data() + revision.size(), document.revision);
    if (parsed.ec != std::errc{} || parsed.ptr != revision.data() + revision.size() || document.revision == 0) return {};
    document.intentionalEmpty = static_cast<bool>(root["intentionalEmpty"]);
    document.migrationId = root["migrationId"].toString();
    const auto xmlText = root["sessionXml"].toString();
    if (root["contentHash"].toString() != digest(contentForHash(xmlText, document.migrationId, document.intentionalEmpty)))
    { error = "Session content checksum does not match"; return {}; }
    const auto xml = parseBoundedXml(xmlText);
    if (!xml || !document.instances.deserialize(*xml)
        || document.instances.records.empty() != document.intentionalEmpty) return {};
    error.clear();
    return document;
}

DiskSessionStorage::DiskSessionStorage(const juce::File& preferences)
{
    const auto primary = preferences.getSiblingFile(preferences.getFileName() + ".session.json");
    files = {primary, primary.getSiblingFile(primary.getFileName() + ".bak"),
        primary.getSiblingFile(primary.getFileName() + ".pending"), primary.getSiblingFile(primary.getFileName() + ".backup-pending"),
        preferences, preferences.getSiblingFile(preferences.getFileName() + ".pre-session.bak"),
        preferences.getSiblingFile(preferences.getFileName() + ".pre-session.bak.pending")};
}

SessionStorage::Read DiskSessionStorage::read(Slot slot)
{
    Read result;
    Handle handle{CreateFileW(longPath(file(slot)).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        result.exists = code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND;
        if (result.exists) result.error = windowsError("Cannot read session file");
        return result;
    }
    result.exists = true;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.value, &size)) { result.error = windowsError("Cannot read session file size"); return result; }
    if (size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > SessionCodec::maximumFileBytes)
    { result.error = "Session exceeds the 256 MiB storage capacity"; return result; }
    result.bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < result.bytes.size())
    {
        DWORD read = 0;
        if (!ReadFile(handle.value, result.bytes.data() + offset,
            static_cast<DWORD>((std::min)(result.bytes.size() - offset, size_t{1024 * 1024})), &read, nullptr))
        { result.error = windowsError("Cannot read complete session file"); return result; }
        if (read == 0) { result.error = "Session file changed while reading"; return result; }
        offset += read;
    }
    return result;
}

juce::String DiskSessionStorage::writeFlushed(Slot slot, const std::string& bytes)
{
    if (slot == Slot::primary || slot == Slot::backup || slot == Slot::preferences || slot == Slot::legacyBackup)
        return "Direct overwrite of a protected session file is forbidden";
    Handle handle{CreateFileW(longPath(file(slot)).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE) return windowsError("Cannot create session temporary file");
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD written = 0;
        if (!WriteFile(handle.value, bytes.data() + offset,
            static_cast<DWORD>((std::min)(bytes.size() - offset, size_t{1024 * 1024})), &written, nullptr))
            return windowsError("Cannot write complete session file");
        if (written == 0) return "Session storage accepted no bytes";
        offset += written;
    }
    return FlushFileBuffers(handle.value) ? juce::String() : windowsError("Cannot flush session file");
}

juce::String DiskSessionStorage::replace(Slot source, Slot destination)
{
    if (!((source == Slot::pending && destination == Slot::primary)
        || (source == Slot::backupPending && destination == Slot::backup)
        || (source == Slot::legacyBackupPending && destination == Slot::legacyBackup))) return "Invalid session replacement";
    return MoveFileExW(longPath(file(source)).c_str(), longPath(file(destination)).c_str(),
        (destination == Slot::legacyBackup ? 0 : MOVEFILE_REPLACE_EXISTING) | MOVEFILE_WRITE_THROUGH)
        ? juce::String() : windowsError("Cannot replace session file");
}

juce::String DiskSessionStorage::preserve(Slot slot)
{
    const auto archive = file(slot).getSiblingFile(file(slot).getFileName() + ".damaged-" + juce::Uuid().toString());
    if (!CopyFileW(longPath(file(slot)).c_str(), longPath(archive).c_str(), TRUE)) return windowsError("Cannot preserve damaged session file");
    Handle handle{CreateFileW(longPath(archive).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (handle.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(handle.value)) return windowsError("Cannot flush preserved damaged session file");
    return {};
}

SessionRecovery SessionStore::recover(SessionStorage& storage)
{
    SessionRecovery result;
    for (const auto slot : candidates)
    {
        const auto read = storage.read(slot);
        if (!read.exists) continue;
        result.found = true;
        juce::String error;
        const auto document = read.error.isEmpty() ? SessionCodec::decode(read.bytes, error) : std::nullopt;
        if (!document) { result.warning = read.error.isEmpty() ? error : read.error; continue; }
        if (!result.document || document->revision > result.document->revision)
        { result.document = document; result.source = slot; result.bytes = read.bytes; }
    }
    if (result.document && (result.source != Slot::primary || result.warning.isNotEmpty()))
        result.warning = "Session recovered from validated data; original damaged files are retained";
    else if (result.found && !result.document)
        result.warning = "No valid session could be recovered; original files are retained: " + result.warning;
    return result;
}

juce::String SessionStore::backupLegacy(SessionStorage& storage, juce::String& migrationId)
{
    auto backup = storage.read(Slot::legacyBackup);
    if (backup.error.isNotEmpty()) return backup.error;
    if (!backup.exists)
    {
        backup = storage.read(Slot::preferences);
        if (backup.error.isNotEmpty()) return backup.error;
        if (!backup.exists) { migrationId = "new"; return {}; }
        const auto error = storage.writeFlushed(Slot::legacyBackupPending, backup.bytes);
        if (error.isNotEmpty()) return error;
        if (const auto replaced = storage.replace(Slot::legacyBackupPending, Slot::legacyBackup); replaced.isNotEmpty()) return replaced;
    }
    migrationId = SessionCodec::digest(backup.bytes);
    return {};
}

juce::String SessionStore::commit(SessionStorage& storage, const EncodedSession& encoded)
{
    juce::String validation;
    if (!SessionCodec::decode(encoded.bytes, validation)) return validation;
    const auto previous = recover(storage);
    // Preserve incomplete/corrupt originals before reusing any temporary slot.
    for (const auto slot : candidates)
    {
        const auto read = storage.read(slot);
        if (read.error.isNotEmpty()) return read.error;
        if (read.exists && !SessionCodec::decode(read.bytes, validation))
            if (const auto error = storage.preserve(slot); error.isNotEmpty()) return error;
    }
    // The caller supplies an authorized immutable document. A failed first save
    // may leave only partial temporary bytes; retry must still be possible. A
    // store opened with unrecoverable existing files rejects submissions below.
    // Back up the newest valid candidate BEFORE overwriting pending. A completed
    // pending file may be the only valid copy after an interrupted first save.
    if (previous.document)
    {
        const auto backup = storage.read(Slot::backup);
        if (backup.error.isNotEmpty()) return backup.error;
        if (backup.bytes != previous.bytes)
        {
            if (const auto error = storage.writeFlushed(Slot::backupPending, previous.bytes); error.isNotEmpty()) return error;
            if (const auto error = storage.replace(Slot::backupPending, Slot::backup); error.isNotEmpty()) return error;
        }
    }
    if (const auto error = storage.writeFlushed(Slot::pending, encoded.bytes); error.isNotEmpty()) return error;
    return storage.replace(Slot::pending, Slot::primary);
}

SessionStore::SessionStore(std::shared_ptr<SessionStorage> io, const SessionRecovery& recovery,
    std::chrono::milliseconds delay, std::function<Clock::time_point()> now)
    : storage(std::move(io)), debounce(delay), clock(std::move(now))
{
    if (recovery.document)
    {
        current.requestedRevision = current.savedRevision = recovery.document->revision;
        if (recovery.source == Slot::primary && recovery.warning.isEmpty())
            savedDigest = parseBoundedJson(juce::String::fromUTF8(recovery.bytes.data(), static_cast<int>(recovery.bytes.size())), SessionCodec::maximumFileBytes)["contentHash"].toString();
    }
    writable = !recovery.found || recovery.document.has_value();
    if (!writable) current.error = recovery.warning;
    worker = std::thread([this] { run(); });
}

SessionStore::~SessionStore() { shutdown(); }

juce::uint64 SessionStore::submit(PluginInstances instances, bool empty, const juce::String& migration)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping) return current.requestedRevision;
    if (!writable || current.requestedRevision == (std::numeric_limits<juce::uint64>::max)())
    { current.error = "Session is read-only; original data retained"; ++current.changeSerial; return current.requestedRevision; }
    pending = SessionDocument{std::move(instances), ++current.requestedRevision, migration, empty};
    due = clock() + debounce;
    current.pending = true;
    ++current.changeSerial;
    failed = false;
    changed.notify_all();
    return current.requestedRevision;
}

SessionSaveStatus SessionStore::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return current;
}

bool SessionStore::flush(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (!writable) return false;
    const auto target = current.requestedRevision, previousAttempt = attemptSerial;
    if (current.savedRevision >= target && !current.pending) return true;
    if (stopping) return false;
    force = true; failed = false;
    changed.notify_all();
    const bool finished = changed.wait_for(lock, timeout, [&] {
        return current.savedRevision >= target || (attemptSerial != previousAttempt && failed);
    });
    if (!finished) { current.error = "Session save is still pending after the flush deadline"; ++current.changeSerial; }
    return current.savedRevision >= target;
}

void SessionStore::shutdown()
{
    if (joined) return;
    flush();
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; changed.notify_all(); }
    if (worker.joinable()) worker.join();
    joined = true;
}

void SessionStore::run()
{
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopping)
    {
        if (!pending || failed) { changed.wait(lock); continue; }
        if (!force && clock() < due) { changed.wait_for(lock, (std::min)(due - clock(), Clock::duration(std::chrono::seconds(1)))); continue; }
        auto document = std::move(*pending); pending.reset();
        force = false; current.saving = true; ++current.changeSerial;
        const auto previousDigest = savedDigest;
        lock.unlock();
        juce::String error, digest;
        bool wrote = false;
        try
        {
            const auto encoded = SessionCodec::encode(document);
            digest = encoded.digest;
            if (digest != previousDigest) { error = commit(*storage, encoded); wrote = error.isEmpty(); }
        }
        catch (const std::exception& failure) { error = juce::String::fromUTF8(failure.what()); }
        catch (...) { error = "Unexpected session storage failure"; }
        lock.lock();
        current.saving = false; ++attemptSerial; ++current.changeSerial;
        current.error = error;
        if (error.isEmpty())
        {
            current.savedRevision = document.revision; savedDigest = digest;
            if (wrote) ++current.writes;
        }
        else if (!pending)
        {
            pending = std::move(document);
            failed = true;
        }
        current.pending = pending.has_value() || current.savedRevision < current.requestedRevision;
        changed.notify_all();
    }
}
}
