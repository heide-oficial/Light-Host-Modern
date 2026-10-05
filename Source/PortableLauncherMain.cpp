#include "PortableLayout.h"
#include <shellapi.h>

using namespace lightHostModern::update;
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
    try {
        const auto root=processPath(GetCurrentProcess()).parent_path();PortableStore store(root);auto lease=store.lock();auto state=store.load();
        auto selected=store.select(state);
        int count=0;auto* arguments=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring forwarded;
        for(int i=1;arguments&&i<count;++i)forwarded+=L" "+quoteArgument(arguments[i]);if(arguments)LocalFree(arguments);
        for(int attempt=0;attempt<2;++attempt) {
            const bool candidate=!state.candidate.empty()&&selected.id==state.candidate.id;
            const auto executable=root/L"versions"/selected.id.toWideCharPointer()/L"LightHostModern.exe";
            const auto eventName=L"Local\\LightHostModernLauncher-"+std::wstring(juce::Uuid().toString().toWideCharPointer());
            Handle ready(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));windowsCheck(bool(ready),"startup_failed");
            if(candidate){state.launching=true;store.save(state);}
            auto command=quoteArgument(executable.wstring())+forwarded+L" "+quoteArgument(L"--launcher-ready="+eventName);
            STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
            if(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&startup,&process)) {
                Handle child(process.hProcess),thread(process.hThread);HANDLE waits[]{ready.value,child.value};
                const auto result=WaitForMultipleObjects(2,waits,FALSE,15000);
                if(result==WAIT_OBJECT_0) {
                    if(candidate) {state.previous=state.confirmed;state.confirmed=selected;state.candidate={};state.launching=false;store.save(state);store.pruneRetired();}
                    return 0;
                }
                if(result==WAIT_TIMEOUT) {TerminateProcess(child.value,ERROR_TIMEOUT);WaitForSingleObject(child.value,5000);}
                else if(!candidate) {DWORD code=1;GetExitCodeProcess(child.value,&code);return (int)code;}
            }
            if(!candidate)throw Error("startup_failed");
            state.candidate={};state.launching=false;store.save(state);selected=store.select(state);
        }
        throw Error("startup_failed");
    }catch(const std::exception& error){
        const auto message=juce::String("LightHostModern could not start a complete version. Your profiles and settings were not removed.\n\n")+error.what();
        MessageBoxW(nullptr,message.toWideCharPointer(),L"LightHostModern",MB_OK|MB_ICONERROR);return 1;
    }
}
