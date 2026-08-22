#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace LightHostWinUI
{
    class LocalizationCatalog
    {
    public:
        bool load(std::wstring const& languageCode);
        winrt::hstring text(std::string const& key, std::wstring const& fallback = {}) const;
        winrt::hstring format(
            std::string const& key,
            std::wstring const& fallback,
            std::vector<std::wstring> const& arguments) const;
        winrt::hstring translatedSource(winrt::hstring const& source) const;
        std::wstring languageCode() const;

        static std::vector<std::pair<std::wstring, std::wstring>> availableLanguages();

    private:
        std::wstring currentLanguageCode = L"en-us";
        std::unordered_map<std::string, winrt::hstring> strings;
        std::unordered_map<std::wstring, std::string> sourceKeys;
    };
}
