#include "pch.h"
#include "Localization.h"
#include <cwctype>

using namespace winrt;
using namespace Windows::Data::Json;

namespace
{
    constexpr wchar_t DefaultLanguage[] = L"en-us";

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

    std::filesystem::path localizationDirectory()
    {
        return executableDirectory() / L"Locales";
    }

    std::wstring normalizeLanguageCode(std::wstring languageCode)
    {
        const auto first = languageCode.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            return DefaultLanguage;

        const auto last = languageCode.find_last_not_of(L" \t\r\n");
        languageCode = languageCode.substr(first, last - first + 1);
        std::replace(languageCode.begin(), languageCode.end(), L'_', L'-');
        std::transform(languageCode.begin(), languageCode.end(), languageCode.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(std::towlower(value));
        });
        return languageCode;
    }
}

namespace LightHostWinUI
{
    bool LocalizationCatalog::load(std::wstring const& languageCode)
    {
        strings.clear();
        sourceKeys.clear();
        const auto locales = localizationDirectory();
        const auto fallbackLoaded = loadFile(locales / (std::wstring(DefaultLanguage) + L".json"), strings);

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

        currentLanguageCode = normalizeLanguageCode(languageCode);
        if (_wcsicmp(currentLanguageCode.c_str(), DefaultLanguage) != 0)
        {
            std::unordered_map<std::string, hstring> localizedStrings;
            if (loadFile(locales / (currentLanguageCode + L".json"), localizedStrings))
            {
                for (auto const& [key, value] : localizedStrings)
                    strings[key] = value;
            }
            else
            {
                currentLanguageCode = DefaultLanguage;
            }
        }

        return fallbackLoaded;
    }

    hstring LocalizationCatalog::text(std::string const& key, std::wstring const& fallback) const
    {
        const auto match = strings.find(key);
        if (match != strings.end())
            return match->second;
        return hstring(fallback);
    }

    hstring LocalizationCatalog::format(
        std::string const& key,
        std::wstring const& fallback,
        std::vector<std::wstring> const& arguments) const
    {
        std::wstring result = text(key, fallback).c_str();
        for (size_t index = 0; index < arguments.size(); ++index)
        {
            const auto marker = L"{" + std::to_wstring(index) + L"}";
            size_t position = 0;
            while ((position = result.find(marker, position)) != std::wstring::npos)
            {
                result.replace(position, marker.size(), arguments[index]);
                position += arguments[index].size();
            }
        }
        return hstring(result);
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
        const auto locales = localizationDirectory();
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
                if (json.HasKey(L"Language.DisplayName"))
                    displayName = json.GetNamedString(L"Language.DisplayName").c_str();
                else if (json.HasKey(L"language.name"))
                    displayName = json.GetNamedString(L"language.name").c_str();
            }
            catch (...) {}
            languages.emplace_back(normalizeLanguageCode(entry.path().stem().wstring()), std::move(displayName));
        }

        if (languages.empty())
            languages.emplace_back(DefaultLanguage, L"English (United States)");
        std::sort(languages.begin(), languages.end(), [](auto const& left, auto const& right)
        {
            const bool leftIsDefault = _wcsicmp(left.first.c_str(), DefaultLanguage) == 0;
            const bool rightIsDefault = _wcsicmp(right.first.c_str(), DefaultLanguage) == 0;
            if (leftIsDefault != rightIsDefault)
                return leftIsDefault;
            return _wcsicmp(left.second.c_str(), right.second.c_str()) < 0;
        });
        return languages;
    }
}
