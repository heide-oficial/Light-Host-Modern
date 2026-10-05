#pragma once
#include "Localization.h"
#include "DialogPresentation.h"
#include "SurfaceMaterials.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <memory>

namespace lightHostModern::ui
{
inline void focusDialogTextBox(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& dialog,
    winrt::Microsoft::UI::Xaml::FrameworkElement const& owner, winrt::Microsoft::UI::Xaml::Controls::TextBox const& input)
{
    dialog.Opened([weakOwner=winrt::make_weak(owner),weakInput=winrt::make_weak(input)](const auto& sender,const auto&){
        // Material attachment reparents the dialog content. Finish that before
        // assigning focus so the next material poll cannot take it away.
        if(auto root=weakOwner.get())SurfaceMaterials::current().poll(root,true);
        // Reparenting invalidates IsLoaded until the next layout pass. A
        // dispatcher callback alone can run too early and silently skip focus.
        auto token=std::make_shared<winrt::event_token>();
        auto pending=std::make_shared<bool>(true);
        auto focus=[weakInput,token,pending]{
            if(!*pending)return;
            if(auto box=weakInput.get();box&&box.IsLoaded()&&box.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic)){
                *pending=false;box.LayoutUpdated(*token);box.SelectAll();
            }
        };
        if(auto box=weakInput.get())*token=box.LayoutUpdated([focus](const auto&,const auto&){focus();});
        sender.Closed([weakInput,token,pending](const auto&,const auto&){
            if(*pending){*pending=false;if(auto box=weakInput.get())box.LayoutUpdated(*token);}
        });
        sender.DispatcherQueue().TryEnqueue(focus);
    });
}
inline winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IInspectable> editDisplayName(
    winrt::Microsoft::UI::Xaml::FrameworkElement owner,
    ::LightHostModernWinUI::LocalizationCatalog catalog, winrt::hstring title, winrt::hstring current)
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    ContentDialog dialog; dialog.XamlRoot(owner.XamlRoot()); dialog.RequestedTheme(owner.ActualTheme());
    dialog.Title(box_value(title));
    dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(760.0));
    TextBox name; name.Text(current); name.MaxLength(128); name.Header(box_value(catalog.translatedSource(L"Name")));
    name.MinWidth(560); dialog.Content(name); dialog.PrimaryButtonText(catalog.translatedSource(L"Save"));
    dialog.SecondaryButtonText(catalog.translatedSource(L"Restore original name"));
    dialog.CloseButtonText(catalog.translatedSource(L"Cancel")); dialog.DefaultButton(ContentDialogButton::Primary);
    focusDialogTextBox(dialog,owner,name);
    const auto result = co_await lightHostModern::ui::showAppDialog(dialog);
    if (result == ContentDialogResult::Secondary) co_return box_value(hstring{});
    if (result == ContentDialogResult::Primary) co_return box_value(name.Text());
    co_return nullptr;
}
}
