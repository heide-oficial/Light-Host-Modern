#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "DebugLog.h"
#include "RuntimeProfile.h"
#include "ProductIdentity.h"
#include "PreferenceMigration.h"
#include "VerboseLog.h"
#include "PortablePaths.h"
#include "BoundedProperties.h"
#include "SettingsReset.h"

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
        const auto& profile = lightHostModern::RuntimeProfile::current();
        profile.createDirectories();
        const bool debugEnabled = hasParameter("--debug") || hasParameter("-debug");
        setLightHostModernDebugEnabled(debugEnabled);
        openLightHostModernDebugConsoleIfNeeded();
        installLightHostModernCrashDiagnostics();
        lightHostModern::update::retainRunningPayload(std::filesystem::path(File::getSpecialLocation(File::currentExecutableFile).getFullPathName().toWideCharPointer()));
        // Confirm executable startup before preferences, device opening or any
        // third-party plugin. A plugin fault must not roll back a healthy update.
        for (const auto& argument : getCommandLineParameterArray())
            if (argument.startsWith("--launcher-ready=Local\\LightHostModernLauncher-")) {
                const auto eventName=argument.fromFirstOccurrenceOf("=",false,false);
                if(eventName.length()<120) { const auto ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName.toWideCharPointer());
                    if(ready){SetEvent(ready);CloseHandle(ready);} }
            }
#if LIGHTHOST_REALTIME_AUDIT
        if (!installRealtimeAllocationAudit()) lightHostModernLog("Realtime allocation audit is unavailable: executable CRT imports could not be instrumented.");
#endif

        lightHostModernLog("initialise()");

        PropertiesFile::Options options;
        options.applicationName     = lightHostModern::identity::name;
        if (profile.test) options.folderName = String(profile.directory.wstring().c_str());
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";
        lightHostModern::useBoundedPreferences(options);

        checkArguments(&options);

        const auto settingsFile = options.getDefaultFile();
        const lightHostModern::settingsReset::Paths resetPaths{
            std::filesystem::path(settingsFile.getFullPathName().toWideCharPointer()), profile.uiSettings(), lightHostModern::verbose::root()};
        if (hasParameter("--reset-settings") || hasParameter("-reset-settings")) {
            try { lightHostModern::settingsReset::request(resetPaths); }
            catch (const std::exception&) { MessageBoxW(nullptr, L"Could not request a settings reset. Original files were preserved.", L"LightHostModern", MB_OK | MB_ICONERROR); quit(); return; }
        }
        bool factoryReset = false;
        try { factoryReset = lightHostModern::settingsReset::pending(resetPaths); }
        catch (const std::exception&) { MessageBoxW(nullptr, L"Could not inspect the pending settings reset. Original files were preserved.", L"LightHostModern", MB_OK | MB_ICONERROR); quit(); return; }
        if (factoryReset) {
            try { lightHostModern::settingsReset::perform(resetPaths); }
            catch (const std::exception&) {
                MessageBoxW(nullptr, L"Could not complete the settings reset. Close other instances and try again. The pending reset will be retried on restart.", L"LightHostModern", MB_OK | MB_ICONERROR); quit(); return;
            }
        }

        if (!profile.test && !factoryReset)
        {
            auto legacy = options;
            legacy.applicationName = lightHostModern::identity::legacyName;
            const auto migration = lightHostModern::migratePreferences(legacy.getDefaultFile(), options.getDefaultFile());
            if (migration.failed())
            {
                const auto message = "Could not migrate the previous preferences. Original data has been preserved.\n" + migration.getErrorMessage();
                MessageBoxW(nullptr, message.toWideCharPointer(), L"LightHostModern", MB_OK | MB_ICONWARNING);
                quit(); return;
            }
        }

        try {
            lightHostModern::verbose::startHost();
            lightHostModern::verbose::log("startup", std::string("version=")+ProjectInfo::versionString+" architecture=x64 windows="+SystemStats::getOperatingSystemName().toStdString());
        } catch(const std::exception& error) {
            lightHostModern::verbose::reportFailure(lightHostModern::verbose::root(),error.what());
        }

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters (options);
        // Preferences belong to this user/profile. In a test profile JUCE's
        // common and user paths coincide; the second, stale PropertiesFile
        // otherwise resurrects keys removed from the first (pending mode, etc.).
        appProperties->getUserSettings()->setFallbackPropertySet(nullptr);
        if (!appProperties->getUserSettings()->isValidFile()) {
            MessageBoxW(nullptr, L"The settings file is invalid, too large, or unreadable. Original data was preserved. Restore a valid backup or explicitly reset settings.", L"LightHostModern", MB_OK | MB_ICONERROR);
            quit(); return;
        }

        if (hasParameter("--clear-failed-plugins") || hasParameter("-clear-failed-plugins"))
            clearFailedPluginSettings();

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const bool safeMode = hasParameter("--safe-mode") || hasParameter("-safe-mode");
        const bool restoreActivePlugins = !safeMode
            && !hasParameter("--no-restore-active-plugins")
            && !hasParameter("-no-restore-active-plugins");
        mainWindow = std::make_unique<IconMenu>(safeMode, debugEnabled, restoreActivePlugins);
        if (hasParameter("--show-ui")) mainWindow->showInterface();
        if (profile.test)
        {
            auto info = new DynamicObject();
            info->setProperty("profile", String(profile.name.c_str()));
            info->setProperty("pipe", String(profile.pipeName().c_str()));
            info->setProperty("pid", (int) GetCurrentProcessId());
            info->setProperty("audioInitiallySuspended", profile.noAudio);
            File((profile.directory / L"profile.json").wstring().c_str()).replaceWithText(JSON::toString(var(info)));
        }
    }

    void shutdown() override
    {
        lightHostModernLog("shutdown()");
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        lightHostModernLog("Debug log saved to: " + getLightHostModernDebugLogPath());
        lightHostModern::verbose::log("lifecycle","host_shutdown");
        lightHostModern::verbose::logger().shutdown();
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override       {
        const auto& profile = lightHostModern::RuntimeProfile::current();
        return profile.test ? "LightHostModern-profile-" + String(profile.key.c_str()) : "LightHostModern";
    }
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

            auto storage = std::make_shared<lightHostModern::DiskSessionStorage>(settings->getFile());
            const auto recovered = lightHostModern::SessionStore::recover(*storage);
            if (recovered.document)
            {
                auto document = *recovered.document;
                for (auto& record : document.instances.records)
                {
                    record.error.clear();
                    record.loading = "unloaded";
                    // State and recovery bytes are preserved for the next load.
                }
                lightHostModern::SessionStore writer(std::move(storage), recovered);
                writer.submit(std::move(document.instances), document.intentionalEmpty, document.migrationId);
                if (!writer.flush()) lightHostModernLog("Could not save the cleared plugin failure markers: " + writer.status().error);
            }
            else if (recovered.found) lightHostModernLog("Failed plugin markers were retained because session recovery is incomplete");
        }
    }
};

static PluginHostApp& getApp()                      { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager()      { return getApp().commandManager; }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }

START_JUCE_APPLICATION (PluginHostApp)
