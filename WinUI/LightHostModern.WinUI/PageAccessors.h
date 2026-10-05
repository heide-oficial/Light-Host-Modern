// Named controls belong to lazily created page views.
        Microsoft::UI::Xaml::Controls::ComboBox OutputModeBox() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputModeBox() : Microsoft::UI::Xaml::Controls::ComboBox{nullptr}; }
        Microsoft::UI::Xaml::Controls::ComboBox InputGroupingBox() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputGroupingBox() : Microsoft::UI::Xaml::Controls::ComboBox{nullptr}; }
        Microsoft::UI::Xaml::Controls::ComboBox OutputGroupingBox() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputGroupingBox() : Microsoft::UI::Xaml::Controls::ComboBox{nullptr}; }
        Microsoft::UI::Xaml::Controls::ComboBox InputModeBox() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputModeBox() : Microsoft::UI::Xaml::Controls::ComboBox{nullptr}; }
        Microsoft::UI::Xaml::Controls::InfoBar AudioUnavailableNotice() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->AudioUnavailableNotice() : Microsoft::UI::Xaml::Controls::InfoBar{nullptr}; }


        Microsoft::UI::Xaml::Controls::StackPanel PreferencesPanel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->PreferencesPanel() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border DeviceRoutingCard() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->DeviceRoutingCard() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel AudioBackendBoxHost() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->AudioBackendBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid InputDeviceRow() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputDeviceRow() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock InputDeviceLabel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputDeviceLabel() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel InputBoxHost() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid OutputDeviceRow() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputDeviceRow() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock OutputDeviceLabel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputDeviceLabel() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel OutputBoxHost() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid ChannelsCard() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->ChannelsCard() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border InputSettingsCard() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputSettingsCard() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border OutputSettingsCard() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputSettingsCard() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border InputChannelGroup() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputChannelGroup() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button InputChannelsToggleAllButton() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputChannelsToggleAllButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel InputChannelsPanel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->InputChannelsPanel() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border OutputChannelGroup() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputChannelGroup() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button OutputChannelsToggleAllButton() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputChannelsToggleAllButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel OutputChannelsPanel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->OutputChannelsPanel() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border FormatCard() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->FormatCard() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SampleRateLabel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->SampleRateLabel() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel SampleRateBoxHost() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->SampleRateBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock BufferSizeLabel() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->BufferSizeLabel() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel BufferSizeBoxHost() const
        { return audioPageView ? winrt::get_self<AudioPageView>(audioPageView)->BufferSizeBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid PluginsPanel() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->PluginsPanel() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock PluginScanStatusText() const
        { return databasePageView ? winrt::get_self<DatabasePageView>(databasePageView)->PluginScanStatusText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ProgressBar PluginScanProgress() const
        { return databasePageView ? winrt::get_self<DatabasePageView>(databasePageView)->PluginScanProgress() : Microsoft::UI::Xaml::Controls::ProgressBar{nullptr}; }



        Microsoft::UI::Xaml::Controls::TextBlock PluginScanFailuresText() const
        { return databasePageView ? winrt::get_self<DatabasePageView>(databasePageView)->PluginScanFailuresText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::SelectorBarItem RunningPluginsTabButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsTabButton() : Microsoft::UI::Xaml::Controls::SelectorBarItem{nullptr}; }

        Microsoft::UI::Xaml::Controls::SelectorBarItem InstalledPluginsTabButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsTabButton() : Microsoft::UI::Xaml::Controls::SelectorBarItem{nullptr}; }


        Microsoft::UI::Xaml::Controls::Grid RunningPluginsPanel() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsPanel() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::AppBarToggleButton RunningGlobalMuteButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningGlobalMuteButton() : Microsoft::UI::Xaml::Controls::AppBarToggleButton{nullptr}; }

        Microsoft::UI::Xaml::Controls::AppBarToggleButton RunningGlobalBypassButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningGlobalBypassButton() : Microsoft::UI::Xaml::Controls::AppBarToggleButton{nullptr}; }

        Microsoft::UI::Xaml::Controls::AutoSuggestBox RunningPluginSearchBox() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginSearchBox() : Microsoft::UI::Xaml::Controls::AutoSuggestBox{nullptr}; }

        Microsoft::UI::Xaml::Controls::AppBarButton RunningPluginSortButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginSortButton() : Microsoft::UI::Xaml::Controls::AppBarButton{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock RunningPluginsEmptyText() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsEmptyText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid RunningPluginsListCard() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsListCard() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::ListView RunningPluginsListView() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsListView() : Microsoft::UI::Xaml::Controls::ListView{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock RunningPluginsSummaryText() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->RunningPluginsSummaryText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid InstalledPluginsPanel() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsPanel() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::AutoSuggestBox InstalledPluginSearchBox() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginSearchBox() : Microsoft::UI::Xaml::Controls::AutoSuggestBox{nullptr}; }

        Microsoft::UI::Xaml::Controls::AppBarButton InstalledPluginSortButton() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginSortButton() : Microsoft::UI::Xaml::Controls::AppBarButton{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock InstalledPluginsEmptyText() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsEmptyText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid InstalledPluginsListCard() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsListCard() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::ListView InstalledPluginsListView() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsListView() : Microsoft::UI::Xaml::Controls::ListView{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button RemoveMissingPluginsButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->RemoveMissingPluginsButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button ClearPluginDatabaseButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->ClearPluginDatabaseButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock InstalledPluginsSummaryText() const
        { return pluginsPageView ? winrt::get_self<PluginsPageView>(pluginsPageView)->InstalledPluginsSummaryText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel SupportPanel() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportPanel() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportDonateTitleText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportDonateTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ColumnDefinition SupportDonateActionColumn() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportDonateActionColumn() : Microsoft::UI::Xaml::Controls::ColumnDefinition{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportDonateDescriptionText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportDonateDescriptionText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button KoFiButton() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->KoFiButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportRepositoryTitleText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportRepositoryTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ColumnDefinition SupportRepositoryActionColumn() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportRepositoryActionColumn() : Microsoft::UI::Xaml::Controls::ColumnDefinition{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportRepositoryDescriptionText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportRepositoryDescriptionText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button SupportRepositoryButton() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportRepositoryButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportShowcaseTitleText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportShowcaseTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ColumnDefinition SupportShowcaseActionColumn() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportShowcaseActionColumn() : Microsoft::UI::Xaml::Controls::ColumnDefinition{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock SupportShowcaseDescriptionText() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportShowcaseDescriptionText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button SupportShowcaseButton() const
        { return supportPageView ? winrt::get_self<SupportPageView>(supportPageView)->SupportShowcaseButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel ConfigPanel() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->ConfigPanel() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::InfoBar UpdateAvailableCard() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateAvailableCard() : Microsoft::UI::Xaml::Controls::InfoBar{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock UpdateAvailableTitleText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateAvailableTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock UpdateAvailableBodyText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateAvailableBodyText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ProgressRing UpdateProgressRing() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateProgressRing() : Microsoft::UI::Xaml::Controls::ProgressRing{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button DownloadUpdateButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->DownloadUpdateButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock StartWithWindowsStateText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->StartWithWindowsStateText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel StartWithWindowsCheckBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->StartWithWindowsCheckBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock CloseToTrayStateText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CloseToTrayStateText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel CloseToTraySwitchHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CloseToTraySwitchHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock Vst2StatusText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->Vst2StatusText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock EnableVst2StateText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->EnableVst2StateText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel EnableVst2CheckBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->EnableVst2CheckBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel AudioPersistenceModeBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioPersistenceModeBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border AudioPersistenceRetryIntervalGrid() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioPersistenceRetryIntervalGrid() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel AudioRecoveryRetrySecondsBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioRecoveryRetrySecondsBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border AudioPersistenceRetryAttemptsGrid() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioPersistenceRetryAttemptsGrid() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel AudioRecoveryRetryAttemptsBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioRecoveryRetryAttemptsBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock EnabledAudioChoicesSummaryText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->EnabledAudioChoicesSummaryText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button ManageEnabledAudioDevicesButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->ManageEnabledAudioDevicesButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Border CustomAudioPersistenceCard() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomAudioPersistenceCard() : Microsoft::UI::Xaml::Controls::Border{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid CustomAudioPersistenceGrid() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomAudioPersistenceGrid() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button PreferredDeviceButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->PreferredDeviceButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock PreferredDeviceSummaryText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->PreferredDeviceSummaryText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel CustomRecoveryBackendBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomRecoveryBackendBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel CustomRecoveryInputBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomRecoveryInputBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel CustomRecoveryOutputBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomRecoveryOutputBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock CustomRecoveryInputLabel() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomRecoveryInputLabel() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Grid CustomRecoveryOutputRow() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CustomRecoveryOutputRow() : Microsoft::UI::Xaml::Controls::Grid{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock AudioRecoveryStatusText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->AudioRecoveryStatusText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button RetryAudioDeviceButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->RetryAudioDeviceButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button ChooseAudioDeviceButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->ChooseAudioDeviceButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock LanguageTitleText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LanguageTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock LanguageDescriptionText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LanguageDescriptionText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ComboBox LanguageBox() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LanguageBox() : Microsoft::UI::Xaml::Controls::ComboBox{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock LayoutModeTitleText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LayoutModeTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock LayoutModeDescriptionText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LayoutModeDescriptionText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel LayoutModeBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->LayoutModeBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel BackdropModeBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->BackdropModeBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::StackPanel IconModeBoxHost() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->IconModeBoxHost() : Microsoft::UI::Xaml::Controls::StackPanel{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock HideSupportTitleText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->HideSupportTitleText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock HideSupportStateText() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->HideSupportStateText() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }

        Microsoft::UI::Xaml::Controls::ToggleSwitch HideSupportTabSwitch() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->HideSupportTabSwitch() : Microsoft::UI::Xaml::Controls::ToggleSwitch{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button OpenWindowsSoundSettingsButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->OpenWindowsSoundSettingsButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button RepositoryButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->RepositoryButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button OriginalRepositoryButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->OriginalRepositoryButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::Button CancelUpdateButton() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->CancelUpdateButton() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }
        Microsoft::UI::Xaml::Controls::InfoBar UpdateResultCard() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateResultCard() : Microsoft::UI::Xaml::Controls::InfoBar{nullptr}; }
        Microsoft::UI::Xaml::Controls::Button UpdateResultLog() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateResultLog() : Microsoft::UI::Xaml::Controls::Button{nullptr}; }

        Microsoft::UI::Xaml::Controls::ProgressBar UpdateTransferProgress() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateTransferProgress() : Microsoft::UI::Xaml::Controls::ProgressBar{nullptr}; }

        Microsoft::UI::Xaml::Controls::TextBlock UpdateTransferBytes() const
        { return settingsPageView ? winrt::get_self<SettingsPageView>(settingsPageView)->UpdateTransferBytes() : Microsoft::UI::Xaml::Controls::TextBlock{nullptr}; }
