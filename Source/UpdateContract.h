#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lightHostModern::update
{
enum class Distribution { installed, portable };
inline constexpr wchar_t upgradeCode[] = L"{8F28E61C-DC90-4927-B7B4-3E74E4B5960B}";
inline constexpr uint64_t maximumPackageBytes = uint64_t{2} * 1024 * 1024 * 1024;
inline constexpr size_t transferCapacity = 256 * 1024;
struct Error : std::runtime_error
{
    std::string code;
    explicit Error(std::string reason, std::string detail = {})
        : std::runtime_error(detail.empty() ? reason : detail), code(std::move(reason)) {}
};
inline void require(bool condition, const char* code) { if (!condition) throw Error(code); }
inline std::optional<std::array<unsigned, 3>> parseVersion(std::wstring text)
{
    if (!text.empty() && (text.front() == L'v' || text.front() == L'V')) text.erase(text.begin());
    std::array<unsigned, 3> result{};
    size_t at = 0;
    for (size_t part = 0; part < 3; ++part)
    {
        const auto start = at;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9')
        {
            if (result[part] > 6553) return {};
            result[part] = result[part] * 10 + unsigned(text[at++] - L'0');
            if (result[part] > 65535) return {};
        }
        if (at == start || (at - start > 1 && text[start] == L'0')) return {};
        if (part < 2 && (at == text.size() || text[at++] != L'.')) return {};
    }
    return at == text.size() ? std::optional{result} : std::nullopt;
}
inline std::wstring versionedArtifactName(Distribution value, std::wstring version)
{
    require(parseVersion(version).has_value(), "version_mismatch");
    if (!version.empty() && (version.front() == L'v' || version.front() == L'V')) version.erase(0, 1);
    return value == Distribution::installed ? L"LightHostModern-" + version + L"-Setup.msi"
                                           : L"LightHostModern-v" + version + L"-Portable.zip";
}
inline bool artifactNameAllowed(Distribution value, const std::wstring& version, const std::wstring& name)
{ return name == versionedArtifactName(value, version); }
inline std::wstring normalizedDigest(std::wstring value)
{
    if (value.rfind(L"sha256:", 0) == 0) value.erase(0, 7);
    require(value.size() == 64, "checksum_unavailable");
    for (auto& c : value)
    {
        if (c >= L'A' && c <= L'F') c += L'a' - L'A';
        require((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'), "checksum_unavailable");
    }
    return value;
}
struct Artifact
{
    Distribution distribution = Distribution::portable;
    std::wstring version, architecture = L"x64", name, url, digest;
    uint64_t bytes = 0;
    void validate() const
    {
        require(parseVersion(version).has_value(), "version_mismatch");
        require(architecture == L"x64", "architecture_mismatch");
        require(artifactNameAllowed(distribution, version, name), "artifact_mismatch");
        require(bytes > 0 && bytes <= maximumPackageBytes, "size_mismatch");
        (void) normalizedDigest(digest);
        const std::wstring base = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/";
        require(url.rfind(base, 0) == 0 && url == base + version + L"/" + name, "artifact_mismatch");
    }
};
struct Progress { uint64_t received = 0, expected = 0; };
using ProgressCallback = std::function<void(Progress)>;
inline void checkCancelled(const std::atomic<bool>& cancelled)
{ if (cancelled.load(std::memory_order_relaxed)) throw Error("cancelled"); }
struct Input
{
    virtual ~Input() = default;
    virtual size_t read(void*, size_t) = 0;
};
struct Output
{
    virtual ~Output() = default;
    virtual void write(const void*, size_t) = 0;
    virtual void flush() = 0;
};
struct Digest
{
    virtual ~Digest() = default;
    virtual void append(const void*, size_t) = 0;
    virtual std::wstring finish() = 0;
};
inline void transfer(Input& source, Output& sink, Digest& digest, const Artifact& artifact,
                     const std::atomic<bool>& cancelled, const ProgressCallback& progress = {})
{
    artifact.validate();
    std::vector<unsigned char> buffer(transferCapacity);
    uint64_t received = 0;
    if (progress) progress({0, artifact.bytes});
    for (;;)
    {
        checkCancelled(cancelled);
        const auto count = source.read(buffer.data(), buffer.size());
        checkCancelled(cancelled);
        require(count <= buffer.size() && count <= artifact.bytes - received, "size_mismatch");
        if (!count) break;
        sink.write(buffer.data(), count);
        digest.append(buffer.data(), count);
        received += count;
        if (progress) progress({received, artifact.bytes});
    }
    require(received == artifact.bytes, "size_mismatch");
    require(digest.finish() == normalizedDigest(artifact.digest), "checksum_mismatch");
    checkCancelled(cancelled);
    sink.flush();
}
inline const char* installerOutcome(uint32_t code)
{
    if (code == 0) return "completed";
    if (code == 1602) return "cancelled";
    if (code == 3010 || code == 1641) return "restart_required";
    return "install_failed";
}
// Windows command-line quoting, including terminal backslashes before the quote.
inline std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : value)
    {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
        if (c == L'\"') result += L'\\';
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
}
