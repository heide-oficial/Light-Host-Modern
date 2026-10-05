#pragma once
#include "Localization.h"
#include "DialogPresentation.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace lightHostModern::ui
{
inline winrt::Windows::Foundation::IAsyncOperation<bool> confirmDanger(
    winrt::Microsoft::UI::Xaml::FrameworkElement owner,
    ::LightHostModernWinUI::LocalizationCatalog catalog, winrt::hstring title, winrt::hstring message,
    winrt::hstring accept = L"Confirm")
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    ContentDialog dialog;dialog.XamlRoot(owner.XamlRoot());dialog.RequestedTheme(owner.ActualTheme());
    dialog.Title(box_value(title));TextBlock body;body.Text(message);body.TextWrapping(TextWrapping::Wrap);dialog.Content(body);
    const auto label=catalog.translatedSource(accept);dialog.PrimaryButtonText(label+L" (3)");dialog.IsPrimaryButtonEnabled(false);
    dialog.CloseButtonText(catalog.translatedSource(L"Cancel"));dialog.DefaultButton(ContentDialogButton::Close);
    DispatcherTimer timer;timer.Interval(std::chrono::milliseconds(100));
    uint64_t opened=0;
    dialog.Opened([&](const auto&,const auto&){opened=GetTickCount64();timer.Start();});
    timer.Tick([&](const auto&,const auto&){const auto elapsed=GetTickCount64()-opened;
        if(elapsed>=3000){timer.Stop();dialog.PrimaryButtonText(label);dialog.IsPrimaryButtonEnabled(true);}
        else dialog.PrimaryButtonText(label+L" ("+to_hstring(3-elapsed/1000)+L")");});
    ContentDialogResult result=ContentDialogResult::None;
    try{result=co_await lightHostModern::ui::showAppDialog(dialog);}catch(...){timer.Stop();throw;}
    timer.Stop();co_return result==ContentDialogResult::Primary;
}
}
