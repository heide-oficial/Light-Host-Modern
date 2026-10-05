#pragma once
#include "HostJson.h"

namespace lightHostModern::ui
{
// Deliberately excludes viewport edits, wires, per-channel switches and sliders.
// Call only after an acknowledged successful operation, never from snapshots.
inline const wchar_t* actionFeedback(std::string const& command, std::string const& previous)
{
    const auto colon = command.find(':');
    const auto name = command.substr(0,colon);
    const auto arg = colon == std::string::npos ? std::string{} : command.substr(colon+1);
    if(name=="set-global-mute")return arg=="1"?L"Output muted.":L"Output unmuted.";
    if(name=="set-global-bypass")return arg=="1"?L"Chain bypass enabled.":L"Chain bypass disabled.";
    if(name=="toggle-bypass")return L"Plugin bypass updated.";
    if(name=="add-known-plugin")return L"Plugin added to chain.";
    if(name=="remove-plugin")return L"Plugin removed from chain.";
    if(name=="duplicate-plugin")return L"Plugin duplicated.";
    if(name=="set-enable-vst2")return arg=="1"?L"VST2 support enabled.":L"VST2 support disabled.";
    if(name=="set-diagnostics-enabled")return arg=="1"?L"Diagnostics enabled.":L"Diagnostics disabled.";
    if(name=="set-mono-inputs"||name=="set-mono-output")return ipc::extractBool(arg,"enabled")?L"Mono enabled.":L"Mono disabled.";
    if(name=="select-audio-device"){
        auto before=ipc::parseSnapshotObject(previous).GetNamedObject(L"audioSelection").GetNamedObject(L"editable");auto after=ipc::parseObject(arg);
        for(auto key:{L"backend",L"input",L"output"})if(before.GetNamedString(key,L"")!=after.GetNamedString(key,L""))return L"Audio device updated.";
        for(auto key:{L"sampleRate",L"bufferSize"})if(before.GetNamedNumber(key,0)!=after.GetNamedNumber(key,0))return L"Audio format updated.";
    }
    if(name=="operating-command"){
        auto request=ipc::parseObject(arg);auto action=request.GetNamedString(L"action",L"");
        if(action==L"add")return request.GetNamedString(L"kind",L"")==L"mixer"?L"Mixer added to chain.":L"Plugin added to chain.";
        if(action==L"remove")return L"Element removed from chain.";
        if(action==L"batch"){
            auto operation=request.GetNamedString(L"operation",L"");
            if(operation==L"remove")return L"Selected elements removed.";
            if(operation==L"bypass")return L"Selected plugins bypassed.";
            if(operation==L"enable")return L"Selected plugins enabled.";
        }
        if(action==L"create"||action==L"overwrite"||action==L"edit")return L"Profile saved.";
        if(action==L"activate")return L"Profile activated.";
        if(action==L"delete")return L"Profile deleted.";
    }
    return nullptr;
}
}
