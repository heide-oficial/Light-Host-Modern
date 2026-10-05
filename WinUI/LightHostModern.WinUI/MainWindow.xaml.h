#pragma once
#include "SessionStatusPresenter.h"

#include "MainWindow.g.h"
#include "Localization.h"
#include "HostTransport.h"
#include "HostConnection.h"
#include "UpdateService.h"
#include "PluginPageController.h"
#include "AudioPageController.h"
#include "AudioIntent.h"
#include "MeterPresenter.h"
#include "DiagnosticsPresenter.h"
#include "GpuMemorySampler.h"
#include "VerboseLogsPresenter.h"
#include "OperatingPresenter.h"
#include "WindowMaterial.h"
#include "ScrollEdgeFade.h"
#include <winrt/Windows.UI.ViewManagement.h>
#include "AudioPageView.xaml.h"
#include "PluginsPageView.xaml.h"
#include "SupportPageView.xaml.h"
#include "SettingsPageView.xaml.h"
#include "DiagnosticsPageView.xaml.h"
#include "DatabasePageView.xaml.h"
#include "ScanPathsDialog.h"
#include "PageState.h"
#include <map>

namespace winrt::LightHostModernWinUI::implementation
{
    struct ChannelRowData
    {
        std::string label;
        int startIndex = 0;
        int endIndex = 0;
        bool active = false;
        bool partial = false;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        lightHostModern::ui::WindowMaterial windowMaterial;
        winrt::LightHostModernWinUI::PageState Pages() const { return pageState; }
        winrt::fire_and_forget CancelPluginScan_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget RetryPluginScan_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget ViewScanFailures_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget GlobalAudioControl_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);

        void Dashboard_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Preferences_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Plugins_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Config_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Support_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Navigation_SelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const&,
                                         Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
        void KoFi_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SupportRepository_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SupportShowcase_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DownloadUpdate_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HideSupportTabSwitch_Toggled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void LanguageBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void PluginSearchBox_TextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
        void PluginActions_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SidebarToggle_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Refresh_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ThemeModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void FluentDropdownButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget FluentDropdownItem_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget ChannelButton_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        void ComboBox_DropDownOpened(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void ComboBox_DropDownClosed(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void RootLayout_SizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void RootLayout_PointerPressed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void RunningPluginsTab_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void InstalledPluginsTab_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenWindowsSoundSettings_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RepositoryButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OriginalRepositoryButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget AudioBackendBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        winrt::fire_and_forget InputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        winrt::fire_and_forget OutputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        winrt::fire_and_forget renameAudioChannel(bool input, int channel, int width, winrt::hstring label);
        winrt::fire_and_forget ChannelCheckBox_Changed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget InputChannelsToggleAll_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget OutputChannelsToggleAll_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget SampleRateBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        winrt::fire_and_forget BufferSizeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        void RunningPluginsListView_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void RunningPluginsListView_DragItemsStarting(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const&);
        void RunningPluginsListView_DragItemsCompleted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const&);
        void RunningPluginItem_DragStarting(Microsoft::UI::Xaml::UIElement const&, Microsoft::UI::Xaml::DragStartingEventArgs const&);
        void RunningPluginItem_DragOver(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        winrt::fire_and_forget RunningPluginItem_Drop(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::DragEventArgs);
        winrt::fire_and_forget BypassPlugin_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget OpenPluginEditor_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget DuplicatePlugin_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget RemovePlugin_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget ScanDefaultPlugins_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget AddInstalledPlugin_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget OpenInstalledPluginLocation_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget RemoveInstalledPlugin_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget RemoveMissingPlugins_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget ClearPluginDatabase_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget DeletePluginStates_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget StartWithWindowsCheckBox_Changed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget CloseToTraySwitch_Toggled(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget CloseBehaviorRadioButton_Checked(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget EnableVst2CheckBox_Changed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        winrt::fire_and_forget AudioPersistenceModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        winrt::fire_and_forget AudioRecoveryRetrySecondsBox_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs);
        winrt::fire_and_forget AudioRecoveryRetryAttemptsBox_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs);
        winrt::Windows::Foundation::IAsyncAction changeAudioSelection(std::string field, std::string value);
        winrt::fire_and_forget RetryAudioDevice_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        void ChooseAudioDevice_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget ManageEnabledAudioDevices_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        void PreferredDeviceButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget IconModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        void Window_Closed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::WindowEventArgs);

    private:
        std::shared_ptr<lightHostModern::ui::OperatingPresenter> operatingPresenter;
#include "PageAccessors.h"
        winrt::LightHostModernWinUI::PageState pageState = winrt::make<winrt::LightHostModernWinUI::implementation::PageState>();
        winrt::LightHostModernWinUI::AudioPageView audioPageView{nullptr};
        winrt::LightHostModernWinUI::PluginsPageView pluginsPageView{nullptr};
        winrt::LightHostModernWinUI::SupportPageView supportPageView{nullptr};
        winrt::LightHostModernWinUI::SettingsPageView settingsPageView{nullptr};
        std::shared_ptr<lightHostModern::ui::UpdateService> updateService = std::make_shared<lightHostModern::ui::UpdateService>();
        bool ensurePage(std::wstring const& section);
        void initializePage(std::wstring const& section);
        std::map<std::wstring, double> pageScrollOffsets;
        struct FluentDropdown
        {
            Microsoft::UI::Xaml::Controls::Button button{ nullptr };
            Microsoft::UI::Xaml::Controls::Primitives::Popup popup{ nullptr };
            Microsoft::UI::Xaml::Controls::Border popupCard{ nullptr };
            Microsoft::UI::Xaml::Controls::StackPanel flyoutPanel{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock label{ nullptr };
            std::vector<std::string> values;
            std::string command;
            int selectedIndex = -1;
        };

        FluentDropdown audioBackendDropdown;
        FluentDropdown inputDropdown;
        FluentDropdown outputDropdown;
        FluentDropdown sampleRateDropdown;
        FluentDropdown bufferSizeDropdown;
        FluentDropdown themeModeDropdown;
        Microsoft::UI::Xaml::Controls::ComboBox audioBackendBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox inputBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox outputBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox sampleRateBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox bufferSizeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox themeModeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox backdropModeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox layoutModeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox sidebarOnOpenBox{ nullptr };
        bool syncingSidebarPreference = false;
        Microsoft::UI::Xaml::Controls::ComboBox iconModeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox audioPersistenceModeBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox customRecoveryBackendBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox customRecoveryInputBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ComboBox customRecoveryOutputBox{ nullptr };
        Microsoft::UI::Xaml::Controls::NumberBox audioRecoveryRetrySecondsBox{ nullptr };
        Microsoft::UI::Xaml::Controls::NumberBox audioRecoveryRetryAttemptsBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ToggleSwitch startWithWindowsCheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::ToggleSwitch closeToTraySwitch{ nullptr };
        Microsoft::UI::Xaml::Controls::ToggleSwitch enableVst2CheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::CheckBox scanVstCheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::CheckBox scanVst3CheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::RadioButton closeQuitsAppRadioButton{ nullptr };
        Microsoft::UI::Xaml::Controls::RadioButton closeToTrayRadioButton{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer refreshTimer{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer notificationTimer{ nullptr };
        Microsoft::UI::Xaml::ElementTheme selectedTheme = Microsoft::UI::Xaml::ElementTheme::Default;
        std::string currentIconMode;
        std::wstring hostPipeName;
        std::wstring appNotifiedRelease;
        bool lastNotificationImportant = true;
        bool syncingThemeControls = false;
        bool syncingHostControls = false;
        bool syncingConfigControls = false;
        bool closeQuitsHost = false;
        bool vst2RestartRequired = false;
        bool comboDropDownOpen = false;
        bool commandInProgress = false;
        bool enabledDevicesOpening = false;
        lightHostModern::ui::IntentAdmission commandIntents;
        uint64_t normalCloseDeadline=0;
        std::string audioLastGeneration;
        std::set<std::string> audioOwnedGenerations;
        winrt::Windows::Foundation::IAsyncAction changeAudioChannels(bool input,int first,int last,bool enabled);
        bool telemetryInProgress = false;
        bool snapshotInProgress = false;
        bool windowClosing = false;
        std::shared_ptr<lightHostModern::ui::HostConnection> hostConnection = std::make_shared<lightHostModern::ui::HostConnection>();
        std::shared_ptr<lightHostModern::ipc::ClientState> hostTransport = hostConnection->commands;
        bool heartbeatInProgress = false;
        uint64_t lastHeartbeatTick = 0, lastDiagnosticTick = 0;
        uint64_t lastSnapshotAttemptTick = 0;
        lightHostModern::ui::PluginPageController runningPage{true}, installedPage{false};
        winrt::fire_and_forget receiveHostEvents();
        winrt::fire_and_forget heartbeat();
        bool isMinimized() const;
        bool pluginDragInProgress = false;
        bool scanStatusInProgress = false, pluginScanActive = false, scanQueuePending = false;
        uint64_t lastScanStatusTick = 0;
        Windows::Foundation::IAsyncAction refreshPluginScanStatus();
        bool globalMuted = false, globalBypassed = false, globalControlPending = false;
        void updateGlobalAudioControls();
        void updateAudioAvailability();
        winrt::fire_and_forget AudioMode_Changed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);
        bool inputChannelPairs = false, outputChannelPairs = true;
        std::wstring channelPreferenceKey;
        winrt::fire_and_forget changeMono(bool input);
        winrt::fire_and_forget ChannelGrouping_Changed(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs);

        bool asioDeviceMode = false;
        bool sidebarCollapsed = false;
        bool syncingLanguageControls = false;
        bool languageChangeQueued = false;
        bool updateInstallInProgress = false;
        bool updateCheckStarted = false, updateCheckInProgress = false;
        Microsoft::UI::Xaml::DispatcherTimer updateCheckTimer{nullptr};
        void notifyAvailableRelease();
        bool hideSupportTab = false;
        bool diagnosticsEnabled = true, syncingDiagnosticsControls = false, diagnosticsChangePending = false;
        void syncDiagnosticsSetting(bool enabled);
        winrt::fire_and_forget DiagnosticsEnabled_Toggled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        bool compactLayout = false;
        bool compactPluginCards = false;
        bool preferredDeviceDialogOpen = false;
        int runningPluginSortMode = 0;
        int installedPluginSortMode = 1;
        std::wstring runningPluginSearch;
        std::wstring installedPluginSearch;
        std::wstring pendingLanguageCode;
        std::wstring currentSection = L"Dashboard";
        ::LightHostModernWinUI::LocalizationCatalog localization;
        std::string draggedPluginSourceId, draggedPluginTargetId;
        int draggedPluginSourceIndex = -1;
        int draggedPluginTargetIndex = -1;
        int draggedPluginDropIndex = -1;
        int activePluginCount = 0;
        int installedPluginCount = 0;
        int64_t lastChainVersion = -1;
        int64_t lastPluginDbVersion = -1;
        int64_t lastAudioConfigVersion = -1;
        bool hasFullSnapshot = false;
        bool sessionWritable = false;
        std::string lastCommandResponse;
        std::vector<std::string> renderedRunningPluginLabels;
        std::vector<std::string> renderedInstalledPluginLabels;
        std::vector<std::string> activePluginIdentityKeys;
        std::vector<std::string> knownPluginIdentityKeys;
        std::vector<std::wstring> knownPluginDisplayNames;
        std::vector<std::string> renderedInputChannelKeys;
        std::vector<std::string> renderedOutputChannelKeys;
        std::vector<ChannelRowData> currentInputChannelRows;
        std::vector<ChannelRowData> currentOutputChannelRows;
        std::string currentAudioBackendName;
        std::string currentAudioInputDeviceName;
        std::string currentAudioOutputDeviceName;
        std::vector<std::string> allAudioBackendNames;
        std::vector<bool> allAudioBackendEnabled;
        std::vector<std::string> allAudioDeviceChoices;
        std::vector<bool> allAudioDeviceChoiceEnabled;
        std::vector<std::string> pluginScanPaths;
        std::set<std::string> optionalPluginScanPaths;
        std::vector<Microsoft::UI::Xaml::Controls::Border> runningPluginItemBorders;
        std::vector<Microsoft::UI::Xaml::Controls::Border> installedPluginItemBorders;

        Microsoft::UI::Xaml::Controls::ComboBox AudioBackendBox() const { return audioBackendBox; }
        Microsoft::UI::Xaml::Controls::ComboBox InputBox() const { return inputBox; }
        Microsoft::UI::Xaml::Controls::ComboBox OutputBox() const { return outputBox; }
        Microsoft::UI::Xaml::Controls::ComboBox SampleRateBox() const { return sampleRateBox; }
        Microsoft::UI::Xaml::Controls::ComboBox BufferSizeBox() const { return bufferSizeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox ThemeModeBox() const { return themeModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox BackdropModeBox() const { return backdropModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox LayoutModeBox() const { return layoutModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox IconModeBox() const { return iconModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox AudioPersistenceModeBox() const { return audioPersistenceModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox CustomRecoveryBackendBox() const { return customRecoveryBackendBox; }
        Microsoft::UI::Xaml::Controls::ComboBox CustomRecoveryInputBox() const { return customRecoveryInputBox; }
        Microsoft::UI::Xaml::Controls::ComboBox CustomRecoveryOutputBox() const { return customRecoveryOutputBox; }
        Microsoft::UI::Xaml::Controls::NumberBox AudioRecoveryRetrySecondsBox() const { return audioRecoveryRetrySecondsBox; }
        Microsoft::UI::Xaml::Controls::NumberBox AudioRecoveryRetryAttemptsBox() const { return audioRecoveryRetryAttemptsBox; }
        Microsoft::UI::Xaml::Controls::ToggleSwitch StartWithWindowsCheckBox() const { return startWithWindowsCheckBox; }
        Microsoft::UI::Xaml::Controls::ToggleSwitch CloseToTraySwitch() const { return closeToTraySwitch; }
        Microsoft::UI::Xaml::Controls::ToggleSwitch EnableVst2CheckBox() const { return enableVst2CheckBox; }
        Microsoft::UI::Xaml::Controls::CheckBox ScanVstCheckBox() const { return scanVstCheckBox; }
        Microsoft::UI::Xaml::Controls::CheckBox ScanVst3CheckBox() const { return scanVst3CheckBox; }
        Microsoft::UI::Xaml::Controls::RadioButton CloseQuitsAppRadioButton() const { return closeQuitsAppRadioButton; }
        Microsoft::UI::Xaml::Controls::RadioButton CloseToTrayRadioButton() const { return closeToTrayRadioButton; }

        void createDynamicControls(std::wstring const& section);
        void createFluentDropdown(FluentDropdown& dropdown,
            Microsoft::UI::Xaml::Controls::StackPanel const& host,
            std::string const& command);
        void setFluentDropdownItems(FluentDropdown& dropdown,
            std::vector<std::string> const& values,
            int selectedIndex);
        void syncFluentDropdownLabel(FluentDropdown& dropdown);
        void closeFluentDropdowns();
        void openFluentDropdown(FluentDropdown& dropdown);
        lightHostModern::ui::MeterPresenter inputMeter, outputMeter;
        lightHostModern::ui::DiagnosticsPresenter diagnosticsPresenter;
        lightHostModern::CpuUsageSampler dashboardCpuSampler;
        lightHostModern::ui::GpuMemorySampler dashboardGpuSampler;
        bool dashboardGpuInProgress = false;
        void updateDashboardSummary(const std::string& json);
        void updateDashboardPerformance(const std::string& json);
        winrt::fire_and_forget refreshDashboardGpu(const std::string& json);
        std::shared_ptr<lightHostModern::ui::VerboseLogsPresenter> verboseLogsPresenter;
        lightHostModern::ui::SessionStatusPresenter sessionStatusPresenter;
        winrt::LightHostModernWinUI::DiagnosticsPageView diagnosticsPageView{nullptr};
        winrt::LightHostModernWinUI::DatabasePageView databasePageView{nullptr};
        std::shared_ptr<lightHostModern::ui::ScanPathsDialog> databasePaths;
        Microsoft::UI::Xaml::Controls::ContentDialog pluginScanDialog{nullptr};
        Microsoft::UI::Xaml::Controls::ContentDialog scanProgressDialog{nullptr};
        bool scanShowingProgress = false;
        int scanFailureCount = 0;
        bool scanHasResult = false;
        bool scanDialogOpen = false;
        bool scanStatusKnown = false;
        bool scanFailuresRequested = false;
        bool scanCancelRequested = false;
        void updateScanDialogActions();
        winrt::fire_and_forget ScanForPlugins_Click(winrt::Windows::Foundation::IInspectable, Microsoft::UI::Xaml::RoutedEventArgs);
        Windows::Foundation::IAsyncAction showScanFailures();
        Windows::Foundation::IAsyncAction confirmClearPluginDatabase();
        bool meterReadInProgress = false;
        winrt::fire_and_forget refreshMeterLevels();
        bool isControlVisible(Microsoft::UI::Xaml::FrameworkElement control);
        void updateMeters(const std::string& json);
        void showNotification(std::wstring const& message, bool important = true);
        void showSection(std::wstring const& section);
        bool pendingNormalClose=false, normalCloseReady=false;
        winrt::fire_and_forget prepareNormalClose();
        winrt::Windows::Foundation::IAsyncAction finishNormalCloseAsync();
        winrt::fire_and_forget refreshTelemetry();
        winrt::Windows::Foundation::IAsyncAction refreshSnapshot(bool fromCache = false, uint64_t deadline = 0);
        winrt::Windows::Foundation::IAsyncOperation<bool> sendCommand(std::string command, bool chainPrepared = false);
        int taggedIndexOrSelected(winrt::Windows::Foundation::IInspectable const& sender, int selectedIndex) const;
        int selectedRunningPluginIndex();
        void updateRunningPluginActions();
        void updateInstalledPluginActions();
        winrt::fire_and_forget openPluginDialog(std::string action, winrt::LightHostModernWinUI::PluginItem item, Microsoft::UI::Xaml::Controls::Button button);
        bool pluginDialogOpen = false;
        bool installedGrouped = false;
        void applyTheme(Microsoft::UI::Xaml::ElementTheme theme);
        void updateThemeVisuals();
        void queueThemeRefresh();
        bool themeRefreshQueued = false;
        int appliedBackdropIndex = -1;
        winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibilitySettings;
        winrt::Windows::UI::ViewManagement::AccessibilitySettings::HighContrastChanged_revoker contrastChanged;
        void applyBackdrop(int selectedIndex);
        void applyVisualPreferences();
        std::shared_ptr<lightHostModern::ui::ScrollEdgeFade> contentFade;
        winrt::fire_and_forget notifyWindowsRelease(std::wstring version);
        bool windowsNoticePending = false;
        std::wstring windowsNotifiedRelease;
        bool updateChoiceOpen = false;
        fire_and_forget chooseUpdateMethodAsync();
        void applyLayoutMode();
        void updatePreferredDeviceSummary();
        fire_and_forget showPreferredDeviceDialogAsync();
        void applyIconMode(std::string const& mode);
        void syncIconMode(std::string const& mode);
        void applyResponsiveLayout(double width);
        void updateSidebarLayout();
        void updateToggleStateLabels();
        void syncThemeSelectors(int selectedIndex);
        void setVisible(Microsoft::UI::Xaml::UIElement const& element, bool visible);
        void syncChannelCheckBoxes(Microsoft::UI::Xaml::Controls::StackPanel const& panel,
            std::vector<ChannelRowData> const& rows,
            std::string const& commandPrefix,
            std::vector<std::string>& renderedKeys);
        void syncChannelToggleButton(Microsoft::UI::Xaml::Controls::Button const& button,
            std::vector<ChannelRowData> const& rows,
            std::string const& commandPrefix);
        void syncEnabledAudioChoicesSummary();
        void resetRunningPluginDragVisuals();
        void showPluginSubsection(std::wstring const& section);
        void configurePluginSortMenus();
        void refreshPluginViews();
        void localizeVisualTree(Microsoft::UI::Xaml::DependencyObject const& root);
        void applyLocalization();
        void refreshLanguageItems();
        void updateDownloadButtonText();
        void presentUpdateResult();
        fire_and_forget checkForUpdatesAsync();
        fire_and_forget downloadAndInstallUpdateAsync();
        void updateDebugControls();
        void resetDefaultPluginScanPaths();
    };
}

namespace winrt::LightHostModernWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
