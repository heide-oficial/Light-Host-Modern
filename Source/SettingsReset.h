#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace lightHostModern::settingsReset
{
struct Paths { std::filesystem::path preferences, uiSettings, captures; };
inline std::filesystem::path marker(const Paths& paths) { return paths.preferences.wstring() + L".factory-reset"; }
inline std::filesystem::path completed(const Paths& paths) { return paths.preferences.wstring() + L".factory-reset-completed"; }

inline void validate(const std::filesystem::path& path, const std::filesystem::path& root)
{
    if (!path.is_absolute() || path.lexically_normal() != path || !root.is_absolute())
        throw std::runtime_error("Invalid reset path");
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == L"..")
        throw std::runtime_error("Reset path is outside its settings directory");
    for (auto at = path; !at.empty();) {
        const auto flags = GetFileAttributesW(at.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                throw std::runtime_error("Cannot inspect reset path");
        } else if (flags & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Reset refuses linked paths");
        const auto parent = at.parent_path(); if (parent == at) break; at = parent;
    }
}
inline void removeFile(const std::filesystem::path& path)
{
    if (DeleteFileW(path.c_str())) return;
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Cannot remove an internal settings file; reset remains pending");
}
inline bool pending(const Paths& paths)
{
    const auto path = marker(paths);
    const auto flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw std::runtime_error("Cannot inspect the pending reset marker");
    }
    validate(path, paths.preferences.parent_path());
    if (flags & FILE_ATTRIBUTE_DIRECTORY) throw std::runtime_error("Invalid pending reset marker");
    return true;
}
inline void writeDurable(const std::filesystem::path& path, const std::string& bytes)
{
    const auto pending = std::filesystem::path(path.wstring() + L".reset-pending");
    validate(path, path.parent_path()); validate(pending, path.parent_path());
    removeFile(pending);
    const auto file = CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot prepare reset state");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish reset state");
}
inline bool ownedRecoveryName(const std::wstring& name)
{
    const std::wstring prefix = L"chain-edit-recovery-";
    if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 32) return false;
    const auto id = name.substr(prefix.size(), 32), suffix = name.substr(prefix.size() + 32);
    return id.find_first_not_of(L"0123456789abcdef") == std::wstring::npos
        && (suffix == L".json" || suffix == L".json.pending");
}
inline std::vector<std::filesystem::path> inventory(const Paths& paths)
{
    std::vector<std::filesystem::path> result;
    for (const auto* suffix : {L"", L".profiles.xml", L".profiles.xml.bak", L".session.json", L".session.json.bak",
        L".session.json.pending", L".session.json.backup-pending", L".pre-session.bak", L".pre-session.bak.pending"}) {
        result.emplace_back(paths.preferences.wstring() + suffix);
        result.emplace_back(paths.preferences.wstring() + suffix + L".copy-pending");
    }
    result.push_back(paths.preferences.parent_path() / L"RecentlyCrashedPluginsList");
    result.push_back(paths.uiSettings);
    result.emplace_back(paths.preferences.wstring() + L".reset-pending");
    result.emplace_back(completed(paths).wstring() + L".reset-pending");
    for (const auto& root : {paths.preferences.parent_path(), paths.uiSettings.parent_path()}) {
        validate(root / L"reset-path-check", root);
        if (!std::filesystem::exists(root)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            const auto name = entry.path().filename().wstring();
            bool owned = root == paths.uiSettings.parent_path() && ownedRecoveryName(name);
            if (root == paths.preferences.parent_path()) {
                for (const auto* suffix : {L".profiles.xml.recovery-", L".session.json.damaged-", L".session.json.bak.damaged-",
                    L".session.json.pending.damaged-", L".session.json.backup-pending.damaged-"}) {
                    const auto prefix = paths.preferences.filename().wstring() + suffix;
                    if (name.rfind(prefix, 0) == 0 && name.size() == prefix.size() + 32
                        && name.substr(prefix.size()).find_first_not_of(L"0123456789abcdef") == std::wstring::npos) owned = true;
                }
            }
            if (owned) result.push_back(entry.path());
        }
    }
    return result;
}
inline void request(const Paths& paths)
{
    validate(marker(paths), paths.preferences.parent_path());
    std::filesystem::create_directories(paths.preferences.parent_path());
    writeDurable(marker(paths), "reset\n");
}
inline void perform(const Paths& paths)
{
    validate(marker(paths), paths.preferences.parent_path());
    if (!std::filesystem::is_regular_file(marker(paths))) throw std::runtime_error("Reset was not requested");
    const auto files = inventory(paths);
    // Validate the complete inventory before deleting anything. Never recurse
    // into profiles, migration archives, diagnostic captures or user exports.
    for (const auto& file : files) {
        const auto root = file.parent_path() == paths.uiSettings.parent_path()
            ? paths.uiSettings.parent_path() : paths.preferences.parent_path();
        validate(file, root);
        const auto flags = GetFileAttributesW(file.c_str());
        if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("An internal settings file became a directory");
    }
    validate(completed(paths), paths.preferences.parent_path());
    const auto logState = paths.captures / L"state.txt";
    validate(logState, paths.captures);
    validate(logState.wstring() + L".reset-pending", paths.captures);
    for (const auto& file : files) removeFile(file);
    // An interrupted identity migration must never resurrect its frozen files.
    // Its original backups remain available for manual recovery.
    writeDurable(completed(paths), "1\n");
    writeDurable(paths.preferences, "<?xml version=\"1.0\"?><PROPERTIES/>");
    if (std::filesystem::exists(paths.captures)) writeDurable(logState, "off\n\n\n\n");
    removeFile(marker(paths)); // Commit only after every operation succeeded.
}
}
