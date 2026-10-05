#pragma once
#include "PluginsPageView.g.h"
#include "ScrollEdgeFade.h"

namespace winrt::LightHostModernWinUI::implementation
{
struct PluginsPageView : PluginsPageViewT<PluginsPageView>
{
    PluginsPageView();
    void refreshSearchAccessibility();
    void configureEffects(bool contrast);
    std::shared_ptr<lightHostModern::ui::ScrollEdgeFade> runningFade,installedFade;
    std::function<void(Microsoft::UI::Xaml::Controls::Button)> catalogActions;
    void setContentInsets(double inset);
    double contentInset = 24;
    void Toolbar_SizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
    void PluginCard_PointerEntered(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
    void PluginCard_PointerExited(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> owner;
    void GlobalAudioControl_Click(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::RoutedEventArgs arg1);
    void PluginActions_Click(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::RoutedEventArgs const& arg1);
    void RunningPluginItem_DragOver(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::DragEventArgs const& arg1);
    void RunningPluginItem_Drop(winrt::Windows::Foundation::IInspectable arg0, Microsoft::UI::Xaml::DragEventArgs arg1);
    void RunningPluginsListView_DragItemsCompleted(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& arg1);
    void RunningPluginsListView_DragItemsStarting(winrt::Windows::Foundation::IInspectable const& arg0, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& arg1);
};
}
namespace winrt::LightHostModernWinUI::factory_implementation
{
struct PluginsPageView : PluginsPageViewT<PluginsPageView, implementation::PluginsPageView> {};
}
