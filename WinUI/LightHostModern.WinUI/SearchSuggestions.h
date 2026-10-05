#pragma once
#include "SurfaceMaterials.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace lightHostModern::ui
{
// Suggestions are passive content. The AutoSuggestBox's own item container
// owns pointer selection, keyboard navigation and the themed hover states.
inline void dismissSuggestionsOutside(winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox const& search)
{
    SurfaceMaterials::current().watch(search);
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    // The plugin page makes list-card hover surfaces transparent. Restore
    // native suggestion feedback locally so that it does not inherit that rule.
    for(auto key:{L"ListViewItemBackgroundPointerOver",L"ListViewItemBackgroundSelectedPointerOver",L"ListViewItemBackgroundPressed"})
        search.Resources().Insert(box_value(key),Application::Current().Resources().Lookup(box_value(L"AppControlBrush")));
    search.Loaded([weak=make_weak(search)](auto const&, auto const&) {
        auto box=weak.get();if(!box||!box.XamlRoot()||box.Tag())return;
        box.Tag(box_value(true));
        box.XamlRoot().Content().AddHandler(UIElement::PointerPressedEvent(), box_value(Input::PointerEventHandler(
            [weak](auto const&, Input::PointerRoutedEventArgs const& e) {
                auto box=weak.get();if(!box||!box.IsSuggestionListOpen())return;
                auto element=e.OriginalSource().try_as<DependencyObject>();
                while(element){if(element==box)return;element=Media::VisualTreeHelper::GetParent(element);}
                box.IsSuggestionListOpen(false);
            })), true);
    });
}
}
