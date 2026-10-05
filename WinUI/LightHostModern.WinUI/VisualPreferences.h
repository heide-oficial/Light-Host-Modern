#pragma once
#include "UiPreferences.h"

namespace lightHostModern::ui
{
struct VisualPreferences
{
    bool hoverTooltips = loadUiSetting(L"General", L"HoverTooltips", L"1") != L"0";
    bool dottedCanvas = loadUiSetting(L"Chain", L"DottedBackground", L"0") == L"1";
    bool performance = loadUiSetting(L"General", L"PerformanceMode", L"0") == L"1";
    bool actionNotifications = loadUiSetting(L"General", L"ActionNotifications", L"1") != L"0";
    bool warningNotifications = loadUiSetting(L"General", L"WarningNotifications", L"1") != L"0";
    bool fade = loadUiSetting(L"Appearance", L"EdgeFade", L"1") != L"0";
    static VisualPreferences& current() { static VisualPreferences value; return value; }
};
}
