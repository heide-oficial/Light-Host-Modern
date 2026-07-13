#include <JuceHeader.h>
#include "IconMenu.hpp"
#include "DebugLog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows.h"
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#pragma comment(lib, "ole32.lib")

namespace
{
	HWND findWinUIWindow()
	{
		return FindWindowW(nullptr, L"Light Host Modern");
	}

	bool focusWinUIWindow()
	{
		if (auto* hwnd = findWinUIWindow())
		{
			if (IsIconic(hwnd))
				ShowWindow(hwnd, SW_SHOWMAXIMIZED);
			else
				ShowWindow(hwnd, SW_SHOWMAXIMIZED);

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
	: INDEX_OPEN_WINUI(900000),
	  INDEX_QUIT(900001),
	  engine(std::make_unique<AudioEngine>(startInSafeMode, restoreActivePluginsOnStartup)),
	  debugMode(debugEnabled)
{
	ipcServer = std::make_unique<HostIpcServer>(*engine, [this] { setIcon(); });
	lightHostLog("IconMenu created. safeMode=" + String(startInSafeMode ? "true" : "false")
		+ " restoreActivePluginsOnStartup=" + String(restoreActivePluginsOnStartup ? "true" : "false"));
	setIcon();
	setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
}

IconMenu::~IconMenu()
{
	stopTimer(menuTimerId);
	closeWinUIWindow();

	if (engine != nullptr)
		engine->flushPendingSaves();
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
	if (timerId != menuTimerId)
		return;

	stopTimer(menuTimerId);
	showNativeContextMenu();
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    Process::makeForegroundProcess();
	if (e.mods.isLeftButtonDown())
	{
		openWinUI();
		return;
	}

	showNativeContextMenu();
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
	if (im == nullptr || im->engine == nullptr)
		return;

    if (id == im->INDEX_OPEN_WINUI)
	{
        im->openWinUI();
		return;
	}
	if (id == im->INDEX_QUIT)
	{
		im->engine->savePluginStates();
		im->engine->flushPendingSaves();
		closeWinUIWindow();
		JUCEApplication::getInstance()->quit();
	}
}

void IconMenu::showNativeContextMenu()
{
	POINT iconLocation {};
	GetCursorPos(&iconLocation);

	HMENU nativeMenu = CreatePopupMenu();
	if (nativeMenu == nullptr)
		return;

	AppendMenuW(nativeMenu, MF_STRING, (UINT_PTR) INDEX_OPEN_WINUI, L"Open app UI");
	AppendMenuW(nativeMenu, MF_STRING, (UINT_PTR) INDEX_QUIT, L"Quit");

	HWND owner = GetForegroundWindow();
	if (owner == nullptr)
		owner = GetDesktopWindow();

	SetForegroundWindow(owner);
	const UINT command = TrackPopupMenu(nativeMenu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
		iconLocation.x,
		iconLocation.y,
		0,
		owner,
		nullptr);

	DestroyMenu(nativeMenu);

	if (command == (UINT) INDEX_OPEN_WINUI)
	{
		openWinUI();
		return;
	}

	if (command == (UINT) INDEX_QUIT)
	{
		if (engine != nullptr)
		{
			engine->savePluginStates();
			engine->flushPendingSaves();
		}
		closeWinUIWindow();
		JUCEApplication::getInstance()->quit();
	}
}

void IconMenu::openWinUI()
{
	lightHostLog("Open New UI clicked.");

	if (focusWinUIWindow())
	{
		lightHostLog("Focused existing WinUI window.");
		return;
	}

	String parameters = "--host-pipe=\"" + ipcServer->getPipeName() + "\"";
	if (debugMode)
	{
		parameters << " --debug";
		const auto logPath = getLightHostDebugLogPath();
		if (logPath.isNotEmpty())
			parameters << " --debug-log=\"" << logPath << "\"";
	}

	const auto executableName = "LightHostWinUI.exe";
	Array<File> searchRoots;

	searchRoots.add(File::getCurrentWorkingDirectory());
	lightHostLog("Current working directory: " + File::getCurrentWorkingDirectory().getFullPathName());

	auto current = File::getSpecialLocation(File::currentExecutableFile);
	lightHostLog("Current executable file: " + current.getFullPathName());

	for (int i = 0; i < 8 && current.exists(); ++i)
	{
		searchRoots.addIfNotAlreadyThere(current);
		current = current.getParentDirectory();
	}

	for (auto root : searchRoots)
	{
		lightHostLog("Search root: " + root.getFullPathName());

		for (int depth = 0; depth < 8 && root.exists(); ++depth)
		{
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
				candidates.add(root.getChildFile("WinUI")
					.getChildFile("x64")
					.getChildFile(configuration)
					.getChildFile("LightHost.WinUI")
					.getChildFile(executableName));
				candidates.add(root.getChildFile("LightHost.WinUI").getChildFile(executableName));
				candidates.add(root.getChildFile("WinUI").getChildFile("LightHost.WinUI").getChildFile(executableName));
				candidates.add(root.getChildFile("WinUI").getChildFile(executableName));

				for (const auto& candidate : candidates)
				{
					lightHostLog("Checking WinUI candidate: " + candidate.getFullPathName());

					if (candidate.existsAsFile())
					{
						lightHostLog("Found WinUI executable.");

						const auto workingDirectory = candidate.getParentDirectory();
						const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,
							L"open",
							candidate.getFullPathName().toWideCharPointer(),
							parameters.toWideCharPointer(),
							workingDirectory.getFullPathName().toWideCharPointer(),
							SW_SHOWNORMAL));

						lightHostLog("ShellExecute result: " + String((int) result));

						if (result <= 32)
						{
							AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
								"Light Host Modern",
								"Failed to open WinUI test build. ShellExecute result: " + String((int) result));
						}

						return;
					}
				}
			}

			root = root.getParentDirectory();
		}
	}

	lightHostLog("Loose WinUI build not found; trying packaged WinUI app.");
	if (openPackagedWinUI(parameters))
		return;

	lightHostLog("WinUI test build not found.");

	AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
		"Light Host Modern",
		"WinUI test build not found. Build WinUI\\LightHost.WinUI.sln first.");
}

bool IconMenu::openPackagedWinUI(const String& parameters)
{
	const auto aumid = resolvePackagedWinUIAumid();

	if (aumid.isEmpty())
	{
		lightHostLog("Packaged WinUI app not registered; falling back to loose executable.");
		return false;
	}

	lightHostLog("Found packaged WinUI AUMID: " + aumid);

	const auto coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool shouldUninitialise = SUCCEEDED(coInitResult);

	if (FAILED(coInitResult) && coInitResult != RPC_E_CHANGED_MODE)
	{
		lightHostLog("CoInitializeEx failed. HRESULT=0x" + String::toHexString(static_cast<int>(coInitResult)));
		return false;
	}

	IApplicationActivationManager* activationManager = nullptr;
	const auto createResult = CoCreateInstance(CLSID_ApplicationActivationManager,
		nullptr,
		CLSCTX_LOCAL_SERVER,
		IID_PPV_ARGS(&activationManager));

	if (FAILED(createResult) || activationManager == nullptr)
	{
		lightHostLog("IApplicationActivationManager creation failed. HRESULT=0x" + String::toHexString(static_cast<int>(createResult)));

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
		lightHostLog("Packaged WinUI activation failed. HRESULT=0x" + String::toHexString(static_cast<int>(activationResult)));
		return false;
	}

	lightHostLog("Packaged WinUI activated. pid=" + String(static_cast<int>(processId)));
	return true;
}

String IconMenu::resolvePackagedWinUIAumid()
{
	ChildProcess process;
	const String command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"$p = Get-AppxPackage -Name LightHost.WinUI | Select-Object -First 1; if ($null -ne $p) { [Console]::Out.Write($p.PackageFamilyName + '!App') }\"";

	if (!process.start(command, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
	{
		lightHostLog("Failed to start PowerShell to resolve packaged WinUI AUMID.");
		return {};
	}

	if (!process.waitForProcessToFinish(5000))
	{
		process.kill();
		lightHostLog("Timed out while resolving packaged WinUI AUMID.");
		return {};
	}

	const auto output = process.readAllProcessOutput().trim();
	const auto exitCode = process.getExitCode();

	if (exitCode != 0)
	{
		lightHostLog("PowerShell failed while resolving packaged WinUI AUMID. exitCode=" + String(exitCode) + " output=" + output);
		return {};
	}

	return output;
}
