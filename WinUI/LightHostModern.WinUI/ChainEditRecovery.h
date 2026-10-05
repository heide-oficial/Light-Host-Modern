#pragma once
#include "UiPreferences.h"
#include <fstream>
#include <filesystem>
#include "HostJson.h"

namespace lightHostModern::ui
{
inline std::filesystem::path chainRecoveryPath(winrt::hstring const& profile)
{
    const std::wstring id(profile);
    if (id.size()!=32 || id.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos) return {};
    return std::filesystem::path(uiSettingsFilePath()).parent_path() / (L"chain-edit-recovery-"+id+L".json");
}
inline bool writeChainRecovery(winrt::hstring const& profile,winrt::Windows::Data::Json::JsonObject const& document) noexcept
{
    try {
        const auto file=chainRecoveryPath(profile);if(file.empty())return false;
        const auto bytes=winrt::to_string(document.Stringify());if(bytes.size()>4*1024*1024)return false;
        const auto pending=std::filesystem::path(file.wstring()+L".pending");
        const auto handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return false;
        DWORD written=0;
        const bool complete=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(handle);
        CloseHandle(handle);
        return complete&&MoveFileExW(pending.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    } catch(...) {return false;}
}
inline void clearChainRecovery(winrt::hstring const& profile) noexcept
{
    try{const auto file=chainRecoveryPath(profile);if(!file.empty())DeleteFileW(file.c_str());}catch(...){}
}
inline winrt::Windows::Data::Json::JsonObject readChainRecovery(winrt::hstring const& profile) noexcept
{
    using winrt::Windows::Data::Json::JsonObject;
    try {
        const auto path=chainRecoveryPath(profile);if(path.empty()||!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>4*1024*1024)return {};
        std::ifstream stream(path,std::ios::binary);const auto value=ipc::parseObject(std::string(std::istreambuf_iterator<char>(stream),{}));
        if(value.GetNamedString(L"profileId",L"")!=profile||!value.HasKey(L"graph"))return {};
        return value;
    }catch(...){return JsonObject{};}
}
}
