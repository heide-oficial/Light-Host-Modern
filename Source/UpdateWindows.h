#pragma once
#include "UpdateContract.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <msi.h>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "msi.lib")

namespace lightHostModern::update
{
inline void windowsCheck(bool ok, const char* code)
{ if (!ok) throw Error(code, std::string(code) + ": " + std::to_string(GetLastError())); }
// Versioned payloads add staging directories to otherwise ordinary install
// paths. Use extended absolute paths without requiring a machine-wide setting.
inline std::wstring extendedFilePath(const std::filesystem::path& path)
{
    auto value = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    if (value.rfind(LR"(\\?\)", 0) == 0) return value;
    if (value.rfind(LR"(\\)", 0) == 0) return LR"(\\?\UNC\)" + value.substr(2);
    return LR"(\\?\)" + value;
}
struct Handle
{
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept { reset(); value = std::exchange(other.value, nullptr); return *this; }
    void reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
class Sha256 final : public Digest
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
public:
    Sha256()
    {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "hash_failed");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; throw Error("hash_failed"); }
    }
    ~Sha256() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void append(const void* data, size_t count) override
    { require(BCryptHashData(hash, (PUCHAR)data, static_cast<ULONG>(count), 0) >= 0, "hash_failed"); }
    std::wstring finish() override
    {
        std::array<unsigned char, 32> bytes{};
        require(BCryptFinishHash(hash, bytes.data(), (ULONG)bytes.size(), 0) >= 0, "hash_failed");
        std::wstring result;
        for (auto b : bytes) { result += L"0123456789abcdef"[b >> 4]; result += L"0123456789abcdef"[b & 15]; }
        return result;
    }
};
class FileOutput final : public Output
{
    Handle file;
public:
    explicit FileOutput(const std::filesystem::path& path)
        : file(CreateFileW(extendedFilePath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr))
    { windowsCheck(bool(file), "storage_failed"); }
    void write(const void* bytes, size_t count) override
    {
        DWORD written = 0;
        windowsCheck(WriteFile(file.value, bytes, static_cast<DWORD>(count), &written, nullptr) && written == count, "storage_failed");
    }
    void flush() override { windowsCheck(FlushFileBuffers(file.value), "storage_failed"); }
};
class FileInput final : public Input
{
    Handle file;
public:
    explicit FileInput(const std::filesystem::path& path)
        : file(CreateFileW(extendedFilePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr))
    {
        windowsCheck(bool(file), "package_unavailable");
        BY_HANDLE_FILE_INFORMATION info{};
        windowsCheck(GetFileInformationByHandle(file.value, &info), "package_unavailable");
        require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)), "artifact_mismatch");
    }
    size_t read(void* bytes, size_t count) override
    { DWORD readBytes = 0; windowsCheck(ReadFile(file.value, bytes, static_cast<DWORD>(count), &readBytes, nullptr), "storage_failed"); return readBytes; }
};
struct InternetHandle
{
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
class HttpInput final : public Input
{
    InternetHandle session, connection, request;
public:
    explicit HttpInput(const std::wstring& url, uint64_t expectedBytes = 0)
    {
        URL_COMPONENTS parts{sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        windowsCheck(WinHttpCrackUrl(url.c_str(), 0, 0, &parts), "network_failed");
        require(parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == INTERNET_DEFAULT_HTTPS_PORT, "artifact_mismatch");
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        const std::wstring route = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength)
            + std::wstring(parts.lpszExtraInfo ? parts.lpszExtraInfo : L"", parts.dwExtraInfoLength);
        session.value = WinHttpOpen(L"LightHostModern/2.0.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        windowsCheck(session.value != nullptr, "network_failed");
        windowsCheck(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000), "network_failed");
        connection.value = WinHttpConnect(session.value, host.c_str(), parts.nPort, 0);
        windowsCheck(connection.value != nullptr, "network_failed");
        request.value = WinHttpOpenRequest(connection.value, L"GET", route.c_str(), nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        windowsCheck(request.value != nullptr, "network_failed");
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        windowsCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)), "network_failed");
        windowsCheck(WinHttpSendRequest(request.value, L"Accept: application/octet-stream\r\nAccept-Encoding: identity\r\n", DWORD(-1), nullptr, 0, 0, 0)
            && WinHttpReceiveResponse(request.value, nullptr), "network_failed");
        DWORD status = 0, length = sizeof(status);
        windowsCheck(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &length, nullptr), "network_failed");
        require(status == 200, "network_failed");
        wchar_t contentLength[32]{}; length = sizeof(contentLength);
        if (expectedBytes && WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, contentLength, &length, nullptr))
        {
            wchar_t* end = nullptr;
            const auto size = wcstoull(contentLength, &end, 10);
            require(end && !*end && size == expectedBytes, "size_mismatch");
        }
    }
    size_t read(void* buffer, size_t capacity) override
    { DWORD count = 0; windowsCheck(WinHttpReadData(request.value, buffer, static_cast<DWORD>(capacity), &count), "network_failed"); return count; }
};
inline std::wstring fileDigest(const std::filesystem::path& path)
{
    FileInput input(path); Sha256 digest;
    std::vector<unsigned char> buffer(transferCapacity);
    while (auto count = input.read(buffer.data(), buffer.size())) digest.append(buffer.data(), count);
    return digest.finish();
}
// Own one operation's two fixed filenames. No recursive cleanup or shared temp names.
inline std::filesystem::path download(const Artifact& artifact, const std::filesystem::path& operation,
    const std::atomic<bool>& cancelled, const ProgressCallback& progress = {}, Input* injectedInput = nullptr)
{
    artifact.validate(); checkCancelled(cancelled);
    const auto partial = operation / (artifact.name + L".partial"), final = operation / artifact.name;
    require(!std::filesystem::exists(partial) && !std::filesystem::exists(final), "operation_conflict");
    try
    {
        {
            std::unique_ptr<Input> network;
            if (!injectedInput) network = std::make_unique<HttpInput>(artifact.url, artifact.bytes);
            checkCancelled(cancelled);
            FileOutput output(partial); Sha256 digest;
            transfer(injectedInput ? *injectedInput : *network, output, digest, artifact, cancelled, progress);
        }
        checkCancelled(cancelled);
        windowsCheck(MoveFileExW(extendedFilePath(partial).c_str(), extendedFilePath(final).c_str(), MOVEFILE_WRITE_THROUGH), "storage_failed");
        return final;
    }
    catch (...) { DeleteFileW(extendedFilePath(partial).c_str()); throw; }
}
inline uint64_t processCreation(HANDLE process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    windowsCheck(GetProcessTimes(process, &created, &exited, &kernel, &user), "process_unavailable");
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline Handle processHandle(DWORD pid, uint64_t creation = 0)
{
    Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    windowsCheck(bool(process), "process_unavailable");
    require(!creation || processCreation(process.value) == creation, "process_mismatch");
    return process;
}
inline std::filesystem::path processPath(HANDLE process)
{
    std::wstring buffer(32768, L'\0'); DWORD size = static_cast<DWORD>(buffer.size());
    windowsCheck(QueryFullProcessImageNameW(process, 0, buffer.data(), &size), "process_unavailable");
    buffer.resize(size); return buffer;
}
inline std::wstring registryString(HKEY hive, const std::wstring& key, const wchar_t* name)
{
    DWORD size = 0;
    if (RegGetValueW(hive, key.c_str(), name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, nullptr, &size) != ERROR_SUCCESS || size > 65536) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(hive, key.c_str(), name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value.data(), &size) != ERROR_SUCCESS) return {};
    value.resize(wcsnlen(value.c_str(), value.size())); return value;
}
inline bool sameDirectory(const std::filesystem::path& host, const std::wstring& registered)
{
    if (registered.empty()) return false;
    std::error_code error;
    return std::filesystem::equivalent(host.parent_path(), std::filesystem::path(registered), error) && !error;
}
inline Distribution detectDistribution(const std::filesystem::path& host)
{
    for (DWORD index = 0;; ++index)
    {
        wchar_t product[39]{};
        if (MsiEnumRelatedProductsW(upgradeCode, 0, index, product) != ERROR_SUCCESS) break;
        DWORD count = 32768; std::wstring location(count, L'\0');
        if (MsiGetProductInfoW(product, L"InstallLocation", location.data(), &count) == ERROR_SUCCESS)
        { location.resize(count); if (sameDirectory(host, location)) return Distribution::installed; }
        // Earlier MSI packages did not set ARPINSTALLLOCATION. Their main component
        // key path is authoritative; a portable copy elsewhere must remain portable.
        wchar_t component[39]{};
        for (DWORD componentIndex = 0; MsiEnumComponentsW(componentIndex, component) == ERROR_SUCCESS; ++componentIndex)
        {
            DWORD pathSize = 32768; std::wstring keyPath(pathSize, L'\0');
            if (MsiGetComponentPathW(product, component, keyPath.data(), &pathSize) != INSTALLSTATE_LOCAL) continue;
            keyPath.resize(pathSize);
            std::error_code error;
            if (std::filesystem::equivalent(host, keyPath, error) && !error) return Distribution::installed;
        }
    }
    const std::wstring legacy = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\LightHostModern";
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        if (sameDirectory(host, registryString(hive, legacy, L"InstallLocation"))) return Distribution::installed;
    return Distribution::portable;
}
}
