#pragma once
#include "Localization.h"
#include "UiPreferences.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <unordered_map>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace lightHostModern::ui {
// Hover help is independent of action notifications and screen-reader help.
// Keep owners weak, including explicit ToolTips, so recycled UI can be released.
class HoverHelp {
    using Object=winrt::Microsoft::UI::Xaml::DependencyObject;
    using Tip=winrt::Microsoft::UI::Xaml::Controls::ToolTip;
    struct Entry {winrt::weak_ref<Object> owner;winrt::hstring text;winrt::weak_ref<Tip> tip;};
    std::unordered_map<void*,Entry> entries;
    ::LightHostModernWinUI::LocalizationCatalog catalog;
    bool enabled=loadUiSetting(L"General",L"HoverTooltips",L"1")!=L"0";
    unsigned updates=0;
    static winrt::hstring string(winrt::Windows::Foundation::IInspectable const& value) {
        if(auto p=value.try_as<winrt::Windows::Foundation::IPropertyValue>();p&&p.Type()==winrt::Windows::Foundation::PropertyType::String)return p.GetString();
        return {};
    }
    static bool settingsOption(Object object){
        using namespace winrt::Microsoft::UI::Xaml;
        while(object){if(auto e=object.try_as<FrameworkElement>();e&&e.Name()==L"ConfigPanel")return true;object=Media::VisualTreeHelper::GetParent(object);}return false;
    }
    void attach(Object const& owner,Entry const& entry){
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        if(!enabled){if(auto tip=ToolTipService::GetToolTip(owner).try_as<Tip>())tip.IsOpen(false);ToolTipService::SetToolTip(owner,nullptr);return;}
        if(auto tip=entry.tip.get())ToolTipService::SetToolTip(owner,tip);
        else ToolTipService::SetToolTip(owner,winrt::box_value(entry.text));
    }
public:
    static HoverHelp& current(){static HoverHelp value;return value;}
    void localize(::LightHostModernWinUI::LocalizationCatalog const& value){catalog=value;}
    void configure(bool value){
        enabled=value;
        for(auto i=entries.begin();i!=entries.end();){if(auto owner=i->second.owner.get()){attach(owner,i->second);++i;}else i=entries.erase(i);}
    }
    // Existing explicit tooltips (notably Diagnostics) retain their live Content.
    static void SetToolTip(Object const& owner,winrt::Windows::Foundation::IInspectable const& value){
        if(!owner)return;auto& self=current();
        auto text=string(value);auto tip=value.try_as<Tip>();if(tip)text=string(tip.Content());
        if(!tip){auto help=self.explanation(text);if(!help.empty())text=help;}
        auto key=winrt::get_abi(owner);auto found=self.entries.find(key);
        if(found!=self.entries.end()&&found->second.owner.get()==owner&&found->second.text==text&&found->second.tip.get()==tip)return;
        Entry entry{winrt::make_weak(owner),text,tip?winrt::make_weak(tip):winrt::weak_ref<Tip>{}};
        self.entries.insert_or_assign(key,entry);self.attach(owner,entry);
        if(++self.updates%256==0)for(auto i=self.entries.begin();i!=self.entries.end();)if(!i->second.owner.get())i=self.entries.erase(i);else ++i;
    }
    winrt::hstring explanation(winrt::hstring const& label) const;
    void describe(Object const& object){
        using namespace winrt;using namespace Microsoft::UI::Xaml;using namespace Microsoft::UI::Xaml::Controls;
        if(settingsOption(object))return;
        const auto keyOwner=winrt::get_abi(object);
        if(auto nativeTip=ToolTipService::GetToolTip(object);nativeTip){
            const auto found=entries.find(keyOwner);
            if(found==entries.end()||found->second.owner.get()!=object)SetToolTip(object,nativeTip);
        }
        hstring label=Automation::AutomationProperties::GetName(object),id=Automation::AutomationProperties::GetAutomationId(object);
        // Stable IDs cover controls whose visible values are device names/numbers.
        const wchar_t* key=nullptr;
        if(id==L"AudioBackend"||id==L"EnabledAudioBackend")key=L"Audio backend";
        else if(id==L"AudioInputDevice")key=L"Input device";else if(id==L"AudioOutputDevice")key=L"Output device";
        else if(id==L"AudioSampleRate")key=L"Sample rate";else if(id==L"AudioBufferSize")key=L"Buffer size";
        else if(id==L"ProfileName"||id==L"InstanceName")key=L"Name";
        else if(id==L"NewScanPath")key=L"Scan folder";
        else if(id==L"EnabledAudioBackendToggle")key=L"Enable this audio backend";
        else if(std::wstring_view(id).starts_with(L"ChainInsertInput"))key=L"Insert input channel";
        else if(std::wstring_view(id).starts_with(L"ChainInsertOutput"))key=L"Insert output channel";
        else if(std::wstring_view(id).starts_with(L"PluginInputBus-")||std::wstring_view(id).starts_with(L"PluginOutputBus-"))key=L"Plugin audio format";
        else if(std::wstring_view(id).starts_with(L"set-input-channel-")||std::wstring_view(id).starts_with(L"set-output-channel-"))key=L"Active audio channel";
        if(key)label=key;
        else if(auto m=object.try_as<MenuFlyoutItem>())label=m.Text();
        else if(auto m=object.try_as<MenuFlyoutSubItem>())label=m.Text();
        else if(auto m=object.try_as<ToggleMenuFlyoutItem>())label=m.Text();
        else if(auto t=object.try_as<ToggleSwitch>();t&&!string(t.Header()).empty())label=string(t.Header());
        else if(auto b=object.try_as<AppBarButton>())label=b.Label();
        else if(auto b=object.try_as<AppBarToggleButton>())label=b.Label();
        else if(auto b=object.try_as<AutoSuggestBox>())label=b.PlaceholderText();
        else if(auto b=object.try_as<TextBox>()){if(!string(b.Header()).empty())label=string(b.Header());else if(!b.PlaceholderText().empty())label=b.PlaceholderText();}
        else if(object.try_as<ColorPicker>())label=L"Color";
        else if(auto b=object.try_as<ContentControl>();b&&!string(b.Content()).empty())label=string(b.Content());
        auto help=explanation(label);if(!help.empty()){SetToolTip(object,box_value(help));Automation::AutomationProperties::SetHelpText(object,help);}
    }
};
}
#include "HoverHelpText.h"
