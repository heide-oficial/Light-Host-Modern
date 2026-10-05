#ifndef IconMenu_hpp
#define IconMenu_hpp

#include "AudioEngine.h"
#include "HostIpcServer.h"
#include "UiProcessLifetime.h"
#include "PluginTrayMenu.h"
#include "BackgroundRelease.h"

class IconMenu : public SystemTrayIconComponent, private MultiTimer
{
public:
    void showInterface() { openWinUI(); }
    IconMenu(bool startInSafeMode = false, bool debugEnabled = false, bool restoreActivePluginsOnStartup = false);
    ~IconMenu() override;

    void mouseDown(const MouseEvent&) override;

private:
	enum TimerIds
	{
		menuTimerId = 1,
        releaseTimerId = 2
	};

	void timerCallback(int timerId) override;
	void showTrayContextMenu();
	void performTrayAction(const lightHostModern::TrayAction& action);
	void showTrayError(const String& message);
	void openWinUI();
	void monitorWinUI(HANDLE process);
	bool openPackagedWinUI(const String& parameters);
	String resolvePackagedWinUIAumid();
	void setIcon();
    lightHostModern::backgroundRelease::NotificationHistory releaseNotifications;
    std::unique_ptr<lightHostModern::backgroundRelease::Checker> releaseChecker;
    void checkBackgroundRelease();
    bool notifyRelease(const String& version);
    static LRESULT CALLBACK notificationCallback(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    std::unique_ptr<AudioEngine> engine;
	std::unique_ptr<HostIpcServer> ipcServer;
	ScopedMessageBox trayDialog;
	bool menuOpen = false;
	bool debugMode = false;
	std::unique_ptr<lightHostModern::UiProcessLifetime> uiLifetime;
	int x = 0, y = 0;

};

#endif /* IconMenu_hpp */
