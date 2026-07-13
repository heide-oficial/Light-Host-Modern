#include "pch.h"
#include "WinUIDebug.h"

namespace
{
	bool debugEnabled = false;
	std::wstring debugFilePath;

	std::wstring debugLogPath()
	{
		if (!debugFilePath.empty())
			return debugFilePath;

		wchar_t tempPath[MAX_PATH] = {};
		if (GetTempPathW(MAX_PATH, tempPath) == 0)
			return L"LightHostWinUI-debug.log";

		return std::wstring(tempPath) + L"LightHostWinUI-debug.log";
	}

	void writeDebugLogFile(std::string const& line)
	{
		std::ofstream stream(debugLogPath(), std::ios::app);
		if (stream)
			stream << line << std::endl;
	}
}

bool winUIDebugEnabled()
{
	return debugEnabled;
}

void setWinUIDebugEnabled(bool enabled)
{
	debugEnabled = enabled;
}

void setWinUIDebugLogPath(std::wstring const& path)
{
	debugFilePath = path;
}

void initialiseWinUIDebugConsole()
{
	if (!debugEnabled || GetConsoleWindow() != nullptr)
		return;

	const auto attachedToParent = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
	if (!attachedToParent && !AllocConsole())
		return;

	if (!attachedToParent)
		SetConsoleTitleW(L"Light Host Modern Debug Console");

	FILE* stream = nullptr;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	freopen_s(&stream, "CONIN$", "r", stdin);

	std::ios::sync_with_stdio(true);
	std::cout.clear();
	std::cerr.clear();
	std::clog.clear();

	winUILog(attachedToParent ? "Debug console attached to LightHost." : "Debug console opened.");
	if (!debugLogPath().empty())
		winUILog("Debug log file: " + winrt::to_string(debugLogPath()));
}

void winUILog(std::string const& message)
{
	if (!debugEnabled)
		return;

	const auto line = "[LightHostWinUI] " + message;
	writeDebugLogFile(line);
	std::cout << line << std::endl;
}

bool commandLineHasFlag(std::wstring const& flag)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr)
		return false;

	bool found = false;
	for (int i = 0; i < argc; ++i)
	{
		if (flag == argv[i])
		{
			found = true;
			break;
		}
	}

	LocalFree(argv);
	return found;
}

std::wstring commandLineOptionValue(std::wstring const& optionName)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr)
		return {};

	std::wstring value;
	const auto prefix = optionName + L"=";
	for (int i = 0; i < argc; ++i)
	{
		std::wstring arg = argv[i];
		if (arg.rfind(prefix, 0) == 0)
		{
			value = arg.substr(prefix.size());
			break;
		}

		if (arg == optionName && i + 1 < argc)
		{
			value = argv[i + 1];
			break;
		}
	}

	LocalFree(argv);
	return value;
}
