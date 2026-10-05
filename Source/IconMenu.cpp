#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "DebugLog.h"
#include "RuntimeProfile.h"
#include "LightHostModernLocales.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows.h"
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "ole32.lib")

namespace
{
	var trayLocale()
	{
		const auto settings = File(lightHostModern::RuntimeProfile::current().uiSettings().wstring().c_str());
		wchar_t language[32] {};
		GetPrivateProfileStringW(L"Localization", L"Language", L"en-us", language, 32, settings.getFullPathName().toWideCharPointer());
		const String fileName = String(language).equalsIgnoreCase("pt-br") ? "pt-br.json" : "en-us.json";
		for (int i = 0; i < LightHostModernLocales::namedResourceListSize; ++i)
			if (String(LightHostModernLocales::originalFilenames[i]).endsWith(fileName))
			{
				int size = 0;
				const auto* data = LightHostModernLocales::getNamedResource(LightHostModernLocales::namedResourceList[i], size);
				return lightHostModern::parseBoundedJson(String::fromUTF8(data, size));
			}
		return {};
	}

	HWND findWinUIWindow()
	{
		return FindWindowW(nullptr, lightHostModern::RuntimeProfile::current().windowTitle().c_str());
	}

	bool focusWinUIWindow()
	{
		if (auto* hwnd = findWinUIWindow())
		{
			if (IsIconic(hwnd))
				ShowWindow(hwnd, SW_RESTORE);
			else
				ShowWindow(hwnd, SW_SHOW);

			SetForegroundWindow(hwnd);
			return true;
		}

		return false;
	}

	void closeWinUIWindow()
	{
		if (auto* hwnd = findWinUIWindow())
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
	}
}

IconMenu::IconMenu(bool startInSafeMode, bool debugEnabled, bool restoreActivePluginsOnStartup)
	: engine(std::make_unique<AudioEngine>(startInSafeMode, restoreActivePluginsOnStartup)),
	  debugMode(debugEnabled)
{
	ipcServer = std::make_unique<HostIpcServer>(*engine, [this] { setIcon(); }, [this](const String& version) { return notifyRelease(version); });
	lightHostModernLog("IconMenu created. safeMode=" + String(startInSafeMode ? "true" : "false")
		+ " restoreActivePluginsOnStartup=" + String(restoreActivePluginsOnStartup ? "true" : "false"));
	setIcon();
	setIconTooltip(String(lightHostModern::RuntimeProfile::current().windowTitle().c_str()));
    SetWindowSubclass(static_cast<HWND>(getWindowHandle()),notificationCallback,1,reinterpret_cast<DWORD_PTR>(this));
    const auto& profile = lightHostModern::RuntimeProfile::current();
    releaseChecker = std::make_unique<lightHostModern::backgroundRelease::Checker>(
        lightHostModern::backgroundRelease::Checker::Config {
            String(ProjectInfo::versionString).toWideCharPointer(), profile.test,
            profile.test ? profile.directory / L"Temp" / L"background-release-fixture.json" : std::filesystem::path{} });
    startTimer(releaseTimerId, 1000);
    checkBackgroundRelease();
}

IconMenu::~IconMenu()
{
    stopTimer(releaseTimerId);
    // The worker owns no UI/audio callbacks. Cancel and drain it before the
    // tray component, IPC handler or engine can be destroyed.
    releaseChecker.reset();
    RemoveWindowSubclass(static_cast<HWND>(getWindowHandle()),notificationCallback,1);
	stopTimer(menuTimerId);
	if (menuOpen) PopupMenu::dismissAllActiveMenus();
	trayDialog.close();
	uiLifetime.reset();
	closeWinUIWindow();

	if (engine != nullptr)
		engine->flushPendingSaves();
}

bool IconMenu::notifyRelease(const String& version)
{
    const auto settings=lightHostModern::RuntimeProfile::current().uiSettings().wstring();
    if(!lightHostModern::backgroundRelease::windowsNotificationsEnabled(settings))return false;
    const std::wstring normalizedVersion(version.toWideCharPointer());
    if(!lightHostModern::update::parseVersion(normalizedVersion))return false;
    if(!releaseNotifications.shouldNotify(normalizedVersion))return true;
    const auto* native=static_cast<const NOTIFYICONDATAW*>(getNativeHandle());if(!native)return false;
    const auto locale=trayLocale();
    const auto title=lightHostModern::trayText(locale,"update.windows.title","LightHostModern update available");
    const auto message=lightHostModern::trayText(locale,"update.windows.body","Version {0} is available. Open Settings in LightHostModern to review the update.").replace("{0}",version);
    NOTIFYICONDATAW notice=*native;notice.uFlags=NIF_INFO;notice.dwInfoFlags=NIIF_INFO|NIIF_NOSOUND;
    title.copyToUTF16(notice.szInfoTitle,sizeof(notice.szInfoTitle));message.copyToUTF16(notice.szInfo,sizeof(notice.szInfo));
    // Isolated profiles exercise the delivery path without posting fake releases
    // to the user's Windows notification center.
    const bool sent=lightHostModern::RuntimeProfile::current().test || Shell_NotifyIconW(NIM_MODIFY,&notice)!=FALSE;
    if(sent){releaseNotifications.didNotify(normalizedVersion);WritePrivateProfileStringW(L"Updates",L"LastWindowsNotifiedRelease",version.toWideCharPointer(),settings.c_str());lightHostModernLog("Windows release notification submitted: "+version);}
    return sent;
}

void IconMenu::checkBackgroundRelease()
{
    if (!releaseChecker) return;
    const auto enabled = lightHostModern::backgroundRelease::windowsNotificationsEnabled(
        lightHostModern::RuntimeProfile::current().uiSettings());
    if (const auto version = releaseChecker->poll(enabled))
        notifyRelease(String(version->c_str()));
}

LRESULT CALLBACK IconMenu::notificationCallback(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR data)
{
    auto* owner=reinterpret_cast<IconMenu*>(data);
    const auto* native=static_cast<const NOTIFYICONDATAW*>(owner->getNativeHandle());
    if(native && message==native->uCallbackMessage && LOWORD(lParam)==NIN_BALLOONUSERCLICK){
        Component::SafePointer<IconMenu> safe(owner);
        MessageManager::callAsync([safe]{if(safe)safe->openWinUI();});return 0;
    }
    return DefSubclassProc(hwnd,message,wParam,lParam);
}

void IconMenu::setIcon()
{
	if (!getAppProperties().getUserSettings()->containsKey("trayIconMode"))
		getAppProperties().getUserSettings()->setValue("trayIconMode", "color");

	String color = getAppProperties().getUserSettings()->getValue("trayIconMode",
		getAppProperties().getUserSettings()->getValue("icon", "color")).toLowerCase();
	Image icon;

	if (color.equalsIgnoreCase("white"))
		icon = ImageFileFormat::loadFrom(BinaryData::logowhite_png, BinaryData::logowhite_pngSize);
	else if (color.equalsIgnoreCase("black"))
		icon = ImageFileFormat::loadFrom(BinaryData::logoblack_png, BinaryData::logoblack_pngSize);
	else
		icon = ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);

	setIconImage(icon, icon);
}

void IconMenu::timerCallback(int timerId)
{
    if (timerId == releaseTimerId)
    {
        checkBackgroundRelease();
        return;
    }
	if (timerId != menuTimerId)
		return;

	stopTimer(menuTimerId);
	showTrayContextMenu();
}

void IconMenu::mouseDown(const MouseEvent& e)
{
	if (menuOpen || !(e.mods.isLeftButtonDown() || e.mods.isRightButtonDown())) return;
	POINT location {};
	GetCursorPos(&location);
	x = location.x;
	y = location.y;
	// Leave the tray notification callback before showing the popup.
	// Both mouse buttons expose Quick Access; Open app UI stays first.
	startTimer(menuTimerId, 1);
}

void IconMenu::showTrayContextMenu()
{
	if (menuOpen || engine == nullptr) return;
	const auto owner = static_cast<HWND>(getWindowHandle());
	if (!IsWindow(owner)) return;
	Component::SafePointer<IconMenu> safeThis(this);
	menuOpen = true;
	try
	{
		lightHostModern::showPluginTrayMenu(*engine, trayLocale(), *this, x, y,
			[safeThis] { return safeThis && safeThis->uiLifetime ? safeThis->uiLifetime->processId() : DWORD{0}; },
			[safeThis](std::optional<lightHostModern::TrayAction> action)
			{
				if (!safeThis) return;
				safeThis->menuOpen = false;
				if (!action) return;
				try { safeThis->performTrayAction(*action); }
				catch (...)
				{
					if (safeThis) safeThis->showTrayError(lightHostModern::trayText(trayLocale(), "tray.operationFailed", "The action could not be completed. Open the app for details."));
				}
			});
	}
	catch (const std::exception& error)
	{
		lightHostModernLog("Tray operation failed: " + String(error.what()));
		if (safeThis)
		{
			menuOpen = false;
			showTrayError(lightHostModern::trayText(trayLocale(), "tray.operationFailed", "The action could not be completed. Open the app for details."));
		}
	}
	catch (...)
	{
		lightHostModernLog("Tray operation threw an unknown exception.");
		if (safeThis)
		{
			menuOpen = false;
			showTrayError(lightHostModern::trayText(trayLocale(), "tray.operationFailed", "The action could not be completed. Open the app for details."));
		}
	}
}

void IconMenu::showTrayError(const String& message)
{
	lightHostModernLog("Tray: " + message);
	trayDialog = NativeMessageBox::showScopedAsync(MessageBoxOptions()
		.withIconType(MessageBoxIconType::WarningIcon).withTitle("LightHostModern")
		.withMessage(message).withButton(lightHostModern::trayText(trayLocale(), "common.close", "Close")), nullptr);
}

void IconMenu::performTrayAction(const lightHostModern::TrayAction& action)
{
	using Kind = lightHostModern::TrayAction::Kind;
	const auto locale = trayLocale();
	const auto text = [&locale](const char* key, const char* fallback) { return lightHostModern::trayText(locale, key, fallback); };
	lightHostModernLog("Tray action=" + String(static_cast<int>(action.kind)) + " identity=" + action.identity);
	if (action.kind == Kind::openUi) { openWinUI(); return; }
	if (action.kind == Kind::quit)
	{
        Component::SafePointer<IconMenu> safeThis(this);
        ipcServer->requestLocal("quit-host", {}, [safeThis](const var& result) {
            if (safeThis && result["status"].toString() != "ok") safeThis->showTrayError("The session could not be saved. The app remains open.");
        });
		return;
	}
	if (action.kind == Kind::mute) { engine->setGlobalMuted(action.enabled); return; }
	if (action.kind == Kind::bypassChain) { engine->setGlobalBypassed(action.enabled); return; }
	if (action.kind != Kind::openEditor && !engine->isSessionWritable())
	{
		showTrayError(text("tray.sessionReadOnly", "The session is protected. Open the app to review recovery information."));
		return;
	}
	const auto openCreatedInstance = [&](const PluginInstanceId& id)
	{
		const auto index = engine->findPluginIndexById(id);
		if (index >= 0 && engine->getPluginInstances()[static_cast<size_t>(index)].loading == "loaded")
			engine->showPluginEditor(index);
		else
		{
			auto message = text("tray.loadFailed", "The plugin could not be loaded. Open the app to review its status.");
			if (index >= 0)
			{
				const auto error = engine->getPluginInstances()[static_cast<size_t>(index)].error;
				if (error.isNotEmpty()) message += "\n\n" + error;
			}
			showTrayError(message);
		}
	};
	if (action.kind == Kind::addPlugin)
	{
		const auto index = engine->findKnownPluginIndexById(action.identity);
		if (index < 0) { showTrayError(text("tray.pluginChanged", "This plugin is no longer available. Reopen the tray menu.")); return; }
		PluginInstanceId created;
		engine->addKnownPluginByIndex(index, &created);
		openCreatedInstance(created);
		return;
	}
	const auto index = engine->findPluginIndexById(action.identity);
	if (index < 0) { showTrayError(text("tray.pluginChanged", "This plugin is no longer available. Reopen the tray menu.")); return; }
	const auto record = engine->getPluginInstances()[static_cast<size_t>(index)];
	if ((action.kind == Kind::openEditor || action.kind == Kind::duplicatePlugin || action.kind == Kind::bypassPlugin)
		&& record.loading != "loaded")
	{
		showTrayError(text("tray.pluginChanged", "This plugin is no longer available. Reopen the tray menu."));
		return;
	}
	switch (action.kind)
	{
		case Kind::openEditor: engine->showPluginEditor(index); break;
		case Kind::bypassPlugin: engine->setPluginBypassed(index, action.enabled); break;
		case Kind::duplicatePlugin:
		{
            Component::SafePointer<IconMenu> safeThis(this);
            ipcServer->requestLocal("duplicate-plugin", {record.id}, [safeThis](const var& result) {
                if (!safeThis) return;
                if (result["status"].toString() == "ok") {
                    const auto created = safeThis->engine->findPluginIndexById(result["instanceId"].toString());
                    if (created >= 0) safeThis->engine->showPluginEditor(created);
                } else safeThis->showTrayError("The plugin could not be duplicated. Open the app for details.");
            });
			break;
		}
		case Kind::moveUp: engine->movePluginUp(index); break;
		case Kind::moveDown: engine->movePluginDown(index); break;
		case Kind::removePlugin:
		{
			Component::SafePointer<IconMenu> safeThis(this);
			trayDialog = NativeMessageBox::showScopedAsync(MessageBoxOptions()
				.withIconType(MessageBoxIconType::QuestionIcon).withTitle(text("tray.removeTitle", "Remove plugin from chain?"))
				.withMessage(text("tray.removeMessage", "Remove {name} from the running chain? Its installed entry will be kept.").replace("{name}", record.displayName()))
				.withButton(text("common.remove", "Remove")).withButton(text("common.cancel", "Cancel")),
				[safeThis, id = record.id](int result)
				{
					if (result != 1 || !safeThis) return;
					// Resolve again after confirmation; another UI may have reordered it.
					try
					{
						if (!safeThis->engine->isSessionWritable())
							safeThis->showTrayError(lightHostModern::trayText(trayLocale(), "tray.sessionReadOnly", "The session is protected. Open the app to review recovery information."));
						else
						{
							const auto current = safeThis->engine->findPluginIndexById(id);
							if (current >= 0) safeThis->engine->removePlugin(current);
						}
					}
					catch (...)
					{
						if (safeThis) safeThis->showTrayError(lightHostModern::trayText(trayLocale(), "tray.operationFailed", "The action could not be completed. Open the app for details."));
					}
				});
			break;
		}
		default: break;
	}
}

void IconMenu::openWinUI()
{
	lightHostModernLog("Open New UI clicked.");
	// Do not launch a second shell while the first is starting/closing, or
	// replace the monitor while unexpected-exit shutdown is being dispatched.
	if (uiLifetime && (uiLifetime->running() || uiLifetime->endedUnexpectedly()))
	{
		focusWinUIWindow();
		return;
	}

	if (focusWinUIWindow())
	{
		lightHostModernLog("Focused existing WinUI window.");
		return;
	}

	try
	{
		uiLifetime = std::make_unique<lightHostModern::UiProcessLifetime>(
			L"Local\\LightHostModernUiClose-" + std::wstring(Uuid().toString().toWideCharPointer()));
	}
	catch (const std::exception& error)
	{
		lightHostModernLog("Cannot prepare UI lifetime monitor: " + String(error.what()));
		return;
	}
	String parameters = "--host-pipe=\"" + ipcServer->getPipeName() + "\"";
	parameters << " --ui-close-event=\"" << String(uiLifetime->eventName().c_str()) << "\"";
	parameters << String(lightHostModern::RuntimeProfile::current().arguments().c_str());
	if (debugMode)
	{
		parameters << " --debug";
		const auto logPath = getLightHostModernDebugLogPath();
		if (logPath.isNotEmpty())
			parameters << " --debug-log=\"" << logPath << "\"";
	}

	const auto executableName = "LightHostModernWinUI.exe";
	Array<File> searchRoots;
	const auto executableDirectory = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
	// Packaged UI belongs to this host. Development fallbacks must identify a
	// repository; an unrelated current directory must not select another build.
	searchRoots.add(executableDirectory);
	auto current = executableDirectory;
	for (int i = 0; i < 8; ++i)
	{
		if (current.getChildFile("WinUI/LightHostModern.WinUI.sln").existsAsFile()) searchRoots.addIfNotAlreadyThere(current);
		const auto parent = current.getParentDirectory();
		if (parent == current) break;
		current = parent;
	}

	for (auto root : searchRoots)
	{
		lightHostModernLog("Search root: " + root.getFullPathName());

		StringArray configurations;
		if (debugMode)
		{
			configurations.add("Debug");
			configurations.add("Release");
		}
		else
		{
			configurations.add("Release");
			configurations.add("Debug");
		}

		for (const auto& configuration : configurations)
		{
			Array<File> candidates;
			// Match LightHostModern.Output.props and the distribution layout first.
			candidates.add(root.getChildFile("WinUI")
				.getChildFile("x64")
				.getChildFile(configuration)
				.getChildFile("LightHostModern.WinUI")
				.getChildFile(executableName));
			candidates.add(root.getChildFile("LightHostModern.WinUI").getChildFile(executableName));
			candidates.add(root.getChildFile("WinUI").getChildFile("LightHostModern.WinUI").getChildFile(executableName));
			candidates.add(root.getChildFile("WinUI").getChildFile(executableName));

			for (const auto& candidate : candidates)
			{
				lightHostModernLog("Checking WinUI candidate: " + candidate.getFullPathName());

				if (candidate.existsAsFile())
				{
					lightHostModernLog("Found WinUI executable.");

					const auto workingDirectory = candidate.getParentDirectory();
					const auto executablePath = candidate.getFullPathName();
					const auto directoryPath = workingDirectory.getFullPathName();
					SHELLEXECUTEINFOW launch { sizeof(launch) };
					launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
					launch.lpVerb = L"open";
					launch.lpFile = executablePath.toWideCharPointer();
					launch.lpParameters = parameters.toWideCharPointer();
					launch.lpDirectory = directoryPath.toWideCharPointer();
					launch.nShow = SW_SHOWNORMAL;
					if (!ShellExecuteExW(&launch))
					{
						const auto result = GetLastError();
						const auto locale = trayLocale();
						auto message = locale["tray.uiLaunchFailed"].toString();
						if (message.isEmpty()) message = "Could not open the application interface. Error: {code}";
						AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
							"LightHostModern",
							message.replace("{code}", String((int) result)));
					}
					else monitorWinUI(launch.hProcess);

					return;
				}
			}
		}
	}

	lightHostModernLog("Loose WinUI build not found; trying packaged WinUI app.");
	if (openPackagedWinUI(parameters))
		return;

	lightHostModernLog("WinUI executable not found.");
	const auto locale = trayLocale();
	auto message = locale["tray.uiMissing"].toString();
	if (message.isEmpty()) message = "The application interface was not found. Repair the installation or extract the complete portable package.";

	AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
		"LightHostModern",
		message);
}

void IconMenu::monitorWinUI(HANDLE process)
{
	try
	{
		uiLifetime->monitor(process, [] {
			// Shutdown may encounter stalled plugin code. Bound it independently of
			// the message/audio threads; this deadline survives IconMenu destruction.
			std::thread([] {
				if (WaitForSingleObject(GetCurrentProcess(), 10000) == WAIT_TIMEOUT)
					TerminateProcess(GetCurrentProcess(), ERROR_PROCESS_ABORTED);
			}).detach();
			lightHostModernLog("UI ended without a normal close; shutting down the host.");
			MessageManager::callAsync([] { JUCEApplication::getInstance()->quit(); });
		});
	}
	catch (const std::exception& error)
	{
		lightHostModernLog("Cannot monitor UI lifetime: " + String(error.what()));
		JUCEApplication::getInstance()->quit();
	}
}

bool IconMenu::openPackagedWinUI(const String& parameters)
{
	const auto aumid = resolvePackagedWinUIAumid();

	if (aumid.isEmpty())
	{
		lightHostModernLog("Packaged WinUI app not registered; falling back to loose executable.");
		return false;
	}

	lightHostModernLog("Found packaged WinUI AUMID: " + aumid);

	const auto coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool shouldUninitialise = SUCCEEDED(coInitResult);

	if (FAILED(coInitResult) && coInitResult != RPC_E_CHANGED_MODE)
	{
		lightHostModernLog("CoInitializeEx failed. HRESULT=0x" + String::toHexString(static_cast<int>(coInitResult)));
		return false;
	}

	IApplicationActivationManager* activationManager = nullptr;
	const auto createResult = CoCreateInstance(CLSID_ApplicationActivationManager,
		nullptr,
		CLSCTX_LOCAL_SERVER,
		IID_PPV_ARGS(&activationManager));

	if (FAILED(createResult) || activationManager == nullptr)
	{
		lightHostModernLog("IApplicationActivationManager creation failed. HRESULT=0x" + String::toHexString(static_cast<int>(createResult)));

		if (shouldUninitialise)
			CoUninitialize();

		return false;
	}

	DWORD processId = 0;
	const auto activationResult = activationManager->ActivateApplication(aumid.toWideCharPointer(),
		parameters.toWideCharPointer(),
		AO_NONE,
		&processId);

	activationManager->Release();

	if (shouldUninitialise)
		CoUninitialize();

	if (FAILED(activationResult))
	{
		lightHostModernLog("Packaged WinUI activation failed. HRESULT=0x" + String::toHexString(static_cast<int>(activationResult)));
		return false;
	}

	lightHostModernLog("Packaged WinUI activated. pid=" + String(static_cast<int>(processId)));
	monitorWinUI(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
	return true;
}

String IconMenu::resolvePackagedWinUIAumid()
{
	ChildProcess process;
	const String command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$p = Get-AppxPackage -Name LightHost.WinUI | Select-Object -First 1; if ($null -ne $p) { [Console]::Out.Write($p.PackageFamilyName + '!App') }\"";

	if (!process.start(command, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
	{
		lightHostModernLog("Failed to start PowerShell to resolve packaged WinUI AUMID.");
		return {};
	}

	if (!process.waitForProcessToFinish(5000))
	{
		process.kill();
		lightHostModernLog("Timed out while resolving packaged WinUI AUMID.");
		return {};
	}

	const auto output = process.readAllProcessOutput().trim();
	const auto exitCode = process.getExitCode();

	if (exitCode != 0)
	{
		lightHostModernLog("PowerShell failed while resolving packaged WinUI AUMID. exitCode=" + String(exitCode) + " output=" + output);
		return {};
	}

	return output;
}
