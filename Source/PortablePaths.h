#pragma once
#include <filesystem>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace lightHostModern::update
{
// Payload-relative resources (UI/scanner/helper) stay beside the host. Only
// shortcuts, restart and update activation use this stable entry point.
inline std::filesystem::path portableRoot(const std::filesystem::path& host)
{
    const auto payload = host.parent_path();
    if (payload.parent_path().filename() == L"versions"
        && std::filesystem::is_regular_file(payload.parent_path().parent_path() / L"portable-layout.json"))
        return payload.parent_path().parent_path();
    return {};
}
inline std::filesystem::path launchEntry(const std::filesystem::path& host)
{
    const auto root = portableRoot(host);
    return root.empty() ? host : root / L"LightHostModern.exe";
}
inline bool portableDurabilitySupported(const std::filesystem::path& root)
{
    wchar_t volume[MAX_PATH]{},format[MAX_PATH]{};
    return !root.empty()&&GetVolumePathNameW(root.c_str(),volume,MAX_PATH)
        &&GetVolumeInformationW(volume,nullptr,0,nullptr,nullptr,nullptr,format,MAX_PATH)
        &&_wcsicmp(format,L"NTFS")==0;
}
inline void retainRunningPayload(const std::filesystem::path& host)
{
    if(portableRoot(host).empty())return;
    struct Lease { HANDLE handle=INVALID_HANDLE_VALUE;~Lease(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);} };
    static Lease lease;
    if(lease.handle==INVALID_HANDLE_VALUE)lease.handle=CreateFileW((host.parent_path()/L"payload-manifest.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
}
}
