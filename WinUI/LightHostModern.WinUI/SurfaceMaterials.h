#pragma once
#include "HoverHelp.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace lightHostModern::ui
{
// One policy for bounded surfaces and native popups. Weak references let closed
// dialogs and rebuilt canvas cards release their backdrop targets immediately.
class SurfaceMaterials
{
    using Border = winrt::Microsoft::UI::Xaml::Controls::Border;
    using Element = winrt::Microsoft::UI::Xaml::FrameworkElement;
    using BackdropElement = winrt::Microsoft::UI::Xaml::Controls::SystemBackdropElement;
    using Menu = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutPresenter;
    using Popup = winrt::Microsoft::UI::Xaml::Controls::Primitives::Popup;
    struct Surface { winrt::weak_ref<Border> border; winrt::weak_ref<BackdropElement> backdrop; };
    std::vector<Surface> surfaces;
    std::vector<winrt::weak_ref<Menu>> menus;
    std::vector<winrt::weak_ref<Popup>> nativePopups;
    std::vector<winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement>> inspected;
    int mode = 3;
    bool contrast = false;
    winrt::Microsoft::UI::Xaml::ElementTheme theme = winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
    uint64_t lastPoll = 0;
    bool preparing = false;
    std::vector<winrt::weak_ref<Element>> watched;

    void prepareTree(winrt::Microsoft::UI::Xaml::DependencyObject const& object)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        if(auto popup=object.try_as<Popup>()){
            if(!popup.IsOpen()) {
                if(auto element=popup.Child().try_as<Element>();element&&element.RequestedTheme()!=theme)
                    element.RequestedTheme(theme);
                if(auto border=popup.Child().try_as<Border>())attach(border);
            }
            return;
        }
        if(auto control=object.try_as<Controls::Control>())control.ApplyTemplate();
        if(auto border=object.try_as<Border>();border&&border.Name()==L"BackgroundElement")attach(border);
        // Closed Popup elements are logical Panel children; their content does
        // not participate in the open visual tree yet.
        if(auto panel=object.try_as<Controls::Panel>()){
            for(auto const& child:panel.Children())prepareTree(child);
        }else{
            for(int i=0;i<Media::VisualTreeHelper::GetChildrenCount(object);++i)
                prepareTree(Media::VisualTreeHelper::GetChild(object,i));
        }
    }

    template<class T> static void remember(std::vector<winrt::weak_ref<T>>& entries, T const& value)
    {
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& entry){return !entry.get();}),entries.end());
        if (std::none_of(entries.begin(),entries.end(),[&](const auto& entry){return entry.get()==value;}))
            entries.push_back(winrt::make_weak(value));
    }

    winrt::Microsoft::UI::Xaml::Media::Brush fallback() const
    {
        using namespace winrt;
        auto color = theme == Microsoft::UI::Xaml::ElementTheme::Dark
            ? Windows::UI::Color{255,45,45,45} : Windows::UI::Color{255,255,255,255};
        if (contrast) color = Windows::UI::ViewManagement::UISettings().GetColorValue(Windows::UI::ViewManagement::UIColorType::Background);
        return Microsoft::UI::Xaml::Media::SolidColorBrush(color);
    }
    winrt::Microsoft::UI::Xaml::Media::SystemBackdrop backdrop() const
    {
        using namespace winrt::Microsoft::UI;
        if (contrast || mode == 3) return nullptr;
        if (mode == 2) return Xaml::Media::DesktopAcrylicBackdrop();
        Xaml::Media::MicaBackdrop mica;
        mica.Kind(mode == 1 ? Composition::SystemBackdrops::MicaKind::BaseAlt : Composition::SystemBackdrops::MicaKind::Base);
        return mica;
    }
    void apply(Surface const& surface)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        if (auto border = surface.border.get()) if (auto native = surface.backdrop.get()) {
            border.Background(fallback());
            native.RequestedTheme(theme);
            native.CornerRadius(border.CornerRadius());
            const auto padding = border.Padding();
            native.Margin({-padding.Left,-padding.Top,-padding.Right,-padding.Bottom});
            native.SystemBackdrop(backdrop());
            native.Visibility(mode == 3 || contrast ? Visibility::Collapsed : Visibility::Visible);
        }
    }
    void apply(Menu const& menu)
    {
        menu.RequestedTheme(theme);
        menu.SystemBackdrop(backdrop());
        menu.Background(mode == 3 || contrast ? fallback() : winrt::Microsoft::UI::Xaml::Media::SolidColorBrush({0,0,0,0}));
    }
    void inspect(winrt::Microsoft::UI::Xaml::DependencyObject const& object, Popup const& popup)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        HoverHelp::current().describe(object);
        if (auto menu = object.try_as<Menu>()) {
            remember(menus,menu); apply(menu);
        }
        if (auto border = object.try_as<Border>()) {
            const auto name = border.Name();
            if (name == L"BackgroundElement") attach(border);
            if (name == L"PopupBorder" || name == L"SuggestionsContainer") {
                // Prepared by watch() before opening. Never reparent content
                // or create a windowed Popup backdrop during input handling.
                return;
            }
        }
        // The dialog footer binds to the original opaque dialog background.
        // Let the same bounded material show through both content and buttons;
        // the template's top overlay and divider retain the visual separation.
        if (auto grid = object.try_as<Controls::Grid>(); grid && grid.Name() == L"CommandSpace")
            grid.Background(Media::SolidColorBrush({0,0,0,0}));
        if (auto control = object.try_as<Controls::Control>(); control &&
            (control.try_as<Controls::ToolTip>() || control.try_as<Controls::FlyoutPresenter>())) {
            popup.SystemBackdrop(backdrop()); remember(nativePopups,popup);
            control.Background(mode == 3 || contrast ? fallback() : Media::SolidColorBrush({0,0,0,0}));
        }
        const int count = Media::VisualTreeHelper::GetChildrenCount(object);
        for (int i = 0; i < count; ++i) inspect(Media::VisualTreeHelper::GetChild(object,i), popup);
    }
public:
    static SurfaceMaterials& current() { static SurfaceMaterials value; return value; }
    void prepare(Element const& root) {
        if(preparing)return;preparing=true;
        try{prepareTree(root);}catch(...){preparing=false;throw;}
        preparing=false;
    }
    void watch(Element const& root) {
        watched.erase(std::remove_if(watched.begin(),watched.end(),[](const auto& w){return !w.get();}),watched.end());
        if(std::any_of(watched.begin(),watched.end(),[&](const auto& w){return w.get()==root;}))return;
        watched.push_back(winrt::make_weak(root));
        const auto weak=winrt::make_weak(root);
        root.Loaded([weak](const auto&,const auto&){if(auto value=weak.get())current().prepare(value);});
        root.PreviewKeyDown([weak](const auto&,const auto&){if(auto value=weak.get())current().prepare(value);});
        root.AddHandler(winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),winrt::box_value(winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
            [weak](const auto&,const auto&){if(auto value=weak.get())current().prepare(value);})),true);
        if(root.IsLoaded())prepare(root);
    }
    void attach(Border const& border)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        surfaces.erase(std::remove_if(surfaces.begin(),surfaces.end(),[](const auto& s){return !s.border.get();}),surfaces.end());
        for (const auto& surface : surfaces) if (surface.border.get() == border) return;
        auto child = border.Child(); if (!child) return;
        // DialogSpace is already a Grid. Insert the backdrop behind its rows
        // without detaching the content or its buttons: reparenting an open
        // dialog drops focus/capture and can swallow the first click.
        if(auto grid=child.try_as<Controls::Grid>()){
            BackdropElement native;native.IsHitTestVisible(false);
            Automation::AutomationProperties::SetAccessibilityView(native,Automation::Peers::AccessibilityView::Raw);
            Controls::Grid::SetRowSpan(native,(std::max)(1,static_cast<int>(grid.RowDefinitions().Size())));
            Controls::Grid::SetColumnSpan(native,(std::max)(1,static_cast<int>(grid.ColumnDefinitions().Size())));
            grid.Children().InsertAt(0,native);
            Surface surface{winrt::make_weak(border),winrt::make_weak(native)};surfaces.push_back(surface);apply(surface);return;
        }
        Controls::Control focused{nullptr};
        if(border.XamlRoot()) {
            auto candidate=Input::FocusManager::GetFocusedElement(border.XamlRoot()).try_as<Controls::Control>();
            for(DependencyObject ancestor=candidate;ancestor;ancestor=Media::VisualTreeHelper::GetParent(ancestor))
                if(ancestor==border){focused=candidate;break;}
        }
        border.Child(nullptr);
        Controls::Grid layers;
        BackdropElement native; native.IsHitTestVisible(false);
        Automation::AutomationProperties::SetAccessibilityView(native, Automation::Peers::AccessibilityView::Raw);
        layers.Children().Append(native); layers.Children().Append(child); border.Child(layers);
        Surface surface{winrt::make_weak(border),winrt::make_weak(native)};
        surfaces.push_back(surface); apply(surface);
        // Applying a dialog material removes its content from the visual tree.
        // Keep the control the user was editing focused after it is reloaded.
        if(focused){
            auto token=std::make_shared<winrt::event_token>();
            *token=focused.LayoutUpdated([weak=winrt::make_weak(focused),token](const auto&,const auto&){
                if(auto control=weak.get();control&&control.IsLoaded()&&control.Focus(FocusState::Programmatic))
                    control.LayoutUpdated(*token);
            });
        }
    }
    void configure(int requested, winrt::Microsoft::UI::Xaml::ElementTheme actualTheme, bool highContrast)
    {
        if (mode == requested && theme == actualTheme && contrast == highContrast) return;
        mode = requested; theme = actualTheme; contrast = highContrast;
        surfaces.erase(std::remove_if(surfaces.begin(),surfaces.end(),[](const auto& s){return !s.border.get();}),surfaces.end());
        for (const auto& surface : surfaces) apply(surface);
        menus.erase(std::remove_if(menus.begin(),menus.end(),[](const auto& s){return !s.get();}),menus.end());
        // A material selection may be raised inside a popup's close operation.
        // Replacing its backdrop here re-enters native popup teardown. Defer
        // popup changes to the next inspection, outside that event stack.
        inspected.clear();
        nativePopups.erase(std::remove_if(nativePopups.begin(),nativePopups.end(),[](const auto& s){return !s.get();}),nativePopups.end());
    }
    void poll(Element const& root, bool force = false)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        if (!root.XamlRoot() || (!force && GetTickCount64()-lastPoll < 100)) return;
        lastPoll = GetTickCount64();
        auto popups = Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root.XamlRoot());
        // Keep only open roots: a reused popup can have different template parts.
        inspected.erase(std::remove_if(inspected.begin(),inspected.end(),[&](const auto& weak){auto child=weak.get();if(!child)return true;for(const auto& p:popups)if(p.Child()==child)return false;return true;}),inspected.end());
        for (const auto& popup : popups) {
            auto child = popup.Child(); if (!child || !Media::VisualTreeHelper::GetChildrenCount(child)) continue;
            if (std::any_of(inspected.begin(),inspected.end(),[&](const auto& weak){return weak.get()==child;})) continue;
            inspect(child,popup); inspected.push_back(winrt::make_weak(child));
        }
    }
};
}
