#include "pch.h"
#include "AudioPageView.xaml.h"
#include "AudioPageView.g.cpp"
#include "MainWindow.xaml.h"

namespace winrt::LightHostModernWinUI::implementation
{
AudioPageView::AudioPageView() {
    InitializeComponent();using namespace Microsoft::UI::Xaml;
    AudioUnavailableNotice().Visibility(Visibility::Collapsed);
    AudioUnavailableNotice().RegisterPropertyChangedCallback(Controls::InfoBar::IsOpenProperty(),[](DependencyObject const& sender,DependencyProperty const&){const auto bar=sender.as<Controls::InfoBar>();bar.Visibility(bar.IsOpen()?Visibility::Visible:Visibility::Collapsed);});
}

}
