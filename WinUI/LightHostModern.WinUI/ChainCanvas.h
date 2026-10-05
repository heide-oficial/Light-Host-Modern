#pragma once
#include "HoverHelp.h"
#include "DialogPresentation.h"
#include "Localization.h"
#include "VisualPreferences.h"
#include "UiPreferences.h"
#include "ChainEditRecovery.h"
#include "UiCoordination.h"
#include "DisplayNameDialog.h"
#include "VisualColorDialog.h"
#include "SearchSuggestions.h"
#include "ChannelDisplayNames.h"
#include "AudioPageController.h"
#include "SurfaceMaterials.h"
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <map>
#include <memory>
#include <set>
#include <queue>
#include <limits>

namespace lightHostModern::ui
{
using namespace winrt;
using namespace Windows::Data::Json;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using Windows::Foundation::Point;
inline hstring chainText(std::wstring const& value) { return hstring(value); }
template<class T> hstring chainText(T const& value) { return winrt::to_hstring(value); }

class ChainCanvas : public std::enable_shared_from_this<ChainCanvas>
{
public:
    using Action = std::function<Windows::Foundation::IAsyncOperation<bool>(JsonObject)>;
    Grid root;
    void create(::LightHostModernWinUI::LocalizationCatalog const& loc, Action command,
                std::function<void(std::string)> pluginCommand, std::function<void(Point)> addPlugin)
    {
        catalog = loc; dispatch = std::move(command); pluginAction = std::move(pluginCommand); add = std::move(addPlugin);
        root.RowDefinitions().Append(RowDefinition()); RowDefinition main; main.Height({1, GridUnitType::Star}); root.RowDefinitions().Append(main);
        root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        Grid toolbar;toolbar.ColumnSpacing(12);toolbar.RowSpacing(0);
        for(int i=0;i<2;++i){RowDefinition row;row.Height(GridLengthHelper::Auto());toolbar.RowDefinitions().Append(row);}
        ColumnDefinition searchColumn,actionsColumn;searchColumn.Width({1,GridUnitType::Star});actionsColumn.Width(GridLengthHelper::Auto());toolbar.ColumnDefinitions().Append(searchColumn);toolbar.ColumnDefinitions().Append(actionsColumn);
        searchBox.PlaceholderText(text(L"Find an element in the chain"));searchBox.TextMemberPath(L"Text");dismissSuggestionsOutside(searchBox);searchBox.MinWidth(180);searchBox.MaxWidth(360);searchBox.HorizontalAlignment(HorizontalAlignment::Left);searchBox.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetAutomationId(searchBox,L"ChainSearch");
        searchBox.TextChanged([weak=weak_from_this()](const auto&,const AutoSuggestBoxTextChangedEventArgs& e){if(auto s=weak.lock();s&&e.Reason()!=AutoSuggestionBoxTextChangeReason::SuggestionChosen)s->search();});
        searchBox.SuggestionChosen([weak=weak_from_this()](const auto&,const AutoSuggestBoxSuggestionChosenEventArgs& e){if(auto s=weak.lock()){
            auto item=e.SelectedItem().as<TextBlock>();s->highlighted=unbox_value<hstring>(item.Tag());s->focusNode(s->highlighted);s->updateCardStates();
        }});
        searchBox.QuerySubmitted([](const AutoSuggestBox& box,const AutoSuggestBoxQuerySubmittedEventArgs&){box.IsSuggestionListOpen(false);});
        toolbar.Children().Append(searchBox);
        Grid commands;commands.ColumnSpacing(4);commands.RowSpacing(4);commands.VerticalAlignment(VerticalAlignment::Center);
        undo=barButton(L"Undo",Symbol::Undo,[this]{action(L"undo");});redo=barButton(L"Redo",Symbol::Redo,[this]{action(L"redo");});
        StackPanel addCommands;addCommands.Orientation(Orientation::Horizontal);addCommands.Spacing(4);
        addCommands.Children().Append(barButton(L"Add plugin",Symbol::Add,[this]{add(centerPoint());}));
        addCommands.Children().Append(barButton(L"Add mixer",Symbol::Audio,[this]{addMixer(centerPoint());}));
        const auto separator=[] {Border line;line.Width(1);line.Height(24);line.Margin({8,0,8,0});line.VerticalAlignment(VerticalAlignment::Center);line.Style(Application::Current().Resources().Lookup(box_value(L"ChainToolbarSeparatorStyle")).as<Style>());line.Opacity(.45);return line;};
        commands.Children().Append(addCommands);commands.Children().Append(separator());
        commands.Children().Append(barButton(L"Organize",Symbol::ViewAll,[this]{confirmOrganize();}));
        commands.Children().Append(separator());StackPanel audioCommands;audioCommands.Orientation(Orientation::Horizontal);audioCommands.Spacing(4);
        styleToggle(muteCommand);styleToggle(bypassCommand);
        Automation::AutomationProperties::SetName(muteCommand,text(L"Mute output"));muteCommand.MinHeight(36);muteCommand.Content(commandContent(L"Mute output",Symbol::Mute));muteCommand.VerticalContentAlignment(VerticalAlignment::Center);
        muteCommand.Click([this](const auto&,const auto&){pluginAction(std::string("set-global-mute:")+(globalMuted?"0":"1"));});audioCommands.Children().Append(muteCommand);
        Automation::AutomationProperties::SetName(bypassCommand,text(L"Bypass chain"));bypassCommand.MinHeight(36);bypassCommand.Content(commandContent(L"Bypass chain",Symbol::Forward));bypassCommand.VerticalContentAlignment(VerticalAlignment::Center);
        bypassCommand.Click([this](const auto&,const auto&){pluginAction(std::string("set-global-bypass:")+(globalBypassed?"0":"1"));});audioCommands.Children().Append(bypassCommand);commands.Children().Append(audioCommands);
        
        ScrollViewer commandScroll;commandScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);commandScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);commandScroll.HorizontalScrollMode(ScrollMode::Enabled);commandScroll.VerticalScrollMode(ScrollMode::Disabled);commandScroll.VerticalAlignment(VerticalAlignment::Center);commandScroll.Content(commands);
        toolbar.Children().Append(commandScroll);Grid::SetColumn(commandScroll,1);
        toolbar.SizeChanged([weakToolbar=make_weak(toolbar),weakCommands=make_weak(commands),weakScroll=make_weak(commandScroll),weakSearch=make_weak(searchBox)](const auto&,const SizeChangedEventArgs& e){
            auto t=weakToolbar.get();auto c=weakCommands.get();auto sc=weakScroll.get();auto search=weakSearch.get();if(!t||!c||!sc||!search)return;
            double total=0,maxWidth=0;for(const auto& child:c.Children()){child.Measure({10000,100});const double width=child.try_as<Border>()?17:child.DesiredSize().Width;total+=width+4;maxWidth=(std::max)(maxWidth,width+4);}
            const double available=e.NewSize().Width;const bool wrap=available<total+240;t.RowSpacing(wrap?8:0);
            const bool stack=available<total;const int columns=stack?1:5;
            c.ColumnDefinitions().Clear();c.RowDefinitions().Clear();for(int i=0;i<columns;++i){ColumnDefinition col;col.Width(GridLengthHelper::Auto());c.ColumnDefinitions().Append(col);}
            for(int i=0;i<(stack?3:1);++i){RowDefinition row;row.Height(GridLengthHelper::Auto());c.RowDefinitions().Append(row);}
            for(uint32_t i=0;i<c.Children().Size();++i){const auto item=c.Children().GetAt(i).as<FrameworkElement>();item.Visibility(stack&&i%2?Visibility::Collapsed:Visibility::Visible);Grid::SetRow(item,stack?i/2:0);Grid::SetColumn(item,stack?0:i);item.HorizontalAlignment(HorizontalAlignment::Left);}
            Grid::SetRow(sc,wrap?1:0);Grid::SetColumn(sc,wrap?0:1);Grid::SetColumnSpan(sc,wrap?2:1);Grid::SetColumnSpan(search,wrap?2:1);
            search.HorizontalAlignment(HorizontalAlignment::Left);search.Width((std::min)(360.0,(std::max)(180.0,available-(wrap?0:total+12))));

        });
        Border toolbarCard;toolbarCard.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());toolbarCard.MinHeight(0);toolbarCard.Padding({8,8,8,8});toolbarCard.Margin({0,0,0,12});toolbar.VerticalAlignment(VerticalAlignment::Center);toolbarCard.Child(toolbar);root.Children().Append(toolbarCard);
        viewport.Style(Application::Current().Resources().Lookup(box_value(L"ChainViewportStyle")).as<Style>()); viewport.CornerRadius({8,8,8,8}); viewport.MinHeight(180);
        viewport.IsTabStop(true); viewport.UseSystemFocusVisuals(true);
        Automation::AutomationProperties::SetName(viewport, text(L"Plugin chain canvas"));
        dots.IsHitTestVisible(false);dots.Opacity(.16);dots.RenderTransform(dotTransform);viewport.Children().Append(dots);
        viewport.Children().Append(world); world.Children().Append(wires); world.Children().Append(cards);
        wireEnds.IsHitTestVisible(false);connectionOverlay.IsHitTestVisible(false);
        world.Children().Append(wireEnds);world.Children().Append(connectionOverlay);
        selectionLayer.IsHitTestVisible(false);viewport.Children().Append(selectionLayer);
        indicatorLayer.IsHitTestVisible(false);viewport.Children().Append(indicatorLayer);
        selectionBox.Style(Application::Current().Resources().Lookup(box_value(L"ChainSelectionStyle")).as<Style>());
        selectionBox.Visibility(Visibility::Collapsed);selectionLayer.Children().Append(selectionBox);
        viewport.BringIntoViewRequested([](const auto&,const BringIntoViewRequestedEventArgs& e){e.Handled(true);});
        world.RenderTransform(viewTransform); root.Children().Append(viewport); Grid::SetRow(viewport, 1);
        Border zoomPanel;zoomPanel.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>());zoomPanel.MinHeight(0);zoomPanel.Padding({4,4,4,4});StackPanel navigationPanels;navigationPanels.Orientation(Orientation::Horizontal);navigationPanels.Spacing(8);navigationPanels.HorizontalAlignment(HorizontalAlignment::Right);navigationPanels.VerticalAlignment(VerticalAlignment::Bottom);navigationPanels.Margin({12,12,12,12});
        Border viewOptions;viewOptions.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>());viewOptions.MinHeight(0);viewOptions.Padding({4,4,4,4});StackPanel viewControls;viewControls.Orientation(Orientation::Horizontal);viewControls.Spacing(4);
        StackPanel zoomControls;zoomControls.Orientation(Orientation::Horizontal);zoomControls.Spacing(4);
        const auto zoomButton=[&](const wchar_t* label,Symbol glyph,std::function<void()> fn){auto b=barButton(label,glyph,std::move(fn));b.Content(SymbolIcon(glyph));Automation::AutomationProperties::SetAutomationId(b,std::wstring(label)==L"Zoom in"?L"ChainZoomIn":std::wstring(label)==L"Zoom out"?L"ChainZoomOut":L"ChainFit");b.Width(36);b.Padding({0,0,0,0});zoomControls.Children().Append(b);};
        zoomButton(L"Zoom out",Symbol::Remove,[this]{zoomAt(viewCenter(),std::clamp(number(L"zoom",1)/1.15,minZoom,3.0));});
        zoomText.MinWidth(48);zoomText.TextAlignment(TextAlignment::Center);zoomText.VerticalAlignment(VerticalAlignment::Center);zoomText.TextLineBounds(TextLineBounds::Tight);zoomControls.Children().Append(zoomText);
        zoomButton(L"Zoom in",Symbol::Add,[this]{zoomAt(viewCenter(),std::clamp(number(L"zoom",1)*1.15,minZoom,3.0));});
        zoomButton(L"Fit to screen",static_cast<Symbol>(0xE9A6),[this]{fit();});
        Primitives::ToggleButton indicators;styleToggle(indicators);indicators.Width(36);indicators.Height(36);indicators.Padding({0,0,0,0});indicators.Content(SymbolIcon(static_cast<Symbol>(0xE8F0)));indicators.IsChecked(loadUiSetting(L"Chain",L"OffscreenIndicators",L"1")!=L"0");showIndicators=indicators.IsChecked().Value();
        Automation::AutomationProperties::SetName(indicators,text(L"Show offscreen elements"));Automation::AutomationProperties::SetAutomationId(indicators,L"ChainOffscreenIndicators");lightHostModern::ui::HoverHelp::SetToolTip(indicators,box_value(text(L"Show offscreen elements")));
        indicators.Click([weak=weak_from_this()](const Windows::Foundation::IInspectable& sender,const auto&){if(auto s=weak.lock()){s->showIndicators=sender.as<Primitives::ToggleButton>().IsChecked().Value();saveUiSetting(L"Chain",L"OffscreenIndicators",s->showIndicators?L"1":L"0");s->updateIndicators();}});viewControls.Children().Append(indicators);
        styleToggle(animationCommand);animationCommand.Width(36);animationCommand.Height(36);animationCommand.Padding({0,0,0,0});Automation::AutomationProperties::SetAutomationId(animationCommand,L"ChainAnimateFlow");
        animationCommand.Click([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock()){s->graph.SetNamedValue(L"animate",JsonValue::CreateBooleanValue(!s->graph.GetNamedBoolean(L"animate",true)));s->updateAnimationControl();s->submit();}});viewControls.Children().Append(animationCommand);
        zoomPanel.Child(zoomControls);viewOptions.Child(viewControls);navigationPanels.Children().Append(zoomPanel);navigationPanels.Children().Append(viewOptions);viewport.Children().Append(navigationPanels);SurfaceMaterials::current().attach(zoomPanel);SurfaceMaterials::current().attach(viewOptions);
        Border historyPanel;historyPanel.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>());historyPanel.MinHeight(0);historyPanel.Padding({4,4,4,4});historyPanel.HorizontalAlignment(HorizontalAlignment::Left);historyPanel.VerticalAlignment(VerticalAlignment::Bottom);historyPanel.Margin({12,12,12,12});
        undo.Content(SymbolIcon(Symbol::Undo));redo.Content(SymbolIcon(Symbol::Redo));for(auto b:{undo,redo}){b.Width(36);b.Padding({0,0,0,0});}
        StackPanel history;history.Orientation(Orientation::Horizontal);history.Spacing(4);history.Children().Append(undo);history.Children().Append(redo);historyPanel.Child(history);viewport.Children().Append(historyPanel);SurfaceMaterials::current().attach(historyPanel);
        Border statsPanel;statsPanel.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>());statsPanel.MinHeight(0);statsPanel.Padding({12,8,12,8});statsPanel.HorizontalAlignment(HorizontalAlignment::Left);statsPanel.VerticalAlignment(VerticalAlignment::Top);statsPanel.Margin({12,12,12,12});statsPanel.IsHitTestVisible(false);statsText.FontSize(12);statsText.TextWrapping(TextWrapping::Wrap);statsPanel.Child(statsText);viewport.Children().Append(statsPanel);SurfaceMaterials::current().attach(statsPanel);
        wireHint.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>());wireHint.MinHeight(0);wireHint.Padding({10,6,10,6});wireHint.HorizontalAlignment(HorizontalAlignment::Left);wireHint.VerticalAlignment(VerticalAlignment::Top);wireHint.IsHitTestVisible(false);wireHint.Visibility(Visibility::Collapsed);
        wireHintText.FontSize(12);wireHintText.TextWrapping(TextWrapping::Wrap);Automation::AutomationProperties::SetAutomationId(wireHintText,L"ChainWireHint");wireHint.Child(wireHintText);viewport.Children().Append(wireHint);SurfaceMaterials::current().attach(wireHint);
        viewport.SizeChanged([weak = weak_from_this()](const auto&, const auto&) { if (auto self = weak.lock()) {
            RectangleGeometry clip; clip.Rect({0,0,static_cast<float>(self->viewport.ActualWidth()),static_cast<float>(self->viewport.ActualHeight())}); self->viewport.Clip(clip);self->updateIndicators();
        }});
        viewport.LayoutUpdated([weak = weak_from_this()](const auto&, const auto&) { if (auto s = weak.lock()) s->updatePortPositions(); });
        viewport.PointerWheelChanged([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock()){
            e.Handled(true);if(s->busy||s->committing)return;
            const auto p=e.GetCurrentPoint(s->viewport);s->zoomAt(p.Position(),std::clamp(s->number(L"zoom",1)*std::pow(1.15,p.Properties().MouseWheelDelta()/120.0),minZoom,3.0));
        }});
        viewport.PointerPressed([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock())s->press(e);});
        // Buttons consume PointerReleased. Observe the routed event even when
        // handled so a port drag can finish over another button.
        viewport.AddHandler(UIElement::PointerMovedEvent(),box_value(Input::PointerEventHandler([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock())s->move(e);})),true);
        viewport.AddHandler(UIElement::PointerReleasedEvent(),box_value(Input::PointerEventHandler([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock())s->release(e);})),true);
        viewport.PointerExited([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock();s&&e.OriginalSource()==s->viewport&&!s->panning&&s->dragId.empty())s->wireFeedback(L"",{});});
        viewport.PointerCanceled([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock();s&&e.OriginalSource()==s->viewport)s->cancelGesture();});
        viewport.PointerCaptureLost([weak=weak_from_this()](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock();s&&!s->releasing&&e.OriginalSource()==s->viewport)s->cancelGesture();});
        viewport.RightTapped([](const auto&,const Input::RightTappedRoutedEventArgs& e){e.Handled(true);});
        viewport.KeyDown([weak=weak_from_this()](const auto&,const Input::KeyRoutedEventArgs& e){if(auto s=weak.lock()){
            auto source=e.OriginalSource().try_as<DependencyObject>();while(source){if(source.try_as<TextBox>()||source.try_as<Slider>())return;source=VisualTreeHelper::GetParent(source);}
            if(e.Key()==Windows::System::VirtualKey::Escape){s->cancelGesture();s->selected.clear();s->highlighted=L"";s->updateCardStates();e.Handled(true);}
            else if(e.Key()==Windows::System::VirtualKey::Delete&&!s->selected.empty()){s->batch(L"remove");e.Handled(true);}
        }});
        timer.Interval(std::chrono::milliseconds(33));
        timer.Tick([weak = weak_from_this()](const auto&, const auto&) { if (auto s = weak.lock()) s->tick(); }); timer.Start();
    }
    void applyDistancePreference(){distancePending=true;distanceChanged=GetTickCount64();timer.Start();}
    void preservePendingRecovery() {
        if(!contentSavePending||profileId.empty()||!graph.HasKey(L"nodes"))return;
        JsonObject recovery;recovery.SetNamedValue(L"profileId",JsonValue::CreateStringValue(profileId));recovery.SetNamedValue(L"generation",JsonValue::CreateStringValue(profileGeneration));recovery.SetNamedValue(L"graph",graph);
        writeChainRecovery(profileId,recovery);
    }
    void stop() { preservePendingRecovery(); timer.Stop(); }
    bool isFrozen()const{return editsFrozen;}
    void freezeEdits(bool freeze) { if(freeze&&!editsFrozen&&(panning||selecting||connecting||!dragId.empty()||!resizeId.empty()))cancelGesture();editsFrozen=freeze;root.IsHitTestVisible(!freeze);for(auto& mixer:mixerControls)for(auto& controls:mixer.second){controls.first.IsEnabled(!freeze);controls.second.IsEnabled(!freeze);} }
    Windows::Foundation::IAsyncOperation<bool> flushPending(uint64_t deadline=0) {
        auto lifetime=shared_from_this();winrt::apartment_context foreground;if(!deadline)deadline=GetTickCount64()+5000;
        const bool wasFrozen=editsFrozen;freezeEdits(true);
        struct Unfreeze { ChainCanvas& owner;bool previous;~Unfreeze(){owner.freezeEdits(previous);} } unfreeze{*this,wasFrozen};
        while((busy||committing||gainPreviewBusy)&&GetTickCount64()<deadline){co_await winrt::resume_after(std::chrono::milliseconds(20));co_await foreground;}
        if(busy||committing||gainPreviewBusy||GetTickCount64()>=deadline){preservePendingRecovery();co_return false;}
        if(!contentSavePending){persistView();viewSavePending=false;if(latestState.HasKey(L"graph"))update(latestState,latestPlugins,catalog,latestAudio,latestSelection,latestRevision,latestSession);co_return true;}
        preservePendingRecovery();const auto submitted=editRevision.local;
        JsonObject request;request.SetNamedValue(L"action",JsonValue::CreateStringValue(L"graph"));request.SetNamedValue(L"graph",JsonObject::Parse(graph.Stringify()));
        stamp(request);request.SetNamedValue(L"uiDeadline",JsonValue::CreateStringValue(to_hstring(deadline)));
        committing=true;bool ok=false;try{ok=co_await dispatch(request);}catch(...){}committing=false;
        if(ok&&editRevision.acknowledge(submitted)){contentSavePending=viewSavePending=false;gainDeltas.clear();gainPreviewPending=false;saveFailed=false;clearChainRecovery(profileId);}
        else{saveFailed=!ok;preservePendingRecovery();}
        if(ok&&!contentSavePending&&latestState.HasKey(L"graph"))update(latestState,latestPlugins,catalog,latestAudio,latestSelection,latestRevision,latestSession);
        co_return ok&&!contentSavePending;
    }
    void visible(bool value) { if(value&&!shown)pendingFit=true; shown = value; if (shown || viewSavePending || distancePending) timer.Start(); else timer.Stop(); if (!shown) for (auto& w : wireVisuals) w.dot.Visibility(Visibility::Collapsed); }
    void setBusy(bool value) { busy = value; }
    void update(JsonObject const& state, JsonArray const& active, ::LightHostModernWinUI::LocalizationCatalog const& loc, JsonObject const& audio, JsonObject const& selection, hstring revision=L"0", hstring session=L"")
    {
        latestState=state;latestPlugins=active;latestAudio=audio;latestSelection=selection;latestRevision=revision;latestSession=session;
        if (catalog.languageCode() != loc.languageCode()) lastSignature.clear();
        catalog = loc; plugins = active;audioSelection=selection;audioConfig=audio;
        for (const auto& entry : toolbarLabels) entry.first.Text(text(entry.second.c_str()));
        undo.IsEnabled(state.GetNamedBoolean(L"canUndo", false)); redo.IsEnabled(state.GetNamedBoolean(L"canRedo", false));
        searchBox.PlaceholderText(text(L"Find an element in the chain"));
        const auto incomingProfile=state.GetNamedString(L"activeProfile",L""), incomingGeneration=state.GetNamedString(L"generation",L"0");
        if(!profileId.empty() && (incomingProfile!=profileId || incomingGeneration!=profileGeneration || session!=hostSession)) {
            preservePendingRecovery();gainDeltas.clear();editRevision.reset();saveFailed=false;gainPreviewPending=false;viewSavePending=contentSavePending=false;connecting=panning=selecting=false;dragId=resizeId=L"";selected.clear();acceptView=true;
        }
        const auto incoming = state.GetNamedObject(L"graph", JsonObject{});
        if(!incoming.HasKey(L"nodes")||!resizeId.empty()||panning||selecting||!dragId.empty()||connecting||viewSavePending||contentSavePending||committing)return;
        auto next=JsonObject::Parse(incoming.Stringify());
        const auto profile=state.GetNamedString(L"activeProfile",L"");
        if(profile==profileId&&graph.HasKey(L"nodes")&&!acceptView){
            for(auto key:{L"zoom",L"panX",L"panY"})next.SetNamedValue(key,graph.GetNamedValue(key));
        }else{selected.clear();highlighted=L"";
            try{auto view=JsonObject::Parse(loadUiSetting(L"ChainViews",profile.empty()?L"unsaved":profile.c_str(),L"{}"));
                for(auto key:{L"zoom",L"panX",L"panY"})if(view.HasKey(key)){
                    const double value=view.GetNamedNumber(key,key==std::wstring(L"zoom")?1.0:0.0);
                    if(std::isfinite(value))next.SetNamedValue(key,JsonValue::CreateNumberValue(std::wstring(key)==L"zoom"?std::clamp(value,minZoom,3.0):std::clamp(value,-100000.0,100000.0)));
                }
            }catch(...){}
        }
        profileId=profile;profileGeneration=incomingGeneration;graphRevision=revision;hostSession=session;acceptView=false;
        activeInputs=audio.GetNamedArray(L"activeInputChannels",JsonArray{});activeOutputs=audio.GetNamedArray(L"activeOutputChannels",JsonArray{});
        const auto section=L"AudioChannels."+std::wstring(selection.GetNamedString(L"preferenceKey",L""));
        inputPairs=loadUiSetting(section.c_str(),L"InputMode",L"Individual")==L"Pairs";outputPairs=loadUiSetting(section.c_str(),L"OutputMode",L"Pairs")==L"Pairs";
        const auto signature=std::to_string(inputPairs)+std::to_string(outputPairs)+std::to_string(monoInputs)+std::to_string(monoOutput)+to_string(next.Stringify())+to_string(active.Stringify())+to_string(activeInputs.Stringify())+to_string(activeOutputs.Stringify())+to_string(audio.Stringify())+to_string(selection.Stringify());
        if(signature==lastSignature)return;
        lastSignature=signature;graph=next;draw();updateAnimationControl();
    }
    void levels(JsonObject const& values) { meterValues = values; lastLevels = GetTickCount64(); }
    void mono(bool input,bool output){monoInputs=input;monoOutput=output;}
    void globals(bool mute,bool bypass) { globalMuted=mute;globalBypassed=bypass;muteCommand.IsChecked(mute);bypassCommand.IsChecked(bypass); }
private:
    static bool interactiveSource(Windows::Foundation::IInspectable const& source)
    {
        auto element = source.try_as<DependencyObject>();
        while (element) {
            if (element.try_as<Primitives::ButtonBase>() || element.try_as<Slider>() || element.try_as<Primitives::Thumb>()) return true;
            element = VisualTreeHelper::GetParent(element);
        }
        return false;
    }
    hstring text(const wchar_t* value) const { return catalog.translatedSource(value); }
    void menuItem(MenuFlyout const& menu, const wchar_t* label, std::function<void()> fn,bool enabled=true)
    { MenuFlyoutItem item; item.IsEnabled(enabled); item.Text(text(label)); item.Click([fn=std::move(fn)](const auto&, const auto&) { fn(); }); menu.Items().Append(item); }
    double number(const wchar_t* key, double fallback = 0) const { return graph.GetNamedNumber(key, fallback); }
    Point worldPoint(Point p) const { const auto z = number(L"zoom",1); return {static_cast<float>((p.X-number(L"panX"))/z),static_cast<float>((p.Y-number(L"panY"))/z)}; }
    void transform() { viewTransform.ScaleX(number(L"zoom",1)); viewTransform.ScaleY(number(L"zoom",1)); viewTransform.TranslateX(number(L"panX")); viewTransform.TranslateY(number(L"panY")); const double percent=number(L"zoom",1)*100;wchar_t label[32]{};swprintf_s(label,percent<10?L"%.1f%%":L"%.0f%%",percent);zoomText.Text(label);updateDots();updateIndicators(); }
    JsonArray nodes() const { return graph.GetNamedArray(L"nodes", JsonArray{}); }
    JsonArray edges() const { return graph.GetNamedArray(L"edges", JsonArray{}); }
    JsonObject node(hstring const& id) const { for (const auto& v : nodes()) { auto n=v.GetObject(); if(n.GetNamedString(L"id")==id)return n; } return {}; }
    void action(const wchar_t* name) { if(busy||committing)return;acceptView=true; JsonObject request; request.SetNamedValue(L"action",JsonValue::CreateStringValue(name)); sendRequest(request); }
    void markContentChanged(){editRevision.changed();contentSavePending=true;saveFailed=false;}
    void stamp(JsonObject const& request) {
        request.SetNamedValue(L"profileId",JsonValue::CreateStringValue(profileId));request.SetNamedValue(L"generation",JsonValue::CreateStringValue(profileGeneration));
        request.SetNamedValue(L"revision",JsonValue::CreateStringValue(graphRevision));request.SetNamedValue(L"hostSession",JsonValue::CreateStringValue(hostSession));
    }
    void submit() { persistView();markContentChanged();preservePendingRecovery();viewSavePending=true;if(busy||committing)return;commitPending(); }
    fire_and_forget commitPending(){auto lifetime=shared_from_this();try{co_await flushPending();}catch(...){preservePendingRecovery();saveFailed=true;}}
    void addMixer(Point p) { JsonObject r; r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"add")); r.SetNamedValue(L"kind",JsonValue::CreateStringValue(L"mixer")); r.SetNamedValue(L"x",JsonValue::CreateNumberValue(p.X)); r.SetNamedValue(L"y",JsonValue::CreateNumberValue(p.Y)); sendRequest(r); }
    fire_and_forget sendRequest(JsonObject request)
    {
        auto lifetime=shared_from_this();if(committing||busy||editsFrozen)co_return;
        request=JsonObject::Parse(request.Stringify());
        try {
            // Structural requests are built only after the local graph has been
            // acknowledged and its snapshot adopted. Never relabel stale data.
            if((contentSavePending||viewSavePending)&&!(co_await flushPending()))co_return;
            if(busy||committing||editsFrozen)co_return;
            stamp(request);committing=true;
            const bool ok=co_await dispatch(request);committing=false;
            if(!ok){acceptView=false;lastSignature.clear();}
            if(latestState.HasKey(L"graph"))update(latestState,latestPlugins,catalog,latestAudio,latestSelection,latestRevision,latestSession);
        }catch(...){committing=false;preservePendingRecovery();}
    }
    void zoomAt(Point anchor,double zoom)
    {
        const auto p=worldPoint(anchor);graph.SetNamedValue(L"zoom",JsonValue::CreateNumberValue(zoom));
        graph.SetNamedValue(L"panX",JsonValue::CreateNumberValue(anchor.X-p.X*zoom));graph.SetNamedValue(L"panY",JsonValue::CreateNumberValue(anchor.Y-p.Y*zoom));
        transform();viewSavePending=true;viewChanged=GetTickCount64();
    }
    hstring title(JsonObject const& n)const {if(!n.GetNamedString(L"customName",L"").empty())return n.GetNamedString(L"customName");const auto k=n.GetNamedString(L"kind");return k==L"plugin"?n.GetNamedString(L"name"):text(k==L"input"?L"Audio input":k==L"output"?L"Audio output":L"Mixer");}
    void search()
    {
        auto query=std::wstring(searchBox.Text());std::transform(query.begin(),query.end(),query.begin(),towlower);
        auto items=single_threaded_observable_vector<Windows::Foundation::IInspectable>();
        for(const auto& v:nodes()){auto n=v.GetObject();auto name=std::wstring(title(n));std::transform(name.begin(),name.end(),name.begin(),towlower);
            if(query.empty()||name.find(query)!=std::wstring::npos){TextBlock item;item.Text(title(n));item.IsHitTestVisible(false);item.Tag(box_value(n.GetNamedString(L"id")));items.Append(item);}}
        searchBox.ItemsSource(items);searchBox.IsSuggestionListOpen(!query.empty()&&items.Size()>0);
    }
    double width(hstring const& id)const{const auto limit=maximumWidths.find(id);return std::clamp(node(id).GetNamedNumber(L"cardWidth",cardWidth),cardWidth,limit==maximumWidths.end()?262144.0:limit->second);}
    bool splitChannels(JsonObject const& n,bool output)const{const auto kind=n.GetNamedString(L"kind");return kind==L"input"?!inputPairs:kind==L"output"?!outputPairs:n.GetNamedBoolean(output?L"splitOutputs":L"splitInputs",false);}
    double height(hstring const& id)const {auto i=cardViews.find(id);return i!=cardViews.end()&&i->second.ActualHeight()>0?i->second.ActualHeight():220;}
    void focusNode(hstring const& id){selected.clear();auto n=node(id);const auto z=number(L"zoom",1);graph.SetNamedValue(L"panX",JsonValue::CreateNumberValue(viewport.ActualWidth()/2-(n.GetNamedNumber(L"x")+width(id)/2)*z));graph.SetNamedValue(L"panY",JsonValue::CreateNumberValue(viewport.ActualHeight()/2-(n.GetNamedNumber(L"y")+height(id)/2)*z));transform();viewSavePending=true;viewChanged=GetTickCount64();}
    bool enabled(JsonObject const& n,bool output,int channel)const{
        const auto k=n.GetNamedString(L"kind");if(k!=L"input"&&k!=L"output")return true;
        const auto& list=k==L"input"?activeInputs:activeOutputs;
        return channel>=0&&static_cast<uint32_t>(channel)<list.Size()&&list.GetBooleanAt(channel);
    }
    bool available(JsonObject const& n,bool output,int channel,int width=1)const {
        if(n.GetNamedString(L"kind")!=L"plugin")return true;
        const auto ports=n.GetNamedArray(output?L"outputPorts":L"inputPorts",JsonArray{});if(ports.Size()==0)return true;
        for(int c=channel;c<channel+width;++c)if(c<0||c>=(int)ports.Size()||ports.GetObjectAt(c).GetNamedNumber(L"physical",-1)<0)return false;
        return true;
    }
    int channelWidth(JsonObject const& n,bool output,int c)const {
        const int count=static_cast<int>(n.GetNamedNumber(output?L"outputs":L"inputs",0));
        if(splitChannels(n,output)||c+1>=count)return 1;
        if(n.GetNamedString(L"kind")==L"plugin"){
            const auto ports=n.GetNamedArray(output?L"outputPorts":L"inputPorts",JsonArray{});
            if(c+1>=(int)ports.Size())return 1;
            const auto a=ports.GetObjectAt(c),b=ports.GetObjectAt(c+1);
            return a.GetNamedNumber(L"bus",-1)==b.GetNamedNumber(L"bus",-2)&&a.GetNamedNumber(L"type")==1&&b.GetNamedNumber(L"type")==2?2:1;
        }
        return c%2==0?2:1;
    }
    bool visibleEdge(JsonObject const& e)const {
        const auto from=node(e.GetNamedString(L"from")),to=node(e.GetNamedString(L"to"));
        for(int c=0;c<e.GetNamedNumber(L"sourceWidth");++c)if(!enabled(from,true,static_cast<int>(e.GetNamedNumber(L"output"))+c))return false;
        for(int c=0;c<e.GetNamedNumber(L"targetWidth");++c)if(!enabled(to,false,static_cast<int>(e.GetNamedNumber(L"input"))+c))return false;
        return true;
    }
    hstring cardAtSource(Windows::Foundation::IInspectable const& source)const{
        auto element=source.try_as<DependencyObject>();while(element&&element!=viewport){
            if(auto b=element.try_as<Border>();b&&b.Tag())return unbox_value<hstring>(b.Tag());element=VisualTreeHelper::GetParent(element);
        }return L"";
    }
    void press(Input::PointerRoutedEventArgs const& e)
    {
        if(busy||committing)return;const auto point=e.GetCurrentPoint(viewport);const auto props=point.Properties();
        if(!props.IsRightButtonPressed()&&interactiveSource(e.OriginalSource()))return;
        pressPoint=lastPointer=point.Position();movedGesture=false;contextId=cardAtSource(e.OriginalSource());
        if(props.IsRightButtonPressed()){panning=true;}
        else if(props.IsLeftButtonPressed()){
            const bool additive=(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_SHIFT)&0x8000);
            if(!contextId.empty()){
                if(highlighted==contextId)highlighted=L"";
                if(additive&&selected.count(contextId)){selected.erase(contextId);updateCardStates();e.Handled(true);return;}
                if(!selected.count(contextId)){if(!additive)selected.clear();selected.insert(contextId);}dragId=contextId;
            }else{selecting=true;selectionStart=worldPoint(pressPoint);selectionBase=additive?selected:std::set<hstring>{};selected=selectionBase;selectionBox.Visibility(Visibility::Visible);selectionBox.Width(0);selectionBox.Height(0);}
            updateCardStates();
        }else return;
        viewport.CapturePointer(e.Pointer());e.Handled(true);
    }
    void batch(const wchar_t* operation){if(selected.empty()||busy||committing)return;JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"batch"));r.SetNamedValue(L"operation",JsonValue::CreateStringValue(operation));JsonArray ids;for(const auto& id:selected)ids.Append(JsonValue::CreateStringValue(id));r.SetNamedValue(L"ids",ids);sendRequest(r);}
    void disconnectSelection(int direction){auto list=edges();for(uint32_t i=list.Size();i>0;--i){auto e=list.GetObjectAt(i-1);if((direction!=1&&selected.count(e.GetNamedString(L"from")))||(direction!=2&&selected.count(e.GetNamedString(L"to"))))list.RemoveAt(i-1);}drawWires();updateCardStates();submit();}
    void disconnectAction(Windows::Foundation::Collections::IVector<MenuFlyoutItemBase> const& items,bool output){
        bool connected=false;for(const auto& v:edges())connected=connected||selected.count(v.GetObject().GetNamedString(output?L"from":L"to"));
        MenuFlyoutItem item;item.Text(text(output?L"Disconnect output wires":L"Disconnect input wires"));item.IsEnabled(connected);
        item.Click([weak=weak_from_this(),output](const auto&,const auto&){if(auto s=weak.lock())s->disconnectSelection(output?2:1);});items.Append(item);
    }
    std::vector<hstring> layerOrder() const {
        std::vector<hstring> order;for(const auto& value:nodes())order.push_back(value.GetObject().GetNamedString(L"id"));
        std::stable_sort(order.begin(),order.end(),[&](const auto& a,const auto& b){return node(a).GetNamedNumber(L"zOrder",0)<node(b).GetNamedNumber(L"zOrder",0);});return order;
    }
    void changeLayer(int operation){
        if(busy||committing||selected.empty())return;auto order=layerOrder();
        if(operation==2)std::stable_partition(order.begin(),order.end(),[&](const auto& id){return !selected.count(id);});
        else if(operation==-2)std::stable_partition(order.begin(),order.end(),[&](const auto& id){return selected.count(id)!=0;});
        else if(operation==1){for(size_t i=order.size();i>1;--i)if(selected.count(order[i-2])&&!selected.count(order[i-1]))std::swap(order[i-2],order[i-1]);}
        else{for(size_t i=1;i<order.size();++i)if(selected.count(order[i])&&!selected.count(order[i-1]))std::swap(order[i],order[i-1]);}
        for(size_t i=0;i<order.size();++i){node(order[i]).SetNamedValue(L"zOrder",JsonValue::CreateNumberValue(i));Canvas::SetZIndex(cardViews.at(order[i]),static_cast<int>(i));}
        submit();
    }
    void layerActions(MenuFlyout const& menu){
        const auto order=layerOrder();bool forward=false,backward=false;
        for(size_t i=0;i<order.size();++i)if(selected.count(order[i])){for(size_t j=0;j<i;++j)backward=backward||!selected.count(order[j]);for(size_t j=i+1;j<order.size();++j)forward=forward||!selected.count(order[j]);}
        MenuFlyoutSubItem layers;layers.Text(text(L"Arrange layers"));
        for(const auto& choice:std::vector<std::pair<const wchar_t*,int>>{{L"Bring to front",2},{L"Bring forward",1},{L"Send backward",-1},{L"Send to back",-2}}){
            MenuFlyoutItem item;item.Text(text(choice.first));item.IsEnabled(choice.second>0?forward:backward);
            item.Click([weak=weak_from_this(),operation=choice.second](const auto&,const auto&){if(auto s=weak.lock())s->changeLayer(operation);});layers.Items().Append(item);
        }menu.Items().Append(layers);
    }
    void selectionActions(MenuFlyout const& menu){
        bool pluginsSelected=false,removable=false,allHorizontal=true;
        for(const auto& id:selected){const auto n=node(id);const auto kind=n.GetNamedString(L"kind");pluginsSelected=pluginsSelected||kind==L"plugin";removable=removable||kind==L"plugin"||kind==L"mixer";allHorizontal=allHorizontal&&n.GetNamedBoolean(L"horizontalPorts",false);}
        if(pluginsSelected){
            menuItem(menu,L"Bypass selected plugins",[weak=weak_from_this()]{if(auto s=weak.lock())s->batch(L"bypass");});
            menuItem(menu,L"Enable selected plugins",[weak=weak_from_this()]{if(auto s=weak.lock())s->batch(L"enable");});
            menu.Items().Append(MenuFlyoutSeparator());
        }
        menuItem(menu,L"Card color",[weak=weak_from_this()]{if(auto s=weak.lock();s&&!s->selected.empty())s->changeColor(*s->selected.begin(),false,false,0,1,true);});
        layerActions(menu);
        menuItem(menu,allHorizontal?L"Vertical channels":L"Horizontal channels",[weak=weak_from_this(),allHorizontal]{if(auto s=weak.lock()){for(const auto& id:s->selected)s->node(id).SetNamedValue(L"horizontalPorts",JsonValue::CreateBooleanValue(!allHorizontal));s->draw();s->submit();}});
        menu.Items().Append(MenuFlyoutSeparator());
        for(bool output:{false,true}){MenuFlyoutSubItem channels;channels.Text(text(output?L"Output channels":L"Input channels"));disconnectAction(channels.Items(),output);menu.Items().Append(channels);}
        menu.Items().Append(MenuFlyoutSeparator());
        menuItem(menu,L"Delete selected elements",[weak=weak_from_this()]{if(auto s=weak.lock())s->batch(L"remove");},removable);
    }
    struct Port { hstring id; int channel=0,width=1; bool output=false; Point point{}; FrameworkElement control{nullptr}; TextBlock label{nullptr}; bool available=true; Shapes::Ellipse socket{nullptr}; };
    double portWidth(hstring const& id,bool output)const {
        double measured=100;for(auto const& p:ports)if(p.id==id&&p.output==output&&p.label){TextBlock label;label.FontSize(12);label.Text(p.label.Text());label.Measure({100000,1000});measured=(std::max)(measured,static_cast<double>(label.DesiredSize().Width)+34);}
        return measured;
    }
    double widthLimit(hstring const& id)const {
        const bool horizontal=node(id).GetNamedBoolean(L"horizontalPorts",false);double limit=360;
        for(bool output:{false,true}){int count=0;for(auto const& p:ports)if(p.id==id&&p.output==output)++count;
            const double w=portWidth(id,output);limit=(std::max)(limit,horizontal?28+count*w+(std::max)(0,count-1)*8:56+2*w);}
        return (std::min)(262144.0,limit);
    }
    void reflowPorts(hstring const& id,double w){
        const bool horizontal=node(id).GetNamedBoolean(L"horizontalPorts",false);
        const auto found=portGrids.find(id);if(found==portGrids.end())return;int side=0;
        for(auto grid:found->second){const int count=static_cast<int>(grid.Children().Size());const int columns=horizontal?(std::max)(1,(std::min)(count,static_cast<int>((w-20)/(portWidth(id,side!=0)+8)))):1;++side;
            if(grid.ColumnDefinitions().Size()==columns)continue;
            grid.ColumnDefinitions().Clear();grid.RowDefinitions().Clear();for(int c=0;c<columns;++c){ColumnDefinition col;col.Width({1,GridUnitType::Star});grid.ColumnDefinitions().Append(col);}
            for(int i=0;i<count;++i){if(i%columns==0){RowDefinition row;row.Height(GridLengthHelper::Auto());grid.RowDefinitions().Append(row);}auto child=grid.Children().GetAt(i).as<FrameworkElement>();Grid::SetColumn(child,i%columns);Grid::SetRow(child,i/columns);}
        }
    }
    void resizeCard(hstring const& id,double proposed){auto n=node(id);const double w=std::clamp(proposed,360.0,widthLimit(id));n.SetNamedValue(L"cardWidth",JsonValue::CreateNumberValue(w));n.SetNamedValue(L"cardHeight",JsonValue::CreateNumberValue(0));cardViews.at(id).Width(w);reflowPorts(id,w);}
    struct Wire { bool available=true; hstring id,key; TextBlock note{nullptr}; Shapes::Path line,glow,flow,startTail,endTail; Shapes::Ellipse dot,startSocket,endSocket; SolidColorBrush flowBrush; TranslateTransform motion; Point a{},b{}; Windows::UI::Color from{},to{}; };
    std::vector<Port> ports;
    std::vector<Wire> wireVisuals;
    Shapes::Path wirePreview{nullptr};
    struct ConnectionCue {Border frame{nullptr};Shapes::Ellipse ring{nullptr};hstring key;};
    ConnectionCue sourceCue,targetCue;
    static Point bezier(Point a, Point b, float t)
    { const float span=(std::max)(70.0f,std::abs(b.X-a.X)*.5f),u=1-t; return {u*u*u*a.X+3*u*u*t*(a.X+span)+3*u*t*t*(b.X-span)+t*t*t*b.X,u*u*u*a.Y+3*u*u*t*a.Y+3*u*t*t*b.Y+t*t*t*b.Y}; }
    static PathGeometry geometry(Point a, Point b)
    {
        const float span=(std::max)(70.0f,std::abs(b.X-a.X)*.5f);
        PathGeometry geometry; PathFigure f; f.StartPoint(a); BezierSegment curve; curve.Point1({a.X+span,a.Y}); curve.Point2({b.X-span,b.Y}); curve.Point3(b);
        f.Segments().Append(curve); geometry.Figures().Append(f); return geometry;
    }
    const Port* port(hstring const& id,int channel,bool output) const
    { for(const auto& p:ports)if(p.id==id&&p.output==output&&channel>=p.channel&&channel<p.channel+p.width)return &p;return nullptr; }
    bool hidden(JsonObject const& n,bool output,int channel) const
    { for(const auto& v:n.GetNamedArray(output?L"hiddenOutputs":L"hiddenInputs",JsonArray{}))if(static_cast<int>(v.GetNumber())==channel)return true;return false; }
    void draw()
    {
        // Keep controls (and keyboard focus) when only geometry, wire state or
        // worker telemetry changes. Recreate only structurally changed cards.
        std::map<hstring,std::string> signatures;
        for(const auto& value:nodes()) {
            auto n=value.GetObject();const auto id=n.GetNamedString(L"id");auto display=JsonObject::Parse(n.Stringify());
            for(auto key:{L"x",L"y",L"zOrder",L"gains",L"muted"})if(display.HasKey(key))display.Remove(key);
            auto key=to_string(display.Stringify())+to_string(title(n))+to_string(text(L"Inputs"));
            if(n.GetNamedString(L"kind")==L"input"||n.GetNamedString(L"kind")==L"output")
                key+=std::to_string(inputPairs)+std::to_string(outputPairs)+to_string(audioConfig.Stringify())+to_string(activeInputs.Stringify())+to_string(activeOutputs.Stringify());
            signatures[id]=std::move(key);
        }
        hstring restoreFocus;
        for(auto it=cardViews.begin();it!=cardViews.end();) {
            const auto id=it->first;
            if(signatures.count(id)&&cardSignatures[id]==signatures[id]){++it;continue;}
            if(cardMenus.count(id)&&cardMenus.at(id).FocusState()!=FocusState::Unfocused)restoreFocus=id;
            uint32_t index=0;if(cards.Children().IndexOf(it->second,index))cards.Children().RemoveAt(index);
            ports.erase(std::remove_if(ports.begin(),ports.end(),[&](const Port& p){return p.id==id;}),ports.end());
            statusViews.erase(id);cardStyles.erase(id);maximumWidths.erase(id);portGrids.erase(id);stripes.erase(id);dottedBorders.erase(id);cardContents.erase(id);cardMenus.erase(id);mixerControls.erase(id);silentSince.erase(id);
            it=cardViews.erase(it);
        }
        cardSignatures=std::move(signatures);
        for(auto i=selected.begin();i!=selected.end();)if(node(*i).HasKey(L"id"))++i;else i=selected.erase(i);
        updatingMixerControls=true;
        for(const auto& value:nodes()) {
            auto n=value.GetObject();const auto id=n.GetNamedString(L"id");
            if(!cardViews.count(id))drawCard(n);
            else {
                Canvas::SetLeft(cardViews.at(id),n.GetNamedNumber(L"x"));Canvas::SetTop(cardViews.at(id),n.GetNamedNumber(L"y"));Canvas::SetZIndex(cardViews.at(id),static_cast<int>(n.GetNamedNumber(L"zOrder",0)));
                if(mixerControls.count(id)) {const auto gains=n.GetNamedArray(L"gains"),muted=n.GetNamedArray(L"muted");size_t lane=0;
                    for(auto& control:mixerControls.at(id)){control.first.Value(gains.GetNumberAt(static_cast<uint32_t>(lane)));control.second.IsChecked(muted.GetBooleanAt(static_cast<uint32_t>(lane)));++lane;}}
            }
        }
        updatingMixerControls=false;
        if(!restoreFocus.empty()&&cardMenus.count(restoreFocus))cardMenus.at(restoreFocus).Focus(FocusState::Keyboard);
        transform(); drawWires();updateCardStates();
    }
    Windows::UI::Color portColor(hstring id,bool output,int channel) const {
        return visualColor(node(id).GetNamedObject(output?L"outputColors":L"inputColors",JsonObject{}).GetNamedString(chainText(channel),L""));
    }
    fire_and_forget changeColor(hstring id,bool channel=false,bool output=false,int start=0,int count=1,bool allSelected=false) {
        auto lifetime=shared_from_this();if(dialogOpen||busy||committing)co_return;dialogOpen=true;
        try {
            const auto profile=profileId,generation=profileGeneration;const auto targets=allSelected?selected:std::set<hstring>{id};auto n=node(id);const auto key=output?L"outputColors":L"inputColors";
            const auto current=channel?n.GetNamedObject(key,JsonObject{}).GetNamedString(chainText(start),L""):n.GetNamedString(L"cardColor",L"");
            const auto result=co_await chooseVisualColor(root,catalog,text(channel?L"Channel color":L"Card color"),current);
            if(result&&profile==profileId&&generation==profileGeneration&&node(id).HasKey(L"id")) {
                n=node(id);const auto color=unbox_value<hstring>(result);
                if(channel){auto colors=n.GetNamedObject(key,JsonObject{});for(int c=start;c<start+count;++c){const auto number=chainText(c);if(color.empty()){if(colors.HasKey(number))colors.Remove(number);}else colors.SetNamedValue(number,JsonValue::CreateStringValue(color));}n.SetNamedValue(key,colors);}
                else for(const auto& target:targets)if(auto card=node(target);card.HasKey(L"id"))card.SetNamedValue(L"cardColor",JsonValue::CreateStringValue(color));
                draw();submit();
            }
        }catch(...){}dialogOpen=false;
    }
    void updateDots() {
        const bool enabled=VisualPreferences::current().dottedCanvas&&!VisualPreferences::current().performance;
        dots.Visibility(enabled?Visibility::Visible:Visibility::Collapsed);if(!enabled)return;
        double step=28*number(L"zoom",1);if(!std::isfinite(step)||step<=0)step=20;for(int guard=0;step<20&&guard<32;++guard)step*=2;while(step>56)step/=2;
        const auto w=viewport.ActualWidth(),h=viewport.ActualHeight();if(w<=0||h<=0)return;
        const auto theme=root.ActualTheme();
        if(std::abs(dotStep-step)>.01||dotWidth!=w||dotHeight!=h||dotTheme!=theme) {
            PathGeometry geometry;
            for(double y=0;y<h+step*2;y+=step)for(double x=0;x<w+step*2;x+=step){PathFigure p;p.StartPoint({static_cast<float>(x),static_cast<float>(y)});LineSegment segment;segment.Point({static_cast<float>(x+.1),static_cast<float>(y)});p.Segments().Append(segment);geometry.Figures().Append(p);}
            dots.Data(geometry);dots.Stroke(SolidColorBrush(theme==ElementTheme::Light?Windows::UI::Color{255,40,40,40}:Windows::UI::Color{255,180,180,180}));dots.StrokeThickness(1.5);dots.StrokeStartLineCap(PenLineCap::Round);dots.StrokeEndLineCap(PenLineCap::Round);
            dotStep=step;dotWidth=w;dotHeight=h;dotTheme=theme;
        }
        const auto offset=[&](double v){return std::fmod(std::fmod(v,step)+step,step)-step;};dotTransform.X(offset(number(L"panX")));dotTransform.Y(offset(number(L"panY")));
    }
    void drawCard(JsonObject n)
    {
        const auto id=n.GetNamedString(L"id"),kind=n.GetNamedString(L"kind");
        Border card; card.Width(width(id));card.MinHeight(0);card.Tag(box_value(id));Automation::AutomationProperties::SetAutomationId(card,L"ChainCard-"+id); card.CornerRadius({8,8,8,8}); card.BorderThickness({1,1,1,1});
        try { card.Style(Application::Current().Resources().Lookup(box_value(L"ChainCardStyle")).as<Style>()); } catch(...){}
        card.Padding({0,0,0,0}); StackPanel content; content.Spacing(12);
        Grid header; header.ColumnSpacing(8); ColumnDefinition headerIcon,headerText,headerActions; headerIcon.Width(GridLengthHelper::Auto()); headerText.Width({1,GridUnitType::Star});headerActions.Width(GridLengthHelper::Auto()); header.ColumnDefinitions().Append(headerIcon); header.ColumnDefinitions().Append(headerText);header.ColumnDefinitions().Append(headerActions);
        Button options;options.Style(Application::Current().Resources().Lookup(box_value(L"SubtleButtonStyle")).as<Style>());options.Padding({4,4,4,4});options.Width(28);options.Height(28);options.MinWidth(0);options.MinHeight(0);options.VerticalAlignment(VerticalAlignment::Top);
        FontIcon more;more.FontFamily(FontFamily(L"Segoe Fluent Icons"));more.Glyph(L"\xE712");more.FontSize(16);options.Content(more);
        Automation::AutomationProperties::SetAutomationId(options,L"ChainMenu-"+id);Automation::AutomationProperties::SetName(options,text(L"Plugin options"));lightHostModern::ui::HoverHelp::SetToolTip(options,box_value(text(L"Plugin options")));
        options.Click([weak=weak_from_this(),id](const Windows::Foundation::IInspectable& sender,const auto&){if(auto s=weak.lock()){
            const auto button=sender.as<Button>();
            if(!s->selected.count(id)){s->selected.clear();s->selected.insert(id);s->updateCardStates();}s->showMenu(s->cardActions(id),button.TransformToVisual(s->root).TransformPoint({0,static_cast<float>(button.ActualHeight())}));
        }});
        options.GotFocus([weak=weak_from_this(),id](const Windows::Foundation::IInspectable& sender,const auto&){if(auto s=weak.lock();s&&sender.as<Button>().FocusState()==FocusState::Keyboard&&!s->selected.count(id)){s->selected.clear();s->selected.insert(id);s->updateCardStates();}});
        options.PreviewKeyDown([weak=weak_from_this(),id](const auto&,const Input::KeyRoutedEventArgs& e){if(auto s=weak.lock())s->moveCardWithKeyboard(id,e);});
        cardMenus[id]=options;
        Automation::AutomationProperties::SetName(options,this->title(n)+L". "+text(L"Options. Arrow keys move the card; Shift moves farther."));
        header.Children().Append(options);Grid::SetColumn(options,2);
        FontIcon nodeIcon; nodeIcon.FontFamily(FontFamily(L"Segoe Fluent Icons")); nodeIcon.Glyph(kind==L"input"?L"\xE720":kind==L"output"?L"\xE767":kind==L"mixer"?L"\xE9E9":L"\xEA86"); nodeIcon.FontSize(20); nodeIcon.VerticalAlignment(VerticalAlignment::Top); nodeIcon.Margin({0,2,0,0}); header.Children().Append(nodeIcon);
        StackPanel heading; heading.Spacing(3); header.Children().Append(heading); Grid::SetColumn(heading,1);
        header.Background(SolidColorBrush(Windows::UI::Color{0,0,0,0}));
        TextBlock title; title.Text(this->title(n));
        Automation::AutomationProperties::SetAutomationId(title,L"ChainTitle-"+id);
        title.FontSize(15); title.FontWeight(Windows::UI::Text::FontWeights::SemiBold()); title.TextTrimming(TextTrimming::CharacterEllipsis);
        heading.Children().Append(title);lightHostModern::ui::HoverHelp::SetToolTip(title,box_value(title.Text()));
        TextBlock state; state.Style(Application::Current().Resources().Lookup(box_value(L"SecondaryCaptionStyle")).as<Style>()); state.TextTrimming(TextTrimming::CharacterEllipsis);state.MaxLines(2); state.Text(text(kind==L"plugin"?L"Plugin":kind==L"mixer"?L"Mixer":kind==L"input"?L"Audio input":L"Audio output"));
        for(const auto& v:plugins){const auto p=v.GetObject();if(p.GetNamedString(L"instanceId",L"")==id){
            if(p.GetNamedString(L"loading",L"")!=L"loaded") {state.Text(p.GetNamedString(L"error",text(L"Plugin unavailable")));state.Opacity(1);state.TextWrapping(TextWrapping::Wrap);}
            else if(p.GetNamedBoolean(L"bypassed",false))state.Text(text(L"Bypassed"));
        }}
        statusViews[id]=state;heading.Children().Append(state);lightHostModern::ui::HoverHelp::SetToolTip(state,box_value(state.Text()));content.Children().Append(header);
        header.DoubleTapped([weak=weak_from_this(),id,kind](const auto&,const Input::DoubleTappedRoutedEventArgs& e){if(auto s=weak.lock();s&&kind==L"plugin"&&!interactiveSource(e.OriginalSource()))s->pluginAction("open-plugin-editor:"+to_string(id));});
        Grid rows; rows.ColumnSpacing(28); ColumnDefinition left,right; left.Width({1,GridUnitType::Star}); right.Width({1,GridUnitType::Star}); rows.ColumnDefinitions().Append(left);rows.ColumnDefinitions().Append(right);
        StackPanel ins,outs;ins.Spacing(6);outs.Spacing(6);rows.Children().Append(ins);rows.Children().Append(outs);Grid::SetColumn(outs,1);
        const bool horizontal=n.GetNamedBoolean(L"horizontalPorts",false);
        if(horizontal){rows.ColumnDefinitions().Clear();rows.ColumnDefinitions().Append(ColumnDefinition());for(int r=0;r<2;++r){RowDefinition row;row.Height(GridLengthHelper::Auto());rows.RowDefinitions().Append(row);}rows.RowSpacing(12);Grid::SetColumn(outs,0);Grid::SetRow(outs,1);}
        for(bool output:{false,true}){
            const int count=static_cast<int>(n.GetNamedNumber(output?L"outputs":L"inputs",0));
            auto& column=output?outs:ins;
            if(count>0){TextBlock caption; caption.Text(text(output?L"Outputs":L"Inputs"));caption.Style(Application::Current().Resources().Lookup(box_value(L"SecondaryCaptionStyle")).as<Style>());caption.HorizontalAlignment(output&&!horizontal?HorizontalAlignment::Right:HorizontalAlignment::Left);column.Children().Append(caption);}
            Grid portGrid;portGrid.RowSpacing(6);portGrid.ColumnSpacing(8);const int columns=horizontal?(std::max)(1,(std::min)(splitChannels(n,output)?count:(count+1)/2,static_cast<int>((width(id)-28)/140))):1;
            for(int c=0;c<columns;++c){ColumnDefinition col;col.Width({1,GridUnitType::Star});portGrid.ColumnDefinitions().Append(col);}column.Children().Append(portGrid);portGrids[id].push_back(portGrid);
            const bool split=splitChannels(n,output);
            const auto labels=n.GetNamedArray(output?L"outputNames":L"inputNames",JsonArray{});int row=0;
            for(int c=0;c<count;){
                if(((kind!=L"input"&&kind!=L"output")&&hidden(n,output,c))||!enabled(n,output,c)){++c;continue;}
                int width=channelWidth(n,output,c);
                if(width==2&&((kind!=L"input"&&kind!=L"output"&&hidden(n,output,c+1))||!enabled(n,output,c+1)))width=1;
                hstring label=width==2?chainText(std::to_wstring(c+1)+L"/"+std::to_wstring(c+2)+L" "+std::wstring(text(L"Stereo"))):static_cast<uint32_t>(c)<labels.Size()?labels.GetStringAt(c):chainText(std::to_wstring(c+1));
                if (width == 2 && kind == L"plugin" && static_cast<uint32_t>(c) < labels.Size()) {
                    const auto bus = std::wstring(labels.GetStringAt(c));
                    if (bus.size() > 2 && bus.substr(bus.size()-2) == L" L") label = chainText(bus.substr(0,bus.size()-2)+L" L/R");
                }
                label=channelLabel(n,output,c,width,label);
                const bool usable=available(n,output,c,width);if(!usable)label=label+L" \u00b7 "+text(L"Unavailable");
                Button b; b.Padding({6,5,6,5}); b.MinHeight(32); b.HorizontalAlignment(HorizontalAlignment::Stretch); b.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                Automation::AutomationProperties::SetAutomationId(b,L"ChainPort-"+id+(output?L"-out-":L"-in-")+chainText(c));
                Grid portContent; portContent.ColumnSpacing(6); ColumnDefinition dotColumn,labelColumn;
                dotColumn.Width(GridLengthHelper::Auto()); labelColumn.Width({1,GridUnitType::Star});
                portContent.ColumnDefinitions().Append(output?labelColumn:dotColumn); portContent.ColumnDefinitions().Append(output?dotColumn:labelColumn);
                Shapes::Ellipse socket; socket.Width(7);socket.Height(7);socket.Style(Application::Current().Resources().Lookup(box_value(L"ChainSocketStyle")).as<Style>());socket.Fill(SolidColorBrush(portColor(id,output,c)));socket.VerticalAlignment(VerticalAlignment::Center);portContent.Children().Append(socket);Grid::SetColumn(socket,output?1:0);
                TextBlock portLabel;portLabel.Text(label);portLabel.FontSize(12);portLabel.TextTrimming(TextTrimming::CharacterEllipsis);portLabel.VerticalAlignment(VerticalAlignment::Center);portLabel.TextAlignment(output?TextAlignment::Right:TextAlignment::Left);portContent.Children().Append(portLabel);Grid::SetColumn(portLabel,output?0:1);b.Content(portContent);
                lightHostModern::ui::HoverHelp::SetToolTip(b,box_value(label));Automation::AutomationProperties::SetName(b,chainText(std::wstring(text(output?L"Output":L"Input"))+L" "+std::wstring(label)));
                Port p{id,c,width,output,{static_cast<float>(n.GetNamedNumber(L"x")+(output?this->width(id)-14:14)),static_cast<float>(n.GetNamedNumber(L"y")+98+row*38)},nullptr};
                p.available=usable;b.Opacity(usable?1.0:.45);
                Border surface;surface.Background(SolidColorBrush(Windows::UI::Color{0,0,0,0}));surface.Child(b);b.IsTabStop(true);
                lightHostModern::ui::HoverHelp::SetToolTip(surface,box_value(label));
                surface.AddHandler(UIElement::PointerPressedEvent(),box_value(Input::PointerEventHandler([weak=weak_from_this(),p](const Windows::Foundation::IInspectable& sender,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock()){
                    if(!p.available||s->busy||s->committing||!e.GetCurrentPoint(s->viewport).Properties().IsLeftButtonPressed())return;
                    e.Handled(true);s->dragId=L"";s->selecting=false;s->highlighted=L"";s->updateCardStates();
                    const auto current=s->port(p.id,p.channel,p.output);s->source=current?*current:p;s->connecting=true;s->wirePointer=s->source.point;
                    // Transfer capture from ButtonBase to the canvas. Otherwise
                    // ButtonBase may consume release or keep capture itself.
                    sender.as<Border>().Child().as<Button>().ReleasePointerCaptures();
                    if(!s->viewport.CapturePointer(e.Pointer()))s->connecting=false;
                    s->wireFeedback(L"",{});s->drawWires();
                }})),true);
                b.PreviewKeyDown([weak=weak_from_this(),p](const auto&,const Input::KeyRoutedEventArgs& e){if(auto self=weak.lock()){
                    if(e.Key()==Windows::System::VirtualKey::Enter||e.Key()==Windows::System::VirtualKey::Space){e.Handled(true);self->connectMenu(p);}
                }});
                b.Click([weak=weak_from_this(),p](const auto&,const auto&){if(auto s=weak.lock();s&&p.available&&!s->busy&&!s->committing){if(s->connecting&&s->source.output!=p.output)s->connect(p);else{const auto current=s->port(p.id,p.channel,p.output);s->source=current?*current:p;s->connecting=true;s->wirePointer=s->source.point;s->drawWires();}}});
                p.control=surface;p.label=portLabel;p.socket=socket;if(row%columns==0){RowDefinition line;line.Height(GridLengthHelper::Auto());portGrid.RowDefinitions().Append(line);}portGrid.Children().Append(surface);Grid::SetRow(surface,row/columns);Grid::SetColumn(surface,row%columns);ports.push_back(p);c+=width;++row;
            }
        }
        content.Children().Append(rows);
        if(kind==L"mixer"){
            const auto gains=n.GetNamedArray(L"gains"),muted=n.GetNamedArray(L"muted");
            for(uint32_t lane=0;lane<gains.Size();++lane){
                Grid row;row.ColumnSpacing(8);ColumnDefinition numberColumn,sliderColumn,muteColumn;numberColumn.Width(GridLengthHelper::Auto());sliderColumn.Width({1,GridUnitType::Star});muteColumn.Width(GridLengthHelper::Auto());row.ColumnDefinitions().Append(numberColumn);row.ColumnDefinitions().Append(sliderColumn);row.ColumnDefinitions().Append(muteColumn);TextBlock label;label.Text(chainText(lane+1)+L" L/R");label.VerticalAlignment(VerticalAlignment::Center);row.Children().Append(label);
                Slider volume;volume.Minimum(0);volume.Maximum(2);volume.StepFrequency(.01);volume.MinWidth(60);volume.HorizontalAlignment(HorizontalAlignment::Stretch);volume.Value(gains.GetNumberAt(lane));lightHostModern::ui::HoverHelp::SetToolTip(volume,box_value(text(L"Controls Left and Right together.")));
                Automation::AutomationProperties::SetName(volume,text(L"Stereo input")+L" "+chainText(lane+1)+L" L/R");
                volume.ValueChanged([weak=weak_from_this(),id,lane](const auto&,const Primitives::RangeBaseValueChangedEventArgs& e){if(auto s=weak.lock();s&&!s->updatingMixerControls&&!s->editsFrozen&&!s->committing&&!s->busy){
                    auto n=s->node(id);n.GetNamedArray(L"gains").SetAt(lane,JsonValue::CreateNumberValue(e.NewValue()));s->markContentChanged();s->viewSavePending=true;s->viewChanged=GetTickCount64();s->gainDeltas.change(std::wstring(id),lane,e.NewValue());s->gainPreviewPending=true;
                }});row.Children().Append(volume);Grid::SetColumn(volume,1);
                volume.AddHandler(UIElement::PointerReleasedEvent(),box_value(Input::PointerEventHandler([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock()){s->lastGainPreview=0;s->previewMixerGains();}})),true);
                Primitives::ToggleButton mute;mute.Width(32);mute.Height(32);mute.Padding({0,0,0,0});FontIcon muteIcon;muteIcon.FontFamily(FontFamily(L"Segoe Fluent Icons"));muteIcon.Glyph(L"\xE74F");muteIcon.FontSize(14);mute.Content(muteIcon);lightHostModern::ui::HoverHelp::SetToolTip(mute,box_value(text(L"Mute")));Automation::AutomationProperties::SetName(mute,chainText(std::wstring(text(L"Mute"))+L" "+std::to_wstring(lane+1)));mute.IsChecked(muted.GetBooleanAt(lane));
                mute.Click([weak=weak_from_this(),id,lane](const Windows::Foundation::IInspectable& sender,const auto&){if(auto s=weak.lock()){s->node(id).GetNamedArray(L"muted").SetAt(lane,JsonValue::CreateBooleanValue(sender.as<Primitives::ToggleButton>().IsChecked().Value()));s->submit();}});
                mixerControls[id].emplace_back(volume,mute);
                row.Children().Append(mute);Grid::SetColumn(mute,2);content.Children().Append(row);
            }
        }
        Grid frame;frame.CornerRadius({7,7,7,7});Border tint;tint.Background(cardTint(n.GetNamedString(L"cardColor",L"")));tint.CornerRadius({7,7,7,7});tint.IsHitTestVisible(false);frame.Children().Append(tint);Shapes::Path stripe;stripe.IsHitTestVisible(false);stripe.Opacity(.08);stripe.StrokeThickness(7);stripe.Style(Application::Current().Resources().Lookup(box_value(L"ChainStripeStyle")).as<Style>());stripe.Visibility(Visibility::Collapsed);stripes[id]=stripe;Canvas stripeLayer;stripeLayer.IsHitTestVisible(false);stripeLayer.Children().Append(stripe);frame.Children().Append(stripeLayer);
        frame.SizeChanged([stripe](const auto&,const SizeChangedEventArgs& e){PathGeometry geometry;const double w=e.NewSize().Width,h=e.NewSize().Height;for(double x=-h;x<w;x+=24){PathFigure f;f.StartPoint({static_cast<float>(x),static_cast<float>(h)});LineSegment segment;segment.Point({static_cast<float>(x+h),0});f.Segments().Append(segment);geometry.Figures().Append(f);}stripe.Data(geometry);RectangleGeometry clip;clip.Rect({0,0,static_cast<float>(w),static_cast<float>(h)});stripe.Clip(clip);});cardContents[id]=content;content.Margin({14,14,14,30});frame.Children().Append(content);
        Shapes::Rectangle dotted;dotted.Style(Application::Current().Resources().Lookup(box_value(L"ChainDottedBorderStyle")).as<Style>());DoubleCollection borderDashes;borderDashes.Append(1);borderDashes.Append(3);dotted.StrokeDashArray(borderDashes);dotted.IsHitTestVisible(false);dotted.Visibility(Visibility::Collapsed);frame.Children().Append(dotted);dottedBorders[id]=dotted;
        Border grip;grip.Width(32);grip.Height(28);grip.HorizontalAlignment(HorizontalAlignment::Right);grip.VerticalAlignment(VerticalAlignment::Bottom);grip.Background(SolidColorBrush(Windows::UI::Color{0,0,0,0}));
        grip.Child(Markup::XamlReader::Load(L"<Path xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Data='M 8,21 L 21,8 M 14,21 L 21,14' Stroke='{ThemeResource TextFillColorSecondaryBrush}' StrokeThickness='1'/>").as<UIElement>());
        Automation::AutomationProperties::SetAutomationId(grip,L"ChainResize-"+id);Automation::AutomationProperties::SetName(grip,text(L"Resize card"));lightHostModern::ui::HoverHelp::SetToolTip(grip,box_value(text(L"Resize card")));
        grip.PointerEntered([](const auto&,const auto&){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));});grip.PointerMoved([](const auto&,const auto&){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));});grip.PointerExited([](const auto&,const auto&){SetCursor(LoadCursorW(nullptr,IDC_ARROW));});
        grip.PointerPressed([weak=weak_from_this(),id](const auto&,const Input::PointerRoutedEventArgs& e){if(auto s=weak.lock();s&&e.GetCurrentPoint(s->viewport).Properties().IsLeftButtonPressed()){
            e.Handled(true);if(s->busy||s->committing)return;s->cancelGesture();s->resizeId=id;s->resizeWidth=s->width(id);s->pressPoint=e.GetCurrentPoint(s->viewport).Position();s->viewport.CapturePointer(e.Pointer());
        }});
        frame.Children().Append(grip);card.Child(frame);SurfaceMaterials::current().attach(card);Canvas::SetZIndex(card,static_cast<int>(n.GetNamedNumber(L"zOrder",0)));cards.Children().Append(card);Canvas::SetLeft(card,n.GetNamedNumber(L"x"));Canvas::SetTop(card,n.GetNamedNumber(L"y"));cardViews[id]=card;maximumWidths[id]=widthLimit(id);card.Width(width(id));reflowPorts(id,width(id));
    }
    void moveCardWithKeyboard(hstring const& id,Input::KeyRoutedEventArgs const& e) {
        using Key=Windows::System::VirtualKey;if(busy||committing)return;
        const auto key=e.Key();if(key!=Key::Left&&key!=Key::Right&&key!=Key::Up&&key!=Key::Down)return;
        e.Handled(true);if(!selected.count(id)){selected.clear();selected.insert(id);}
        const double step=(GetKeyState(VK_SHIFT)&0x8000)?20.0:2.0;
        for(const auto& selectedId:selected){auto n=node(selectedId);
            const auto x=std::clamp(n.GetNamedNumber(L"x")+(key==Key::Left?-step:key==Key::Right?step:0),-100000.0,100000.0);
            const auto y=std::clamp(n.GetNamedNumber(L"y")+(key==Key::Up?-step:key==Key::Down?step:0),-100000.0,100000.0);
            n.SetNamedValue(L"x",JsonValue::CreateNumberValue(x));n.SetNamedValue(L"y",JsonValue::CreateNumberValue(y));Canvas::SetLeft(cardViews.at(selectedId),x);Canvas::SetTop(cardViews.at(selectedId),y);
        }
        limitDistance(selected);contentSavePending=viewSavePending=true;viewChanged=GetTickCount64();updatePortPositions();drawWires();updateCardStates();
    }
    void updatePortPositions()
    {
        bool changed=false;
        for(auto& port:ports)if(port.socket&&port.socket.IsLoaded()&&port.socket.ActualWidth()>0){
            const auto p=port.socket.TransformToVisual(world).TransformPoint({static_cast<float>(port.socket.ActualWidth()*.5),static_cast<float>(port.socket.ActualHeight()*.5)});
            if(std::abs(p.X-port.point.X)>.25f||std::abs(p.Y-port.point.Y)>.25f){port.point=p;changed=true;}
        }
        if(changed){drawWires();updateIndicators();}
    }
    void selectChannels(hstring id,bool output,bool pairs){auto n=node(id);const auto kind=n.GetNamedString(L"kind");
        if(kind==L"input"||kind==L"output"){const auto section=L"AudioChannels."+std::wstring(audioSelection.GetNamedString(L"preferenceKey",L""));saveUiSetting(section.c_str(),kind==L"input"?L"InputMode":L"OutputMode",pairs?L"Pairs":L"Individual");if(kind==L"input")inputPairs=pairs;else outputPairs=pairs;lastSignature.clear();draw();pluginAction("refresh-audio-view");}
        else{n.SetNamedValue(output?L"splitOutputs":L"splitInputs",JsonValue::CreateBooleanValue(!pairs));draw();submit();}
    }
    void styleToggle(Primitives::ToggleButton const& b){b.Background(SolidColorBrush(Windows::UI::Color{0,0,0,0}));b.BorderThickness({0,0,0,0});b.Padding({12,5,12,5});b.VerticalAlignment(VerticalAlignment::Center);}
    StackPanel commandContent(const wchar_t* label,Symbol glyph){StackPanel content;content.Orientation(Orientation::Horizontal);content.Spacing(8);content.VerticalAlignment(VerticalAlignment::Center);FontIcon icon;icon.FontFamily(FontFamily(L"Segoe Fluent Icons"));icon.Glyph(hstring(std::wstring(1,static_cast<wchar_t>(glyph))));icon.FontSize(16);icon.IsTextScaleFactorEnabled(true);icon.VerticalAlignment(VerticalAlignment::Center);content.Children().Append(icon);TextBlock caption;caption.Text(text(label));caption.TextLineBounds(TextLineBounds::Tight);caption.VerticalAlignment(VerticalAlignment::Center);content.Children().Append(caption);toolbarLabels.emplace_back(caption,label);return content;}
    Button barButton(const wchar_t* label,Symbol icon,std::function<void()> fn)
    {Button b;b.Content(commandContent(label,icon));b.Style(Application::Current().Resources().Lookup(box_value(L"SubtleButtonStyle")).as<Style>());b.VerticalContentAlignment(VerticalAlignment::Center);b.MinHeight(36);b.Padding({12,5,12,5});b.VerticalAlignment(VerticalAlignment::Center);lightHostModern::ui::HoverHelp::SetToolTip(b,box_value(text(label)));Automation::AutomationProperties::SetName(b,text(label));b.Click([fn=std::move(fn)](const auto&,const auto&){fn();});return b;}
    static void moveGeometry(Shapes::Path const& path,Point a,Point b) {
        auto g=path.Data().try_as<PathGeometry>();if(!g){path.Data(geometry(a,b));return;}
        auto f=g.Figures().GetAt(0);auto curve=f.Segments().GetAt(0).as<BezierSegment>();
        const float span=(std::max)(70.0f,std::abs(b.X-a.X)*.5f);
        f.StartPoint(a);curve.Point1({a.X+span,a.Y});curve.Point2({b.X-span,b.Y});curve.Point3(b);
    }
    bool canConnect(Port const& start,Port const& destination) const {
        if(!start.available||!destination.available||destination.output==start.output||destination.id==start.id)return false;
        const auto& from=start.output?start:destination;const auto& to=start.output?destination:start;
        std::set<hstring> reachable{to.id};bool changed=true;
        while(changed){changed=false;for(const auto& v:edges()){auto e=v.GetObject();if(reachable.count(e.GetNamedString(L"from")))changed=reachable.insert(e.GetNamedString(L"to")).second||changed;}}
        if(reachable.count(from.id))return false;
        for(const auto& v:edges()){auto e=v.GetObject();if(e.GetNamedString(L"from")==from.id&&e.GetNamedString(L"to")==to.id&&static_cast<int>(e.GetNamedNumber(L"output"))==from.channel&&static_cast<int>(e.GetNamedNumber(L"input"))==to.channel)return false;}
        return true;
    }
    const Port* connectionTarget(Point p) const {
        // Use the same topmost-card hit test for feedback and mouse release.
        hstring topCard;auto order=layerOrder();
        for(auto i=order.rbegin();i!=order.rend();++i){const auto card=cardViews.at(*i);const auto r=card.TransformToVisual(viewport).TransformBounds({0,0,static_cast<float>(card.ActualWidth()),static_cast<float>(card.ActualHeight())});if(inside(p,r)){topCard=*i;break;}}
        for(const auto& target:ports)if(target.id==topCard&&target.output!=source.output&&target.id!=source.id){
            auto r=target.control.TransformToVisual(viewport).TransformBounds({0,0,static_cast<float>(target.control.ActualWidth()),static_cast<float>(target.control.ActualHeight())});r.X-=4;r.Y-=4;r.Width+=8;r.Height+=8;
            if(inside(p,r)&&canConnect(source,target))return &target;
        }return nullptr;
    }
    void connectionCue(ConnectionCue& cue,const Port* p) {
        if(!p){cue.key=L"";if(cue.frame)cue.frame.Visibility(Visibility::Collapsed);if(cue.ring)cue.ring.Visibility(Visibility::Collapsed);return;}
        if(!cue.frame){cue.frame=Border();cue.frame.CornerRadius({5,5,5,5});cue.frame.BorderThickness({2,2,2,2});cue.frame.IsHitTestVisible(false);connectionOverlay.Children().Append(cue.frame);
            cue.ring=Shapes::Ellipse();cue.ring.StrokeThickness(2.5);cue.ring.Width(18);cue.ring.Height(18);cue.ring.IsHitTestVisible(false);connectionOverlay.Children().Append(cue.ring);}
        const auto bounds=p->control.TransformToVisual(world).TransformBounds({0,0,static_cast<float>(p->control.ActualWidth()),static_cast<float>(p->control.ActualHeight())});
        const auto color=portColor(p->id,p->output,p->channel);auto fill=color;fill.A=40;
        cue.frame.BorderBrush(SolidColorBrush(color));cue.frame.Background(SolidColorBrush(fill));cue.frame.Width(bounds.Width+4);cue.frame.Height(bounds.Height+4);Canvas::SetLeft(cue.frame,bounds.X-2);Canvas::SetTop(cue.frame,bounds.Y-2);
        cue.ring.Stroke(SolidColorBrush(color));Canvas::SetLeft(cue.ring,p->point.X-9);Canvas::SetTop(cue.ring,p->point.Y-9);
        cue.frame.Visibility(Visibility::Visible);cue.ring.Visibility(Visibility::Visible);
        const auto key=p->id+(p->output?L":out:":L":in:")+chainText(p->channel);if(cue.key==key)return;cue.key=key;
        // One short compositor animation on entry, not a looping UI-thread timer.
        bool animate=!VisualPreferences::current().performance;try{animate=animate&&Windows::UI::ViewManagement::UISettings().AnimationsEnabled();}catch(...){}
        for(const auto& element:std::vector<UIElement>{cue.frame,cue.ring}){auto visual=Hosting::ElementCompositionPreview::GetElementVisual(element);visual.StopAnimation(L"Opacity");visual.Opacity(1);
            if(animate){auto compositor=visual.Compositor();auto animation=compositor.CreateScalarKeyFrameAnimation();animation.Duration(std::chrono::milliseconds(180));animation.InsertKeyFrame(0,.25f);animation.InsertKeyFrame(1,1,compositor.CreateCubicBezierEasingFunction({.23f,1},{.32f,1}));visual.StartAnimation(L"Opacity",animation);}}
    }
    void drawPreview() {
        if(!connecting){if(wirePreview)wirePreview.Visibility(Visibility::Collapsed);connectionCue(sourceCue,nullptr);connectionCue(targetCue,nullptr);return;}
        if(const auto current=port(source.id,source.channel,source.output))source=*current;
        if(!wirePreview){wirePreview=Shapes::Path();wirePreview.Style(Application::Current().Resources().Lookup(box_value(L"ChainWireStyle")).as<Style>());wirePreview.IsHitTestVisible(false);connectionOverlay.Children().Append(wirePreview);}
        const auto target=connectionTarget(world.TransformToVisual(viewport).TransformPoint(wirePointer));const auto end=target?target->point:wirePointer;
        wirePreview.Visibility(Visibility::Visible);wirePreview.StrokeThickness((std::max)(3.0,3.0/number(L"zoom",1)));
        moveGeometry(wirePreview,source.output?source.point:end,source.output?end:source.point);
        wirePreview.Stroke(SolidColorBrush(portColor(source.id,source.output,source.channel)));
        connectionCue(sourceCue,&source);connectionCue(targetCue,target);
    }
    void endpointTail(Shapes::Path const& tail,Shapes::Ellipse const& socket,Point a,Point b,Port const& endpoint,bool start,Windows::UI::Color color) {
        // Extract a short segment of the SAME cubic up to the card boundary.
        // The long body stays below cards; only its socket leads are above them.
        const auto card=cardViews.at(endpoint.id);auto bounds=card.TransformToVisual(world).TransformBounds({0,0,static_cast<float>(card.ActualWidth()),static_cast<float>(card.ActualHeight())});bounds.X-=32;bounds.Y-=32;bounds.Width+=64;bounds.Height+=64;
        float length=.45f;for(int i=1;i<=128;++i){const float distance=.45f*i/128;const auto p=bezier(a,b,start?distance:1-distance);if(!inside(p,bounds)){length=distance;break;}}
        const float t0=start?0:1-length,t1=start?length:1,span=(std::max)(70.0f,std::abs(b.X-a.X)*.5f);
        const auto tangent=[&](float t){const float u=1-t;return Point{3*u*u*span+6*u*t*(b.X-a.X-2*span)+3*t*t*span,6*u*t*(b.Y-a.Y)};};
        const auto first=bezier(a,b,t0),last=bezier(a,b,t1),d0=tangent(t0),d1=tangent(t1);const float factor=(t1-t0)/3;
        auto g=tail.Data().try_as<PathGeometry>();if(!g){g=PathGeometry();PathFigure f;f.Segments().Append(BezierSegment());g.Figures().Append(f);tail.Data(g);}auto figure=g.Figures().GetAt(0);auto curve=figure.Segments().GetAt(0).as<BezierSegment>();figure.StartPoint(first);curve.Point1({first.X+d0.X*factor,first.Y+d0.Y*factor});curve.Point2({last.X-d1.X*factor,last.Y-d1.Y*factor});curve.Point3(last);
        // Fade the front layer into the wire body instead of ending an opaque
        // overlay at a hard boundary. Keep the endpoint's custom color.
        float left=(std::min)(first.X,last.X),right=(std::max)(first.X,last.X),top=(std::min)(first.Y,last.Y),bottom=(std::max)(first.Y,last.Y);
        for(int i=1;i<32;++i){const auto p=bezier(a,b,t0+(t1-t0)*i/32);left=(std::min)(left,p.X);right=(std::max)(right,p.X);top=(std::min)(top,p.Y);bottom=(std::max)(bottom,p.Y);}
        const auto relative=[&](Point p){return Point{right-left>.01f?(p.X-left)/(right-left):.5f,bottom-top>.01f?(p.Y-top)/(bottom-top):.5f};};
        auto gradient=tail.Stroke().try_as<LinearGradientBrush>();if(!gradient){gradient=LinearGradientBrush();GradientStop firstStop,lastStop;firstStop.Offset(0);lastStop.Offset(1);gradient.GradientStops().Append(firstStop);gradient.GradientStops().Append(lastStop);tail.Stroke(gradient);}
        auto transparent=color;transparent.A=0;gradient.StartPoint(relative(first));gradient.EndPoint(relative(last));gradient.GradientStops().GetAt(0).Color(start?color:transparent);gradient.GradientStops().GetAt(1).Color(start?transparent:color);
        socket.Fill(SolidColorBrush(color));Canvas::SetLeft(socket,endpoint.point.X-3.5);Canvas::SetTop(socket,endpoint.point.Y-3.5);
    }
    void drawWires()
    {
        std::map<hstring,Wire> previous;for(const auto& w:wireVisuals)previous.emplace(w.key,w);wireVisuals.clear();
        const auto sameColor=[](Windows::UI::Color a,Windows::UI::Color b){return a.A==b.A&&a.R==b.R&&a.G==b.G&&a.B==b.B;};
        for(const auto& value:edges()){
            const auto e=value.GetObject();if(!visibleEdge(e))continue;
            const auto* a=port(e.GetNamedString(L"from"),static_cast<int>(e.GetNamedNumber(L"output")),true);
            const auto* b=port(e.GetNamedString(L"to"),static_cast<int>(e.GetNamedNumber(L"input")),false);if(!a||!b)continue;
            const int lanes=((e.GetNamedNumber(L"sourceWidth")==2&&a->width==1)||(e.GetNamedNumber(L"targetWidth")==2&&b->width==1))?2:1;
            for(int lane=0;lane<lanes;++lane){
                const int output=static_cast<int>(e.GetNamedNumber(L"output"))+(e.GetNamedNumber(L"sourceWidth")==2?lane:0),input=static_cast<int>(e.GetNamedNumber(L"input"))+(e.GetNamedNumber(L"targetWidth")==2?lane:0);
                const auto* from=port(e.GetNamedString(L"from"),output,true);const auto* to=port(e.GetNamedString(L"to"),input,false);if(!from||!to)continue;
                const auto id=e.GetNamedString(L"id"),key=id+L":"+chainText(lane);auto found=previous.find(key);const bool fresh=found==previous.end();
                Wire w=fresh?Wire{}:found->second;if(!fresh)previous.erase(found);w.id=id;w.key=key;
                const auto fromColor=portColor(from->id,true,output),toColor=portColor(to->id,false,input);
                const bool changed=fresh||w.a.X!=from->point.X||w.a.Y!=from->point.Y||w.b.X!=to->point.X||w.b.Y!=to->point.Y||!sameColor(w.from,fromColor)||!sameColor(w.to,toColor);
                w.a=from->point;w.b=to->point;w.from=fromColor;w.to=toColor;
                if(fresh){
                    w.line.Style(Application::Current().Resources().Lookup(box_value(L"ChainWireStyle")).as<Style>());w.line.StrokeThickness(2);w.line.Opacity(.25);w.line.IsHitTestVisible(false);
                    w.glow.StrokeThickness(10);w.glow.IsHitTestVisible(false);w.glow.Opacity(0);
                    w.flow.StrokeThickness(5);DoubleCollection dashes;dashes.Append(2);dashes.Append(10);w.flow.StrokeDashArray(dashes);w.flow.IsHitTestVisible(false);w.flow.Visibility(Visibility::Collapsed);
                    w.dot.Width(9);w.dot.Height(9);w.dot.Fill(w.flowBrush);w.dot.Style(Application::Current().Resources().Lookup(box_value(L"ChainSocketStyle")).as<Style>());w.dot.RenderTransform(w.motion);w.dot.IsHitTestVisible(false);w.dot.Visibility(Visibility::Collapsed);
                    wires.Children().Append(w.glow);wires.Children().Append(w.line);wires.Children().Append(w.flow);wires.Children().Append(w.dot);
                    for(const auto& tail:{w.startTail,w.endTail}){tail.IsHitTestVisible(false);tail.StrokeStartLineCap(PenLineCap::Round);tail.StrokeEndLineCap(PenLineCap::Round);wireEnds.Children().Append(tail);}
                    for(const auto& socket:{w.startSocket,w.endSocket}){socket.IsHitTestVisible(false);socket.Width(7);socket.Height(7);wireEnds.Children().Append(socket);}
                }
                if(changed){
                    // Each visual owns its geometry; update it without recreating XAML paths.
                    for(const auto& path:{w.line,w.glow,w.flow})moveGeometry(path,w.a,w.b);
                    float left=(std::min)(w.a.X,w.b.X),right=(std::max)(w.a.X,w.b.X),top=(std::min)(w.a.Y,w.b.Y),bottom=(std::max)(w.a.Y,w.b.Y);
                    for(int sample=1;sample<64;++sample){const auto p=bezier(w.a,w.b,sample/64.0f);left=(std::min)(left,p.X);right=(std::max)(right,p.X);top=(std::min)(top,p.Y);bottom=(std::max)(bottom,p.Y);}
                    const auto relative=[&](Point p){return Point{right-left>.01f?(p.X-left)/(right-left):.5f,bottom-top>.01f?(p.Y-top)/(bottom-top):.5f};};
                    auto gradient=w.line.Stroke().try_as<LinearGradientBrush>();if(!gradient){gradient=LinearGradientBrush();GradientStop first,last;first.Offset(0);last.Offset(1);gradient.GradientStops().Append(first);gradient.GradientStops().Append(last);for(const auto& path:{w.line,w.glow,w.flow})path.Stroke(gradient);}
                    gradient.StartPoint(relative(w.a));gradient.EndPoint(relative(w.b));gradient.GradientStops().GetAt(0).Color(w.from);gradient.GradientStops().GetAt(1).Color(w.to);
                    endpointTail(w.startTail,w.startSocket,w.a,w.b,*from,true,w.from);endpointTail(w.endTail,w.endSocket,w.a,w.b,*to,false,w.to);
                }
                w.available=available(node(e.GetNamedString(L"from")),true,static_cast<int>(e.GetNamedNumber(L"output")),static_cast<int>(e.GetNamedNumber(L"sourceWidth")))&&available(node(e.GetNamedString(L"to")),false,static_cast<int>(e.GetNamedNumber(L"input")),static_cast<int>(e.GetNamedNumber(L"targetWidth")));
                for(const auto& path:{w.line,w.startTail,w.endTail}){DoubleCollection pattern;if(!w.available){pattern.Append(4);pattern.Append(4);}path.StrokeDashArray(pattern);}
                if(!w.available||e.GetNamedNumber(L"sourceWidth")!=e.GetNamedNumber(L"targetWidth")){
                    if(!w.note){w.note=TextBlock();w.note.FontSize(10);w.note.IsHitTestVisible(false);wires.Children().Append(w.note);}
                    w.note.Text(text(!w.available?L"Unavailable":e.GetNamedNumber(L"sourceWidth")==2?L"Stereo \u2192 mono":L"Mono \u2192 stereo"));const auto mid=bezier(w.a,w.b,.5f);Canvas::SetLeft(w.note,mid.X);Canvas::SetTop(w.note,mid.Y-18);
                }else if(w.note){uint32_t at;if(wires.Children().IndexOf(w.note,at))wires.Children().RemoveAt(at);w.note=nullptr;}
                wireAppearance(w);wireVisuals.push_back(std::move(w));
            }
        }
        for(const auto& pair:previous){const auto& w=pair.second;for(const auto& element:std::vector<UIElement>{w.line,w.glow,w.flow,w.dot,w.note})if(element){uint32_t at;if(wires.Children().IndexOf(element,at))wires.Children().RemoveAt(at);}for(const auto& element:std::vector<UIElement>{w.startTail,w.endTail,w.startSocket,w.endSocket}){uint32_t at;if(wireEnds.Children().IndexOf(element,at))wireEnds.Children().RemoveAt(at);}}
        if(!hoveredWire.empty()&&std::none_of(wireVisuals.begin(),wireVisuals.end(),[&](const auto& wire){return wire.id==hoveredWire;}))wireFeedback(L"",{});
        drawPreview();
    }
    void move(Input::PointerRoutedEventArgs const& e)
    {
        const auto p=e.GetCurrentPoint(viewport).Position();
        if(!resizeId.empty()){resizeCard(resizeId,resizeWidth+(p.X-pressPoint.X)/number(L"zoom",1));SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));e.Handled(true);return;}
        if(connecting){wirePointer=worldPoint(p);drawPreview();e.Handled(true);return;}
        if(!panning&&!selecting&&dragId.empty()){
            wireFeedback(cardAtSource(e.OriginalSource()).empty()&&!interactiveSource(e.OriginalSource())?wireAt(worldPoint(p)):L"",p);
            return;
        }
        const auto dx=p.X-lastPointer.X,dy=p.Y-lastPointer.Y;
        if(!movedGesture&&std::hypot(p.X-pressPoint.X,p.Y-pressPoint.Y)<4)return;
        movedGesture=true;lastPointer=p;
        if(panning){graph.SetNamedValue(L"panX",JsonValue::CreateNumberValue(std::clamp(number(L"panX")+dx,-100000.0,100000.0)));graph.SetNamedValue(L"panY",JsonValue::CreateNumberValue(std::clamp(number(L"panY")+dy,-100000.0,100000.0)));transform();}
        else if(selecting){
            const auto q=worldPoint(p);const auto l=(std::min)(q.X,selectionStart.X),t=(std::min)(q.Y,selectionStart.Y),r=(std::max)(q.X,selectionStart.X),b=(std::max)(q.Y,selectionStart.Y);
            Canvas::SetLeft(selectionBox,(std::min)(p.X,pressPoint.X));Canvas::SetTop(selectionBox,(std::min)(p.Y,pressPoint.Y));selectionBox.Width(std::abs(p.X-pressPoint.X));selectionBox.Height(std::abs(p.Y-pressPoint.Y));
            selected=selectionBase;for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id");const auto x=n.GetNamedNumber(L"x"),y=n.GetNamedNumber(L"y");if(x<r&&x+width(id)>l&&y<b&&y+height(id)>t)selected.insert(id);}updateCardStates();
        }else{
            const auto z=number(L"zoom",1);double wx=dx/z,wy=dy/z;
            for(const auto& id:selected){auto n=node(id);wx=std::clamp(wx,-50000-n.GetNamedNumber(L"x"),50000-n.GetNamedNumber(L"x"));wy=std::clamp(wy,-50000-n.GetNamedNumber(L"y"),50000-n.GetNamedNumber(L"y"));}
            for(const auto& id:selected){auto n=node(id);const auto x=n.GetNamedNumber(L"x")+wx,y=n.GetNamedNumber(L"y")+wy;n.SetNamedValue(L"x",JsonValue::CreateNumberValue(x));n.SetNamedValue(L"y",JsonValue::CreateNumberValue(y));Canvas::SetLeft(cardViews.at(id),x);Canvas::SetTop(cardViews.at(id),y);}
            updatePortPositions();updateIndicators();
            wireFeedback(selected.size()==1?insertionAt(*selected.begin()):L"",p,true);
        }e.Handled(true);
    }
    void release(Input::PointerRoutedEventArgs const& e)
    {
        if(!resizeId.empty()){resizeId=L"";releasing=true;viewport.ReleasePointerCaptures();releasing=false;draw();submit();e.Handled(true);return;}
        if(!panning&&!selecting&&dragId.empty()&&!connecting)return;
        const auto p=e.GetCurrentPoint(viewport).Position();const bool pan=panning,box=selecting,moved=movedGesture;const auto targetId=contextId;
        panning=false;selecting=false;dragId=L"";selectionBox.Visibility(Visibility::Collapsed);
        releasing=true;viewport.ReleasePointerCaptures();releasing=false;
        if(connecting){
            if(const auto target=connectionTarget(p))connect(*target);
            else{connecting=false;drawWires();}
        }
        else if(pan&&!moved){
            if(selected.size()>1&&selected.count(targetId)){showMenu(cardActions(targetId),viewport.TransformToVisual(root).TransformPoint(p));e.Handled(true);return;}
            if(!targetId.empty()&&!selected.count(targetId)){selected.clear();selected.insert(targetId);highlighted=L"";updateCardStates();}
            const auto q=worldPoint(p);const Port* hit=nullptr;
            for(const auto& candidate:ports){const auto top=candidate.control.TransformToVisual(world).TransformPoint({0,0});if(candidate.id==targetId&&q.X>=top.X&&q.Y>=top.Y&&q.X<=top.X+candidate.control.ActualWidth()&&q.Y<=top.Y+candidate.control.ActualHeight()){hit=&candidate;break;}}
            if(hit){const auto channel=*hit;MenuFlyout menu;menuItem(menu,L"Rename channel",[weak=weak_from_this(),channel]{if(auto s=weak.lock())s->renameChannel(channel.id,channel.output,channel.channel,channel.width,s->channelLabel(s->node(channel.id),channel.output,channel.channel,channel.width,L""));});menuItem(menu,L"Channel color",[weak=weak_from_this(),channel]{if(auto s=weak.lock())s->changeColor(channel.id,true,channel.output,channel.channel,channel.width);});if(node(channel.id).GetNamedString(L"kind")==L"mixer"){menuItem(menu,L"Delete channel pair",[weak=weak_from_this(),channel]{if(auto s=weak.lock())s->mixerChannels(channel.id,channel.output,channel.channel/2);});menu.Items().GetAt(menu.Items().Size()-1).as<MenuFlyoutItem>().IsEnabled(node(channel.id).GetNamedNumber(channel.output?L"outputs":L"inputs")>2);}showMenu(menu,viewport.TransformToVisual(root).TransformPoint(p));}
            else if(!targetId.empty()){if(!selected.count(targetId)){selected.clear();selected.insert(targetId);}updateCardStates();showMenu(cardActions(targetId),viewport.TransformToVisual(root).TransformPoint(p));}
            else backgroundMenu(p);
        }else if(!box&&moved){
            if(!pan)limitDistance(selected);
            if(!pan&&selected.size()==1){const auto id=*selected.begin();const auto wire=insertionAt(id);
                wireFeedback(L"",{});
                if(!wire.empty()){insert(id,wire);e.Handled(true);return;}
            }
            if(!pan)markContentChanged();viewSavePending=true;viewChanged=GetTickCount64();
        }
        e.Handled(true);
    }
    void cancelGesture(){if(!resizeId.empty()){resizeId=L"";markContentChanged();viewSavePending=true;viewChanged=GetTickCount64();}const bool changed=movedGesture&&(panning||!dragId.empty());if(movedGesture&&!dragId.empty())markContentChanged();panning=false;selecting=false;dragId=L"";connecting=false;wireFeedback(L"",{});selectionBox.Visibility(Visibility::Collapsed);releasing=true;viewport.ReleasePointerCaptures();releasing=false;if(changed){viewSavePending=true;viewChanged=GetTickCount64();}drawWires();}
    static hstring newId(){GUID id{};CoCreateGuid(&id);wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);std::wstring result;for(const auto c:buffer)if(iswxdigit(c))result+=static_cast<wchar_t>(towlower(c));return hstring(result);}
    JsonObject connection(hstring from,int output,int sourceWidth,hstring to,int input,int targetWidth)
    {
        JsonObject e;e.SetNamedValue(L"id",JsonValue::CreateStringValue(newId()));e.SetNamedValue(L"from",JsonValue::CreateStringValue(from));e.SetNamedValue(L"to",JsonValue::CreateStringValue(to));
        e.SetNamedValue(L"output",JsonValue::CreateNumberValue(output));e.SetNamedValue(L"input",JsonValue::CreateNumberValue(input));e.SetNamedValue(L"sourceWidth",JsonValue::CreateNumberValue(sourceWidth));e.SetNamedValue(L"targetWidth",JsonValue::CreateNumberValue(targetWidth));return e;
    }
    void connectMenu(Port start) {
        if(busy||committing||!start.available)return;MenuFlyout menu;
        for(const auto& destination:ports){
            if(!canConnect(start,destination))continue;
            MenuFlyoutItem choice;choice.Text(title(node(destination.id))+L" \u2192 "+(destination.label?destination.label.Text():chainText(destination.channel+1)));
            choice.Click([weak=weak_from_this(),start,destination](const auto&,const auto&){if(auto self=weak.lock()){self->source=start;self->connecting=true;self->connect(destination);}});menu.Items().Append(choice);
        }
        if(menu.Items().Size()==0){MenuFlyoutItem empty;empty.Text(text(L"No available connections"));empty.IsEnabled(false);menu.Items().Append(empty);}
        if(const auto* current=port(start.id,start.channel,start.output);current&&current->control)
            showMenu(menu,current->control.TransformToVisual(root).TransformPoint({0,0}));
    }
    void connect(Port target)
    {
        if(!connecting||!canConnect(source,target))return;
        const auto from=source.output?source:target,to=source.output?target:source;
        edges().Append(connection(from.id,from.channel,from.width,to.id,to.channel,to.width));connecting=false;drawWires();submit();
    }
    static bool inside(Point p,Windows::Foundation::Rect r)
    {return p.X>=r.X&&p.X<=r.X+r.Width&&p.Y>=r.Y&&p.Y<=r.Y+r.Height;}
    static float segmentDistance(Point p,Point a,Point b) {
        const float dx=b.X-a.X,dy=b.Y-a.Y,length=dx*dx+dy*dy;
        const float t=length>0?std::clamp(((p.X-a.X)*dx+(p.Y-a.Y)*dy)/length,0.0f,1.0f):0;
        return std::hypot(p.X-a.X-t*dx,p.Y-a.Y-t*dy);
    }
    static bool crosses(Point a,Point b,Windows::Foundation::Rect r) {
        float first=0,last=1;
        const auto clip=[&](float start,float delta,float low,float high) {
            if(std::abs(delta)<.00001f)return start>=low&&start<=high;
            auto t0=(low-start)/delta,t1=(high-start)/delta;if(t0>t1)std::swap(t0,t1);
            first=(std::max)(first,t0);last=(std::min)(last,t1);return first<=last;
        };
        return clip(a.X,b.X-a.X,r.X,r.X+r.Width)&&clip(a.Y,b.Y-a.Y,r.Y,r.Y+r.Height);
    }
    int wireSteps(Wire const& wire) const {
        return std::clamp(static_cast<int>((std::abs(wire.b.X-wire.a.X)+std::abs(wire.b.Y-wire.a.Y)+280)*number(L"zoom",1)/8),24,512);
    }
    hstring wireAt(Point p)const {
        float nearest=static_cast<float>(10/number(L"zoom",1));hstring result;
        for(const auto& wire:wireVisuals){
            const auto span=(std::max)(70.0f,std::abs(wire.b.X-wire.a.X)*.5f);
            const auto left=(std::min)(wire.a.X,wire.b.X-span)-nearest,right=(std::max)(wire.b.X,wire.a.X+span)+nearest;
            if(p.X<left||p.X>right||p.Y<(std::min)(wire.a.Y,wire.b.Y)-nearest||p.Y>(std::max)(wire.a.Y,wire.b.Y)+nearest)continue;
            const int steps=wireSteps(wire);auto a=wire.a;
            for(int i=1;i<=steps;++i){const auto b=bezier(wire.a,wire.b,static_cast<float>(i)/steps);const auto distance=segmentDistance(p,a,b);if(distance<nearest){nearest=distance;result=wire.id;}a=b;}
        }return result;
    }
    const Port* firstPort(hstring const& id,bool output)const {
        for(const auto& p:ports)if(p.available&&p.id==id&&p.output==output)return &p;return nullptr;
    }
    bool canInsert(hstring const& id)const {
        const auto kind=node(id).GetNamedString(L"kind",L"");
        if((kind!=L"plugin"&&kind!=L"mixer")||!firstPort(id,false)||!firstPort(id,true))return false;
        for(const auto& v:edges()){auto e=v.GetObject();if(e.GetNamedString(L"from")==id||e.GetNamedString(L"to")==id)return false;}
        return true;
    }
    hstring insertionAt(hstring const& id)const {
        if(!canInsert(id))return L"";
        const auto n=node(id);const Windows::Foundation::Rect bounds{static_cast<float>(n.GetNamedNumber(L"x")+8),static_cast<float>(n.GetNamedNumber(L"y")+8),static_cast<float>(width(id)-16),static_cast<float>(height(id)-16)};
        const Point center{bounds.X+bounds.Width/2,bounds.Y+bounds.Height/2};float nearest=std::numeric_limits<float>::max();hstring result;
        for(const auto& wire:wireVisuals){auto a=wire.a;const int steps=wireSteps(wire);
            for(int i=1;i<=steps;++i){const auto b=bezier(wire.a,wire.b,static_cast<float>(i)/steps);
                if(crosses(a,b,bounds)){const auto distance=segmentDistance(center,a,b);if(distance<nearest){nearest=distance;result=wire.id;}}a=b;}
        }return result;
    }
    void wireFeedback(hstring id,Point pointer,bool insertion=false) {
        hoveredWire=id;wireHint.Visibility(id.empty()?Visibility::Collapsed:Visibility::Visible);
        if(!id.empty()){
            wireHintText.Text(text(insertion?L"Release to insert into this connection":L"Connection · Right-click for options"));
            // Measure the text alone: the border's DesiredSize includes its
            // positioning margin and would make the hint jump on every move.
            wireHintText.MaxWidth((std::max)(1.0,viewport.ActualWidth()-38));
            wireHintText.Measure({static_cast<float>(wireHintText.MaxWidth()),std::numeric_limits<float>::infinity()});
            const float x=std::clamp(pointer.X+14,8.0f,static_cast<float>((std::max)(8.0,viewport.ActualWidth()-wireHintText.DesiredSize().Width-30)));
            const float y=std::clamp(pointer.Y+18,8.0f,static_cast<float>((std::max)(8.0,viewport.ActualHeight()-wireHintText.DesiredSize().Height-22)));
            wireHint.Margin({x,y,0,0});
        }
        for(auto& wire:wireVisuals)wireAppearance(wire);
    }
    void wireAppearance(Wire& wire) {
        const bool active=wire.available&&GetTickCount64()-lastLevels<1000&&meterValues.GetNamedNumber(wire.id,0)>.00003&&!globalBypassed&&!globalMuted;
        const bool highlighted=wire.id==hoveredWire;
        wire.line.Opacity(active||highlighted?1.0:.25);
        wire.line.StrokeThickness(highlighted?(std::max)(3.0,4/number(L"zoom",1)):(active?3.0:2.0));
        for(const auto& tail:{wire.startTail,wire.endTail}){tail.Opacity(wire.available?1:.45);tail.StrokeThickness(wire.line.StrokeThickness());}
        wire.startSocket.Opacity(wire.available?1:.45);wire.endSocket.Opacity(wire.available?1:.45);
        wire.glow.StrokeThickness(highlighted?12/number(L"zoom",1):10);
        wire.glow.Opacity(!VisualPreferences::current().performance?(highlighted?.32:(active?.20:0)):0);
    }
    fire_and_forget insert(hstring id,hstring edgeId)
    {
        auto lifetime=shared_from_this();
        contentSavePending=viewSavePending=true;viewChanged=GetTickCount64();
        if(!canInsert(id)){submit();co_return;}
        const auto originalProfile=profileId,originalGeneration=profileGeneration;
        JsonObject original;for(const auto& v:edges())if(v.GetObject().GetNamedString(L"id")==edgeId){original=v.GetObject();break;}
        if(!original.HasKey(L"id")){submit();co_return;}
        insertionOpen=true;try{
            ContentDialog dialog;dialog.XamlRoot(root.XamlRoot());dialog.RequestedTheme(root.ActualTheme());dialog.Title(box_value(text(L"Insert element into this connection?")));dialog.PrimaryButtonText(text(L"Insert"));dialog.CloseButtonText(text(L"Keep position only"));dialog.DefaultButton(ContentDialogButton::Primary);
            StackPanel content;content.Spacing(16);content.MinWidth(360);content.MaxWidth(520);
            TextBlock description;description.Text(text(L"Choose the input and output channels for this element. The other ends of the connection will stay the same."));description.TextWrapping(TextWrapping::Wrap);content.Children().Append(description);
            TextBlock endpoints;const auto endpoint=[&](bool output){auto n=node(original.GetNamedString(output?L"from":L"to"));return title(n)+L" \u00b7 "+channelLabel(n,output,static_cast<int>(original.GetNamedNumber(output?L"output":L"input")),static_cast<int>(original.GetNamedNumber(output?L"sourceWidth":L"targetWidth")),L"");};
            endpoints.Text(endpoint(true)+L" \u2192 "+endpoint(false));endpoints.TextWrapping(TextWrapping::Wrap);content.Children().Append(endpoints);
            struct Side { std::vector<Port> available;std::vector<ComboBox> choices; };
            Side inputs,outputs;
            const auto addSide=[&](Side& side,bool output){
                for(const auto& p:ports)if(p.available&&p.id==id&&p.output==output)side.available.push_back(p);
                const bool stereo=original.GetNamedNumber(output?L"targetWidth":L"sourceWidth")==2;
                const bool individual=splitChannels(node(id),output);
                const int count=stereo&&individual&&side.available.size()>1?2:1;
                StackPanel group;group.Spacing(8);TextBlock heading;heading.Text(text(output?L"Output channels":L"Input channels"));heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());group.Children().Append(heading);
                for(int lane=0;lane<count;++lane){
                    ComboBox choice;choice.HorizontalAlignment(HorizontalAlignment::Stretch);
                    if(count==2)choice.Header(box_value(text(lane==0?L"Left":L"Right")));
                    Automation::AutomationProperties::SetAutomationId(choice,(output?L"ChainInsertOutput":L"ChainInsertInput")+chainText(lane));
                    Automation::AutomationProperties::SetName(choice,text(output?L"Output channels":L"Input channels")+(count==2?L" "+text(lane==0?L"Left":L"Right"):L""));
                    for(const auto& p:side.available){ComboBoxItem item;item.Content(box_value(p.label?p.label.Text():chainText(p.channel+1)));choice.Items().Append(item);}
                    choice.SelectedIndex(lane);side.choices.push_back(choice);group.Children().Append(choice);
                }content.Children().Append(group);
            };
            addSide(inputs,false);addSide(outputs,true);
            TextBlock warning;warning.Text(text(L"Choose different input channels for Left and Right."));warning.TextWrapping(TextWrapping::Wrap);warning.Visibility(Visibility::Collapsed);content.Children().Append(warning);
            std::vector<winrt::weak_ref<ComboBox>> weakInputs,weakOutputs;for(auto c:inputs.choices)weakInputs.push_back(make_weak(c));for(auto c:outputs.choices)weakOutputs.push_back(make_weak(c));
            const auto validate=[weakDialog=make_weak(dialog),weakWarning=make_weak(warning),weakInputs,weakOutputs](const auto&,const auto&){
                const auto valid=[](const auto& choices,bool distinct){std::set<int> selected;for(const auto& weak:choices){auto choice=weak.get();if(!choice||choice.SelectedIndex()<0||(distinct&&!selected.insert(choice.SelectedIndex()).second))return false;}return true;};
                const bool ok=valid(weakInputs,true)&&valid(weakOutputs,false);if(auto d=weakDialog.get())d.IsPrimaryButtonEnabled(ok);if(auto label=weakWarning.get())label.Visibility(ok?Visibility::Collapsed:Visibility::Visible);
            };
            for(const auto& c:inputs.choices)c.SelectionChanged(validate);for(const auto& c:outputs.choices)c.SelectionChanged(validate);
            dialog.Content(content);
            if(co_await lightHostModern::ui::showAppDialog(dialog)==ContentDialogResult::Primary&&originalProfile==profileId&&originalGeneration==profileGeneration&&canInsert(id)){
                const auto stillValid=[&](const Side& side,bool output){for(const auto& choice:side.choices){const int index=choice.SelectedIndex();if(index<0||static_cast<size_t>(index)>=side.available.size())return false;const auto& p=side.available[static_cast<size_t>(index)];const auto* current=port(id,p.channel,output);if(!current||!current->available||current->width!=p.width)return false;}return true;};
                if(stillValid(inputs,false)&&stillValid(outputs,true)){auto list=edges();for(uint32_t i=0;i<list.Size();++i){auto old=list.GetObjectAt(i);if(old.GetNamedString(L"id")!=edgeId)continue;
                    list.RemoveAt(i);
                    const auto splice=[&](const Side& side,bool output){for(size_t lane=0;lane<side.choices.size();++lane){const auto& p=side.available[static_cast<size_t>(side.choices[lane].SelectedIndex())];const bool split=side.choices.size()==2;const int oldChannel=static_cast<int>(old.GetNamedNumber(output?L"input":L"output"))+(split?static_cast<int>(lane):0);const int oldWidth=split?1:static_cast<int>(old.GetNamedNumber(output?L"targetWidth":L"sourceWidth"));
                        list.Append(output?connection(id,p.channel,p.width,old.GetNamedString(L"to"),oldChannel,oldWidth):connection(old.GetNamedString(L"from"),oldChannel,oldWidth,id,p.channel,p.width));
                    }};splice(inputs,false);splice(outputs,true);break;
                }}
            }insertionOpen=false;if(originalProfile==profileId&&originalGeneration==profileGeneration)submit();
        }catch(...){insertionOpen=false;if(originalProfile==profileId&&originalGeneration==profileGeneration)submit();}
    }
    void mixerChannels(hstring id,bool output,int removePair=-1) {
        JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"mixer-channels"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));r.SetNamedValue(L"output",JsonValue::CreateBooleanValue(output));r.SetNamedValue(L"removePair",JsonValue::CreateNumberValue(removePair));sendRequest(r);
    }
    void disconnect(hstring id){auto list=edges();for(uint32_t i=list.Size();i>0;--i)if(list.GetObjectAt(i-1).GetNamedString(L"id")==id)list.RemoveAt(i-1);submit();}
    void backgroundMenu(Point p)
    {
        if(busy||committing)return;selected.clear();highlighted=L"";updateCardStates();MenuFlyout menu;const auto world=worldPoint(p);const auto wire=wireAt(world);
        if(!wire.empty()){
            menuItem(menu,L"Disconnect",[weak=weak_from_this(),wire]{if(auto s=weak.lock())s->disconnect(wire);});
            showMenu(menu,viewport.TransformToVisual(root).TransformPoint(p));return;
        }
        menuItem(menu,L"Add plugin",[weak=weak_from_this(),world]{if(auto s=weak.lock())s->add(world);});
        menuItem(menu,L"Add mixer",[weak=weak_from_this(),world]{if(auto s=weak.lock())s->addMixer(world);});
        menu.Items().Append(MenuFlyoutSeparator());
        menuItem(menu,L"Organize",[weak=weak_from_this()]{if(auto s=weak.lock())s->confirmOrganize();});
        menu.Items().Append(MenuFlyoutSeparator());
        menuItem(menu,globalMuted?L"Unmute output":L"Mute output",[weak=weak_from_this()]{if(auto s=weak.lock())s->pluginAction(std::string("set-global-mute:")+(s->globalMuted?"0":"1"));});
        menuItem(menu,globalBypassed?L"Disable bypass chain":L"Bypass chain",[weak=weak_from_this()]{if(auto s=weak.lock())s->pluginAction(std::string("set-global-bypass:")+(s->globalBypassed?"0":"1"));});
        showMenu(menu,viewport.TransformToVisual(root).TransformPoint(p));
    }
    void decorateMenu(Windows::Foundation::Collections::IVector<MenuFlyoutItemBase> const& items){
        for(auto base:items){hstring label;if(auto item=base.try_as<MenuFlyoutItem>())label=item.Text();else if(auto item=base.try_as<ToggleMenuFlyoutItem>())label=item.Text();else if(auto sub=base.try_as<MenuFlyoutSubItem>())label=sub.Text();else continue;
            HoverHelp::current().describe(base);
            const wchar_t* glyph=L"\xE713";const auto is=[&](const wchar_t* source){return label==text(source);};
            if(is(L"Card color")||is(L"Channel color"))glyph=L"\xE790";else if(is(L"Rename")||is(L"Rename channel"))glyph=L"\xE70F";else if(is(L"Add plugin")||is(L"Add new input channel")||is(L"Add new output channel"))glyph=L"\xE710";else if(is(L"Add mixer"))glyph=L"\xE9E9";else if(is(L"Open editor"))glyph=L"\xE8A7";else if(is(L"Duplicate"))glyph=L"\xE8C8";else if(is(L"Remove")||is(L"Delete channel pair")||is(L"Delete selected elements"))glyph=L"\xE74D";else if(is(L"Mute output")||is(L"Unmute output"))glyph=L"\xE74F";else if(is(L"Bypass")||is(L"Bypass chain")||is(L"Disable bypass chain")||is(L"Bypass selected plugins"))glyph=L"\xE8AB";else if(is(L"Arrange layers"))glyph=L"\xE81E";else if(is(L"Bring to front"))glyph=L"\xE74A";else if(is(L"Bring forward"))glyph=L"\xE70E";else if(is(L"Send backward"))glyph=L"\xE70D";else if(is(L"Send to back"))glyph=L"\xE74B";else if(is(L"Organize"))glyph=L"\xECA5";else if(is(L"Horizontal channels")||is(L"Vertical channels"))glyph=L"\xE8C0";else if(is(L"Configure plugin channels"))glyph=L"\xE713";else if(is(L"Input mode")||is(L"Output mode")||is(L"Stereo")||is(L"Mono")||is(L"Individual")||is(L"Pairs")||is(L"Channel selection")||is(L"Enable channels")||is(L"Disable channels"))glyph=L"\xE8FD";else if(is(L"Input channels"))glyph=L"\xE720";else if(is(L"Output channels"))glyph=L"\xE767";else if(is(L"Retry loading"))glyph=L"\xE72C";else if(is(L"Disconnect")||is(L"Disconnect input wires")||is(L"Disconnect output wires"))glyph=L"\xE711";
            FontIcon icon;icon.FontFamily(FontFamily(L"Segoe Fluent Icons"));icon.Glyph(glyph);icon.FontSize(16);
            if(auto item=base.try_as<MenuFlyoutItem>())item.Icon(icon);else if(auto item=base.try_as<ToggleMenuFlyoutItem>())item.Icon(icon);else if(auto sub=base.try_as<MenuFlyoutSubItem>()){sub.Icon(icon);decorateMenu(sub.Items());}
        }
    }
    void showMenu(MenuFlyout const& menu,Point position)
    {
        // Anchor outside the transformed/clipped canvas so the popup retains a
        // valid viewport at any zoom and is not clipped by graph containers.
        decorateMenu(menu.Items());activeMenu=menu;Primitives::FlyoutShowOptions options;
        options.Position(position);activeMenu.ShowAt(root,options);
    }
    MenuFlyout cardActions(hstring id)
    {
        MenuFlyout menu;if(selected.size()>1){selectionActions(menu);return menu;}const auto n=node(id);const auto kind=n.GetNamedString(L"kind");
        if(kind==L"plugin"){
            menuItem(menu,L"Open editor",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->pluginAction("open-plugin-editor:"+to_string(id));});
            menuItem(menu,globalBypassed?L"Disable bypass chain":L"Bypass",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->pluginAction(s->globalBypassed?"set-global-bypass:0":"toggle-bypass:"+to_string(id));});

            menuItem(menu,L"Retry loading",[weak=weak_from_this(),id]{if(auto s=weak.lock()){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"retry"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));s->sendRequest(r);}});
            menuItem(menu,L"Duplicate",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->pluginAction("duplicate-plugin:"+to_string(id));});
            bool isolated=false;for(const auto& v:plugins){const auto p=v.GetObject();if(p.GetNamedString(L"instanceId",L"")==id){isolated=p.GetNamedBoolean(L"isolated",false);break;}}
            menuItem(menu,isolated?L"Run inside the host":L"Run in a separate process (experimental)",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->pluginAction("configure-isolation:"+to_string(id));});
            menu.Items().Append(MenuFlyoutSeparator());
        }
        menuItem(menu,L"Rename",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->renameNode(id);});
        menuItem(menu,L"Card color",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->changeColor(id);});
        layerActions(menu);
        const bool horizontal=n.GetNamedBoolean(L"horizontalPorts",false);
        menuItem(menu,horizontal?L"Vertical channels":L"Horizontal channels",[weak=weak_from_this(),id,horizontal]{if(auto s=weak.lock()){s->node(id).SetNamedValue(L"horizontalPorts",JsonValue::CreateBooleanValue(!horizontal));s->draw();s->submit();}});
        menu.Items().Append(MenuFlyoutSeparator());
        if(kind==L"plugin")menuItem(menu,L"Configure plugin channels",[weak=weak_from_this(),id]{if(auto s=weak.lock())s->pluginAction("configure-plugin-buses:"+to_string(id));});
        for(bool output:{false,true}){
            const int count=static_cast<int>(n.GetNamedNumber(output?L"outputs":L"inputs",0));if(!count&&kind!=L"plugin")continue;
            const bool hardware=kind==L"input"||kind==L"output";
            MenuFlyoutSubItem channels;channels.Text(text(kind==L"input"?L"Input channels":kind==L"output"?L"Output channels":output?L"Output channels":L"Input channels"));
            if(hardware){const bool monoOn=kind==L"input"?monoInputs:monoOutput;MenuFlyoutSubItem mode;mode.Text(text(kind==L"input"?L"Input mode":L"Output mode"));
                for(bool mono:{false,true}){MenuFlyoutItem item;item.Text(text(mono?L"Mono":L"Stereo"));item.IsEnabled(mono!=monoOn);
                    item.Click([weak=weak_from_this(),kind,mono](const auto&,const auto&){if(auto s=weak.lock()){JsonObject r;r.SetNamedValue(L"enabled",JsonValue::CreateBooleanValue(mono));r.SetNamedValue(L"expectedGeneration",s->audioSelection.GetNamedValue(L"generation",JsonValue::CreateStringValue(L"0")));s->pluginAction(std::string(kind==L"input"?"set-mono-inputs:":"set-mono-output:")+to_string(r.Stringify()));}});mode.Items().Append(item);}
                channels.Items().Append(mode);channels.Items().Append(MenuFlyoutSeparator());}
            MenuFlyoutSubItem selection;selection.Text(text(L"Channel selection"));
            for(bool pairs:{true,false}){MenuFlyoutItem item;item.Text(text(pairs?L"Pairs":L"Individual"));item.IsEnabled(pairs==splitChannels(n,output));item.Click([weak=weak_from_this(),id,output,pairs](const auto&,const auto&){if(auto s=weak.lock())s->selectChannels(id,output,pairs);});selection.Items().Append(item);}channels.Items().Append(selection);
            for(bool enable:{true,false}){
                MenuFlyoutSubItem channelItems;channelItems.Text(text(enable?L"Enable channels":L"Disable channels"));
                for(int c=0;c<count;){const int laneWidth=channelWidth(n,output,c);const auto labels=n.GetNamedArray(output?L"outputNames":L"inputNames",JsonArray{});auto label=channelLabel(n,output,c,laneWidth,c<(int)labels.Size()?labels.GetStringAt(c):chainText(c+1));
                    bool anyEnabled=false,anyDisabled=false,connected=false;
                    for(int lane=c;lane<c+laneWidth;++lane){const bool active=hardware?enabled(n,output,lane):!hidden(n,output,lane);anyEnabled|=active;anyDisabled|=!active;}
                    for(const auto& v:edges()){auto e=v.GetObject();const int start=static_cast<int>(e.GetNamedNumber(output?L"output":L"input")),w=static_cast<int>(e.GetNamedNumber(output?L"sourceWidth":L"targetWidth"));if(e.GetNamedString(output?L"from":L"to")==id&&c<start+w&&c+laneWidth>start)connected=true;}
                    if(enable?anyDisabled:anyEnabled){MenuFlyoutItem item;item.Text(label);item.IsEnabled(hardware||enable||!connected);
                        lightHostModern::ui::HoverHelp::SetToolTip(item,box_value(lightHostModern::ui::HoverHelp::current().explanation(enable?L"Enable channels":L"Disable channels")));
                        if(connected&&!hardware&&!enable)lightHostModern::ui::HoverHelp::SetToolTip(item,box_value(text(L"Disconnect first")));
                        item.Click([weak=weak_from_this(),id,output,c,laneWidth,enable,hardware](const auto&,const auto&){if(auto s=weak.lock()){
                            auto n=s->node(id);const auto kind=n.GetNamedString(L"kind");
                            if(hardware){JsonObject snapshot;snapshot.SetNamedValue(L"audioSelection",s->audioSelection);s->pluginAction(to_string(AudioPageController::channelCommand(to_string(snapshot.Stringify()),kind==L"input",c,c+laneWidth-1,enable)));return;}
                            auto hidden=n.GetNamedArray(output?L"hiddenOutputs":L"hiddenInputs",JsonArray{});for(uint32_t i=hidden.Size();i>0;--i){const int lane=static_cast<int>(hidden.GetNumberAt(i-1));if(lane>=c&&lane<c+laneWidth)hidden.RemoveAt(i-1);}if(!enable)for(int lane=c;lane<c+laneWidth;++lane)hidden.Append(JsonValue::CreateNumberValue(lane));n.SetNamedValue(output?L"hiddenOutputs":L"hiddenInputs",hidden);s->submit();
                        }});channelItems.Items().Append(item);
                    }c+=laneWidth;
                }channelItems.IsEnabled(channelItems.Items().Size()>0);channels.Items().Append(channelItems);
            }
            if(kind==L"mixer"){MenuFlyoutItem add;add.Text(text(output?L"Add new output channel":L"Add new input channel"));add.IsEnabled(count<256);add.Click([weak=weak_from_this(),id,output](const auto&,const auto&){if(auto s=weak.lock())s->mixerChannels(id,output);});channels.Items().Append(add);}
            channels.Items().Append(MenuFlyoutSeparator());disconnectAction(channels.Items(),output);menu.Items().Append(channels);
        }
        if(kind==L"plugin"||kind==L"mixer")menu.Items().Append(MenuFlyoutSeparator());
        if(kind==L"plugin"||kind==L"mixer")menuItem(menu,L"Remove",[weak=weak_from_this(),id]{if(auto s=weak.lock()){JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"remove"));r.SetNamedValue(L"id",JsonValue::CreateStringValue(id));s->sendRequest(r);}});
        return menu;
    }
    Point viewCenter()const{return {static_cast<float>(viewport.ActualWidth()/2),static_cast<float>(viewport.ActualHeight()/2)};}
    Point centerPoint()const{return worldPoint(viewCenter());}
    void persistView(){if(!graph.HasKey(L"nodes"))return;JsonObject view;for(auto key:{L"zoom",L"panX",L"panY"})view.SetNamedValue(key,graph.GetNamedValue(key));saveUiSetting(L"ChainViews",profileId.empty()?L"unsaved":profileId.c_str(),std::wstring(view.Stringify()));}
    fire_and_forget confirmOrganize(){auto lifetime=shared_from_this();if(dialogOpen||busy||committing)co_return;dialogOpen=true;try{ContentDialog d;d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(text(L"Organize the chain?")));d.Content(box_value(text(L"Arrange the cards from input to output. Connections and audio settings will stay the same.")));d.PrimaryButtonText(text(L"Organize"));d.CloseButtonText(text(L"Cancel"));d.DefaultButton(ContentDialogButton::Close);if(co_await lightHostModern::ui::showAppDialog(d)==ContentDialogResult::Primary)organize();}catch(...){}dialogOpen=false;}
    hstring channelLabel(JsonObject const& n,bool output,int channel,int width,hstring fallback)const{
        const auto kind=n.GetNamedString(L"kind");JsonObject aliases;
        if(kind==L"input"||kind==L"output"){aliases=audioConfig.GetNamedObject(L"channelAliases",JsonObject{}).GetNamedObject(kind==L"input"?L"input":L"output",JsonObject{});
            return audioChannelName(n.GetNamedArray(output?L"outputNames":L"inputNames",JsonArray{}),aliases,channel,width,kind==L"input",catalog);}
        else aliases=n.GetNamedObject(output?L"outputAliases":L"inputAliases",JsonObject{});
        const auto key=chainText(std::to_wstring(channel)+L":"+std::to_wstring(width));
        const auto custom=aliases.GetNamedString(key,L"");if(!custom.empty())return custom;
        const auto labels=n.GetNamedArray(output?L"outputNames":L"inputNames",JsonArray{});
        if(kind==L"mixer") {
            const auto individual=[&](int c) {
                const auto alias=aliases.GetNamedString(chainText(std::to_wstring(c)+L":1"),L"");
                if(!alias.empty())return alias;
                return (output&&n.GetNamedNumber(L"outputs")==2?hstring{}:chainText(c/2+1)+L" ")+text(c%2==0?L"Left":L"Right");
            };
            return width==2?individual(channel)+L" + "+individual(channel+1):individual(channel);
        }
        if(width==2){const auto a=aliases.GetNamedString(chainText(std::to_wstring(channel)+L":1"),L""),b=aliases.GetNamedString(chainText(std::to_wstring(channel+1)+L":1"),L"");
            return (a.empty()?(channel<(int)labels.Size()?labels.GetStringAt(channel):chainText(channel+1)):a)+L" + "+(b.empty()?(channel+1<(int)labels.Size()?labels.GetStringAt(channel+1):chainText(channel+2)):b);}
        return fallback;
    }
    fire_and_forget renameNode(hstring id){auto lifetime=shared_from_this();if(dialogOpen||busy||committing)co_return;dialogOpen=true;try{
        auto n=node(id);auto result=co_await editDisplayName(root,catalog,text(L"Rename"),title(n));
        if(result&&node(id).HasKey(L"id")){const auto name=unbox_value<hstring>(result);
            if(n.GetNamedString(L"kind")==L"plugin")pluginAction("rename-plugin:"+to_string(id)+":"+to_string(name));
            else{node(id).SetNamedValue(L"customName",JsonValue::CreateStringValue(name));submit();}
        }
    }catch(...){}dialogOpen=false;}
    fire_and_forget renameChannel(hstring id,bool output,int channel,int width,hstring label){auto lifetime=shared_from_this();if(dialogOpen||busy||committing)co_return;dialogOpen=true;try{
        const auto generation=audioSelection.GetNamedValue(L"generation",JsonValue::CreateStringValue(L"0"));
        const auto currentNode=node(id);
        if(currentNode.GetNamedString(L"kind")==L"plugin"){
            const auto names=currentNode.GetNamedArray(output?L"outputNames":L"inputNames",JsonArray{});
            label=channelLabel(currentNode,output,channel,width,channel<(int)names.Size()?names.GetStringAt(channel):chainText(channel+1));
        }
        auto result=co_await editDisplayName(root,catalog,text(L"Rename channel"),label);
        if(result&&node(id).HasKey(L"id")){auto n=node(id);const auto kind=n.GetNamedString(L"kind");const auto name=unbox_value<hstring>(result);
            if(kind==L"input"||kind==L"output"){JsonObject r;r.SetNamedValue(L"direction",JsonValue::CreateStringValue(kind==L"input"?L"input":L"output"));r.SetNamedValue(L"channel",JsonValue::CreateNumberValue(channel));r.SetNamedValue(L"width",JsonValue::CreateNumberValue(width));r.SetNamedValue(L"name",JsonValue::CreateStringValue(name));r.SetNamedValue(L"expectedGeneration",generation);pluginAction("rename-audio-channel:"+to_string(r.Stringify()));}
            else{auto aliases=JsonObject::Parse(n.GetNamedObject(output?L"outputAliases":L"inputAliases",JsonObject{}).Stringify());const auto key=chainText(std::to_wstring(channel)+L":"+std::to_wstring(width));if(width==1){int start=channel-channel%2;
                if(kind==L"plugin"){start=-1;const auto ports=n.GetNamedArray(output?L"outputPorts":L"inputPorts",JsonArray{});
                    for(int c=(std::max)(0,channel-1);c<=channel&&c+1<(int)ports.Size();++c){const auto a=ports.GetObjectAt(c),b=ports.GetObjectAt(c+1);
                        if(a.GetNamedNumber(L"bus",-1)==b.GetNamedNumber(L"bus",-2)&&a.GetNamedNumber(L"type")==1&&b.GetNamedNumber(L"type")==2)start=c;}
                }
                if(start>=0){const auto pair=chainText(std::to_wstring(start)+L":2");if(aliases.HasKey(pair))aliases.Remove(pair);}
            }if(name.empty()){if(aliases.HasKey(key))aliases.Remove(key);for(int lane=channel;lane<channel+width;++lane){auto individual=chainText(std::to_wstring(lane)+L":1");if(aliases.HasKey(individual))aliases.Remove(individual);}}else aliases.SetNamedValue(key,JsonValue::CreateStringValue(name));n.SetNamedValue(output?L"outputAliases":L"inputAliases",aliases);submit();}
        }
    }catch(...){}dialogOpen=false;}
    void fit()
    {
        if(nodes().Size()==0||viewport.ActualWidth()<80||viewport.ActualHeight()<80)return;
        double left=50000,top=50000,right=-50000,bottom=-50000;
        for(const auto& v:nodes()){
            auto n=v.GetObject(); const auto view=cardViews.find(n.GetNamedString(L"id"));
            const auto height=view!=cardViews.end()&&view->second.ActualHeight()>0?view->second.ActualHeight():180.0;
            left=(std::min)(left,n.GetNamedNumber(L"x"));top=(std::min)(top,n.GetNamedNumber(L"y"));right=(std::max)(right,n.GetNamedNumber(L"x")+width(n.GetNamedString(L"id")));bottom=(std::max)(bottom,n.GetNamedNumber(L"y")+height);
        }
        const auto z=std::clamp((std::min)((viewport.ActualWidth()-64)/(right-left),(viewport.ActualHeight()-120)/(bottom-top)),minZoom,1.0);
        graph.SetNamedValue(L"zoom",JsonValue::CreateNumberValue(z));
        graph.SetNamedValue(L"panX",JsonValue::CreateNumberValue((viewport.ActualWidth()-(right-left)*z)*.5-left*z));
        graph.SetNamedValue(L"panY",JsonValue::CreateNumberValue(56+(viewport.ActualHeight()-120-(bottom-top)*z)*.5-top*z));transform();viewSavePending=true;viewChanged=GetTickCount64();
    }
    void organize()
    {
        if(busy||committing||nodes().Size()==0)return;cancelGesture();selected.clear();
        std::map<hstring,int> depth,degree;std::map<hstring,std::vector<hstring>> targets;std::queue<hstring> ready;
        for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id");degree[id]=0;depth[id]=n.GetNamedString(L"kind")==L"input"?0:1;}
        for(const auto& v:edges()){auto e=v.GetObject();targets[e.GetNamedString(L"from")].push_back(e.GetNamedString(L"to"));++degree[e.GetNamedString(L"to")];}
        for(const auto& pair:degree)if(pair.second==0)ready.push(pair.first);
        while(!ready.empty()){auto id=ready.front();ready.pop();for(const auto& to:targets[id]){depth[to]=(std::max)(depth[to],depth[id]+1);if(--degree[to]==0)ready.push(to);}}
        int isolated=0;for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id"),kind=n.GetNamedString(L"kind");
            if(kind==L"input"||kind==L"output")continue;bool connected=false;
            for(const auto& e:edges()){auto edge=e.GetObject();if(edge.GetNamedString(L"from")==id||edge.GetNamedString(L"to")==id){connected=true;break;}}
            if(!connected)depth[id]=1+(isolated++%3);
        }
        int last=2;for(const auto& v:nodes()){auto n=v.GetObject();if(n.GetNamedString(L"kind")!=L"output")last=(std::max)(last,depth[n.GetNamedString(L"id")]+1);}
        std::map<int,double> lane,columnWidth,columnX;for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id");const int d=n.GetNamedString(L"kind")==L"output"?last:depth[id];columnWidth[d]=(std::max)(columnWidth[d],width(id));}double nextX=40;for(const auto& c:columnWidth){columnX[c.first]=nextX;nextX+=c.second+120;}
        for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id");int d=n.GetNamedString(L"kind")==L"output"?last:depth[id];
            n.SetNamedValue(L"x",JsonValue::CreateNumberValue(columnX[d]));n.SetNamedValue(L"y",JsonValue::CreateNumberValue(40+lane[d]));lane[d]+=height(id)+64;
            Canvas::SetLeft(cardViews.at(id),n.GetNamedNumber(L"x"));Canvas::SetTop(cardViews.at(id),n.GetNamedNumber(L"y"));
        }
        viewport.UpdateLayout();updatePortPositions();updateCardStates();markContentChanged();fit();
    }
    void updateCardStates()
    {
        int total=0,bypassed=0,unconnected=0;
        struct Connections { bool incoming=false,outgoing=false,connected=false;double peak=0; };
        std::map<hstring,Connections> connections;
        for(const auto& value:edges()) {
            const auto e=value.GetObject();const auto from=e.GetNamedString(L"from"),to=e.GetNamedString(L"to");
            connections[from].connected=connections[to].connected=true;
            if(!visibleEdge(e))continue;
            if(port(from,static_cast<int>(e.GetNamedNumber(L"output")),true)) {
                connections[to].incoming=true;connections[to].peak=(std::max)(connections[to].peak,meterValues.GetNamedNumber(e.GetNamedString(L"id"),0));
            }
            if(port(to,static_cast<int>(e.GetNamedNumber(L"input")),false))connections[from].outgoing=true;
        }
        std::map<hstring,JsonObject> pluginStates;for(const auto& value:plugins){const auto p=value.GetObject();pluginStates[p.GetNamedString(L"instanceId",L"")]=p;}
        for(const auto& item:cardViews){const auto id=item.first;auto n=node(id);const auto kind=n.GetNamedString(L"kind");
            const auto& routes=connections[id];const bool incoming=routes.incoming,outgoing=routes.outgoing;const double inputPeak=routes.peak;
            bool failed=false,bypass=globalBypassed;
            if(pluginStates.count(id)){const auto p=pluginStates.at(id);bypass=bypass||p.GetNamedBoolean(L"bypassed",false);failed=p.GetNamedString(L"loading",L"loaded")!=L"loaded";}
            if(kind==L"plugin"){++total;if(bypass)++bypassed;if(!routes.connected)++unconnected;}
            const bool hardware=kind==L"input"||kind==L"output";
            const bool noRoute=(kind!=L"input"&&n.GetNamedNumber(L"inputs")>0&&!incoming)||(kind!=L"output"&&n.GetNamedNumber(L"outputs")>0&&!outgoing);
            const bool deviceAvailable=audioSelection.GetNamedBoolean(L"driverAvailable",false);
            const bool processing=audioSelection.GetNamedBoolean(L"processingAvailable",false);
            const bool muted=kind==L"output"&&globalMuted;
            const bool disconnected=hardware?!deviceAvailable:noRoute;
            const bool inactive=disconnected||(hardware&&!processing)||muted;
            const bool stalled=kind!=L"output"&&inputPeak>.0001&&meterValues.GetNamedNumber(id,0)<=.00003&&GetTickCount64()-lastLevels<1000;
            if(stalled){if(!silentSince.count(id))silentSince[id]=GetTickCount64();}else silentSince.erase(id);
            const bool silent=stalled&&GetTickCount64()-silentSince[id]>1500;
            const wchar_t* state=failed?L"Plugin unavailable":bypass?L"Bypassed":disconnected?(hardware?L"Device unavailable":L"Disconnected"):hardware&&!processing?L"Audio stopped":muted?L"Muted":hardware&&noRoute?L"No chain connection":silent?L"Input signal, no output":kind==L"plugin"?L"Plugin":kind==L"mixer"?L"Mixer":kind==L"input"?L"Audio input":L"Audio output";
            const bool marked=selected.count(id)||highlighted==id;
            const bool dotted=disconnected||noRoute;
            const wchar_t* style=highlighted==id?L"ChainSearchCardStyle":marked?L"ChainSelectedCardStyle":failed||silent?L"ChainWarningCardStyle":dotted?L"ChainDisconnectedCardStyle":L"ChainConnectedCardStyle";
            if(cardStyles[id]!=style){item.second.Style(Application::Current().Resources().Lookup(box_value(style)).as<Style>());item.second.Padding({0,0,0,0});cardStyles[id]=style;}
            if(dottedBorders.count(id))dottedBorders.at(id).Visibility(dotted&&!marked&&!failed&&!silent?Visibility::Visible:Visibility::Collapsed);
            item.second.Opacity(1);if(cardContents.count(id))cardContents.at(id).Opacity(bypass||inactive?.65:1);if(stripes.count(id))stripes.at(id).Visibility(bypass?Visibility::Visible:Visibility::Collapsed);
            if(auto label=statusViews.find(id);label!=statusViews.end()){const auto status=hardware&&((kind==L"input"&&monoInputs)||(kind==L"output"&&monoOutput))?text(state)+L" · "+text(L"Mono"):text(state);label->second.Text(text(kind==L"plugin"?L"Plugin":kind==L"mixer"?L"Mixer":kind==L"input"?L"Audio input":L"Audio output")+(hardware&&((kind==L"input"&&monoInputs)||(kind==L"output"&&monoOutput))?L" · "+text(L"Mono"):L""));lightHostModern::ui::HoverHelp::SetToolTip(label->second,box_value(status));}
        }
        const auto summary=text(L"Plugins")+L": "+chainText(total)+L"   ·   "+text(L"Bypassed")+L": "+chainText(bypassed)+L"   ·   "+text(L"Unconnected")+L": "+chainText(unconnected);if(statsText.Text()!=summary)statsText.Text(summary);
    }
    void updateIndicators(){
        indicatorLayer.Children().Clear();if(!showIndicators||!shown)return;
        const double w=viewport.ActualWidth(),h=viewport.ActualHeight(),z=number(L"zoom",1);if(w<80||h<80)return;
        const double cx=w/2,cy=h/2;std::vector<Point> placed;
        for(const auto& v:nodes()){const auto n=v.GetObject();const auto id=n.GetNamedString(L"id");const double x=n.GetNamedNumber(L"x")*z+number(L"panX"),y=n.GetNamedNumber(L"y")*z+number(L"panY"),nw=width(id)*z,nh=height(id)*z;
            if(x+nw>=0&&x<=w&&y+nh>=0&&y<=h)continue;
            const double dx=x+nw/2-cx,dy=y+nh/2-cy;
            const double scale=(std::min)((cx-18)/(std::max)(.001,std::abs(dx)),(cy-18)/(std::max)(.001,std::abs(dy)));
            const Point p{static_cast<float>(cx+dx*scale),static_cast<float>(cy+dy*scale)};
            // Coincident directions share one marker instead of overpainting it.
            if(std::any_of(placed.begin(),placed.end(),[&](Point q){return std::hypot(p.X-q.X,p.Y-q.Y)<18;}))continue;placed.push_back(p);
            Shapes::Path arrow;PathGeometry geometry;PathFigure f;f.StartPoint({-7,-6});LineSegment tip;tip.Point({1,0});LineSegment tail;tail.Point({-7,6});f.Segments().Append(tip);f.Segments().Append(tail);geometry.Figures().Append(f);arrow.Data(geometry);arrow.Style(Application::Current().Resources().Lookup(box_value(L"ChainWireStyle")).as<Style>());arrow.StrokeThickness(3);RotateTransform rotation;rotation.Angle(std::atan2(dy,dx)*180/3.141592653589793);arrow.RenderTransform(rotation);Canvas::SetLeft(arrow,p.X);Canvas::SetTop(arrow,p.Y);indicatorLayer.Children().Append(arrow);
        }
    }
    bool limitDistance(std::set<hstring> const& moving){
        if(moving.empty()||moving.size()==nodes().Size())return false;
        const double limit=std::clamp(static_cast<double>(GetPrivateProfileIntW(L"Chain",L"MaxSeparation",2400,uiSettingsFilePath().c_str())),600.0,12000.0);
        double best=1e30,shiftX=0,shiftY=0;
        // Find the nearest pair of rectangle edges; moving a selection preserves its layout.
        for(const auto& id:moving){auto a=node(id);const double ax=a.GetNamedNumber(L"x"),ay=a.GetNamedNumber(L"y");
            for(const auto& v:nodes()){auto b=v.GetObject();auto other=b.GetNamedString(L"id");if(moving.count(other))continue;const double bx=b.GetNamedNumber(L"x"),by=b.GetNamedNumber(L"y");
                const double dx=ax>bx+width(other)?bx+width(other)-ax:ax+width(id)<bx?bx-ax-width(id):0;
                const double dy=ay>by+height(other)?by+height(other)-ay:ay+height(id)<by?by-ay-height(id):0;
                const double distance=std::hypot(dx,dy);if(distance<best){best=distance;shiftX=dx;shiftY=dy;}
            }
        }
        if(best<=limit||best==1e30)return false;const double amount=(best-limit)/best;
        shiftX*=amount;shiftY*=amount;
        for(const auto& id:moving){const auto n=node(id);shiftX=std::clamp(shiftX,-50000-n.GetNamedNumber(L"x"),50000-n.GetNamedNumber(L"x"));shiftY=std::clamp(shiftY,-50000-n.GetNamedNumber(L"y"),50000-n.GetNamedNumber(L"y"));}
        for(const auto& id:moving){auto n=node(id);const double x=n.GetNamedNumber(L"x")+shiftX,y=n.GetNamedNumber(L"y")+shiftY;n.SetNamedValue(L"x",JsonValue::CreateNumberValue(x));n.SetNamedValue(L"y",JsonValue::CreateNumberValue(y));Canvas::SetLeft(cardViews.at(id),x);Canvas::SetTop(cardViews.at(id),y);}
        updatePortPositions();updateIndicators();return true;
    }
    void updateAnimationControl(){const bool performance=VisualPreferences::current().performance;animationCommand.IsEnabled(!performance);const bool on=!performance&&graph.GetNamedBoolean(L"animate",true);animationCommand.IsChecked(on);animationCommand.Content(Markup::XamlReader::Load(LR"(<PathIcon xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Width="20" Height="20" Data="M0,10 C5,10 5,2 10,2 C15,2 15,10 20,10 L20,12 C14,12 14,4 10,4 C6,4 6,12 0,12 Z M1,15 A2,2 0 1 1 1,19 A2,2 0 1 1 1,15 Z M10,15 A2,2 0 1 1 10,19 A2,2 0 1 1 10,15 Z M19,15 A2,2 0 1 1 19,19 A2,2 0 1 1 19,15 Z"/>)").as<PathIcon>());const auto label=text(on?L"Disable wire animation (audio stays on)":L"Enable wire animation (visual effect only)");lightHostModern::ui::HoverHelp::SetToolTip(animationCommand,box_value(label));Automation::AutomationProperties::SetName(animationCommand,label);}
    void constrainLayout(){
        if(nodes().Size()<2)return;std::set<hstring> remaining;hstring anchor;
        for(const auto& v:nodes()){auto n=v.GetObject();const auto id=n.GetNamedString(L"id");remaining.insert(id);if(anchor.empty()||n.GetNamedString(L"kind")==L"input")anchor=id;}remaining.erase(anchor);
        bool changed=false;
        while(!remaining.empty()){
            double best=1e30;hstring next;
            for(const auto& id:remaining){auto a=node(id);const double ax=a.GetNamedNumber(L"x"),ay=a.GetNamedNumber(L"y");for(const auto& v:nodes()){auto b=v.GetObject();auto other=b.GetNamedString(L"id");if(remaining.count(other))continue;const double bx=b.GetNamedNumber(L"x"),by=b.GetNamedNumber(L"y");const double dx=(std::max)({0.0,ax-bx-width(other),bx-ax-width(id)}),dy=(std::max)({0.0,ay-by-height(other),by-ay-height(id)});const double gap=std::hypot(dx,dy);if(gap<best){best=gap;next=id;}}}
            // Move the unplaced group together so nearby clusters retain their layout.
            changed=limitDistance(remaining)||changed;remaining.erase(next);
        }
        if(changed)submit();
    }
    fire_and_forget previewMixerGains() {
        auto lifetime=shared_from_this();if(!gainPreviewPending||gainPreviewBusy||busy||committing||editsFrozen)co_return;
        const auto batch=gainDeltas.batch();JsonArray values;
        for(const auto& delta:batch){auto current=node(hstring(delta.id));if(!current.HasKey(L"gains")||delta.lane>=current.GetNamedArray(L"gains").Size())continue;
            JsonObject value;value.SetNamedValue(L"id",JsonValue::CreateStringValue(delta.id));value.SetNamedValue(L"lane",JsonValue::CreateNumberValue(delta.lane));value.SetNamedValue(L"gain",JsonValue::CreateNumberValue(delta.gain));values.Append(value);}
        if(!values.Size()){gainDeltas.acknowledge(batch);gainPreviewPending=!gainDeltas.empty();co_return;}
        JsonObject r;r.SetNamedValue(L"action",JsonValue::CreateStringValue(L"mixer-gain"));r.SetNamedValue(L"values",values);stamp(r);
        gainPreviewPending=false;gainPreviewBusy=true;lastGainPreview=GetTickCount64();bool ok=false;try{ok=co_await dispatch(r);}catch(...){}gainPreviewBusy=false;
        if(ok){gainDeltas.acknowledge(batch);gainPreviewPending=!gainDeltas.empty();}
        // Failed preview is reported by the command layer. The final persistent
        // commit remains pending; do not emit an error on every timer tick.
    }
    void tick()
    {
        if(gainPreviewPending&&GetTickCount64()-lastGainPreview>=50)previewMixerGains();
        if(shown)updateDots();
        const auto interval=std::chrono::milliseconds(VisualPreferences::current().performance?100:33);if(timer.Interval()!=interval){timer.Interval(interval);updateAnimationControl();}
        if(distancePending&&!busy&&!committing&&!panning&&dragId.empty()&&resizeId.empty()&&!connecting&&GetTickCount64()-distanceChanged>400&&!cardViews.empty()){distancePending=false;constrainLayout();}
        if(pendingFit&&shown&&!busy&&!committing&&!panning&&dragId.empty()&&resizeId.empty()&&viewport.ActualWidth()>80&&viewport.ActualHeight()>80&&!cardViews.empty()){
            bool laidOut=true;for(const auto& pair:cardViews)laidOut=laidOut&&pair.second.ActualHeight()>0;
            if(laidOut){pendingFit=false;fit();}
        }
        if(viewSavePending&&!saveFailed&&!editsFrozen&&!gainPreviewBusy&&!insertionOpen&&!busy&&!committing&&!panning&&!selecting&&resizeId.empty()&&dragId.empty()&&!connecting&&GetTickCount64()-viewChanged>400){if(contentSavePending)commitPending();else{persistView();viewSavePending=false;}return;}
        if(shown&&GetTickCount64()-lastStatusUpdate>200){lastStatusUpdate=GetTickCount64();updateCardStates();}
        bool animate=!VisualPreferences::current().performance&&shown&&!busy&&graph.GetNamedBoolean(L"animate",true)&&GetTickCount64()-lastLevels<1000;
        try{animate=animate&&Windows::UI::ViewManagement::UISettings().AnimationsEnabled();}catch(...){}
        const auto phase=static_cast<float>((GetTickCount64()%1800)/1800.0);
        for(auto& wire:wireVisuals){const auto level=meterValues.GetNamedNumber(wire.id,0);wire.dot.Visibility(animate&&level>.00003&&!globalBypassed&&!globalMuted?Visibility::Visible:Visibility::Collapsed);
            const bool active=wire.available&&GetTickCount64()-lastLevels<1000&&level>.00003&&!globalBypassed&&!globalMuted;wireAppearance(wire);wire.flow.Visibility(active&&animate?Visibility::Visible:Visibility::Collapsed);if(active&&animate)wire.flow.StrokeDashOffset(-phase*24);if(animate){const auto p=bezier(wire.a,wire.b,phase);wire.motion.X(p.X-4.5);wire.motion.Y(p.Y-4.5);const auto blend=[&](uint8_t a,uint8_t b){return static_cast<uint8_t>(a+(b-a)*phase);};wire.flowBrush.Color(Windows::UI::Color{blend(wire.from.A,wire.to.A),blend(wire.from.R,wire.to.R),blend(wire.from.G,wire.to.G),blend(wire.from.B,wire.to.B)});}}
    }
    ::LightHostModernWinUI::LocalizationCatalog catalog;
    static constexpr double cardWidth=360,minZoom=.001;
    bool pendingFit=false,showIndicators=true,distancePending=false;uint64_t distanceChanged=0;Canvas indicatorLayer;TextBlock statsText;
    std::map<hstring,double> maximumWidths;
    std::map<hstring,std::vector<Grid>> portGrids;
    std::map<hstring,std::string> cardSignatures;
    std::map<hstring,Button> cardMenus;
    std::map<hstring,std::vector<std::pair<Slider,Primitives::ToggleButton>>> mixerControls;
    bool updatingMixerControls=false,editsFrozen=false,saveFailed=false;
    EditRevision editRevision;GainDeltas gainDeltas;
    hstring graphRevision=L"0",hostSession,latestRevision=L"0",latestSession;
    std::map<hstring,Shapes::Path> stripes;std::map<hstring,Shapes::Rectangle> dottedBorders;std::map<hstring,StackPanel> cardContents;
    hstring resizeId;double resizeWidth=0;bool inputPairs=false,outputPairs=true,monoInputs=false,monoOutput=false;
    AutoSuggestBox searchBox;Canvas selectionLayer;Shapes::Rectangle selectionBox;
    std::set<hstring> selected,selectionBase;std::map<hstring,TextBlock> statusViews;std::map<hstring,std::wstring> cardStyles;std::map<hstring,uint64_t> silentSince;
    JsonArray activeInputs,activeOutputs;hstring highlighted,profileId,profileGeneration,contextId;Point pressPoint{},selectionStart{};
    bool insertionOpen=false,selecting=false,movedGesture=false,releasing=false,committing=false,acceptView=true;uint64_t lastStatusUpdate=0;
    Action dispatch;std::function<void(std::string)> pluginAction;std::function<void(Point)> add;
    Shapes::Path dots;TranslateTransform dotTransform;double dotStep=0,dotWidth=0,dotHeight=0;ElementTheme dotTheme=ElementTheme::Default;
    Border wireHint;TextBlock wireHintText;hstring hoveredWire;
    Grid viewport;Canvas world,wires,cards,wireEnds,connectionOverlay;CompositeTransform viewTransform;Button undo,redo;Primitives::ToggleButton muteCommand,bypassCommand,animationCommand;TextBlock zoomText;DispatcherTimer timer;
    std::vector<std::pair<TextBlock,std::wstring>> toolbarLabels;
    MenuFlyout activeMenu{nullptr};
    JsonObject graph,meterValues,audioConfig,audioSelection,latestState,latestAudio,latestSelection;JsonArray plugins,latestPlugins;std::string lastSignature;std::map<hstring,Border> cardViews;
    bool busy=false,shown=false,panning=false,connecting=false,viewSavePending=false,contentSavePending=false,dialogOpen=false;
    bool globalMuted=false,globalBypassed=false;
    bool gainPreviewPending=false,gainPreviewBusy=false;uint64_t lastGainPreview=0;
    hstring dragId;Point lastPointer{},wirePointer{};Port source;uint64_t lastLevels=0,viewChanged=0;
};
}
