#pragma once

#include "MainWindow.g.h"
#include "Localization.h"

namespace winrt::LightHostWinUI::implementation
{
    struct ChannelRowData
    {
        std::string label;
        int startIndex = 0;
        int endIndex = 0;
        bool active = false;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void Dashboard_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Preferences_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Plugins_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Config_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Support_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void KoFi_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DownloadUpdate_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void HideSupportTabSwitch_Toggled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void LanguageBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void PluginSearchBox_TextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
        void SidebarToggle_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Refresh_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ThemeModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void FluentDropdownButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void FluentDropdownItem_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ChannelButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ComboBox_DropDownOpened(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void ComboBox_DropDownClosed(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void RootLayout_SizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void RunningPluginsTab_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void InstalledPluginsTab_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenWindowsSoundSettings_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RepositoryButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OriginalRepositoryButton_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AudioBackendBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void InputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OutputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ChannelCheckBox_Changed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void InputChannelsToggleAll_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OutputChannelsToggleAll_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SampleRateBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void BufferSizeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void RunningPluginsListView_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void RunningPluginsListView_DragItemsStarting(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const&);
        void RunningPluginsListView_DragItemsCompleted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const&);
        void RunningPluginItem_DragStarting(Microsoft::UI::Xaml::UIElement const&, Microsoft::UI::Xaml::DragStartingEventArgs const&);
        void RunningPluginItem_DragOver(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void RunningPluginItem_Drop(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void BypassPlugin_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenPluginEditor_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DuplicatePlugin_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RemovePlugin_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ScanDefaultPlugins_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ScanPaths_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddInstalledPlugin_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenInstalledPluginLocation_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RemoveInstalledPlugin_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RemoveMissingPlugins_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ClearPluginDatabase_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void DeletePluginStates_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StartWithWindowsCheckBox_Changed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CloseToTraySwitch_Toggled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CloseBehaviorRadioButton_Checked(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void EnableVst2CheckBox_Changed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AudioPersistenceModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void AudioRecoveryRetrySecondsBox_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox const&, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void AudioRecoveryRetryAttemptsBox_ValueChanged(Microsoft::UI::Xaml::Controls::NumberBox const&, Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const&);
        void CustomRecoveryBackendBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void CustomRecoveryInputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void CustomRecoveryOutputBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void RetryAudioDevice_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ChooseAudioDevice_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ManageEnabledAudioDevices_Click(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void IconModeBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void Window_Closed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::WindowEventArgs const&);

    private:
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
        Microsoft::UI::Xaml::Controls::ProgressBar inputMeterBar{ nullptr };
        Microsoft::UI::Xaml::Controls::ProgressBar outputMeterBar{ nullptr };
        Microsoft::UI::Xaml::Controls::CheckBox scanVstCheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::CheckBox scanVst3CheckBox{ nullptr };
        Microsoft::UI::Xaml::Controls::RadioButton closeQuitsAppRadioButton{ nullptr };
        Microsoft::UI::Xaml::Controls::RadioButton closeToTrayRadioButton{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer refreshTimer{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer notificationTimer{ nullptr };
        Microsoft::UI::Xaml::ElementTheme selectedTheme = Microsoft::UI::Xaml::ElementTheme::Default;
        std::string currentIconMode;
        std::wstring hostPipeName;
        bool syncingThemeControls = false;
        bool syncingHostControls = false;
        bool syncingConfigControls = false;
        bool closeQuitsHost = false;
        bool vst2RestartRequired = false;
        bool comboDropDownOpen = false;
        bool commandInProgress = false;
        bool pluginDragInProgress = false;
        bool asioDeviceMode = false;
        bool sidebarCollapsed = false;
        bool syncingLanguageControls = false;
        bool hideSupportTab = false;
        int runningPluginSortMode = 0;
        int installedPluginSortMode = 1;
        std::wstring runningPluginSearch;
        std::wstring installedPluginSearch;
        std::wstring latestReleaseUrl;
        std::wstring latestReleaseTag;
        std::wstring currentSection = L"Dashboard";
        ::LightHostWinUI::LocalizationCatalog localization;
        int draggedPluginSourceIndex = -1;
        int draggedPluginTargetIndex = -1;
        int draggedPluginDropIndex = -1;
        int activePluginCount = 0;
        int installedPluginCount = 0;
        int64_t lastChainVersion = -1;
        int64_t lastPluginDbVersion = -1;
        int64_t lastAudioConfigVersion = -1;
        bool hasFullSnapshot = false;
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
        int disabledAudioBackendCount = 0;
        int disabledAudioDeviceCount = 0;
        std::vector<std::string> allAudioBackendNames;
        std::vector<bool> allAudioBackendEnabled;
        std::vector<std::string> allAudioDeviceChoices;
        std::vector<bool> allAudioDeviceChoiceEnabled;
        std::vector<std::string> pluginScanPaths;
        std::vector<Microsoft::UI::Xaml::Controls::Border> runningPluginItemBorders;
        std::vector<Microsoft::UI::Xaml::Controls::Border> installedPluginItemBorders;
        std::vector<Microsoft::UI::Xaml::Controls::Border> inputMeterSegments;
        std::vector<Microsoft::UI::Xaml::Controls::Border> outputMeterSegments;

        Microsoft::UI::Xaml::Controls::ComboBox AudioBackendBox() const { return audioBackendBox; }
        Microsoft::UI::Xaml::Controls::ComboBox InputBox() const { return inputBox; }
        Microsoft::UI::Xaml::Controls::ComboBox OutputBox() const { return outputBox; }
        Microsoft::UI::Xaml::Controls::ComboBox SampleRateBox() const { return sampleRateBox; }
        Microsoft::UI::Xaml::Controls::ComboBox BufferSizeBox() const { return bufferSizeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox ThemeModeBox() const { return themeModeBox; }
        Microsoft::UI::Xaml::Controls::ComboBox BackdropModeBox() const { return backdropModeBox; }
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
        Microsoft::UI::Xaml::Controls::ProgressBar InputMeterBar() const { return inputMeterBar; }
        Microsoft::UI::Xaml::Controls::ProgressBar OutputMeterBar() const { return outputMeterBar; }
        Microsoft::UI::Xaml::Controls::CheckBox ScanVstCheckBox() const { return scanVstCheckBox; }
        Microsoft::UI::Xaml::Controls::CheckBox ScanVst3CheckBox() const { return scanVst3CheckBox; }
        Microsoft::UI::Xaml::Controls::RadioButton CloseQuitsAppRadioButton() const { return closeQuitsAppRadioButton; }
        Microsoft::UI::Xaml::Controls::RadioButton CloseToTrayRadioButton() const { return closeToTrayRadioButton; }

        void createDynamicControls();
        void createFluentDropdown(FluentDropdown& dropdown,
            Microsoft::UI::Xaml::Controls::StackPanel const& host,
            std::string const& command);
        void setFluentDropdownItems(FluentDropdown& dropdown,
            std::vector<std::string> const& values,
            int selectedIndex);
        void syncFluentDropdownLabel(FluentDropdown& dropdown);
        void closeFluentDropdowns();
        void openFluentDropdown(FluentDropdown& dropdown);
        void createMeterSegments(Microsoft::UI::Xaml::Controls::StackPanel const& host,
            std::vector<Microsoft::UI::Xaml::Controls::Border>& segments);
        void updateMeterSegments(std::vector<Microsoft::UI::Xaml::Controls::Border> const& segments, double level);
        void showNotification(std::wstring const& message);
        void showSection(std::wstring const& section);
        void refreshTelemetry();
        void refreshSnapshot();
        bool sendCommand(std::string const& command);
        int taggedIndexOrSelected(winrt::Windows::Foundation::IInspectable const& sender, int selectedIndex) const;
        int selectedRunningPluginIndex();
        void updateRunningPluginActions();
        void updateInstalledPluginActions();
        void applyTheme(Microsoft::UI::Xaml::ElementTheme theme);
        void applyBackdrop(int selectedIndex);
        void applyIconMode(std::string const& mode);
        void syncIconMode(std::string const& mode);
        void pulsePluginList(bool installedList);
        void pulseElement(Microsoft::UI::Xaml::UIElement const& element);
        void pulseRunningPluginCard(int index);
        void pulseInstalledPluginCard(int index);
        void applyResponsiveLayout(double width);
        void updateSidebarLayout();
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
        void localizeVisualTree(Microsoft::UI::Xaml::DependencyObject const& root);
        void applyLocalization();
        winrt::fire_and_forget checkForUpdatesAsync();
        void updateDebugControls();
        void resetDefaultPluginScanPaths();
    };
}

namespace winrt::LightHostWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
