#pragma once
#include "UpdatePackage.h"
#include "PortableLayout.h"
#include <functional>

namespace lightHostModern::update
{
// Preparation publishes only a complete, authenticated, inactive directory.
// Activation replaces one small descriptor. Never replace the running launcher.
class PortableUpdate
{
    std::filesystem::path root,work;
    PayloadRef candidate;
    Handle lease;
    UpdateTrust trust;
    void cleanAbandonedWork() noexcept {
        // Called only while holding the installation's update lock. A live
        // preparer cannot share that lock, including after an interrupted run.
        try {
            for (const auto& entry : std::filesystem::directory_iterator(root)) {
                const auto name = juce::String(entry.path().filename().wstring().c_str());
                const juce::String prefix = ".lighthost-stage-";
                if (!name.startsWith(prefix) || name.length() != prefix.length() + 32
                    || !name.substring(prefix.length()).containsOnly("0123456789abcdef")) continue;
                noReparsePath(entry.path());
                const auto marker = entry.path() / L".lighthost-owned-stage";
                if (!std::filesystem::is_regular_file(marker)) continue;
                noReparsePath(marker);
                if (readSmallFile(marker) != "LightHostModern portable staging v1") continue;
                for (const auto& child : std::filesystem::recursive_directory_iterator(entry.path())) noReparsePath(child.path());
                std::error_code ignored; std::filesystem::remove_all(entry.path(), ignored);
            }
        } catch (...) {} // Cleanup does not invalidate an otherwise usable update.
    }
    void cleanWork() noexcept {
        if(work.empty())return;
        try {noReparsePath(work);require(work.parent_path()==root&&work.filename().wstring().rfind(L".lighthost-stage-",0)==0,"unsafe_update_path");
            for(const auto& entry:std::filesystem::recursive_directory_iterator(work))noReparsePath(entry.path());
            std::error_code error;std::filesystem::remove_all(work,error);
        }catch(...){}
    }
public:
    explicit PortableUpdate(UpdateTrust trusted={}):trust(trusted){}
    ~PortableUpdate(){cleanWork();}
    void releaseLock(){lease.reset();}
    static std::filesystem::path safeRelative(const juce::String& raw) {
        auto path=payloadRelative(raw);const auto name=raw.replaceCharacter('\\','/');const auto first=name.upToFirstOccurrenceOf("/",false,false);
        const bool licenseNotice = first=="Licenses" && name.containsChar('/')
            && (name.endsWithIgnoreCase(".txt") || name.endsWithIgnoreCase(".md"));
        require(first=="WinUI"||licenseNotice||(!name.containsChar('/')&&(first.endsWithIgnoreCase(".dll")||first=="release-info.json"
            ||first=="legacy-payload-files.json"||first=="payload-manifest.json"||first=="README.md"||first.equalsIgnoreCase("LICENSE")||first=="THIRD-PARTY-NOTICES.txt"
            ||first=="LightHostModern.exe"||first=="LightHostModernScanner.exe"||first=="LightHostModernUpdateHelper.exe"||first=="LightHostModernWorker.exe"
            ||first=="Light Host Modern.exe"||first=="LightHostScanner.exe"||first=="LightHostUpdateHelper.exe"||first=="LightHostWinUI.exe")),"unsafe_update_path");return path;
    }
    void prepare(const std::filesystem::path& package,const Artifact& artifact,const std::filesystem::path& destination,
        const std::function<bool()>& cancelled=[] {return false;})
    {
        require(artifact.distribution==Distribution::portable,"distribution_mismatch");
        root=extendedFilePath(destination);noReparsePath(root);
        require(std::filesystem::is_regular_file(root/L"portable-layout.json"),"portable_migration_required");
        require(portableDurabilitySupported(root),"filesystem_unsupported");
        PortableStore store(root,trust);lease=store.lock();
        cleanAbandonedWork();
        validatePackage(package,artifact);const auto signedManifest=verifySignedArtifact(package.parent_path(),artifact,trust);
        require(!cancelled(),"cancelled");
        work=root/(L".lighthost-stage-"+std::wstring(juce::Uuid().toString().toWideCharPointer()));
        require(std::filesystem::create_directory(work),"storage_failed");
        { FileOutput marker(work/L".lighthost-owned-stage"); const std::string body="LightHostModern portable staging v1"; marker.write(body.data(),body.size()); marker.flush(); }
        juce::FileInputStream input(juce::File(package.c_str()));juce::ZipFile zip(input);
        const auto index=zip.getIndexOfFileName("portable-layout.json");require(index>=0,"portable_migration_required");
        auto layoutStream=std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(index));require(bool(layoutStream),"package_invalid");
        const auto layout=parseBoundedJson(layoutStream->readEntireStreamAsString());require((int)layout["formatVersion"]==portableLayoutVersion,"layout_unsupported");
        candidate=PayloadRef::parse(layout["initial"]);
        require(normalizedDigest(signedManifest["portableInventorySha256"].toString().toWideCharPointer())==normalizedDigest(candidate.inventoryHash.toWideCharPointer()),"checksum_mismatch");
        const auto prefix="versions/"+candidate.id+"/";
        const auto staging=work/L"versions"/candidate.id.toWideCharPointer();std::filesystem::create_directories(staging);
        std::array<char,262144> buffer{};
        for(int i=0;i<zip.getNumEntries();++i) {
            require(!cancelled(),"cancelled");const auto* entry=zip.getEntry(i);const auto name=entry->filename.replaceCharacter('\\','/');
            if(name.endsWithChar('/')||!name.startsWith(prefix))continue;
            const auto relative=safeRelative(name.substring(prefix.length()));const auto target=staging/relative;
            noReparsePath(target);std::filesystem::create_directories(target.parent_path());
            auto stream=std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(i));require(bool(stream),"package_invalid");FileOutput output(target);int64_t copied=0;
            for(;;) { require(!cancelled(),"cancelled");const auto n=stream->read(buffer.data(),(int)buffer.size());if(n<=0)break;
                copied+=n;require(copied<=entry->uncompressedSize,"size_mismatch");output.write(buffer.data(),n); }
            require(copied==entry->uncompressedSize,"package_invalid");output.flush();
        }
        for(const auto* name:{L"update-manifest.json",L"update-manifest.sig"}) {
            const auto body=readSmallFile(package.parent_path()/name);FileOutput output(staging/(L"."+std::wstring(name)));output.write(body.data(),body.size());output.flush();
        }
        verifyPayload(work,candidate,false,trust);
        require(!cancelled(),"cancelled");const auto target=root/L"versions"/candidate.id.toWideCharPointer();noReparsePath(target);
        if(std::filesystem::exists(target))verifyPayload(root,candidate,false,trust);
        else windowsCheck(MoveFileExW(extendedFilePath(staging).c_str(),extendedFilePath(target).c_str(),MOVEFILE_WRITE_THROUGH),"storage_failed");
        cleanWork();work.clear();
    }
    void apply(const std::function<void(size_t)>& checkpoint={}) {
        require(bool(lease)&&!candidate.empty(),"package_incomplete");
        if(checkpoint)checkpoint(0);PortableStore(root,trust).activate(candidate);if(checkpoint)checkpoint(1);
        lease.reset();
    }
};
}
