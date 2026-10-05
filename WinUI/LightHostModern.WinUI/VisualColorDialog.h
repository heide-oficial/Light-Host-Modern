#pragma once
#include "HoverHelp.h"
#include "DialogPresentation.h"
#include "Localization.h"
#include "UiPreferences.h"
#include <winrt/Windows.Data.Json.h>
#include <memory>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <optional>

namespace lightHostModern::ui {
using namespace winrt;
inline Windows::UI::Color visualColor(winrt::hstring value, Windows::UI::Color fallback={255,60,131,246}) {
    if(value.size()!=9||value[0]!=L'#')return fallback;
    try {const auto n=std::stoul(std::wstring(value).substr(1),nullptr,16);return {static_cast<uint8_t>(n>>24),static_cast<uint8_t>(n>>16),static_cast<uint8_t>(n>>8),static_cast<uint8_t>(n)};}catch(...){return fallback;}
}
inline winrt::hstring visualColorHex(Windows::UI::Color c) {wchar_t value[10]{};swprintf_s(value,L"#%02X%02X%02X%02X",c.A,c.R,c.G,c.B);return value;}
inline Microsoft::UI::Xaml::Media::SolidColorBrush cardTint(winrt::hstring value) {
    auto color=visualColor(value,{0,0,0,0});color.A=static_cast<uint8_t>(color.A*.18);return Microsoft::UI::Xaml::Media::SolidColorBrush(color);
}
// The palette belongs to the app, independently of the selected card/profile.
struct SavedColorPalette : std::enable_shared_from_this<SavedColorPalette> {
    Microsoft::UI::Xaml::Controls::Grid grid;
    Microsoft::UI::Xaml::Controls::Button save;
    Microsoft::UI::Xaml::Controls::ColorPicker picker;
    ::LightHostModernWinUI::LocalizationCatalog catalog;
    std::vector<hstring> colors;
    SavedColorPalette(Microsoft::UI::Xaml::Controls::ColorPicker const& control, ::LightHostModernWinUI::LocalizationCatalog const& strings):picker(control),catalog(strings) {}
    void persist() {
        Windows::Data::Json::JsonArray values;for(auto color:colors)values.Append(Windows::Data::Json::JsonValue::CreateStringValue(color));
        saveUiSetting(L"Colors",L"Palette",std::wstring(values.Stringify()));
    }
    void initialize() {
        using namespace Microsoft::UI::Xaml;using namespace Microsoft::UI::Xaml::Controls;
        const auto stored=loadUiSetting(L"Colors",L"Palette");bool valid=false;
        if(!stored.empty())try{auto values=Windows::Data::Json::JsonArray::Parse(stored);for(auto value:values){auto color=value.GetString();if(color.size()==9&&color[0]==L'#'&&std::wstring(color).find_first_not_of(L"0123456789abcdefABCDEF",1)==std::wstring::npos&&colors.size()<128)colors.push_back(visualColorHex(visualColor(color)));}valid=true;}catch(...){}
        if(!valid)colors={L"#FF3C83F6",L"#FF06B6D4",L"#FF10B981",L"#FF84CC16",L"#FFF59E0B",L"#FFF97316",L"#FFEF4444",L"#FFEC4899",L"#FFA855F7",L"#FF6366F1",L"#FF94A3B8",L"#FFFFFFFF"};
        grid.ColumnSpacing(8);grid.RowSpacing(8);for(int i=0;i<6;++i)grid.ColumnDefinitions().Append(ColumnDefinition());
        save.Content(box_value(catalog.translatedSource(L"Save custom color")));save.HorizontalAlignment(HorizontalAlignment::Right);
        Automation::AutomationProperties::SetAutomationId(save,L"SaveCustomColor");
        save.Click([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock()){const auto color=visualColorHex(s->picker.Color());if(std::find(s->colors.begin(),s->colors.end(),color)==s->colors.end()&&s->colors.size()<128){s->colors.push_back(color);s->persist();s->render();}}});
        render();
    }
    void render() {
        using namespace Microsoft::UI::Xaml;using namespace Microsoft::UI::Xaml::Controls;using namespace Microsoft::UI::Xaml::Media;
        grid.Children().Clear();grid.RowDefinitions().Clear();for(size_t i=0;i<(colors.size()+5)/6;++i)grid.RowDefinitions().Append(RowDefinition());
        save.IsEnabled(colors.size()<128);
        int index=0;for(auto hex:colors){
            Button swatch;swatch.MinWidth(0);swatch.Height(34);swatch.Padding({0,0,0,0});swatch.HorizontalAlignment(HorizontalAlignment::Stretch);swatch.HorizontalContentAlignment(HorizontalAlignment::Stretch);swatch.VerticalContentAlignment(VerticalAlignment::Stretch);
            // Keep the color visible in every native button state; hover only changes the border.
            Border chip;chip.Background(SolidColorBrush(visualColor(hex)));chip.CornerRadius({4,4,4,4});chip.IsHitTestVisible(false);swatch.Content(chip);
            Automation::AutomationProperties::SetName(swatch,hex);Automation::AutomationProperties::SetAutomationId(swatch,L"ColorPreset"+to_hstring(index));
            lightHostModern::ui::HoverHelp::SetToolTip(swatch,box_value(hex));
            swatch.Click([weak=weak_from_this(),hex](const auto&,const auto&){if(auto s=weak.lock())s->picker.Color(visualColor(hex));});
            MenuFlyout menu;MenuFlyoutItem remove;remove.Text(catalog.translatedSource(L"Delete color"));FontIcon icon;icon.Glyph(L"\xE74D");remove.Icon(icon);
            remove.Click([weak=weak_from_this(),hex](const auto&,const auto&){if(auto s=weak.lock()){s->colors.erase(std::remove(s->colors.begin(),s->colors.end(),hex),s->colors.end());s->persist();s->render();}});menu.Items().Append(remove);swatch.ContextFlyout(menu);
            grid.Children().Append(swatch);Grid::SetColumn(swatch,index%6);Grid::SetRow(swatch,index/6);++index;
        }
    }
};
inline winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IInspectable> chooseVisualColor(
    winrt::Microsoft::UI::Xaml::FrameworkElement owner, ::LightHostModernWinUI::LocalizationCatalog catalog,
    winrt::hstring title, winrt::hstring current)
{
    using namespace winrt;using namespace Microsoft::UI::Xaml;using namespace Microsoft::UI::Xaml::Controls;using namespace Microsoft::UI::Xaml::Media;
    ContentDialog dialog;dialog.XamlRoot(owner.XamlRoot());dialog.RequestedTheme(owner.ActualTheme());dialog.Title(box_value(title));
    dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"),box_value(760.0));
    dialog.PrimaryButtonText(catalog.translatedSource(L"Confirm"));dialog.SecondaryButtonText(catalog.translatedSource(L"Default color"));dialog.CloseButtonText(catalog.translatedSource(L"Cancel"));dialog.DefaultButton(ContentDialogButton::Primary);
    StackPanel body;body.Spacing(16);body.MinWidth((std::min)(620.0,(std::max)(260.0,static_cast<double>(owner.XamlRoot().Size().Width)-100)));
    Grid previews;previews.ColumnSpacing(12);previews.ColumnDefinitions().Append(ColumnDefinition());previews.ColumnDefinitions().Append(ColumnDefinition());
    Border before,after;
    int column=0;for(auto preview:{before,after}){StackPanel group;group.Spacing(6);TextBlock label;label.Text(catalog.translatedSource(column?L"New":L"Current"));group.Children().Append(label);preview.Height(40);preview.CornerRadius({6,6,6,6});preview.Background(SolidColorBrush(visualColor(current)));group.Children().Append(preview);previews.Children().Append(group);Grid::SetColumn(group,column++);}
    body.Children().Append(previews);
    ColorPicker picker;picker.Orientation(owner.XamlRoot().Size().Width>=720?Orientation::Horizontal:Orientation::Vertical);picker.ColorSpectrumComponents(ColorSpectrumComponents::SaturationValue);picker.Color(visualColor(current));picker.IsAlphaEnabled(true);picker.IsAlphaSliderVisible(true);picker.IsHexInputVisible(true);picker.IsColorSpectrumVisible(true);picker.IsColorSliderVisible(true);picker.HorizontalAlignment(HorizontalAlignment::Center);
    Automation::AutomationProperties::SetAutomationId(picker,L"CustomColorPicker");
    picker.ColorChanged([after](const auto&,const ColorChangedEventArgs& e){after.Background(SolidColorBrush(e.NewColor()));});
    auto palette=std::make_shared<SavedColorPalette>(picker,catalog);palette->initialize();body.Children().Append(palette->grid);
    StackPanel custom;custom.Spacing(12);custom.HorizontalAlignment(HorizontalAlignment::Stretch);
    Grid customHeader;customHeader.ColumnSpacing(20);customHeader.ColumnDefinitions().Append(ColumnDefinition());
    ColumnDefinition saveColumn;saveColumn.Width({1,GridUnitType::Auto});customHeader.ColumnDefinitions().Append(saveColumn);
    TextBlock customTitle;customTitle.Text(catalog.translatedSource(L"Custom color"));customTitle.VerticalAlignment(VerticalAlignment::Center);
    customHeader.Children().Append(customTitle);customHeader.Children().Append(palette->save);Grid::SetColumn(palette->save,1);
    custom.Children().Append(customHeader);custom.Children().Append(picker);
    Automation::AutomationProperties::SetAutomationId(custom,L"CustomColorSection");body.Children().Append(custom);
    ScrollViewer scroll;scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);scroll.MaxHeight((std::max)(220.0,owner.XamlRoot().Size().Height-230.0));scroll.Content(body);dialog.Content(scroll);
    const auto result=co_await lightHostModern::ui::showAppDialog(dialog);
    if(result==ContentDialogResult::Secondary)co_return box_value(hstring{});
    if(result==ContentDialogResult::Primary)co_return box_value(visualColorHex(picker.Color()));
    co_return nullptr;
}
}
