#pragma once
#include "HoverHelp.h"
#include "HostJson.h"
#include "VisualPreferences.h"
#include "Localization.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lightHostModern::ui
{
class SessionStatusPresenter
{
public:
    void update(const std::string& json, ::LightHostModernWinUI::LocalizationCatalog& catalog,
        winrt::Microsoft::UI::Xaml::Controls::InfoBar banner,
        winrt::Microsoft::UI::Xaml::Controls::TextBlock pendingText,
        winrt::Microsoft::UI::Xaml::Controls::Button retry)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        const auto value = ipc::field(json, "session");
        if (value.ValueType() != ipc::JsonValueType::Object) return;
        const auto state = to_string(value.Stringify());
        const auto error = ipc::extractString(state, "error"), recovery = ipc::extractString(state, "recoveryError");
        const bool captureFailed = ipc::extractArray(state, "captureFailures").Size() > 0;
        const bool pending = ipc::extractBool(state, "pending");
        const bool readOnlyRecovery = !ipc::extractBool(state, "writable") && !recovery.empty();
        const bool saveFailed = !error.empty() && !readOnlyRecovery;
        pendingText.Text(L"\xE74E");
        lightHostModern::ui::HoverHelp::SetToolTip(pendingText,box_value(catalog.text("session.pending", L"Saving session…")));
        Automation::AutomationProperties::SetName(pendingText,catalog.text("session.pending", L"Saving session…"));
        pendingText.Visibility(pending && error.empty() ? Visibility::Visible : Visibility::Collapsed);
        retry.Content(box_value(catalog.text("session.retry", L"Try saving again")));
        retry.Visibility(saveFailed ? Visibility::Visible : Visibility::Collapsed);
        banner.Title(catalog.text(saveFailed ? "session.saveFailed" : !recovery.empty() ? "session.recovery" : "session.captureFailed",
            saveFailed ? L"Session could not be saved" : !recovery.empty() ? L"Session recovery needs attention" : L"Some plugin states could not be captured"));
        banner.Message(catalog.text(saveFailed ? "session.saveFailedBody" : !recovery.empty() ? "session.recoveryBody" : "session.captureFailedBody",
            saveFailed ? L"Existing files have been kept. Free storage or restore access, then try again."
            : !recovery.empty() ? L"Recoverable data and the original files have been kept. Some information may be unavailable."
            : L"The last valid states have been kept for these plugins."));
        lightHostModern::ui::HoverHelp::SetToolTip(banner, box_value(to_hstring(error.empty() ? recovery : error)));
        banner.Severity(saveFailed ? InfoBarSeverity::Error : InfoBarSeverity::Warning);
        const auto key = error + "\n" + recovery + (captureFailed ? "\ncapture" : "");
        if (key != previous)
        {
            banner.IsOpen(!error.empty() || !recovery.empty() || captureFailed);
            previous = key;
        }
    }
private:
    std::string previous;
};
}
