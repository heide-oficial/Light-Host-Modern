#pragma once

#include <string>

bool winUIDebugEnabled();
void setWinUIDebugEnabled(bool enabled);
void setWinUIDebugLogPath(std::wstring const& path);
void initialiseWinUIDebugConsole();
void winUILog(std::string const& message);
bool commandLineHasFlag(std::wstring const& flag);
std::wstring commandLineOptionValue(std::wstring const& optionName);
