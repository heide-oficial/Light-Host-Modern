#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "WinUIDebug.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::LightHostWinUI::implementation
{
    App::App()
    {
        setWinUIDebugEnabled(commandLineHasFlag(L"--debug") || commandLineHasFlag(L"-debug"));
        setWinUIDebugLogPath(commandLineOptionValue(L"--debug-log"));
        initialiseWinUIDebugConsole();
        winUILog("App constructed.");

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            winUILog("Unhandled XAML exception: " + winrt::to_string(e.Message()));

            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        winUILog("OnLaunched.");

        try
        {
            loadNativeWinUIResources();
            window = make<MainWindow>();
            winUILog("MainWindow created.");
            window.Activate();
            winUILog("MainWindow activated.");
        }
        catch (winrt::hresult_error const& e)
        {
            std::stringstream stream;
            stream << "OnLaunched failed: " << winrt::to_string(e.message())
                << " HRESULT=0x" << std::hex << static_cast<uint32_t>(e.code());
            winUILog(stream.str());
        }
        catch (std::exception const& e)
        {
            winUILog(std::string("OnLaunched failed: ") + e.what());
        }
    }

    void App::loadNativeWinUIResources()
    {
        if (nativeWinUIResourcesLoaded)
            return;

        try
        {
            Resources().MergedDictionaries().Append(Microsoft::UI::Xaml::Controls::XamlControlsResources());
            nativeWinUIResourcesLoaded = true;
            winUILog("Native WinUI XamlControlsResources loaded.");
        }
        catch (winrt::hresult_error const& e)
        {
            std::stringstream stream;
            stream << "Native WinUI XamlControlsResources unavailable: " << winrt::to_string(e.message())
                << " HRESULT=0x" << std::hex << static_cast<uint32_t>(e.code());
            winUILog(stream.str());
        }
        catch (std::exception const& e)
        {
            winUILog(std::string("Native WinUI XamlControlsResources unavailable: ") + e.what());
        }
    }
}
