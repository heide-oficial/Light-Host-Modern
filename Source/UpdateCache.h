#pragma once
#include "UpdateWindows.h"
#include <fstream>
#include <chrono>

namespace lightHostModern::update
{
inline bool cacheTreeSafe(const std::filesystem::path& path)
{
    auto parent=std::filesystem::absolute(path).lexically_normal();
    for(;;){const auto attributes=GetFileAttributesW(parent.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        const auto next=parent.parent_path();if(next==parent)break;parent=next;}
    std::error_code error;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(path,error)){
        const auto attributes=GetFileAttributesW(entry.path().c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return !error;
}
inline Handle updateCacheLease(const std::filesystem::path& operation,bool helper=false)
{
    require(cacheTreeSafe(operation),"unsafe_update_path");
    const auto file=operation/(helper?L".helper-active":L".ui-active");
    Handle lease(CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    windowsCheck(bool(lease),"operation_conflict");return lease;
}
inline void markUpdateCache(const std::filesystem::path& operation)
{
    FileOutput marker(operation/L".lighthost-update-cache");marker.write("1",1);marker.flush();
}
// Only directories created by this cache are eligible. Exclusive delete access
// to both lease files prevents cleanup while either process owns the operation.
inline void pruneUpdateCache(const std::filesystem::path& parent)
{
    try {
        if(!std::filesystem::exists(parent)||!cacheTreeSafe(parent))return;
        std::vector<std::filesystem::directory_entry> candidates;
        for(const auto& entry:std::filesystem::directory_iterator(parent)){
            const auto name=entry.path().filename().wstring();
            if(!entry.is_directory()||name.size()!=38||name.front()!=L'{'||name.back()!=L'}')continue;
            if(!std::filesystem::is_regular_file(entry.path()/L".lighthost-update-cache"))continue;
            candidates.push_back(entry);
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
        const auto now=std::filesystem::file_time_type::clock::now();
        for(size_t index=0;index<candidates.size();++index){
            const auto path=candidates[index].path();
            if(index<3&&now-candidates[index].last_write_time()<std::chrono::hours(24*7))continue;
            if(path.parent_path()!=parent||!cacheTreeSafe(path))continue;
            Handle ui(CreateFileW((path/L".ui-active").c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            if(!ui)continue;
            Handle helper(CreateFileW((path/L".helper-active").c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            if(!helper)continue;
            // Keep the leases open with delete sharing through removal, so no
            // updater can acquire a conflicting write lease midway through it.
            std::error_code ignored;std::filesystem::remove_all(path,ignored);
        }
    }catch(...){} // Cache maintenance must never prevent update notifications.
}
}
