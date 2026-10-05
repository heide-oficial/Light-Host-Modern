#pragma once
#include "SurfaceMaterials.h"
#include "UiCoordination.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace lightHostModern::ui
{
// Use explicitly for code-created dialogs: popup roots do not reliably inherit
// an implicit application style. Prepare the template/material before ShowAsync,
// so pointer handling never has to rebuild the dialog that is being clicked.
inline winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
showAppDialog(winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog)
{
    using namespace winrt::Microsoft::UI::Xaml;
    using Controls::ContentDialogResult;
    // One presentation per root, without queuing stale prompts. Keep the root
    // alive until completion so its identity cannot be reused during an await.
    const auto root = dialog.XamlRoot();
    if (!root) co_return ContentDialogResult::None;
    DialogLease lease(winrt::get_abi(root));
    if (!lease) co_return ContentDialogResult::None;
    try {
        dialog.Style(Application::Current().Resources().Lookup(winrt::box_value(L"AppContentDialogStyle")).as<Style>());
        SurfaceMaterials::current().prepare(dialog);
        co_return co_await dialog.ShowAsync();
    } catch (...) { co_return ContentDialogResult::None; }
}
}
