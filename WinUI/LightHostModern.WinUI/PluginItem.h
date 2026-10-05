#pragma once
#include "PluginItem.g.h"

namespace winrt::LightHostModernWinUI::implementation
{
struct PluginItem : PluginItemT<PluginItem>
{
    PluginItem() = default;
    winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    { return changed.add(handler); }
    void PropertyChanged(winrt::event_token const& token) noexcept { changed.remove(token); }
#define LIGHTHOST_OBSERVABLE(Type, Name, Initial) \
    Type Name() const { return value##Name; } \
    void Name(Type const& value) { if (value##Name != value) { value##Name = value; changed(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(winrt::to_hstring(#Name))); } } \
    private: Type value##Name = Initial; public:
    LIGHTHOST_OBSERVABLE(hstring, Id, {})
    LIGHTHOST_OBSERVABLE(hstring, KnownId, {})
    LIGHTHOST_OBSERVABLE(hstring, Name, {})
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Media::Brush, CardTint, nullptr)
    LIGHTHOST_OBSERVABLE(hstring, Manufacturer, {})
    LIGHTHOST_OBSERVABLE(hstring, Format, {})
    LIGHTHOST_OBSERVABLE(hstring, Status, {})
    LIGHTHOST_OBSERVABLE(hstring, Position, {})
    LIGHTHOST_OBSERVABLE(hstring, AccessibleName, {})
    LIGHTHOST_OBSERVABLE(hstring, ActionName, {})
    LIGHTHOST_OBSERVABLE(hstring, Metadata, {})
    LIGHTHOST_OBSERVABLE(hstring, GroupHeading, {})
    LIGHTHOST_OBSERVABLE(bool, Running, false)
    LIGHTHOST_OBSERVABLE(bool, Bypassed, false)
    LIGHTHOST_OBSERVABLE(bool, CanReorder, false)
    LIGHTHOST_OBSERVABLE(int32_t, OriginalIndex, -1)
    LIGHTHOST_OBSERVABLE(double, RowHeight, 80.0)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Visibility, PositionVisibility, Microsoft::UI::Xaml::Visibility::Collapsed)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Visibility, GroupVisibility, Microsoft::UI::Xaml::Visibility::Collapsed)
    LIGHTHOST_OBSERVABLE(bool, IsGroupHeader, false)
    LIGHTHOST_OBSERVABLE(hstring, StateGlyph, {})
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Visibility, PluginVisibility, Microsoft::UI::Xaml::Visibility::Visible)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Visibility, BranchVisibility, Microsoft::UI::Xaml::Visibility::Collapsed)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Visibility, TailVisibility, Microsoft::UI::Xaml::Visibility::Collapsed)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::GridLength, BranchWidth, {})
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Style, StatusBadgeStyle, nullptr)
    LIGHTHOST_OBSERVABLE(Microsoft::UI::Xaml::Style, StatusIconStyle, nullptr)
#undef LIGHTHOST_OBSERVABLE
private:
    winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> changed;
};
}
namespace winrt::LightHostModernWinUI::factory_implementation
{
struct PluginItem : PluginItemT<PluginItem, implementation::PluginItem> {};
}
