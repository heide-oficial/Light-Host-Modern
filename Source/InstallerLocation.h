#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msi.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace lightHostModern::installation
{
inline bool samePath(const std::filesystem::path& first, const std::filesystem::path& second)
{
    auto normalize = [](const auto& path) {
        auto text = path.lexically_normal().make_preferred().wstring();
        while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
        return text;
    };
    const auto a = normalize(first), b = normalize(second);
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
inline bool validRoot(const std::filesystem::path& path)
{
    const auto text = path.wstring();
    return path.is_absolute() && !samePath(path, path.root_path())
        && text.find_first_of(L"\"\r\n") == std::wstring::npos;
}
inline std::filesystem::path productRoot(const wchar_t* product)
{
    DWORD count = 32768;
    std::wstring location(count, L'\0');
    if (MsiGetProductInfoW(product, L"InstallLocation", location.data(), &count) == ERROR_SUCCESS) {
        location.resize(count);
        if (validRoot(location)) return std::filesystem::path(location).lexically_normal();
    }
    // Older releases did not publish ARPINSTALLLOCATION. Their main executable
    // component is the authoritative fallback, including non-default folders.
    wchar_t component[39]{};
    for (DWORD index = 0; MsiEnumComponentsW(index, component) == ERROR_SUCCESS; ++index) {
        DWORD size = 32768;
        std::wstring keyPath(size, L'\0');
        if (MsiGetComponentPathW(product, component, keyPath.data(), &size) != INSTALLSTATE_LOCAL) continue;
        keyPath.resize(size);
        const std::filesystem::path file(keyPath);
        if ((samePath(file.filename(), L"LightHostModern.exe") || samePath(file.filename(), L"Light Host Modern.exe"))
            && validRoot(file.parent_path())) return file.parent_path().lexically_normal();
    }
    return {};
}
inline std::filesystem::path uniqueRoot(const std::vector<std::filesystem::path>& candidates)
{
    std::filesystem::path result;
    for (const auto& candidate : candidates) {
        if (!validRoot(candidate)) throw std::runtime_error("Invalid installed application directory");
        if (!result.empty() && !samePath(result, candidate))
            throw std::runtime_error("Several installation directories require an explicit selection");
        result = candidate.lexically_normal();
    }
    return result;
}
inline std::filesystem::path relatedRoot(const wchar_t* upgradeCode)
{
    std::vector<std::filesystem::path> candidates;
    for (DWORD index = 0;; ++index) {
        wchar_t product[39]{};
        const auto status = MsiEnumRelatedProductsW(upgradeCode, 0, index, product);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot query previous installation");
        if (MsiQueryProductStateW(product) != INSTALLSTATE_DEFAULT) continue;
        candidates.push_back(productRoot(product));
    }
    return uniqueRoot(candidates);
}
}
