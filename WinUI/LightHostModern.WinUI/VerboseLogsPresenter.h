#pragma once
#include "HoverHelp.h"
#include "DialogPresentation.h"
#include "HostConnection.h"
#include "VisualPreferences.h"
#include "Localization.h"
#include "../../Source/VerboseLog.h"
#include <shobjidl.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHostModern::ui
{
class VerboseLogsPresenter : public std::enable_shared_from_this<VerboseLogsPresenter>
{
    using Action = std::function<winrt::Windows::Foundation::IAsyncOperation<bool>(std::string)>;
    using Panel = winrt::Microsoft::UI::Xaml::Controls::StackPanel;
public:
    ~VerboseLogsPresenter() { close(); }
    void close() { closed=true; if(timer)timer.Stop(); }
    void create(Panel parent, const ::LightHostModernWinUI::LocalizationCatalog& language,
                std::shared_ptr<HostConnection> connection, HWND window, Action command)
    {
        using namespace winrt; using namespace Microsoft::UI::Xaml; using namespace Controls;
        catalog=language;host=std::move(connection);hwnd=window;send=std::move(command);
        Border card;card.Style(Application::Current().Resources().Lookup(box_value(L"AppCardStyle")).as<Style>());
        Grid row;row.ColumnSpacing(16);card.Child(row);parent.Children().InsertAt(0,card);
        ColumnDefinition iconColumn;iconColumn.Width(GridLengthHelper::FromPixels(32));row.ColumnDefinitions().Append(iconColumn);
        ColumnDefinition label;label.Width(GridLengthHelper::FromValueAndType(1,GridUnitType::Star));row.ColumnDefinitions().Append(label);
        ColumnDefinition control;control.Width(GridLengthHelper::Auto());row.ColumnDefinitions().Append(control);
        FontIcon icon;icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(L"\uE9D9");icon.VerticalAlignment(VerticalAlignment::Center);row.Children().Append(icon);
        Panel content;content.Spacing(2);content.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(content,1);row.Children().Append(content);
        title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
        title.TextWrapping(TextWrapping::Wrap);
        const auto captionStyle=Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>();
        description.Style(captionStyle);summary.Style(captionStyle);status.Style(captionStyle);
        description.TextWrapping(TextWrapping::Wrap);summary.TextWrapping(TextWrapping::Wrap);
        status.TextWrapping(TextWrapping::Wrap);status.IsTextSelectionEnabled(true);
        content.Children().Append(title);content.Children().Append(description);
        content.Children().Append(summary);content.Children().Append(restartLink);
        content.Children().Append(status);content.Children().Append(action);
        restartLink.Style(Application::Current().Resources().Lookup(box_value(L"AppStatusLinkStyle")).as<Style>());
        restartLink.Padding(Thickness{0,4,0,4});restartLink.HorizontalAlignment(HorizontalAlignment::Left);
        toggle.Style(Application::Current().Resources().Lookup(box_value(L"CompactToggleSwitchStyle")).as<Style>());
        toggle.HorizontalAlignment(HorizontalAlignment::Right);toggle.VerticalAlignment(VerticalAlignment::Center);
        Panel toggleControls;toggleControls.Orientation(Orientation::Horizontal);toggleControls.Spacing(12);
        toggleControls.HorizontalAlignment(HorizontalAlignment::Right);toggleControls.VerticalAlignment(VerticalAlignment::Center);
        toggleState.VerticalAlignment(VerticalAlignment::Center);
        toggleControls.Children().Append(toggleState);toggleControls.Children().Append(toggle);
        Grid::SetColumn(toggleControls,2);row.Children().Append(toggleControls);
        action.Style(Application::Current().Resources().Lookup(box_value(L"AppStatusLinkStyle")).as<Style>());
        action.HorizontalAlignment(HorizontalAlignment::Left);action.Padding(Thickness{0,4,0,4});
        Automation::AutomationProperties::SetAutomationId(summary,L"VerboseLogsStatus");
        Automation::AutomationProperties::SetAutomationId(restartLink,L"VerboseLogsRestartLink");
        Automation::AutomationProperties::SetAutomationId(toggleState,L"VerboseLogsToggleState");
        Automation::AutomationProperties::SetAutomationId(toggle,L"TrackVerboseLogs");
        Automation::AutomationProperties::SetAutomationId(action,L"SaveVerboseLogs");
        auto weak=weak_from_this();
        toggle.Toggled([weak](auto&&,auto&&){if(auto self=weak.lock();self&&!self->syncing&&!self->busy)self->change();});
        action.Click([weak](auto&&,auto&&){if(auto self=weak.lock();self&&!self->busy)self->act();});
        restartLink.Click([weak](auto&&,auto&&){if(auto self=weak.lock();self&&!self->busy&&self->phase=="armed")self->act();});
        timer.Interval(std::chrono::seconds(1));timer.Tick([weak](auto&&,auto&&){if(auto self=weak.lock())self->refresh();});
        translate(language);timer.Start();refresh();
    }
    void translate(const ::LightHostModernWinUI::LocalizationCatalog& language) {
        catalog=language;title.Text(text("title"));description.Text(text("description"));
        lightHostModern::ui::HoverHelp::SetToolTip(toggle,winrt::box_value(text("tooltip")));
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(toggle,text("track"));
        render();
    }
private:
    winrt::hstring text(const char* key)const {return catalog.text(std::string("diagnostics.logs.")+key);}
    void render() {
        using namespace winrt::Microsoft::UI::Xaml;
        syncing=true;toggle.IsOn(phase!="off");toggle.IsEnabled(!busy&&(phase=="off"||phase=="armed"));syncing=false;
        toggleState.Text(catalog.text(phase=="off"?"common.off":"common.on",phase=="off"?L"Off":L"On"));
        summary.Text(text(("state."+phase).c_str()));
        summary.Visibility(phase=="stopped"||phase=="paused"?Visibility::Visible:Visibility::Collapsed);
        restartLink.Content(winrt::box_value(text("state.armed")));
        restartLink.Visibility(phase=="armed"?Visibility::Visible:Visibility::Collapsed);restartLink.IsEnabled(!busy);
        status.Text(winrt::to_hstring(message));
        if((phase=="collecting"||phase=="paused"||phase=="stopped")&&message.empty()&&!error.empty())
            status.Text(catalog.text("diagnostics.logs.error."+error,verbose::wide(error)));
        status.Visibility(status.Text().empty()?Visibility::Collapsed:Visibility::Visible);
        action.Visibility(phase=="off"||phase=="armed"?Visibility::Collapsed:Visibility::Visible);
        action.IsEnabled(!busy);action.Content(winrt::box_value(text(phase=="stopped"?"save":"stop")));
    }
    winrt::fire_and_forget refresh() {
        auto lifetime=shared_from_this();if(busy||polling||closed)co_return;polling=true;
        try {
            auto response=winrt::to_string(co_await host->requestAsync("verbose-log-status"));
            if(!closed&&!busy&&extractString(response,"status")=="ok") {
                phase=extractString(response,"phase");session=extractString(response,"session");
                error=extractString(response,"error");render();
            }
        } catch(...) {}polling=false;
    }
    winrt::Windows::Foundation::IAsyncAction restart() {
        using namespace winrt;using namespace Microsoft::UI::Xaml::Controls;
        ContentDialog dialog;dialog.XamlRoot(toggle.XamlRoot());dialog.Title(box_value(text("restartTitle")));dialog.Content(box_value(text("restartBody")));
        dialog.PrimaryButtonText(text("restart"));dialog.CloseButtonText(text("later"));dialog.DefaultButton(ContentDialogButton::Close);
        if(co_await lightHostModern::ui::showAppDialog(dialog)==ContentDialogResult::Primary&&!closed) {
            const auto payload="restart-host:{\"uiPid\":"+std::to_string(GetCurrentProcessId())+",\"uiCreated\":\""+std::to_string(verbose::processBirth(GetCurrentProcess()))+"\"}";
            if(!(co_await send(payload)))message=to_string(text("failed"));
        }
    }
    winrt::fire_and_forget change() {
        auto lifetime=shared_from_this();if(closed)co_return;
        const bool enable=toggle.IsOn();busy=true;message.clear();render();
        try {
            if(co_await send(std::string("set-verbose-logs:")+(enable?"1":"0"))) {
                phase=enable?"armed":"off";render();if(enable)co_await restart();
            } else message=winrt::to_string(text("failed"));
        }catch(...){message=winrt::to_string(text("failed"));}
        busy=false;if(!closed){render();refresh();}
    }
    winrt::fire_and_forget act() {
        auto lifetime=shared_from_this();if(closed)co_return;busy=true;message.clear();render();
        try {
            if(phase=="armed") co_await restart();
            else {
                if(phase!="stopped") {
                    if(!(co_await send("stop-verbose-logs")))throw std::runtime_error("stop_failed");
                    phase="stopped";render();
                }
                // Native owner-bound Windows save dialog handles overwrite confirmation.
                winrt::com_ptr<IFileSaveDialog> dialog;winrt::check_hresult(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put())));
                COMDLG_FILTERSPEC filter{L"Text log (*.txt)",L"*.txt"};dialog->SetFileTypes(1,&filter);dialog->SetDefaultExtension(L"txt");
                DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT);
                auto stamp=verbose::wide(verbose::timestamp());for(auto& c:stamp)if(c==L':'||c==L'.')c=L'-';
                dialog->SetFileName((L"LightHostModern-diagnostics-"+stamp+L".txt").c_str());
                const auto result=dialog->Show(hwnd);
                if(result!=HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                    winrt::check_hresult(result);winrt::com_ptr<IShellItem> selected;winrt::check_hresult(dialog->GetResult(selected.put()));
                    PWSTR raw=nullptr;winrt::check_hresult(selected->GetDisplayName(SIGDN_FILESYSPATH,&raw));
                    std::filesystem::path path(raw);CoTaskMemFree(raw);
                    winrt::apartment_context ui;
                    const auto capture=session;std::string failure;
                    co_await winrt::resume_background();
                    try{verbose::logger().shutdown();verbose::exportText(path,capture);}catch(const std::exception& e){failure=e.what();}
                    co_await ui;
                    if(!failure.empty())throw std::runtime_error(failure);
                    if(!(co_await send("complete-verbose-logs:"+capture)))throw std::runtime_error("complete_failed");
                    phase="off";message.clear();
                }
            }
        }catch(const std::exception& e){message=winrt::to_string(text("failed"))+" ("+e.what()+")";}
        catch(...){message=winrt::to_string(text("failed"));}
        busy=false;if(!closed){render();refresh();}
    }
    ::LightHostModernWinUI::LocalizationCatalog catalog;
    std::shared_ptr<HostConnection> host;Action send;HWND hwnd=nullptr;
    winrt::Microsoft::UI::Xaml::DispatcherTimer timer;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock title,description,summary,status,toggleState;
    winrt::Microsoft::UI::Xaml::Controls::HyperlinkButton restartLink;
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch toggle;
    winrt::Microsoft::UI::Xaml::Controls::HyperlinkButton action;
    std::string phase="off",session,error,message;
    bool busy=false,polling=false,syncing=false,closed=false;
};
}
