#include "pch.h"
#include "Localization.h"

using namespace winrt;
using namespace Windows::Data::Json;

namespace
{
    std::filesystem::path executableDirectory()
    {
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }

    std::string readUtf8File(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return {};

        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    bool loadFile(std::filesystem::path const& path,
                  std::unordered_map<std::string, hstring>& destination)
    {
        const auto contents = readUtf8File(path);
        if (contents.empty())
            return false;

        try
        {
            const auto json = JsonObject::Parse(to_hstring(contents));
            for (auto const& entry : json)
            {
                if (entry.Value().ValueType() == JsonValueType::String)
                    destination[to_string(entry.Key())] = entry.Value().GetString();
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

namespace LightHostWinUI
{
    bool LocalizationCatalog::load(std::wstring const& languageCode)
    {
        strings.clear();
        sourceKeys.clear();
        const auto locales = executableDirectory() / L"Locales";
        const auto fallbackLoaded = loadFile(locales / L"en-us.json", strings);

        std::error_code error;
        for (auto const& entry : std::filesystem::directory_iterator(locales, error))
        {
            if (error || !entry.is_regular_file() || entry.path().extension() != L".json")
                continue;

            std::unordered_map<std::string, hstring> localeStrings;
            if (!loadFile(entry.path(), localeStrings))
                continue;
            for (auto const& [key, value] : localeStrings)
                sourceKeys.emplace(value.c_str(), key);
        }

        currentLanguageCode = languageCode.empty() ? L"en-us" : languageCode;
        if (_wcsicmp(currentLanguageCode.c_str(), L"en-us") != 0)
            loadFile(locales / (currentLanguageCode + L".json"), strings);

        return fallbackLoaded;
    }

    hstring LocalizationCatalog::text(std::string const& key, std::wstring const& fallback) const
    {
        const auto match = strings.find(key);
        if (match != strings.end())
            return match->second;
        return hstring(fallback);
    }

    hstring LocalizationCatalog::translatedSource(hstring const& source) const
    {
        const auto match = sourceKeys.find(source.c_str());
        return match == sourceKeys.end() ? source : text(match->second, source.c_str());
    }

    std::wstring LocalizationCatalog::languageCode() const
    {
        return currentLanguageCode;
    }

    std::vector<std::pair<std::wstring, std::wstring>> LocalizationCatalog::availableLanguages()
    {
        std::vector<std::pair<std::wstring, std::wstring>> languages;
        const auto locales = executableDirectory() / L"Locales";
        std::error_code error;
        for (auto const& entry : std::filesystem::directory_iterator(locales, error))
        {
            if (error || !entry.is_regular_file() || entry.path().extension() != L".json")
                continue;

            auto displayName = entry.path().stem().wstring();
            try
            {
                const auto contents = readUtf8File(entry.path());
                const auto json = JsonObject::Parse(to_hstring(contents));
                if (json.HasKey(L"language.name"))
                    displayName = json.GetNamedString(L"language.name").c_str();
            }
            catch (...) {}
            languages.emplace_back(entry.path().stem().wstring(), std::move(displayName));
        }

        if (languages.empty())
            languages.emplace_back(L"en-us", L"English (United States)");
        std::sort(languages.begin(), languages.end());
        return languages;
    }
}
