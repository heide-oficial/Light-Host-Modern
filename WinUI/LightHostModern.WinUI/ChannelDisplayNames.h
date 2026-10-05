#pragma once
#include "Localization.h"
#include <winrt/Windows.Data.Json.h>

namespace lightHostModern::ui
{
inline winrt::hstring audioChannelName(winrt::Windows::Data::Json::JsonArray const& original,
    winrt::Windows::Data::Json::JsonObject const& aliases, int channel, int width,
    bool input, ::LightHostModernWinUI::LocalizationCatalog const& catalog)
{
    using namespace winrt;
    const auto key=to_hstring(channel)+L":"+to_hstring(width);
    if(auto name=aliases.GetNamedString(key,L"");!name.empty())return name;
    const auto individual=[&](int index){
        auto name=aliases.GetNamedString(to_hstring(index)+L":1",L"");
        if(name.empty()&&index>=0&&index<static_cast<int>(original.Size()))name=original.GetStringAt(index);
        if(name.empty())name=catalog.format(input?"audio.inputChannel":"audio.outputChannel",L"Channel {0}",{std::to_wstring(index+1)});
        return name;
    };
    return width==2?individual(channel)+L" + "+individual(channel+1):individual(channel);
}
}
