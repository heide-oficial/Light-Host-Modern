#pragma once
#include "HoverHelp.h"
#include "DialogPresentation.h"
#include "ChainCanvas.h"
#include "VisualPreferences.h"
#include "HostConnection.h"
#include "UiPreferences.h"
#include "DangerDialog.h"
#include "PluginRows.h"
#include "PluginPageController.h"
#include "PluginDialogs.h"
#include "PluginsPageView.xaml.h"
#include "../../Source/VerboseLog.h"

namespace lightHostModern::ui
{
class OperatingPresenter : public std::enable_shared_from_this<OperatingPresenter>
{
public:
    std::function<void()> layoutChanged;
    using Send = std::function<Windows::Foundation::IAsyncOperation<bool>(std::string,bool)>;
    void create(FrameworkElement const& owner, ::LightHostModernWinUI::LocalizationCatalog const& loc,
                std::shared_ptr<HostConnection> connection, HWND window, Send action)
    {
        root=owner;catalog=loc;host=std::move(connection);hwnd=window;sendCallback=std::move(action);
        timer.Interval(std::chrono::milliseconds(200));timer.Tick([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock())s->tick();});timer.Start();
    }
    Windows::Foundation::IAsyncOperation<bool> flushPending(uint64_t deadline=0){auto lifetime=shared_from_this();if(canvas)co_return co_await canvas->flushPending(deadline);co_return true;}
    Windows::Foundation::IAsyncOperation<bool> send(std::string command,bool chainPrepared=false){return sendCallback(std::move(command),chainPrepared);}
    bool editsFrozen()const{return canvas&&canvas->isFrozen();}
    bool transitionPending=false;
    void freezeEdits(bool frozen){if(canvas)canvas->freezeEdits(frozen);}
    void close(){closed=true;timer.Stop();if(canvas)canvas->stop();}
    void localize(::LightHostModernWinUI::LocalizationCatalog const& loc){catalog=loc;syncing=true;for(auto box:{settingsMode})if(box){box.Items().GetAt(0).as<ComboBoxItem>().Content(box_value(text(L"List")));box.Items().GetAt(1).as<ComboBoxItem>().Content(box_value(text(L"Chain")));}syncing=false;lastRender.clear();render();}
    void update(std::string const& json)
    {
        if(json.empty()||closed)return;
        try{snapshot=JsonObject::Parse(chainText(json));state=snapshot.GetNamedObject(L"operating",JsonObject{});if(!busy)render();}catch(...){}
    }
    void visible(bool plugins){pluginsVisible=plugins;if(canvas)canvas->visible(plugins&&mode()==L"chain");}
    void applyDistancePreference(){if(canvas)canvas->applyDistancePreference();}
    bool chainMode() const { return mode() == L"chain"; }
    fire_and_forget configureIsolation(hstring id)
    {
        auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;
        bool isolated=false,found=false;for(const auto& v:snapshot.GetNamedArray(L"activePlugins",JsonArray{})){
            const auto p=v.GetObject();if(p.GetNamedString(L"instanceId",L"")==id){isolated=p.GetNamedBoolean(L"isolated",false);found=true;break;}}
        if(!found)co_return;
        JsonObject request;request.SetNamedValue(L"action",JsonValue::CreateStringValue(L"isolation"));request.SetNamedValue(L"id",JsonValue::CreateStringValue(id));
        request.SetNamedValue(L"enabled",JsonValue::CreateBooleanValue(!isolated));request.SetNamedValue(L"profileId",JsonValue::CreateStringValue(activeId()));request.SetNamedValue(L"generation",state.GetNamedValue(L"generation"));
        dialogOpen=true;
        try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(isolated?L"Run inside the host?":L"Run in a separate process?")));
            d.Content(box_value(text(isolated?L"This removes the extra buffered delay. A crash or hang in this plugin can affect the whole app.":L"Experimental: contains this plugin's crashes and hangs. Adds a buffered audio delay per isolated plugin (at least about 4 ms), shown in Diagnostics, and uses more memory. Heavy load may cause missed blocks. The plugin keeps its settings and connections.")));
            d.PrimaryButtonText(text(L"Apply"));d.CloseButtonText(text(L"Cancel"));d.DefaultButton(ContentDialogButton::Close);
            if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary)co_await perform(request);
        }catch(...){}dialogOpen=false;
    }
    fire_and_forget configurePluginBuses(hstring id)
    {
        auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;
        if(!co_await flushPending())co_return;
        dialogOpen=true;
        try {
            const auto response=co_await host->requestAsync("plugin-buses:"+to_string(id));if(closed){dialogOpen=false;co_return;}
            auto inventory=JsonObject::Parse(response);ContentDialog dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(root.ActualTheme());
            dialog.Title(box_value(text(L"Plugin audio channels")));dialog.CloseButtonText(text(L"Cancel"));
            dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"),box_value(880.0));
            StackPanel content;content.Spacing(16);content.Width((std::min)(760.0,(std::max)(280.0,static_cast<double>(root.XamlRoot().Size().Width)-120)));
            TextBlock description;description.TextWrapping(TextWrapping::Wrap);
            description.Style(Application::Current().Resources().Lookup(box_value(L"BodyTextBlockStyle")).as<Style>());
            description.Text(text(L"Choose the format for each audio group provided by this plugin. Connections to disabled channels are kept silent until you enable them again."));content.Children().Append(description);
            const auto error=inventory.GetNamedString(L"error",L"");
            if(!error.empty()){TextBlock message;message.Text(error);message.TextWrapping(TextWrapping::Wrap);content.Children().Append(message);}
            else {
                const auto previous=inventory.GetNamedObject(L"layout");auto chosen=std::make_shared<JsonObject>(JsonObject::Parse(previous.Stringify()));
                struct BusControl {bool input;uint32_t index;ComboBox control;JsonArray choices;};auto controls=std::make_shared<std::vector<BusControl>>();
                auto updating=std::make_shared<bool>(false);
                SelectorBar tabs;SelectorBarItem inputTab,outputTab;
                inputTab.Text(text(L"Input channels"));outputTab.Text(text(L"Output channels"));
                inputTab.Icon(SymbolIcon(static_cast<Symbol>(0xE720)));outputTab.Icon(SymbolIcon(static_cast<Symbol>(0xE767)));
                Automation::AutomationProperties::SetAutomationId(inputTab,L"PluginInputChannelsTab");Automation::AutomationProperties::SetAutomationId(outputTab,L"PluginOutputChannelsTab");
                tabs.Items().Append(inputTab);tabs.Items().Append(outputTab);
                Border topbar;style(topbar,L"AppCardStyle");topbar.Padding({4,4,4,4});topbar.MinHeight(0);topbar.Child(tabs);content.Children().Append(topbar);
                StackPanel inputContent,outputContent;inputContent.Spacing(8);outputContent.Spacing(8);
                ScrollViewer channelScroll;channelScroll.MaxHeight((std::max)(140.0,(std::min)(380.0,static_cast<double>(root.XamlRoot().Size().Height)-330)));channelScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);channelScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);channelScroll.Content(inputContent);content.Children().Append(channelScroll);
                tabs.SelectionChanged([inputContent,outputContent,channelScroll,inputTab](SelectorBar const& sender,const auto&){channelScroll.Content(sender.SelectedItem()==inputTab?inputContent:outputContent);channelScroll.ChangeView(nullptr,0.0,nullptr,true);});
                tabs.SelectedItem(inputTab);
                for(bool input:{true,false}){
                    const auto buses=inventory.GetNamedArray(input?L"inputs":L"outputs",JsonArray{});const auto side=input?inputContent:outputContent;
                    if(!buses.Size()){Border card;style(card,L"AppCardStyle");TextBlock empty;empty.Text(text(input?L"This plugin has no input channels.":L"This plugin has no output channels."));empty.TextWrapping(TextWrapping::Wrap);card.Child(empty);side.Children().Append(card);}
                    for(uint32_t i=0;i<buses.Size();++i){const auto bus=buses.GetObjectAt(i);ComboBox choice;SurfaceMaterials::current().watch(choice);choice.Width(190);choice.VerticalAlignment(VerticalAlignment::Center);choice.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                        Automation::AutomationProperties::SetName(choice,bus.GetNamedString(L"name"));HoverHelp::SetToolTip(choice,box_value(text(L"Choose the audio format for this group, or disable it.")));
                        Automation::AutomationProperties::SetAutomationId(choice,(input?L"PluginInputBus-":L"PluginOutputBus-")+chainText(i));
                        auto offered=bus.GetNamedArray(L"choices");std::vector<JsonObject> sorted;
                        for(const auto& option:offered)sorted.push_back(option.GetObject());
                        const auto rank=[](const JsonObject& option){const auto types=option.GetNamedArray(L"types");
                            if(types.Size()==2&&types.GetNumberAt(0)==1&&types.GetNumberAt(1)==2)return 0;
                            if(types.Size()==1&&types.GetNumberAt(0)==3)return 1;
                            return types.Size()==0?2:3;};
                        std::stable_sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});
                        JsonArray choices;for(const auto& option:sorted)choices.Append(option);int selected=-1;
                        for(uint32_t j=0;j<choices.Size();++j){const auto option=choices.GetObjectAt(j);ComboBoxItem item;item.Content(box_value(text(option.GetNamedString(L"name").c_str())));choice.Items().Append(item);if(option.GetNamedArray(L"types").Stringify()==bus.GetNamedArray(L"types").Stringify())selected=j;}
                        Border card;style(card,L"AppCardStyle");card.Padding({16,16,16,16});card.MinHeight(76);Grid row;row.ColumnSpacing(20);ColumnDefinition labelColumn,formatColumn;labelColumn.Width({1,GridUnitType::Star});formatColumn.Width(GridLengthHelper::Auto());row.ColumnDefinitions().Append(labelColumn);row.ColumnDefinitions().Append(formatColumn);
                        TextBlock name;name.Text(bus.GetNamedString(L"name"));style(name,L"BodyStrongTextBlockStyle");name.TextWrapping(TextWrapping::Wrap);name.VerticalAlignment(VerticalAlignment::Center);row.Children().Append(name);row.Children().Append(choice);Grid::SetColumn(choice,1);card.Child(row);
                        choice.SelectedIndex(selected);side.Children().Append(card);controls->push_back({input,i,choice,choices});
                    }
                }
                for(const auto& row:*controls){const auto weakControl=make_weak(row.control);const auto choices=row.choices;
                    row.control.SelectionChanged([weakControl,choices,chosen,weakControls=std::weak_ptr(controls),updating](const auto&,const auto&){
                        if(*updating)return;auto control=weakControl.get();auto rows=weakControls.lock();if(!control||!rows||control.SelectedIndex()<0)return;
                        *chosen=JsonObject::Parse(choices.GetObjectAt(control.SelectedIndex()).GetNamedObject(L"layout").Stringify());*updating=true;
                        for(const auto& other:*rows){const auto types=chosen->GetNamedArray(other.input?L"inputs":L"outputs").GetArrayAt(other.index).Stringify();int selected=-1;
                            for(uint32_t j=0;j<other.choices.Size();++j)if(other.choices.GetObjectAt(j).GetNamedArray(L"types").Stringify()==types)selected=j;
                            other.control.SelectedIndex(selected);
                        }*updating=false;
                    });
                }
                dialog.PrimaryButtonText(text(L"Apply"));dialog.DefaultButton(ContentDialogButton::Close);
                dialog.Content(content);
                if(co_await lightHostModern::ui::showAppDialog(dialog)==ContentDialogResult::Primary&&!closed){JsonObject request;request.SetNamedValue(L"action",JsonValue::CreateStringValue(L"plugin-buses"));request.SetNamedValue(L"id",JsonValue::CreateStringValue(id));
                    request.SetNamedValue(L"layout",*chosen);request.SetNamedValue(L"previousLayout",previous);request.SetNamedValue(L"profileId",inventory.GetNamedValue(L"profileId"));request.SetNamedValue(L"generation",inventory.GetNamedValue(L"generation"));co_await perform(request);}
                dialogOpen=false;co_return;
            }
            dialog.Content(content);co_await lightHostModern::ui::showAppDialog(dialog);
        }catch(...){}dialogOpen=false;
    }
    void setContentInsets(double inset)
    {
        pluginHeader.Margin({inset,0,inset,0});
        if (canvas) canvas->root.Margin({inset,0,inset,16});
    }
    void attachPlugins(UserControl const& page)
    {
        lightHostModern::verbose::log("ui","attachPlugins begin");
        Grid layout;RowDefinition bar,main;bar.Height(GridLengthHelper::Auto());main.Height({1,GridUnitType::Star});layout.RowDefinitions().Append(bar);layout.RowDefinitions().Append(main);
        listRoot=page.Content().as<UIElement>();page.Content(nullptr);layout.Children().Append(listRoot);Grid::SetRow(listRoot.as<FrameworkElement>(),1);
        pluginError.IsClosable(false); pluginError.Severity(InfoBarSeverity::Error);
        pluginHeader.Children().Append(pluginError);layout.Children().Append(pluginHeader);
        recoveryNotice.IsClosable(false);recoveryNotice.Severity(InfoBarSeverity::Warning);
        recoveryNotice.Message(text(L"An unfinished edit was recovered for this profile. Review it before applying."));
        auto review=button(L"Review recovered edit",[weak=weak_from_this()]{if(auto s=weak.lock())s->reviewRecoveredEdit();});
        Automation::AutomationProperties::SetAutomationId(review,L"ReviewChainRecovery");recoveryNotice.ActionButton(review);recoveryNotice.Visibility(Visibility::Collapsed);pluginHeader.Children().Append(recoveryNotice);
        lightHostModern::verbose::log("ui","canvas create begin");
        canvas=std::make_shared<ChainCanvas>();canvas->create(catalog,
            [weak=weak_from_this()](JsonObject r)->Windows::Foundation::IAsyncOperation<bool>{if(auto s=weak.lock())co_return co_await s->perform(r);co_return false;},
            [weak=weak_from_this()](std::string command){if(auto s=weak.lock())s->pluginCommand(std::move(command));},
            [weak=weak_from_this()](Point p){if(auto s=weak.lock())s->choosePlugin(p);});
        layout.Children().Append(canvas->root);Grid::SetRow(canvas->root,1);page.Content(layout);setContentInsets(24);render();
    }
    void attachProfiles(ContentControl const& hostPanel)
    {
        StackPanel panel; panel.Spacing(16); Grid tools; tools.ColumnSpacing(16);tools.RowSpacing(0);for(int i=0;i<2;++i){RowDefinition row;row.Height(GridLengthHelper::Auto());tools.RowDefinitions().Append(row);}
        ColumnDefinition searchColumn,actionsColumn; searchColumn.Width({1,GridUnitType::Star}); actionsColumn.Width(GridLengthHelper::Auto()); tools.ColumnDefinitions().Append(searchColumn); tools.ColumnDefinitions().Append(actionsColumn);
        profileSearch.PlaceholderText(text(L"Search profiles")); profileSearch.HorizontalAlignment(HorizontalAlignment::Stretch); profileSearch.MaxWidth(420); profileSearch.HorizontalAlignment(HorizontalAlignment::Left); profileSearch.MinWidth(140);
        tools.Children().Append(profileSearch);
        auto create=button(L"Save current settings as a new profile",[weak=weak_from_this()]{if(auto s=weak.lock())s->edit(L"create",L"");});
        lightHostModern::ui::HoverHelp::SetToolTip(create,box_value(text(L"Save current setup as a new profile…"))); tools.Children().Append(create); Grid::SetColumn(create,1);
        profileSearch.VerticalAlignment(VerticalAlignment::Center);create.VerticalAlignment(VerticalAlignment::Center);
        Border toolbarCard;style(toolbarCard,L"AppCardStyle");toolbarCard.MinHeight(0);toolbarCard.Padding({12,8,12,8});toolbarCard.Child(tools);panel.Children().Append(toolbarCard);
        TextBlock createLabel;createLabel.Text(text(L"Save current settings as a new profile"));createLabel.TextWrapping(TextWrapping::Wrap);createLabel.VerticalAlignment(VerticalAlignment::Center);create.Content(createLabel);create.MinHeight(36);create.Padding({12,7,12,7});create.VerticalContentAlignment(VerticalAlignment::Center);
        tools.SizeChanged([weakTools=make_weak(tools),weakSearch=make_weak(profileSearch),weakCreate=make_weak(create),weakLabel=make_weak(createLabel)](const auto&,const SizeChangedEventArgs& e){auto input=weakSearch.get();auto b=weakCreate.get();auto label=weakLabel.get();if(!input||!b||!label)return;label.Measure({10000,100});const bool wrap=e.NewSize().Width<label.DesiredSize().Width+200;if(auto tools=weakTools.get())tools.RowSpacing(wrap?8:0);Grid::SetRow(b,wrap?1:0);Grid::SetColumn(b,wrap?0:1);Grid::SetColumnSpan(b,wrap?2:1);Grid::SetColumnSpan(input,wrap?2:1);b.HorizontalAlignment(wrap?HorizontalAlignment::Left:HorizontalAlignment::Right);b.MaxWidth(e.NewSize().Width);input.Width((std::max)(140.0,(std::min)(420.0,static_cast<double>(e.NewSize().Width)-(wrap?0.0:label.DesiredSize().Width+50.0))));});
        profilesError.IsClosable(false);profilesError.Severity(InfoBarSeverity::Error);
        auto recover=button(L"Recover profiles",[weak=weak_from_this()]{if(auto s=weak.lock())s->recoverProfiles();});profilesError.ActionButton(recover);panel.Children().Append(profilesError);
        panel.Children().Append(profileRows);hostPanel.Content(panel);profileSearch.TextChanged([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock())s->fillProfiles();});
        profilesAttached=true;fillProfiles();
    }
    void attachSettings(StackPanel const& operatingHost,StackPanel const& appearance)
    {
        StackPanel section;section.Spacing(8);TextBlock heading;heading.Text(text(L"Operating mode"));style(heading,L"SubtitleTextBlockStyle");section.Children().Append(heading);
        settingsMode=modeSelector();Automation::AutomationProperties::SetAutomationId(settingsMode,L"SettingsOperatingMode");
        settingsMode.Width(180);
        settingsPending.Content(box_value(text(L"Waiting for restart")));settingsPending.Click([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock())s->showRestart();});style(settingsPending,L"AppStatusLinkStyle");settingsPending.Padding({0,4,0,0});settingsPending.HorizontalAlignment(HorizontalAlignment::Left);
        section.Children().Append(settingCard(L"Plugin mode",L"List runs plugins in order. Chain lets you connect plugins on a canvas and create parallel audio paths.",settingsMode,L"\xE8FD",settingsPending));operatingHost.Children().Append(section);
        StackPanel distanceControls;distanceControls.Width(220);distanceControls.Spacing(4);
        Slider distance;distance.Minimum(600);distance.Maximum(12000);distance.StepFrequency(200);distance.Value(std::clamp(static_cast<double>(GetPrivateProfileIntW(L"Chain",L"MaxSeparation",2400,uiSettingsFilePath().c_str())),600.0,12000.0));
        Automation::AutomationProperties::SetName(distance,text(L"Maximum space between elements"));Automation::AutomationProperties::SetAutomationId(distance,L"ChainMaxSeparation");distanceControls.Children().Append(distance);
        TextBlock distanceLabel;distanceLabel.Text(chainText(static_cast<int>(distance.Value()))+L" px");distanceLabel.HorizontalAlignment(HorizontalAlignment::Right);style(distanceLabel,L"SecondaryCaptionStyle");distanceControls.Children().Append(distanceLabel);
        distance.ValueChanged([weak=weak_from_this(),weakLabel=make_weak(distanceLabel)](const auto&,const Primitives::RangeBaseValueChangedEventArgs& e){saveUiSetting(L"Chain",L"MaxSeparation",std::to_wstring(static_cast<int>(e.NewValue())));if(auto label=weakLabel.get())label.Text(chainText(static_cast<int>(e.NewValue()))+L" px");if(auto self=weak.lock();self&&self->mode()==L"chain")self->pluginCommand("constrain-chain-spacing");});
        distanceSetting=settingCard(L"Maximum space between elements",L"When you move a card too far from the chain, bring it closer. A smaller distance keeps the canvas more compact.",distanceControls,L"\xE740");section.Children().Append(distanceSetting);
        StackPanel dotsRow;dotsRow.Orientation(Orientation::Horizontal);dotsRow.Spacing(12);TextBlock dotsState;dotsState.VerticalAlignment(VerticalAlignment::Center);
        ToggleSwitch dotsSwitch;style(dotsSwitch,L"CompactToggleSwitchStyle");dotsSwitch.IsOn(VisualPreferences::current().dottedCanvas);dotsState.Text(text(dotsSwitch.IsOn()?L"On":L"Off"));dotsRow.Children().Append(dotsState);dotsRow.Children().Append(dotsSwitch);Automation::AutomationProperties::SetAutomationId(dotsSwitch,L"DottedCanvasBackground");
        dotsSwitch.Toggled([weak=weak_from_this(),dotsState](const Windows::Foundation::IInspectable& sender,const auto&){const auto control=sender.as<ToggleSwitch>();VisualPreferences::current().dottedCanvas=control.IsOn();saveUiSetting(L"Chain",L"DottedBackground",control.IsOn()?L"1":L"0");if(auto s=weak.lock())dotsState.Text(s->text(control.IsOn()?L"On":L"Off"));});
        dotsSetting=settingCard(L"Dotted canvas background",L"Show a subtle grid of dots to help locate and align cards. Hidden while Performance mode is on.",dotsRow,L"\xE80A");
        StackPanel toggleRow;toggleRow.Orientation(Orientation::Horizontal);toggleRow.Spacing(12);toggleText.VerticalAlignment(VerticalAlignment::Center);toggleRow.Children().Append(toggleText);
        style(expandChain,L"CompactToggleSwitchStyle");expandChain.OnContent(box_value(L""));expandChain.OffContent(box_value(L""));
        expandChain.IsOn(loadUiSetting(L"Appearance",L"ExpandChain",L"1")!=L"0");toggleRow.Children().Append(expandChain);
        Automation::AutomationProperties::SetAutomationId(expandChain,L"ExpandChain");
        expandChain.Toggled([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock()){
            saveUiSetting(L"Appearance",L"ExpandChain",s->expandChain.IsOn()?L"1":L"0");s->render();if(s->layoutChanged)s->layoutChanged();
        }});
        expandedSetting=settingCard(L"Always expand Chain",L"Use the full page width for Plugins in Chain mode, even with Compact layout.",toggleRow,L"\xE740");
        appearance.Children().Append(expandedSetting);appearance.Children().Append(dotsSetting);settingsAttached=true;render();
    }
    void attachDanger(StackPanel const& section){
        const auto add=[&](const wchar_t* title,const wchar_t* detail,const wchar_t* command){auto b=button(title,[weak=weak_from_this(),command]{if(auto s=weak.lock())s->danger(command);});b.Content(box_value(text(std::wstring(command)==L"delete-all-profiles"?L"Delete all":std::wstring(command)==L"restore-all-names"?L"Restore":L"Reset")));Automation::AutomationProperties::SetAutomationId(b,command);section.Children().Append(settingCard(title,detail,b,L"\xE7BA"));};
        add(L"Restore all original names",L"Remove custom device, channel, plugin and element names, including names in saved profiles.",L"restore-all-names");
        add(L"Delete all profiles",L"Delete saved profiles and activate the empty default profile. The two default profiles are kept.",L"delete-all-profiles");
        add(L"Restore all default settings",L"Reset the entire app, including profiles, plugin database, audio and appearance preferences. The app will restart.",L"factory-reset");
    }

private:
    template<class T> static void style(T const& control,const wchar_t* key) { control.Style(Application::Current().Resources().Lookup(box_value(key)).template as<Style>()); }
    static FontIcon icon(const wchar_t* glyph) { FontIcon result; result.FontFamily(FontFamily(L"Segoe Fluent Icons")); result.Glyph(glyph); result.FontSize(18); result.VerticalAlignment(VerticalAlignment::Center); return result; }
    hstring text(const wchar_t* value)const{return catalog.translatedSource(value);}
    hstring mode()const{return state.GetNamedString(L"mode",L"list");}
    hstring activeId()const{return state.GetNamedString(L"activeProfile",L"");}
    JsonObject profile(hstring const& id)const{for(const auto& v:state.GetNamedArray(L"profiles",JsonArray{})){auto p=v.GetObject();if(p.GetNamedString(L"id")==id)return p;}return {};}
    Button button(const wchar_t* label,std::function<void()> fn){Button b;b.Content(box_value(text(label)));b.Click([fn=std::move(fn)](const auto&,const auto&){fn();});return b;}
    ComboBox modeSelector()
    {
        ComboBox box;SurfaceMaterials::current().watch(box);box.MinWidth(130);for(auto label:{L"List",L"Chain"}){ComboBoxItem item;item.Content(box_value(text(label)));box.Items().Append(item);}
        box.SelectionChanged([weak=weak_from_this()](const Windows::Foundation::IInspectable& sender,const auto&){const auto box=sender.as<ComboBox>();if(auto s=weak.lock();s&&!s->syncing&&box.SelectedIndex()>=0){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"mode"));r.SetNamedValue(L"mode",JsonValue::CreateStringValue(box.SelectedIndex()==1?L"chain":L"list"));s->switchTo(r);}});return box;
    }
    Border settingCard(const wchar_t* title,const wchar_t* description,UIElement const& control,const wchar_t* glyph, UIElement const& extra=nullptr)
    {
        Border border;border.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());
        Grid row;row.ColumnSpacing(16);ColumnDefinition icon,body,actions;icon.Width({32,GridUnitType::Pixel});body.Width({1,GridUnitType::Star});actions.Width(GridLengthHelper::Auto());row.ColumnDefinitions().Append(icon);row.ColumnDefinitions().Append(body);row.ColumnDefinitions().Append(actions);
        auto image=OperatingPresenter::icon(glyph);row.Children().Append(image);
        StackPanel labels;labels.VerticalAlignment(VerticalAlignment::Center);labels.Spacing(2);TextBlock name;name.Text(text(title));style(name,L"BodyStrongTextBlockStyle");labels.Children().Append(name);
        TextBlock detail;detail.Text(text(description));style(detail,L"CaptionTextBlockStyle");detail.TextWrapping(TextWrapping::Wrap);labels.Children().Append(detail);if(extra)labels.Children().Append(extra);row.Children().Append(labels);Grid::SetColumn(labels,1);
        row.Children().Append(control);Grid::SetColumn(control.as<FrameworkElement>(),2);if(auto fe=control.try_as<FrameworkElement>())fe.VerticalAlignment(VerticalAlignment::Center);border.Child(row);return border;
    }
    void render()
    {
        if(closed)return;syncing=true;
        if(recoveryProfile!=activeId()) {
            recoveryProfile=activeId();recoveredEdit=readChainRecovery(recoveryProfile);
        }
        const bool hasRecovery=mode()==L"chain"&&recoveredEdit.HasKey(L"graph");
        recoveryNotice.IsOpen(hasRecovery);recoveryNotice.Visibility(hasRecovery?Visibility::Visible:Visibility::Collapsed);
        const auto error=state.GetNamedString(L"error",L"");
        for(const auto& banner:{pluginError,profilesError}){banner.Message(catalog.translatedSource(error));banner.IsOpen(!error.empty());banner.Visibility(error.empty()?Visibility::Collapsed:Visibility::Visible);}
        if(profilesError.ActionButton())profilesError.ActionButton().Visibility(state.GetNamedBoolean(L"catalogWritable",true)?Visibility::Collapsed:Visibility::Visible);
        const auto pending=state.GetNamedString(L"pendingMode",L"");
        if(settingsMode){settingsMode.SelectedIndex((pending.empty()?mode():pending)==L"chain"?1:0);settingsMode.IsEnabled(!busy);settingsPending.Visibility(pending.empty()?Visibility::Collapsed:Visibility::Visible);}
        if(settingsAttached){dotsSetting.Visibility(mode()==L"chain"?Visibility::Visible:Visibility::Collapsed);distanceSetting.Visibility(mode()==L"chain"?Visibility::Visible:Visibility::Collapsed);toggleText.Text(text(expandChain.IsOn()?L"On":L"Off"));expandedSetting.Visibility(mode()==L"chain"?Visibility::Visible:Visibility::Collapsed);}
        if(listRoot)listRoot.Visibility(mode()==L"chain"?Visibility::Collapsed:Visibility::Visible);
        if(canvas){canvas->mono(snapshot.GetNamedBoolean(L"monoInputs",false),snapshot.GetNamedBoolean(L"monoOutput",false));canvas->globals(snapshot.GetNamedBoolean(L"globalMuted",false),snapshot.GetNamedBoolean(L"globalBypassed",false));canvas->root.Visibility(mode()==L"chain"?Visibility::Visible:Visibility::Collapsed);canvas->visible(pluginsVisible&&mode()==L"chain"&&IsWindowVisible(hwnd)&&!IsIconic(hwnd));canvas->setBusy(busy);if(mode()==L"chain")canvas->update(state,snapshot.GetNamedArray(L"activePlugins",JsonArray{}),catalog,snapshot.GetNamedObject(L"audioConfig",JsonObject{}),snapshot.GetNamedObject(L"audioSelection",JsonObject{}),to_hstring(static_cast<int64_t>(snapshot.GetNamedNumber(L"chainVersion",0))),snapshot.GetNamedString(L"hostSession",L""));}
        const auto signature=to_string(state.GetNamedArray(L"profiles",JsonArray{}).Stringify())+to_string(activeId())+to_string(mode())+(state.GetNamedBoolean(L"dirty",false)?"dirty":"saved");
        if(lastRender!=signature){lastRender=signature;if(profilesAttached)fillProfiles();}
        syncing=false;
    }
    static bool matches(hstring const& value,hstring const& query)
    {auto a=std::wstring(value),b=std::wstring(query);std::transform(a.begin(),a.end(),a.begin(),towlower);std::transform(b.begin(),b.end(),b.begin(),towlower);return a.find(b)!=std::wstring::npos;}
    void fillProfiles()
    {
        profileRows.Children().Clear();profileRows.Spacing(8);
        for(bool activeSection:{true,false}){
        TextBlock heading;heading.Text(text(activeSection?L"Active profile":L"Other Profiles"));style(heading,L"SubtitleTextBlockStyle");heading.Margin({0,activeSection?0.0:16.0,0,4});profileRows.Children().Append(heading);int count=0;
        for(const auto& value:state.GetNamedArray(L"profiles",JsonArray{})){
            const auto p=value.GetObject();if(p.GetNamedString(L"mode")!=mode()||(p.GetNamedString(L"id")==activeId())!=activeSection||(!activeSection&&!matches(p.GetNamedString(L"name"),profileSearch.Text())))continue;++count;
            const auto id=p.GetNamedString(L"id");Border card;card.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());
            Grid row; row.ColumnSpacing(16); ColumnDefinition bodyColumn,actionColumn; bodyColumn.Width({1,GridUnitType::Star}); actionColumn.Width(GridLengthHelper::Auto()); row.ColumnDefinitions().Append(bodyColumn); row.ColumnDefinitions().Append(actionColumn);
            StackPanel labels; labels.Spacing(6); labels.VerticalAlignment(VerticalAlignment::Center);
            TextBlock title; title.Text(p.GetNamedBoolean(L"isDefault",false)?text(L"Default"):p.GetNamedString(L"name")); style(title,L"BodyStrongTextBlockStyle"); title.TextWrapping(TextWrapping::NoWrap);title.TextTrimming(TextTrimming::CharacterEllipsis); lightHostModern::ui::HoverHelp::SetToolTip(title,box_value(title.Text())); labels.Children().Append(title);
            TextBlock details; details.Text(p.GetNamedBoolean(L"isDefault",false)?text(L"Built-in clean profile"):chainText(std::wstring(text(p.GetNamedBoolean(L"hasUpdates",false)?L"Last updated":L"Created"))+L" "+date(p.GetNamedString(p.GetNamedBoolean(L"hasUpdates",false)?L"modified":L"created")))); style(details,L"SecondaryCaptionStyle"); details.TextWrapping(TextWrapping::Wrap); labels.Children().Append(details);
            if(!p.GetNamedBoolean(L"isDefault",false))lightHostModern::ui::HoverHelp::SetToolTip(details,box_value(chainText(std::wstring(text(L"Modified"))+L" "+date(p.GetNamedString(L"modified"))+(p.GetNamedBoolean(L"includeAudio")?L" · "+std::wstring(text(L"Specific audio settings")):L""))));
            if(!p.GetNamedString(L"description",L"").empty()){TextBlock description; description.Text(p.GetNamedString(L"description")); description.TextWrapping(TextWrapping::Wrap); description.MaxLines(2); description.TextTrimming(TextTrimming::CharacterEllipsis); style(description,L"SecondaryCaptionStyle"); lightHostModern::ui::HoverHelp::SetToolTip(description,box_value(description.Text())); labels.Children().Append(description);}
            row.Children().Append(labels);
            Button actions;style(actions,L"SubtleButtonStyle");actions.Content(icon(L"\xE712"));actions.Width(36);actions.Height(36);actions.Padding({8,8,8,8});actions.VerticalAlignment(VerticalAlignment::Center);
            Automation::AutomationProperties::SetName(actions,text(L"Profile options"));lightHostModern::ui::HoverHelp::SetToolTip(actions,box_value(text(L"Profile options")));Automation::AutomationProperties::SetAutomationId(actions,L"ProfileMenu-"+id);
            MenuFlyout menu;const auto addAction=[&](const wchar_t* label,std::function<void()> fn,bool enabled=true){MenuFlyoutItem item;item.Text(text(label));item.IsEnabled(enabled);item.Click([fn=std::move(fn)](const auto&,const auto&){fn();});menu.Items().Append(item);};
            addAction(L"Activate",[weak=weak_from_this(),id]{if(auto s=weak.lock()){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"activate"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));s->switchTo(r);}},id!=activeId());
            const bool protectedProfile=p.GetNamedBoolean(L"isDefault",false);
            addAction(L"Overwrite with current settings",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->overwrite(id);},!protectedProfile);
            menu.Items().Append(MenuFlyoutSeparator());
            addAction(L"Edit",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->edit(L"edit",id);},!protectedProfile);
            addAction(L"Duplicate",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->edit(L"duplicate",id);});
            addAction(L"Delete",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->remove(id);},!protectedProfile);
            actions.Flyout(menu);row.Children().Append(actions);Grid::SetColumn(actions,1); card.Child(row); profileRows.Children().Append(card);
        }
        if(count==0){TextBlock empty;empty.Text(text(activeSection?L"No active profile. Save this setup as a new profile.":L"No profiles found"));style(empty,L"SecondaryCaptionStyle");profileRows.Children().Append(empty);}
        }
    }
    std::wstring date(hstring const& iso)const
    {
        // ISO remains sortable in storage; display a local Windows date/time.
        SYSTEMTIME utc{};const auto raw=std::wstring(iso);unsigned short y=0,m=0,d=0,h=0,min=0,sec=0;
        if(swscanf_s(raw.c_str(),L"%hu-%hu-%huT%hu:%hu:%hu",&y,&m,&d,&h,&min,&sec)!=6)return raw;
        utc.wYear=y;utc.wMonth=m;utc.wDay=d;utc.wHour=h;utc.wMinute=min;utc.wSecond=sec;SYSTEMTIME local{};
        if(!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return raw;
        wchar_t day[128]{},time[64]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&local,nullptr,day,128,nullptr);GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&local,nullptr,time,64);return std::wstring(day)+L" "+time;
    }
    Windows::Foundation::IAsyncOperation<bool> perform(JsonObject request)
    {
        auto lifetime=shared_from_this();if(closed||busy)co_return false;
        const bool preview=request.GetNamedString(L"action",L"")==L"mixer-gain";
        if(request.HasKey(L"hostSession")&&request.GetNamedString(L"hostSession")!=snapshot.GetNamedString(L"hostSession",L""))co_return false;
        if(!preview&&!request.HasKey(L"revision")&&!(co_await flushPending()))co_return false;
        if(!preview){busy=true;render();}bool ok=false;
        if(!request.HasKey(L"profileId"))request.SetNamedValue(L"profileId",JsonValue::CreateStringValue(activeId()));
        if(!request.HasKey(L"generation"))request.SetNamedValue(L"generation",state.GetNamedValue(L"generation",JsonValue::CreateStringValue(L"0")));
        if(!request.HasKey(L"revision"))request.SetNamedValue(L"revision",JsonValue::CreateStringValue(chainText(static_cast<int64_t>(snapshot.GetNamedNumber(L"chainVersion",0)))));
        try{ok=co_await send("operating-command:"+to_string(request.Stringify()));}catch(...){}
        if(!preview){busy=false;if(!closed){update(host->snapshotJson);render();}}co_return ok;
    }
    fire_and_forget pluginCommand(std::string command){auto lifetime=shared_from_this();if(command.rfind("configure-plugin-buses:",0)==0){configurePluginBuses(to_hstring(command.substr(23)));co_return;}if(command.rfind("configure-isolation:",0)==0){configureIsolation(to_hstring(command.substr(20)));co_return;}if(busy||closed)co_return;try{if(!(co_await flushPending()))co_return;}catch(...){co_return;}busy=true;render();try{co_await send(std::move(command),true);}catch(...){}busy=false;if(!closed)update(host->snapshotJson);}
    Windows::Foundation::IAsyncOperation<bool> editDialog(hstring action,hstring id)
    {
        auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return false;dialogOpen=true;
        bool saved=false;
        try{
            const auto existing=profile(id);ContentDialog dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(root.ActualTheme());
            dialog.Title(box_value(text(action==L"create"?L"New profile":action==L"duplicate"?L"Duplicate profile":action==L"save"?L"Save profile":L"Edit profile")));
            StackPanel content;content.Spacing(12);TextBox name;name.Header(box_value(text(L"Name")));name.MaxLength(128);
            name.Text(chainText(std::wstring(existing.GetNamedString(L"name",L""))+(action==L"duplicate"?L" — "+std::wstring(text(L"Copy")):L"")));content.Children().Append(name);
            Automation::AutomationProperties::SetAutomationId(name,L"ProfileName");
            focusDialogTextBox(dialog,root,name);
            TextBox description;description.Header(box_value(text(L"Description (optional)")));description.MaxLength(1024);description.Text(existing.GetNamedString(L"description",L""));description.AcceptsReturn(true);description.MinHeight(88);description.MaxHeight(160);description.TextWrapping(TextWrapping::Wrap);content.Children().Append(description);

            ToggleSwitch audio;HoverHelp::SetToolTip(audio,box_value(text(L"Save current audio settings")));style(audio,L"CompactToggleSwitchStyle");audio.OnContent(box_value(L""));audio.OffContent(box_value(L""));audio.IsOn(existing.GetNamedBoolean(L"includeAudio",false));
            StackPanel audioControl;audioControl.Orientation(Orientation::Horizontal);audioControl.Spacing(12);TextBlock audioState;audioState.VerticalAlignment(VerticalAlignment::Center);audioState.Text(text(audio.IsOn()?L"On":L"Off"));audioControl.Children().Append(audioState);audioControl.Children().Append(audio);
            audio.Toggled([weak=weak_from_this(),weakLabel=make_weak(audioState)](const Windows::Foundation::IInspectable& sender,const auto&){if(auto s=weak.lock())if(auto label=weakLabel.get())label.Text(s->text(sender.as<ToggleSwitch>().IsOn()?L"On":L"Off"));});
            content.Children().Append(settingCard(L"Save current audio settings",L"Capture the current devices, sample rate, buffer, enabled channels and mono options with this profile.",audioControl,L"\xE767"));
            dialog.Content(content);dialog.PrimaryButtonText(text(L"Save"));dialog.CloseButtonText(text(L"Cancel"));dialog.DefaultButton(ContentDialogButton::Primary);
            dialog.IsPrimaryButtonEnabled(!name.Text().empty());name.TextChanged([weakDialog=make_weak(dialog)](const Windows::Foundation::IInspectable& sender,const auto&){if(auto dialog=weakDialog.get())dialog.IsPrimaryButtonEnabled(!sender.as<TextBox>().Text().empty());});
            if(co_await lightHostModern::ui::showAppDialog(dialog)==ContentDialogResult::Primary&&!closed){
                JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(action));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));r.SetNamedValue(L"name",JsonValue::CreateStringValue(name.Text()));r.SetNamedValue(L"description",JsonValue::CreateStringValue(description.Text()));r.SetNamedValue(L"includeAudio",JsonValue::CreateBooleanValue(audio.IsOn()));
                saved=co_await perform(r);
            }
        }catch(...){}dialogOpen=false;co_return saved;
    }
    fire_and_forget recoverProfiles(){auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;dialogOpen=true;
        try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Recover profiles?")));d.Content(box_value(text(L"Save the readable profiles as a recovered catalogue? The damaged original will be kept as a recovery file.")));d.PrimaryButtonText(text(L"Recover"));d.CloseButtonText(text(L"Cancel"));if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"recover-profiles"));co_await perform(r);}}catch(...){}dialogOpen=false;
    }
    fire_and_forget edit(hstring action,hstring id){auto lifetime=shared_from_this();co_await editDialog(action,id);}
    fire_and_forget danger(hstring command){auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;dialogOpen=true;
        try{const bool reset=command==L"factory-reset",profiles=command==L"delete-all-profiles";
            const auto title=text(reset?L"Restore all default settings?":profiles?L"Delete all profiles?":L"Restore all original names?");
            const auto message=text(reset?L"This deletes app settings, profiles and the plugin database, then restarts the app. Plugin files and exported logs are kept.":profiles?L"All saved profiles will be deleted. The empty default profile will become active and the current plugin chain will stop.":L"All custom names for devices, channels, plugins and elements will be removed, including those in saved profiles.");
            if(co_await confirmDanger(root,catalog,title,message,reset?L"Reset and restart":L"Confirm")){
                if(profiles){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(command));co_await perform(r);}
                else if(reset)co_await send("factory-reset:{\"uiPid\":"+std::to_string(GetCurrentProcessId())+",\"uiCreated\":\""+std::to_string(verbose::processBirth(GetCurrentProcess()))+"\"}");
                else co_await send(to_string(command));
            }
        }catch(...){}dialogOpen=false;
    }
    fire_and_forget overwrite(hstring id){auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;dialogOpen=true;
        try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Overwrite this profile?")));d.Content(box_value(text(L"Replace this profile's saved setup with the current app settings? Its name and description will be kept.")));d.PrimaryButtonText(text(L"Overwrite"));d.CloseButtonText(text(L"Cancel"));d.DefaultButton(ContentDialogButton::Close);
            if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"overwrite"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));co_await perform(r);}
        }catch(...){}dialogOpen=false;}
    fire_and_forget remove(hstring id)
    {
        auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;dialogOpen=true;
        try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Delete profile?")));d.Content(box_value(text(L"The saved profile will be deleted. If it is active, the empty default profile will be activated and audio through plugins will stop.")));d.PrimaryButtonText(text(L"Delete"));d.CloseButtonText(text(L"Cancel"));
            if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"delete"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));co_await perform(r);}
        }catch(...){}dialogOpen=false;
    }
    fire_and_forget switchTo(JsonObject request)
    {
        auto lifetime=shared_from_this();if(transitionPending||dialogOpen||busy||closed){render();co_return;}
        const auto action=request.GetNamedString(L"action");
        if(action==L"activate"&&request.GetNamedString(L"id")==activeId()){render();co_return;}
        transitionPending=true;freezeEdits(true);
        struct Transition { OperatingPresenter& owner;~Transition(){owner.transitionPending=false;owner.freezeEdits(false);} } transition{*this};
        try{
            if(!(co_await flushPending()))co_return;
            // Ask against current host state, including edits made in native editors.
            auto current=co_await host->requestAsync("operating-state");state=JsonObject::Parse(current);
            const bool cancellingModeChange=action==L"mode"&&request.GetNamedString(L"mode")==mode();
            if(state.GetNamedBoolean(L"dirty",false)&&!cancellingModeChange){
                dialogOpen=true;ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Save your changes?")));d.Content(box_value(text(L"Save or discard the current profile changes before switching.")));d.PrimaryButtonText(text(L"Save"));d.SecondaryButtonText(text(L"Discard"));d.CloseButtonText(text(L"Cancel"));
                const auto result=co_await lightHostModern::ui::showAppDialog(d);dialogOpen=false;
                if(result==ContentDialogResult::None){render();co_return;}
                if(result==ContentDialogResult::Primary&&!(co_await editDialog(activeId().empty()||profile(activeId()).GetNamedBoolean(L"isDefault",false)?L"create":L"save",profile(activeId()).GetNamedBoolean(L"isDefault",false)?L"":activeId()))){render();co_return;}
                request.SetNamedValue(L"discard",JsonValue::CreateBooleanValue(result==ContentDialogResult::Secondary));
            }
            if(co_await perform(request))if(!state.GetNamedString(L"pendingMode",L"").empty())co_await restartDialog();
        }catch(...){}dialogOpen=false;render();
    }
    Windows::Foundation::IAsyncAction restartDialog()
    {
        if(dialogOpen||closed)co_return;dialogOpen=true;
        try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Restart required")));d.Content(box_value(text(L"Restart LightHostModern to apply the selected operating mode. Audio will stop during restart.")));d.PrimaryButtonText(text(L"Restart now"));d.CloseButtonText(text(L"Restart later"));d.DefaultButton(ContentDialogButton::Close);
            if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary&&!closed){co_await send("restart-host:{\"uiPid\":"+std::to_string(GetCurrentProcessId())+",\"uiCreated\":\""+std::to_string(verbose::processBirth(GetCurrentProcess()))+"\"}");}
        }catch(...){}dialogOpen=false;
    }
    fire_and_forget showRestart(){auto lifetime=shared_from_this();co_await restartDialog();}
    fire_and_forget choosePlugin(Point position)
    {
        auto lifetime=shared_from_this();if(dialogOpen||busy||closed)co_return;dialogOpen=true;
        try{
            ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Add plugin")));d.PrimaryButtonText(text(L"Add"));d.CloseButtonText(text(L"Cancel"));d.IsPrimaryButtonEnabled(false);d.DefaultButton(ContentDialogButton::Primary);
            d.Resources().Insert(box_value(L"ContentDialogMaxWidth"),box_value(1000.0));
            auto page=winrt::make<winrt::LightHostModernWinUI::implementation::PluginsPageView>();
            auto view=winrt::get_self<winrt::LightHostModernWinUI::implementation::PluginsPageView>(page);
            view->setContentInsets(0);view->PluginsPanel().Visibility(Visibility::Visible);view->PluginsPanel().RowSpacing(0);view->PluginSectionCard().Visibility(Visibility::Collapsed);view->RunningPluginsPanel().Visibility(Visibility::Collapsed);view->InstalledPluginsPanel().Visibility(Visibility::Visible);
            page.MinWidth((std::min)(840.0,(std::max)(320.0,static_cast<double>(root.XamlRoot().Size().Width)-120)));page.Height((std::max)(200.0,(std::min)(560.0,static_cast<double>(root.XamlRoot().Size().Height)-240)));d.Content(page);
            auto list=view->InstalledPluginsListView();list.SelectionMode(ListViewSelectionMode::Multiple);list.ItemContainerStyle(page.Resources().Lookup(box_value(L"PluginPickerContainerStyle")).as<Style>());auto search=view->InstalledPluginSearchBox();search.PlaceholderText(text(L"Search installed plugins"));search.IsSuggestionListOpen(false);
            auto sort=view->InstalledPluginSortButton();sort.Label(text(L"Sort"));view->ScanForPluginsButton().Label(text(L"Scan for plugins"));
            auto controller=std::make_shared<PluginPageController>(false);list.ItemsSource(controller->items);
            struct PickerState {int sort=1;bool grouped=false,syncing=false;std::string action;hstring id;std::vector<hstring> selected;};auto picker=std::make_shared<PickerState>();picker->grouped=loadUiSetting(L"Plugins",L"GroupByManufacturer",L"0")==L"1";
            const auto updateCount=[weak=weak_from_this(),weakDialog=make_weak(d),picker,controller]{if(auto s=weak.lock())if(auto dialog=weakDialog.get()){
                dialog.PrimaryButtonText(s->text(L"Add")+L" ("+to_hstring(picker->selected.size())+L")");dialog.IsPrimaryButtonEnabled(!picker->selected.empty());
                auto accent=Application::Current().Resources().Lookup(box_value(L"AppAccentBrush")).as<SolidColorBrush>().Color();accent.A=100;
                for(const auto& item:controller->items)if(!item.IsGroupHeader())item.CardTint(SolidColorBrush(std::find(picker->selected.begin(),picker->selected.end(),item.Id())!=picker->selected.end()?accent:Windows::UI::Color{0,0,0,0}));
            }};
            auto fill=[weak=weak_from_this(),weakPage=make_weak(page),controller,picker,updateCount]{if(auto s=weak.lock())if(auto page=weakPage.get()){
                picker->syncing=true;auto v=winrt::get_self<winrt::LightHostModernWinUI::implementation::PluginsPageView>(page);auto rows=extractKnownPluginRows(to_string(s->snapshot.Stringify()));applyInstalledPluginRuntimeStatus(rows,extractActivePluginRows(to_string(s->snapshot.Stringify())));controller->adopt(std::move(rows));controller->render(std::wstring(v->InstalledPluginSearchBox().Text()),picker->sort,false,s->catalog,v->InstalledPluginsListView(),picker->grouped);v->InstalledPluginsEmptyText().Text(s->text(L"No plugins found. Try another search or scan for plugins."));v->InstalledPluginsEmptyText().Visibility(controller->items.Size()?Visibility::Collapsed:Visibility::Visible);
                picker->selected.erase(std::remove_if(picker->selected.begin(),picker->selected.end(),[&](const auto& id){return std::none_of(controller->source.begin(),controller->source.end(),[&](const auto& row){return to_hstring(row.knownId)==id;});}),picker->selected.end());
                auto selection=v->InstalledPluginsListView().SelectedItems();selection.Clear();for(const auto& item:controller->items)if(!item.IsGroupHeader()&&std::find(picker->selected.begin(),picker->selected.end(),item.Id())!=picker->selected.end())selection.Append(item);
                picker->syncing=false;updateCount();
            }};
            search.TextChanged([fill](const auto&,const auto&){fill();});
            MenuFlyout sortMenu;ToggleMenuFlyoutItem group;group.Text(text(L"Group by manufacturer"));group.IsChecked(picker->grouped);group.Click([picker,fill](const Windows::Foundation::IInspectable& sender,const auto&){picker->grouped=sender.as<ToggleMenuFlyoutItem>().IsChecked();saveUiSetting(L"Plugins",L"GroupByManufacturer",picker->grouped?L"1":L"0");fill();});sortMenu.Items().Append(group);sortMenu.Items().Append(MenuFlyoutSeparator());
            int mode=1;for(const auto* label:{L"Plugin name (A-Z)",L"Plugin name (Z-A)",L"Manufacturer (A-Z)",L"Manufacturer (Z-A)",L"Available first",L"Running first",L"VST3 then VST2",L"VST2 then VST3"}){MenuFlyoutItem item;item.Text(text(label));item.Click([picker,fill,mode](const auto&,const auto&){picker->sort=mode;fill();});sortMenu.Items().Append(item);++mode;}sort.Flyout(sortMenu);
            view->ScanForPluginsButton().Click([picker,weakDialog=make_weak(d)](const auto&,const auto&){picker->action="scan";if(auto dialog=weakDialog.get())dialog.Hide();});
            view->catalogActions=[weak=weak_from_this(),weakDialog=make_weak(d),picker,controller](Button button){if(auto s=weak.lock()){
                const auto item=button.DataContext().as<winrt::LightHostModernWinUI::PluginItem>();MenuFlyout menu;menu.Placement(Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
                const auto entry=[&](const wchar_t* label,const wchar_t* glyph,const char* action,bool enabled=true){MenuFlyoutItem m;m.Text(s->text(label));m.Icon(icon(glyph));m.IsEnabled(enabled);m.Click([picker,weakDialog,id=item.Id(),action=std::string(action)](const auto&,const auto&){picker->action=action;picker->id=id;if(auto dialog=weakDialog.get())dialog.Hide();});menu.Items().Append(m);};
                entry(L"Plugin details",L"\xE946","details");
                MenuFlyoutItem folder;folder.Text(s->text(L"Open folder"));folder.Icon(icon(L"\xE8B7"));folder.Click([weak,id=item.Id()](const auto&,const auto&){if(auto s=weak.lock())s->pluginCommand("open-known-plugin-location:"+to_string(id));});menu.Items().Append(folder);
                menu.Items().Append(MenuFlyoutSeparator());entry(L"Rename plugin",L"\xE8AC","rename");
                menu.Items().Append(MenuFlyoutSeparator());entry(L"Remove from database",L"\xE74D","remove");menu.ShowAt(button);
            }};
            list.SelectionChanged([picker,updateCount](const auto& sender,const SelectionChangedEventArgs& args){
                if(picker->syncing)return;
                for(const auto& removed:args.RemovedItems())if(auto item=removed.try_as<winrt::LightHostModernWinUI::PluginItem>())picker->selected.erase(std::remove(picker->selected.begin(),picker->selected.end(),item.Id()),picker->selected.end());
                for(const auto& added:args.AddedItems())if(auto item=added.try_as<winrt::LightHostModernWinUI::PluginItem>();item&&!item.IsGroupHeader()&&std::find(picker->selected.begin(),picker->selected.end(),item.Id())==picker->selected.end())picker->selected.push_back(item.Id());
                picker->syncing=true;auto selection=sender.as<ListView>().SelectedItems();for(uint32_t i=selection.Size();i>0;--i)if(selection.GetAt(i-1).as<winrt::LightHostModernWinUI::PluginItem>().IsGroupHeader())selection.RemoveAt(i-1);picker->syncing=false;updateCount();
            });
            while(!closed){
                fill();picker->action.clear();const auto result=co_await lightHostModern::ui::showAppDialog(d);if(closed)break;
                if(result==ContentDialogResult::Primary&&!picker->selected.empty())picker->action="add";
                const auto action=picker->action;const auto id=to_string(picker->id);if(action.empty())break;
                if(action=="scan"){co_await send("show-plugin-scan");break;}
                if(action=="add"){
                    const auto profileId=activeId();const auto generation=state.GetNamedValue(L"generation");size_t index=0;
                    for(const auto& knownId:picker->selected){if(closed)break;JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"add"));r.SetNamedValue(L"kind",JsonValue::CreateStringValue(L"plugin"));r.SetNamedValue(L"knownId",JsonValue::CreateStringValue(knownId));r.SetNamedValue(L"profileId",JsonValue::CreateStringValue(profileId));r.SetNamedValue(L"generation",generation);r.SetNamedValue(L"x",JsonValue::CreateNumberValue(position.X+(index%3)*450));r.SetNamedValue(L"y",JsonValue::CreateNumberValue(position.Y+(index/3)*320));if(!(co_await perform(r)))break;++index;}break;
                }
                if(action=="remove"){
                    ContentDialog confirm;confirm.XamlRoot(root.XamlRoot());confirm.RequestedTheme(root.ActualTheme());confirm.Title(box_value(text(L"Remove from database?")));confirm.Content(box_value(text(L"Remove this plugin from the database and any running instances from the chain? The plugin file will be kept.")));confirm.PrimaryButtonText(text(L"Remove"));confirm.CloseButtonText(text(L"Cancel"));confirm.DefaultButton(ContentDialogButton::Close);if(co_await lightHostModern::ui::showAppDialog(confirm)==ContentDialogResult::Primary)co_await send("remove-known-plugin:"+id);
                }else{
                    auto details=co_await host->requestAsync("known-plugin-details:"+id);if(!closed){auto command=co_await showPluginDialog(root,catalog,action,id,to_string(details),extractActivePluginRows(to_string(snapshot.Stringify())),false);if(!closed&&!command.empty())co_await send(to_string(command));}
                }
                if(!closed)update(host->snapshotJson);
            }
            view->catalogActions={};
        }catch(...){}dialogOpen=false;
    }
    fire_and_forget tick()
    {
        auto lifetime=shared_from_this();if(canvas&&!closed)canvas->visible(pluginsVisible&&mode()==L"chain"&&IsWindowVisible(hwnd)&&!IsIconic(hwnd));if(closed||!canvas||busy||polling||!pluginsVisible||mode()!=L"chain"||!IsWindowVisible(hwnd)||IsIconic(hwnd)||!host->connected)co_return;
        polling=true;try{const auto response=co_await host->requestAsync("routing-meters");if(!closed)canvas->levels(JsonObject::Parse(response));}catch(...){}polling=false;
    }
    FrameworkElement root{nullptr};::LightHostModernWinUI::LocalizationCatalog catalog;std::shared_ptr<HostConnection> host;Send sendCallback;HWND hwnd=nullptr;
    std::shared_ptr<ChainCanvas> canvas;DispatcherTimer timer;JsonObject snapshot,state;UIElement listRoot{nullptr};
    ComboBox settingsMode{nullptr};TextBox profileSearch;
    StackPanel profileRows,pluginHeader;HyperlinkButton settingsPending;ToggleSwitch expandChain;Border expandedSetting,distanceSetting,dotsSetting;TextBlock toggleText;
    fire_and_forget reviewRecoveredEdit() {
        auto lifetime=shared_from_this();if(dialogOpen||busy||!recoveredEdit.HasKey(L"graph"))co_return;
        dialogOpen=true;const auto id=activeId(),generation=state.GetNamedString(L"generation",L"");
        const auto recovery=JsonObject::Parse(recoveredEdit.Stringify());
        try {
            ContentDialog dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(root.ActualTheme());dialog.Title(box_value(text(L"Recover unfinished chain edit?")));
            dialog.Content(box_value(text(L"Restore the saved canvas layout, connections and mixer values for this profile? This replaces its current chain. The saved profile itself is unchanged until you overwrite it.")));
            dialog.PrimaryButtonText(text(L"Restore edit"));dialog.SecondaryButtonText(text(L"Discard recovered edit"));dialog.CloseButtonText(text(L"Cancel"));dialog.DefaultButton(ContentDialogButton::Close);
            const auto result=co_await lightHostModern::ui::showAppDialog(dialog);
            if(id==activeId()&&generation==state.GetNamedString(L"generation",L"")) {
                bool resolved=result==ContentDialogResult::Secondary;
                if(result==ContentDialogResult::Primary&&(co_await flushPending())) {
                    JsonObject request;request.SetNamedValue(L"action",JsonValue::CreateStringValue(L"graph"));request.SetNamedValue(L"graph",recovery.GetNamedObject(L"graph"));
                    request.SetNamedValue(L"profileId",JsonValue::CreateStringValue(id));request.SetNamedValue(L"generation",JsonValue::CreateStringValue(generation));
                    resolved=co_await perform(request);
                }
                if(resolved){clearChainRecovery(id);recoveredEdit=JsonObject{};render();}
            }
        }catch(...){}dialogOpen=false;
    }
    InfoBar pluginError,profilesError,recoveryNotice;hstring recoveryProfile;JsonObject recoveredEdit;
    bool busy=false,closed=false,syncing=false,dialogOpen=false,profilesAttached=false,settingsAttached=false,pluginsVisible=false,polling=false;std::string lastRender;
};
}
