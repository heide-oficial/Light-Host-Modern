#ifndef DebugLog_h
#define DebugLog_h

#include <JuceHeader.h>

void setLightHostDebugEnabled(bool enabled);
bool isLightHostDebugEnabled();
String getLightHostDebugLogPath();
void openLightHostDebugConsoleIfNeeded();
void lightHostLog(const String& message);
void installLightHostCrashDiagnostics();
void setLightHostCrashContext(const String& context);
void clearLightHostCrashContext();

#endif /* DebugLog_h */
