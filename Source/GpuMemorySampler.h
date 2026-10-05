#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <set>
#include <string_view>
#include <vector>
#pragma comment(lib, "pdh.lib")

namespace lightHostModern
{
// Called serially on a background thread, only while a resource view is visible.
// English counter paths work independently of the Windows display language.
class GpuMemorySampler
{
public:
    GpuMemorySampler() = default;
    GpuMemorySampler(const GpuMemorySampler&) = delete;
    GpuMemorySampler& operator=(const GpuMemorySampler&) = delete;
    ~GpuMemorySampler() { close(); }

    std::optional<uint64_t> sample(const std::set<DWORD>& processes)
    {
        if (!query) {
            if (GetTickCount64() < retryAfter) return {};
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS
                || PdhAddEnglishCounterW(query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &counter) != ERROR_SUCCESS) {
                close(); retryAfter = GetTickCount64() + 30000; return {};
            }
        }
        if (PdhCollectQueryData(query) != ERROR_SUCCESS) return {};
        constexpr DWORD format = PDH_FMT_LARGE | PDH_FMT_NOSCALE;
        // Instances may appear between sizing and retrieval; retry with a fresh
        // size rather than trusting the buffer size from a failed second call.
        for (int attempt = 0; attempt < 3; ++attempt) {
            DWORD bytes = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != PDH_MORE_DATA
                || !bytes || bytes > 8 * 1024 * 1024) return {};
            std::vector<uint64_t> storage((bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t));
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
            const auto status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items);
            if (status == PDH_MORE_DATA) continue;
            if (status != ERROR_SUCCESS || count > bytes / sizeof(*items)) return {};
            uint64_t total = 0;
            bool valid = false;
            for (DWORD i = 0; i < count; ++i) {
                const auto& item = items[i];
                if (!item.szName || std::wstring_view(item.szName).substr(0, 4) != L"pid_") continue;
                wchar_t* end = nullptr;
                const auto pid = wcstoul(item.szName + 4, &end, 10);
                if (end == item.szName + 4 || *end != L'_' || processes.find(pid) == processes.end()) continue;
                if ((item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
                    || item.FmtValue.largeValue < 0) return {};
                total += static_cast<uint64_t>(item.FmtValue.largeValue); valid = true;
            }
            // Missing/unsupported data is not a measured zero.
            return valid ? std::optional<uint64_t>(total) : std::nullopt;
        }
        return {};
    }
private:
    void close() { if (query) PdhCloseQuery(query); query = nullptr; counter = nullptr; }
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    uint64_t retryAfter = 0;
};
}
