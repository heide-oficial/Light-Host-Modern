#pragma once
#include "VisualPreferences.h"
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <memory>

namespace lightHostModern::ui
{
// Composition masks the existing scroll viewport without changing its layout,
// focus, input handling or material. No timer or captured bitmap is needed.
class ScrollEdgeFade : public std::enable_shared_from_this<ScrollEdgeFade>
{
    using Element = winrt::Microsoft::UI::Xaml::FrameworkElement;
    using Scroll = winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
    using Preview = winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;
    Element host{nullptr}, content{nullptr}; Scroll scroll{nullptr};
    winrt::Microsoft::UI::Composition::Visual original{nullptr};
    winrt::Microsoft::UI::Composition::SpriteVisual output{nullptr};
    winrt::Microsoft::UI::Composition::CompositionVisualSurface surface{nullptr};
    winrt::Microsoft::UI::Composition::CompositionLinearGradientBrush gradient{nullptr};
    winrt::event_token viewToken{},sizeToken{},loadedToken{};
    bool enabled=true, contrast=false;
    static Scroll findScroll(winrt::Microsoft::UI::Xaml::DependencyObject const& root) {
        using Tree=winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper;
        if(auto found=root.try_as<Scroll>())return found;
        for(int i=0;i<Tree::GetChildrenCount(root);++i)if(auto found=findScroll(Tree::GetChild(root,i)))return found;
        return nullptr;
    }
    void update() {
        if(!content.IsLoaded())return;
        if(!scroll){scroll=findScroll(content);if(scroll)viewToken=scroll.ViewChanged([weak=weak_from_this()](const auto&,const auto&){if(auto s=weak.lock())s->update();});}
        const bool top=scroll&&scroll.VerticalOffset()>0.5;
        const bool bottom=scroll&&scroll.ScrollableHeight()-scroll.VerticalOffset()>0.5;
        const float w=static_cast<float>(content.ActualWidth()),h=static_cast<float>(content.ActualHeight());
        if(!enabled||contrast||VisualPreferences::current().performance||(!top&&!bottom)||w<=0||h<=0){clear();return;}
        if(!output){
            original=Preview::GetElementVisual(content);auto compositor=original.Compositor();
            surface=compositor.CreateVisualSurface();surface.SourceVisual(original);
            auto source=compositor.CreateSurfaceBrush(surface);
            gradient=compositor.CreateLinearGradientBrush();gradient.StartPoint({0,0});gradient.EndPoint({0,1});
            auto mask=compositor.CreateMaskBrush();mask.Source(source);mask.Mask(gradient);
            output=compositor.CreateSpriteVisual();output.Brush(mask);
            Preview::SetElementChildVisual(host,output);original.Opacity(0);
        }
        surface.SourceSize({w,h});output.Size({w,h});
        const auto offset=content.TransformToVisual(host).TransformPoint({0,0});output.Offset({offset.X,offset.Y,0});
        const float fraction=(std::min)(0.2f,20.0f/h);auto compositor=output.Compositor();
        gradient.ColorStops().Clear();
        gradient.ColorStops().Append(compositor.CreateColorGradientStop(0,{static_cast<uint8_t>(top?0:255),255,255,255}));
        gradient.ColorStops().Append(compositor.CreateColorGradientStop(fraction,{255,255,255,255}));
        gradient.ColorStops().Append(compositor.CreateColorGradientStop(1-fraction,{255,255,255,255}));
        gradient.ColorStops().Append(compositor.CreateColorGradientStop(1,{static_cast<uint8_t>(bottom?0:255),255,255,255}));
    }
    void clear() {
        if(original)original.Opacity(1);
        if(output)Preview::SetElementChildVisual(host,nullptr);
        output=nullptr;surface=nullptr;gradient=nullptr;original=nullptr;
    }
public:
    static std::shared_ptr<ScrollEdgeFade> attach(Element const& host,Element const& content) {
        auto result=std::make_shared<ScrollEdgeFade>();result->host=host;result->content=content;
        result->enabled=VisualPreferences::current().fade;
        std::weak_ptr<ScrollEdgeFade> weak=result;
        result->loadedToken=content.Loaded([weak](const auto&,const auto&){if(auto s=weak.lock())s->update();});
        result->sizeToken=content.SizeChanged([weak](const auto&,const auto&){if(auto s=weak.lock())s->update();});
        result->update();return result;
    }
    void configure(bool fade,bool highContrast){enabled=fade;contrast=highContrast;update();}
    ~ScrollEdgeFade(){if(scroll)scroll.ViewChanged(viewToken);if(content){content.Loaded(loadedToken);content.SizeChanged(sizeToken);}clear();}
};
}
