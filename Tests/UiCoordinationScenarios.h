#pragma once
#include "UiCoordination.h"
#include "AudioIntent.h"

inline void runUiCoordinationScenarios()
{
    using namespace lightHostModern::ui;
    int root=0,otherRoot=0;
    {DialogLease first(&root),duplicate(&root),other(&otherRoot);
        require(static_cast<bool>(first)&&!duplicate&&static_cast<bool>(other),"Only one modal can reserve a given window");}
    {DialogLease renamed(&root);require(static_cast<bool>(renamed),"Closing a parent modal must release its slot for Rename");}
    try{DialogLease failed(&root);throw 1;}catch(int){}
    {DialogLease retry(&root);require(static_cast<bool>(retry),"A failed dialog must release its reservation");}
    EditRevision edit;
    edit.changed();const auto captured=edit.local;edit.changed();
    require(!edit.acknowledge(captured), "An acknowledgement of A must retain the newer edit B");
    require(edit.acknowledge(edit.local), "The final acknowledgement covers all local edits");

    IntentAdmission queue;
    auto running=queue.reserve("buffer",16);running->started=true;
    auto old=queue.reserve("buffer",16);
    auto structure=queue.reserve("",16);
    auto last=queue.reserve("buffer",16);
    require(!old->superseded&&!running->superseded, "A structural barrier must protect earlier desired values from coalescing");
    auto replacement=queue.reserve("buffer",16);
    require(last->superseded&&!old->superseded,"Only replacements within the same unsent segment can coalesce");
    queue.release(running);
    require(queue.ready(old),"The earlier absolute value must execute before the structural action");queue.release(old);
    require(queue.ready(structure), "Coalescing must preserve the position of structural actions");
    queue.release(structure);require(queue.ready(replacement), "The latest desired control value must be admitted");
    queue.release(replacement);
    std::vector<IntentAdmission::Ticket> full;
    for(int i=0;i<64;++i)full.push_back(queue.reserve("",16));
    require(!queue.reserve("",16), "UI intention admission must be bounded");
    for(const auto& ticket:full)queue.release(ticket);
    require(static_cast<bool>(queue.reserve("",16)), "Cancelled admission must release capacity");

    GainDeltas gains;
    gains.change(L"one",0,.5);const auto first=gains.batch();
    gains.change(L"one",0,.75);gains.acknowledge(first);
    require(gains.batch().size()==1&&gains.batch().front().gain==.75, "A gain changed while sending must survive the old acknowledgement");
    gains.clear();gains.change(L"mixer8",127,.25);
    require(gains.batch().size()==1, "One changed lane in a large graph must produce one preview value");
    gains.clear();
    for(int mixer=0;mixer<9;++mixer)for(uint32_t lane=0;lane<128;++lane)gains.change(L"mixer"+std::to_wstring(mixer),lane,.5);
    const auto batch=gains.batch();require(batch.size()==1024,"Mixer previews must respect the host batch contract");
    gains.acknowledge(batch);require(gains.batch().size()==128,"Overflow lanes must remain pending for the next bounded batch");
    gains.clear();require(gains.empty(),"A profile transition must invalidate all old gain deltas");

    const auto before=lightHostModern::ipc::parseObject(R"({"hostSession":"host","audioSelection":{"generation":"1","preferenceKey":"pair-a","editable":{"backend":"WASAPI","input":"in","output":"out-a"}}})");
    auto changed=lightHostModern::ipc::parseObject(R"({"hostSession":"host","audioSelection":{"generation":"2","preferenceKey":"pair-b","editable":{"backend":"WASAPI","input":"in","output":"out-b"}}})");
    for(const auto* field:{L"inputChannels",L"outputChannels",L"sampleRate",L"bufferSize",L"monoInputs",L"monoOutput"}){
        lightHostModern::ipc::JsonObject intent;intent.SetNamedValue(L"field",lightHostModern::ipc::JsonValue::CreateStringValue(field));stampAudioIntent(intent,before);
        require(audioIntentMatchesDevice(intent,before),"A desired audio value must remain valid on its original device");
        require(!audioIntentMatchesDevice(intent,changed),"A queued audio control cannot migrate to a newly selected device");
    }
    lightHostModern::ipc::JsonObject deviceIntent;deviceIntent.SetNamedValue(L"field",lightHostModern::ipc::JsonValue::CreateStringValue(L"input"));stampAudioIntent(deviceIntent,before);
    require(audioIntentMatchesDevice(deviceIntent,changed),"Sequential input/output selections within one backend remain possible");
    changed.GetNamedObject(L"audioSelection").GetNamedObject(L"editable").SetNamedValue(L"backend",lightHostModern::ipc::JsonValue::CreateStringValue(L"ASIO"));
    require(!audioIntentMatchesDevice(deviceIntent,changed),"An old device picker choice must not migrate across backends");
}

inline void runQueuedDeadlineScenario()
{
    using namespace lightHostModern::ipc;
    auto state=std::make_shared<ClientState>();state->hostSession="deadline-host";
    std::promise<void> entered, release;
    auto ready=release.get_future();int sent=0;
    state->transport=[&](const auto&,const std::string& wire,DWORD){
        const auto command=extractString(wire,"command");
        require(command=="snapshot", "An expired queued mutation must never reach the host");
        if(++sent==1){entered.set_value();ready.wait();}
        auto response=parseObject(wire);response.SetNamedValue(L"status",JsonValue::CreateStringValue(L"ok"));
        response.SetNamedValue(L"hostSession",JsonValue::CreateStringValue(L"deadline-host"));return winrt::to_string(response.Stringify());
    };
    auto first=requestAsync(state,L"fake","snapshot",5000);
    require(entered.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready,"The first request must occupy the FIFO");
    const auto began=GetTickCount64();
    auto expired=winrt::to_string(requestAsync(state,L"fake","remove-plugin:instance",60).get());
    const bool timedOut=extractString(expired,"code")=="queue_timeout"&&GetTickCount64()-began<1000;
    auto next=requestAsync(state,L"fake","snapshot",2000);
    release.set_value();first.get();auto result=winrt::to_string(next.get());
    require(timedOut,"The request deadline must include time waiting in the FIFO");
    require(extractString(result,"status")=="ok"&&sent==2,"An expired ticket must not leave a hole blocking the FIFO");
    require(state->pending.empty()&&state->queuedRequests==0&&state->queuedBytes==0,"An unsent timeout must neither retain a mutation nor leak queue capacity");
}
