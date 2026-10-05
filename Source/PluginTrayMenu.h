#pragma once

#include "AudioEngine.h"
#include <optional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace lightHostModern
{
// The menu captures identities, never list indices or references into the engine.
// The host keeps dispatching messages while a menu is open, so the chain may change
// before a command is selected.
struct TrayAction
{
    enum class Kind { openUi, quit, mute, bypassChain, openEditor, addPlugin,
                      bypassPlugin, duplicatePlugin, moveUp, moveDown, removePlugin };
    Kind kind;
    juce::String identity;
    bool enabled = false;
};

juce::String trayText(const juce::var& locale, const char* key, const char* fallback);

// Builds a themed asynchronous menu from host state. No scanning, plugin loading,
// filesystem availability probes or audio work takes place while building it.
void showPluginTrayMenu(AudioEngine&, const juce::var& locale, juce::Component& owner, int x, int y,
                        std::function<std::optional<DWORD>()> uiProcessId,
                        std::function<void(std::optional<TrayAction>)> completion);
}
