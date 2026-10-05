#include "InstallerLocation.h"
#include <msiquery.h>

namespace
{
std::wstring property(MSIHANDLE session, const wchar_t* name)
{
    std::wstring value(32768, L'\0'); DWORD size = static_cast<DWORD>(value.size());
    if (MsiGetPropertyW(session, name, value.data(), &size) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot read installer property");
    value.resize(size); return value;
}
}
// Embedded in the MSI, so it runs before files are installed and works for both
// interactive and silent upgrades. It only reads registration and sets a property.
extern "C" __declspec(dllexport) UINT __stdcall PreserveInstallLocation(MSIHANDLE session)
{
    try {
        if (!property(session, L"APPLICATIONFOLDER").empty()) return ERROR_SUCCESS;
        const auto root = lightHostModern::installation::relatedRoot(property(session, L"UpgradeCode").c_str());
        return root.empty() ? ERROR_SUCCESS : MsiSetPropertyW(session, L"APPLICATIONFOLDER", root.c_str());
    } catch (...) {
        const auto record = MsiCreateRecord(1);
        if (record) {
            MsiRecordSetStringW(record, 0, L"LightHostModern could not determine the previous installation directory. Specify APPLICATIONFOLDER explicitly.");
            MsiProcessMessage(session, INSTALLMESSAGE_ERROR, record); MsiCloseHandle(record);
        }
        return ERROR_INSTALL_FAILURE;
    }
}
