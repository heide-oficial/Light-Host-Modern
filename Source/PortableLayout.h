#pragma once
#include "BoundedInput.h"
#include "UpdateSignature.h"
#include "PortablePaths.h"
#include <set>

namespace lightHostModern::update
{
inline constexpr int portableLayoutVersion=1;
inline void noReparsePath(const std::filesystem::path& path)
{
    // Walk ordinary drive/UNC roots, not the extended namespace prefix itself.
    auto plain=path.wstring();
    if(plain.rfind(LR"(\\?\UNC\)",0)==0)plain=LR"(\\)"+plain.substr(8);
    else if(plain.rfind(LR"(\\?\)",0)==0)plain=plain.substr(4);
    for(auto p=std::filesystem::absolute(plain).lexically_normal();!p.empty();) {
        const auto flags=GetFileAttributesW(extendedFilePath(p).c_str());
        if(flags==INVALID_FILE_ATTRIBUTES) {
            const auto error=GetLastError();
            require(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND,"unsafe_update_path");
        } else require(!(flags&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe_update_path");
        const auto parent=p.parent_path();if(parent==p) break;p=parent;
    }
}
inline std::filesystem::path payloadRelative(const juce::String& value)
{
    const auto text=value.replaceCharacter('\\','/');
    require(text.isNotEmpty()&&!text.startsWithChar('/')&&!text.containsChar(':'),"unsafe_update_path");
    for(const auto& part:juce::StringArray::fromTokens(text,"/","")) {
        require(part.isNotEmpty()&&part!="."&&part!=".."&&!part.endsWithChar('.')&&!part.endsWithChar(' ')
            &&!part.containsAnyOf("<>\"|?*"),"unsafe_update_path");
        for(auto c:part) require(c>=32,"unsafe_update_path");
        const auto stem=part.upToFirstOccurrenceOf(".",false,false).toUpperCase();
        require(stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL"&&stem!="CONIN$"&&stem!="CONOUT$"
            &&!(stem.length()==4&&(stem.startsWith("COM")||stem.startsWith("LPT"))&&stem[3]>='1'&&stem[3]<='9'),"unsafe_update_path");
    }
    return std::filesystem::path(text.toWideCharPointer()).make_preferred();
}
inline juce::var readObject(const std::filesystem::path& file)
{
    const auto bytes=readSmallFile(file);const auto value=parseBoundedJson(juce::String::fromUTF8(bytes.data(),(int)bytes.size()));
    require(value.isObject(),"metadata_invalid");return value;
}
inline void durableJson(const std::filesystem::path& target,const juce::var& value)
{
    noReparsePath(target);const auto pending=target.parent_path()/(target.filename().wstring()+L"."+juce::Uuid().toString().toWideCharPointer()+L".pending");
    const auto body=juce::JSON::toString(value,true).toStdString();
    try {
        { FileOutput output(pending);output.write(body.data(),body.size());output.flush(); }
        windowsCheck(MoveFileExW(extendedFilePath(pending).c_str(),extendedFilePath(target).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"storage_failed");
    } catch(...) { DeleteFileW(extendedFilePath(pending).c_str());throw; }
}
struct PayloadRef
{
    juce::String id,inventoryHash;
    bool empty() const { return id.isEmpty(); }
    juce::var json() const { juce::DynamicObject::Ptr o=new juce::DynamicObject;o->setProperty("id",id);o->setProperty("inventoryHash",inventoryHash);return juce::var(o.get()); }
    static PayloadRef parse(const juce::var& value) {
        if(value.isVoid()||value.isUndefined()) return {};
        require(value.isObject(),"metadata_invalid");PayloadRef ref{value["id"].toString(),value["inventoryHash"].toString()};
        if(ref.empty()) {require(ref.inventoryHash.isEmpty(),"metadata_invalid");return ref;}
        require(ref.id.length()<=100&&ref.id.containsOnly("0123456789abcdef.-")&&ref.id!="."&&ref.id!="..","unsafe_update_path");
        normalizedDigest(ref.inventoryHash.toWideCharPointer());return ref;
    }
};
struct UpdateTrust { const TrustedUpdateKey* keys=trustedUpdateKeys.data();size_t count=trustedUpdateKeys.size(); };
inline juce::var verifySignedManifest(const std::string& body,const std::string& signature,UpdateTrust trust={})
{
    const auto value=parseBoundedJson(juce::String::fromUTF8(body.data(),(int)body.size()));
    require(value.isObject(),"metadata_invalid");
    verifyManifestSignature(body,juce::String(signature).trim().toStdString(),value["keyId"].toString().toStdString(),trust.keys,trust.count);
    require((int)value["formatVersion"]==1&&(int)value["minimumLauncher"]==portableLayoutVersion&&value["architecture"].toString()=="x64"
        &&parseVersion(value["version"].toString().toWideCharPointer()).has_value(),"metadata_invalid");
    return value;
}
inline juce::var verifySignedArtifact(const std::filesystem::path& operation,const Artifact& artifact,UpdateTrust trust={})
{
    const auto manifest=verifySignedManifest(readSmallFile(operation/L"update-manifest.json"),readSmallFile(operation/L"update-manifest.sig",16384),trust);
    require(parseVersion(manifest["version"].toString().toWideCharPointer())==parseVersion(artifact.version),"version_mismatch");
    bool found=false;
    if(const auto* entries=manifest["artifacts"].getArray()) for(const auto& entry:*entries) if(entry["name"].toString().toWideCharPointer()==artifact.name) {
        require(!found,"metadata_invalid");found=true;
        require((juce::int64)entry["size"]==(juce::int64)artifact.bytes
            &&normalizedDigest(entry["digest"].toString().toWideCharPointer())==normalizedDigest(artifact.digest),"checksum_mismatch");
    }
    require(found,"artifact_mismatch");return manifest;
}
inline juce::var verifyPayload(const std::filesystem::path& root,const PayloadRef& ref,bool initial=false,UpdateTrust trust={})
{
    require(!ref.empty(),"package_incomplete");const std::filesystem::path path=extendedFilePath(root/L"versions"/ref.id.toWideCharPointer());noReparsePath(path);
    require(fileDigest(path/L"payload-manifest.json")==normalizedDigest(ref.inventoryHash.toWideCharPointer()),"checksum_mismatch");
    const auto manifest=readObject(path/L"payload-manifest.json");
    require((int)manifest["formatVersion"]==1&&manifest["platform"].toString()=="x64"
        &&parseVersion(manifest["version"].toString().toWideCharPointer()).has_value(),"metadata_invalid");
    if(!initial) {
        const auto signedManifest=verifySignedManifest(readSmallFile(path/L".update-manifest.json"),readSmallFile(path/L".update-manifest.sig",16384),trust);
        require(normalizedDigest(signedManifest["portableInventorySha256"].toString().toWideCharPointer())==normalizedDigest(ref.inventoryHash.toWideCharPointer())
            &&parseVersion(signedManifest["version"].toString().toWideCharPointer())==parseVersion(manifest["version"].toString().toWideCharPointer()),"checksum_mismatch");
    }
    const auto* files=manifest["files"].getArray();require(files&&files->size()>0&&files->size()<=20000,"metadata_invalid");
    std::set<std::wstring> allowed;uint64_t size=0;
    for(const auto& entry:*files) {
        const auto relative=payloadRelative(entry["path"].toString());const auto file=path/relative;noReparsePath(file);
        require(allowed.insert(entry["path"].toString().replaceCharacter('\\','/').toLowerCase().toWideCharPointer()).second,"metadata_invalid");
        const auto bytes=(juce::int64)entry["size"];require(bytes>=0&&uint64_t(bytes)<=maximumPackageBytes*4-size,"size_mismatch");size+=bytes;
        require(std::filesystem::file_size(file)==uint64_t(bytes)&&fileDigest(file)==normalizedDigest(entry["sha256"].toString().toWideCharPointer()),"checksum_mismatch");
    }
    for(const auto* required:{L"lighthostmodern.exe",L"lighthostmodernscanner.exe",L"lighthostmodernupdatehelper.exe",L"winui/x64/release/lighthostmodern.winui/lighthostmodernwinui.exe"})
        require(allowed.count(required)>0,"package_incomplete");
    for(const auto& entry:std::filesystem::recursive_directory_iterator(path)) {
        noReparsePath(entry.path());if(entry.is_directory())continue;
        auto name=juce::String(entry.path().lexically_relative(path).wstring().c_str()).replaceCharacter('\\','/').toLowerCase().toStdString();
        require(name=="payload-manifest.json"||name==".update-manifest.json"||name==".update-manifest.sig"
            ||allowed.count(juce::String(name).toWideCharPointer()),"package_invalid");
    }
    return manifest;
}
struct PortableState { uint64_t sequence=0;PayloadRef confirmed,previous,candidate;bool launching=false; };
class PortableStore
{
    std::filesystem::path root;
    UpdateTrust trust;
public:
    explicit PortableStore(std::filesystem::path value,UpdateTrust trusted={}):root(extendedFilePath(value)),trust(trusted) {noReparsePath(root);}
    Handle lock() const {
        Handle result(CreateFileW(extendedFilePath(root/L"portable.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        windowsCheck(bool(result),"operation_conflict");return result;
    }
    PayloadRef initial() const {const auto value=readObject(root/L"portable-layout.json");require((int)value["formatVersion"]==portableLayoutVersion,"layout_unsupported");return PayloadRef::parse(value["initial"]);}
    bool valid(const PayloadRef& ref) const {
        try{const auto first=initial();verifyPayload(root,ref,ref.id==first.id&&ref.inventoryHash==first.inventoryHash,trust);return true;}catch(...){return false;}
    }
    PortableState load() const {
        PortableState state;bool found=false;
        for(int i=0;i<2;++i) try {
            const auto envelope=readObject(root/(i?L"portable-state-1.json":L"portable-state-0.json"));
            const auto body=envelope["body"].toString().toStdString();Sha256 hash;hash.append(body.data(),body.size());
            require(hash.finish()==normalizedDigest(envelope["sha256"].toString().toWideCharPointer()),"checksum_mismatch");
            const auto value=parseBoundedJson(juce::String::fromUTF8(body.c_str()));const auto seq=(juce::int64)value["sequence"];
            require(value.isObject()&&seq>0,"metadata_invalid");if(found&&uint64_t(seq)<=state.sequence)continue;
            PortableState candidate{uint64_t(seq),PayloadRef::parse(value["confirmed"]),PayloadRef::parse(value["previous"]),PayloadRef::parse(value["candidate"]),(bool)value["launching"]};
            require(!candidate.confirmed.empty(),"metadata_invalid");state=candidate;found=true;
        }catch(...){}
        if(!found)state.confirmed=initial();return state;
    }
    void save(PortableState& state) const {
        require(state.sequence<INT64_MAX-1,"metadata_invalid");juce::DynamicObject::Ptr value=new juce::DynamicObject;
        value->setProperty("sequence",(juce::int64)++state.sequence);value->setProperty("confirmed",state.confirmed.json());value->setProperty("previous",state.previous.json());
        value->setProperty("candidate",state.candidate.json());value->setProperty("launching",state.launching);
        const auto body=juce::JSON::toString(juce::var(value.get()),true).toStdString();Sha256 hash;hash.append(body.data(),body.size());
        juce::DynamicObject::Ptr envelope=new juce::DynamicObject;envelope->setProperty("body",juce::String(body));envelope->setProperty("sha256",juce::String(hash.finish().c_str()));
        durableJson(root/(state.sequence%2?L"portable-state-1.json":L"portable-state-0.json"),juce::var(envelope.get()));
    }
    PayloadRef select(PortableState& state) const {
        // An unconfirmed attempt survives a killed launcher. Never retry-loop it.
        if(!state.candidate.empty()&&(state.launching||!valid(state.candidate))) {state.candidate={};state.launching=false;save(state);}
        if(!state.candidate.empty())return state.candidate;
        if(valid(state.confirmed))return state.confirmed;
        if(valid(state.previous)){state.confirmed=state.previous;state.previous={};save(state);return state.confirmed;}
        const auto first=initial();require(valid(first),"no_complete_version");state.confirmed=first;state.previous={};save(state);return first;
    }
    void activate(const PayloadRef& candidate) const {
        auto state=load();require(!state.launching&&state.candidate.empty(),"operation_conflict");require(valid(candidate),"package_invalid");
        const auto current=readObject(root/L"versions"/state.confirmed.id.toWideCharPointer()/L"payload-manifest.json");
        const auto next=readObject(root/L"versions"/candidate.id.toWideCharPointer()/L"payload-manifest.json");
        require(*parseVersion(next["version"].toString().toWideCharPointer())>*parseVersion(current["version"].toString().toWideCharPointer()),"downgrade_refused");
        state.candidate=candidate;state.launching=false;save(state);
    }
    void pruneRetired() const noexcept {
        try {
            const auto state=load();const auto first=initial();
            std::set<juce::String> retained{first.id,state.confirmed.id,state.previous.id,state.candidate.id};
            for(const auto& entry:std::filesystem::directory_iterator(root/L"versions")) {
                const auto id=juce::String(entry.path().filename().wstring().c_str());if(retained.count(id))continue;
                noReparsePath(entry.path());if(!entry.is_directory()||id.length()>100||!id.containsOnly("0123456789abcdef.-"))continue;
                if(!std::filesystem::is_regular_file(entry.path()/L".update-manifest.json"))continue;
                PayloadRef ref{id,juce::String(fileDigest(entry.path()/L"payload-manifest.json").c_str())};
                if(!valid(ref))continue;
                Handle probe(CreateFileW(extendedFilePath(entry.path()/L"payload-manifest.json").c_str(),GENERIC_READ|DELETE,FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                if(!probe)continue; // Retain the exclusive lease through deletion.
                require(entry.path().parent_path()==root/L"versions","unsafe_update_path");
                for(const auto& child:std::filesystem::recursive_directory_iterator(entry.path()))noReparsePath(child.path());
                std::error_code ignored;std::filesystem::remove_all(entry.path(),ignored);
            }
        }catch(...){}
    }
};
}
