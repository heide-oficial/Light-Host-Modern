#pragma once
#include "UpdateWindows.h"
#include "BoundedInput.h"
#include "StartupRegistration.h"
#include <shlobj.h>
#include <juce_core/juce_core.h>
#include <fstream>
#include <set>

namespace lightHostModern::migration
{
// No recursive deletion and no execution of the legacy uninstaller. Unknown
// files stay in place. Cleanup runs as the installing user after MSI commit.
inline bool safePath(const std::filesystem::path& path)
{
    if (!path.is_absolute() || path == path.root_path() || path.lexically_normal() != path) return false;
    for (auto part = path; part != part.root_path() && !part.empty(); part = part.parent_path()) {
        const auto flags = GetFileAttributesW(part.c_str());
        if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}
inline bool validRelative(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) if (part == L".." || part == L"." || part.wstring().find(L':') != std::wstring::npos) return false;
    return true;
}
inline std::wstring registryText(HKEY key, const wchar_t* name)
{
    wchar_t text[32768]{}; DWORD bytes = sizeof(text), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(text), &bytes) != ERROR_SUCCESS || type != REG_SZ || bytes >= sizeof(text)) return {};
    return text;
}
inline bool legacyExecutable(const std::filesystem::path& executable, const std::wstring& version)
{
    DWORD ignored = 0; const auto length = GetFileVersionInfoSizeW(executable.c_str(), &ignored);
    if (!length || length > 1024 * 1024) return false;
    std::vector<unsigned char> data(length);
    if (!GetFileVersionInfoW(executable.c_str(), 0, length, data.data())) return false;
    VS_FIXEDFILEINFO* fixed = nullptr; UINT size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &size) || size < sizeof(*fixed)) return false;
    const auto expected = update::parseVersion(version);
    if (!expected || (*expected)[0] != 1 || (*expected)[1] != 2 || HIWORD(fixed->dwFileVersionMS) != 1
        || LOWORD(fixed->dwFileVersionMS) != 2 || HIWORD(fixed->dwFileVersionLS) != (*expected)[2]) return false;
    struct Language { WORD language, page; }; Language* languages = nullptr;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&languages), &size)) return false;
    for (size_t i = 0; i < size / sizeof(Language); ++i) {
        wchar_t query[96]{}; swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\ProductName", languages[i].language, languages[i].page);
        wchar_t* name = nullptr; UINT count = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&name), &count) && count && std::wstring(name) == L"Light Host Modern") return true;
    }
    return false;
}
inline bool copyThenRemove(const std::filesystem::path& from, const std::filesystem::path& backup)
{
    if (!safePath(from) || !safePath(backup)) return false;
    std::error_code error;
    std::filesystem::create_directories(backup.parent_path(), error); if (error) return false;
    if (!CopyFileW(from.c_str(), backup.c_str(), TRUE)) return false;
    const auto handle = CreateFileW(backup.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    const bool flushed = handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!flushed) return false;
    const auto digest = [](const std::filesystem::path& file) { return update::fileDigest(file); };
    // Keep a backup before removing even an identified payload file.
    if (digest(from) != digest(backup)) return false;
    return DeleteFileW(from.c_str()) != FALSE;
}
inline int run(const std::filesystem::path& currentRoot)
{
    using namespace std::filesystem;
    constexpr auto keyName = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\LightHostModern";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;
    const auto location = registryText(key, L"InstallLocation"), name = registryText(key, L"DisplayName"), version = registryText(key, L"DisplayVersion");
    RegCloseKey(key);
    const path root = path(location).lexically_normal();
    if (name != L"Light Host Modern" || !safePath(root) || !safePath(currentRoot) || root == currentRoot
        || !legacyExecutable(root / L"Light Host Modern.exe", version)) return 1;
    const auto relativeNew = currentRoot.lexically_relative(root);
    if (!relativeNew.empty() && *relativeNew.begin() != L"..") return 1;
    const auto inventory = lightHostModern::parseBoundedJson(juce::File(juce::String((currentRoot / L"legacy-payload-files.json").c_str())));
    if (!inventory.isArray() || inventory.size() > 20000) return 1;
    wchar_t local[32768]{}; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) return 1;
    const path backup = path(local) / L"LightHostModern/Migrations" / (std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId()));
    if (!safePath(backup)) return 1;
    std::error_code error; create_directories(backup, error); if (error) return 1;
    std::ofstream log(backup / L"migration.log"); log << "Legacy root: " << root.u8string() << '\n';
    std::set<path> directories;
    bool failed = false;
    // Keep the legacy entry point until all other identified files are backed up.
    std::vector<path> files;
    for (const auto& entry : *inventory.getArray()) {
        path relative(entry.toString().toWideCharPointer());
        if (!validRelative(relative)) return 1;
        if (relative != L"Light Host Modern.exe") files.push_back(relative);
    }
    files.push_back(L"Light Host Modern.exe");
    for (const auto& relative : files) {
        if (failed && relative == L"Light Host Modern.exe") break;
        const auto file = root / relative;
        if (!exists(file, error)) continue;
        if (!is_regular_file(file, error) || !copyThenRemove(file, backup / relative)) { failed = true; log << "Preserved: " << relative.u8string() << '\n'; continue; }
        for (auto parent = file.parent_path(); parent != root && parent != parent.root_path(); parent = parent.parent_path()) directories.insert(parent);
    }
    // RemoveDirectory only succeeds for empty directories. User content remains.
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) if (safePath(*it)) RemoveDirectoryW(it->c_str());
    if (!failed) {
        migrateStartupRegistration(root / L"Light Host Modern.exe", currentRoot / L"LightHostModern.exe");
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // Resolve the shortcut target; never remove a link just because of its name.
        for (auto folder : {CSIDL_DESKTOPDIRECTORY, CSIDL_PROGRAMS}) {
            wchar_t directory[MAX_PATH]{};
            if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, directory))) continue;
            const path shortcut = path(directory) / (folder == CSIDL_PROGRAMS ? L"Light Host Modern/Light Host Modern.lnk" : L"Light Host Modern.lnk");
            IShellLinkW* link = nullptr; IPersistFile* persistence = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
                if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persistence)))) {
                    wchar_t target[32768]{};
                    if (SUCCEEDED(persistence->Load(shortcut.c_str(), STGM_READ)) && SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH))
                        && _wcsicmp(target, (root / L"Light Host Modern.exe").c_str()) == 0) {
                        DeleteFileW(shortcut.c_str());
                        if (folder == CSIDL_PROGRAMS) RemoveDirectoryW(shortcut.parent_path().c_str());
                    }
                    persistence->Release();
                }
                link->Release();
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
        RegDeleteKeyW(HKEY_CURRENT_USER, keyName);
        RemoveDirectoryW(root.c_str());
    }
    log << (failed ? "Incomplete; backup and original entry point retained.\n" : "Known payload migrated. Unknown files, if any, retained.\n");
    return failed ? 1 : 0;
}
}
