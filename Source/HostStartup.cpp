#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "DebugLog.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <Windows.h>
#endif

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST and/or AU support"
#endif

class PluginHostApp  : public JUCEApplication
{

public:
    PluginHostApp() {}

    void initialise (const String&) override
    {
        const bool debugEnabled = hasParameter("--debug") || hasParameter("-debug");
        setLightHostDebugEnabled(debugEnabled);
        openLightHostDebugConsoleIfNeeded();
        installLightHostCrashDiagnostics();

        lightHostLog("initialise()");

        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(&options);

        if (hasParameter("--reset-settings") || hasParameter("-reset-settings"))
            resetSettings(options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters (options);

        if (hasParameter("--clear-failed-plugins") || hasParameter("-clear-failed-plugins"))
            clearFailedPluginSettings();

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const bool safeMode = hasParameter("--safe-mode") || hasParameter("-safe-mode");
        const bool restoreActivePlugins = !safeMode
            && !hasParameter("--no-restore-active-plugins")
            && !hasParameter("-no-restore-active-plugins");
        mainWindow = std::make_unique<IconMenu>(safeMode, debugEnabled, restoreActivePlugins);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override       { return "Light Host Modern"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V3 lookAndFeel;

private:
    std::unique_ptr<IconMenu> mainWindow;

    StringArray getParameter(String lookFor) {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf(0, "=") + 1;
                String val = param.substring(delimiter);
                found.add(val);
                return found;
            }
        }
        return found;
    }

    bool hasParameter(String lookFor) {
        StringArray parameters = getCommandLineParameterArray();
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.equalsIgnoreCase(lookFor) || param.startsWithIgnoreCase(lookFor + "="))
                return true;
        }
        return false;
    }

    void checkArguments(PropertiesFile::Options *options) {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }

    void resetSettings(const PropertiesFile::Options& options) {
        ApplicationProperties resetProperties;
        resetProperties.setStorageParameters(options);

        if (PropertiesFile* settings = resetProperties.getUserSettings())
        {
            File settingsFile(settings->getFile());
            File crashedPluginsFile(settingsFile.getSiblingFile("RecentlyCrashedPluginsList"));
            resetProperties.closeFiles();

            crashedPluginsFile.deleteFile();
            settingsFile.deleteFile();
        }
    }

    void clearFailedPluginSettings() {
        if (PropertiesFile* settings = appProperties->getUserSettings())
        {
            StringArray keys = settings->getAllProperties().getAllKeys();
            for (auto& key : keys)
            {
                if (key.startsWithIgnoreCase("plugin-failed-"))
                    settings->removeValue(key);
            }

            settings->saveIfNeeded();
        }
    }
};

static PluginHostApp& getApp()                      { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager()      { return getApp().commandManager; }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }

START_JUCE_APPLICATION (PluginHostApp)
