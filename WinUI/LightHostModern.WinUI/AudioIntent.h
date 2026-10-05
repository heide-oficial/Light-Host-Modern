#pragma once
#include "HostJson.h"

namespace lightHostModern::ui
{
// Retain the identity of the controls the user actually saw. A successful
// change of our own generation cannot authorize old channel choices on a new
// device. The editable device also covers attempted/effective fallback state.
inline void stampAudioIntent(ipc::JsonObject const& intent,ipc::JsonObject const& snapshot)
{
    const auto selection=snapshot.GetNamedObject(L"audioSelection");
    const auto editable=selection.GetNamedObject(L"editable");
    ipc::JsonObject device;
    for(const auto* key:{L"backend",L"input",L"output"})device.SetNamedValue(key,editable.GetNamedValue(key));
    intent.SetNamedValue(L"origin",selection.GetNamedValue(L"generation"));
    intent.SetNamedValue(L"session",snapshot.GetNamedValue(L"hostSession"));
    intent.SetNamedValue(L"device",device);
    intent.SetNamedValue(L"preferenceKey",ipc::JsonValue::CreateStringValue(selection.GetNamedString(L"preferenceKey",L"")));
}

inline bool audioIntentMatchesDevice(ipc::JsonObject const& intent,ipc::JsonObject const& snapshot)
{
    if(intent.GetNamedString(L"session")!=snapshot.GetNamedString(L"hostSession"))return false;
    const auto field=intent.GetNamedString(L"field");
    if(field==L"backend")return true;
    const auto selection=snapshot.GetNamedObject(L"audioSelection");
    const auto device=intent.GetNamedObject(L"device"),current=selection.GetNamedObject(L"editable");
    if(device.GetNamedString(L"backend")!=current.GetNamedString(L"backend"))return false;
    // Device selectors list devices of one backend; rate/buffer/channel/mono
    // controls belong to the complete selected input/output pair.
    if(field==L"input"||field==L"output")return true;
    return device.GetNamedString(L"input")==current.GetNamedString(L"input")
        &&device.GetNamedString(L"output")==current.GetNamedString(L"output")
        &&intent.GetNamedString(L"preferenceKey",L"")==selection.GetNamedString(L"preferenceKey",L"");
}
}
