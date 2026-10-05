// Included by UpdateTests.cpp after its package fixtures. Keys are generated in
// memory for each test; no production trust root or private key is modified.
namespace {
struct TestSigningKey {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE key=nullptr;std::string blobHex;
    TrustedUpdateKey trusted;
    static std::string hex(const std::vector<unsigned char>& bytes) {std::string result;for(auto b:bytes){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;}
    TestSigningKey() {
        require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"test_key_failed");
        require(BCryptGenerateKeyPair(algorithm,&key,2048,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0,"test_key_failed");
        ULONG size=0;require(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,nullptr,0,&size,0)>=0,"test_key_failed");
        std::vector<unsigned char> blob(size);require(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,blob.data(),size,&size,0)>=0,"test_key_failed");
        blobHex=hex(blob);trusted={"ephemeral-test-only",blobHex};
    }
    ~TestSigningKey(){if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    UpdateTrust trust() const{return {&trusted,1};}
    std::string sign(const std::string& body) const {
        Sha256 hash;hash.append(body.data(),body.size());const auto wide=hash.finish();auto digest=decodeHex(std::string(wide.begin(),wide.end()));
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};ULONG size=0;
        require(BCryptSignHash(key,&padding,digest.data(),(ULONG)digest.size(),nullptr,0,&size,BCRYPT_PAD_PKCS1)>=0,"test_key_failed");
        std::vector<unsigned char> signature(size);require(BCryptSignHash(key,&padding,digest.data(),(ULONG)digest.size(),signature.data(),size,&size,BCRYPT_PAD_PKCS1)>=0,"test_key_failed");return hex(signature);
    }
};
PayloadRef layoutFixture(const std::filesystem::path& root,const char* version) {
    PayloadRef ref{juce::String(version)+"-abcdef",{}};const auto payload=root/L"versions"/ref.id.toWideCharPointer();
    std::filesystem::create_directories(payload);juce::Array<juce::var> files;
    const auto add=[&](const char* name,const std::string& bytes) {
        const auto path=payload/payloadRelative(name);std::filesystem::create_directories(path.parent_path());
        {FileOutput file(path);file.write(bytes.data(),bytes.size());file.flush();}
        juce::DynamicObject::Ptr item=new juce::DynamicObject;item->setProperty("path",name);item->setProperty("size",(juce::int64)bytes.size());
        item->setProperty("sha256",juce::String(fileDigest(path).c_str()));files.add(juce::var(item.get()));
    };
    for(const auto* name:{"LightHostModern.exe","LightHostModernScanner.exe","LightHostModernUpdateHelper.exe","WinUI/x64/Release/LightHostModern.WinUI/LightHostModernWinUI.exe"})add(name,pe());
    const auto info=std::string("{\"name\":\"LightHostModern\",\"version\":\"")+version+"\",\"platform\":\"x64\"}";
    add("release-info.json",info);add("legacy-payload-files.json","[]");
    add("THIRD-PARTY-NOTICES.txt","Fixture notices");add("Licenses/JUCE.txt","Fixture license");add("Licenses/nested/NOTICE.md","Fixture notice");
    juce::DynamicObject::Ptr inventory=new juce::DynamicObject;inventory->setProperty("formatVersion",1);inventory->setProperty("platform","x64");inventory->setProperty("version",version);inventory->setProperty("files",files);
    durableJson(payload/L"payload-manifest.json",juce::var(inventory.get()));ref.inventoryHash=juce::String(fileDigest(payload/L"payload-manifest.json").c_str());
    juce::DynamicObject::Ptr layout=new juce::DynamicObject;layout->setProperty("formatVersion",1);layout->setProperty("initial",ref.json());durableJson(root/L"portable-layout.json",juce::var(layout.get()));
    juce::File((root/L"release-info.json").c_str()).replaceWithText(info);juce::File((root/L"LightHostModern.exe").c_str()).replaceWithText("stable launcher");return ref;
}
std::filesystem::path signedZipFixture(const TestSigningKey& key,PayloadRef& candidate) {
    const auto content=directory();candidate=layoutFixture(content,"1.2.2");const auto operation=directory();const auto path=operation/L"LightHostModern-Portable.zip";
    juce::ZipFile::Builder builder;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(content))if(entry.is_regular_file())
        builder.addFile(juce::File(entry.path().c_str()),6,juce::String(entry.path().lexically_relative(content).generic_wstring().c_str()));
    {juce::FileOutputStream output(juce::File(path.c_str()));require(builder.writeToStream(output,nullptr),"test_zip_failed");output.flush();}
    auto artifact=forFile(path);juce::DynamicObject::Ptr item=new juce::DynamicObject;item->setProperty("name",juce::String(artifact.name.c_str()));item->setProperty("size",(juce::int64)artifact.bytes);item->setProperty("digest",juce::String(artifact.digest.c_str()));
    juce::DynamicObject::Ptr manifest=new juce::DynamicObject;manifest->setProperty("formatVersion",1);manifest->setProperty("minimumLauncher",1);manifest->setProperty("architecture","x64");manifest->setProperty("version","1.2.2");manifest->setProperty("keyId","ephemeral-test-only");
    manifest->setProperty("portableInventorySha256",candidate.inventoryHash);manifest->setProperty("artifacts",juce::Array<juce::var>{juce::var(item.get())});
    const auto body=juce::JSON::toString(juce::var(manifest.get()),true).toStdString();juce::File((operation/L"update-manifest.json").c_str()).replaceWithText(body);
    juce::File((operation/L"update-manifest.sig").c_str()).replaceWithText(key.sign(body));return path;
}
void realPortablePackageScenario(scenarios::Runner& runner, const std::filesystem::path& source) {
    runner.run("Actual release ZIP prepares, activates and recovers using the production portable engine", [&] {
        TestSigningKey key; const auto operation = directory(), root = directory();
        const auto package = operation / L"LightHostModern-Portable.zip";
        std::filesystem::copy_file(source, package);
        juce::FileInputStream input(juce::File(package.c_str())); juce::ZipFile zip(input);
        const auto read = [&](const char* name) {
            const auto index = zip.getIndexOfFileName(name); require(index >= 0, "package_invalid");
            auto stream = std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(index)); require(bool(stream), "package_invalid");
            return lightHostModern::parseBoundedJson(stream->readEntireStreamAsString());
        };
        const auto candidate = PayloadRef::parse(read("portable-layout.json")["initial"]);
        const auto version = read("release-info.json")["version"].toString();
        auto artifact = forFile(package); artifact.version = ("v" + version).toWideCharPointer();
        artifact.url = L"https://github.com/heide-oficial/Light-Host-Modern/releases/download/" + artifact.version + L"/" + artifact.name;
        juce::DynamicObject::Ptr item = new juce::DynamicObject;
        item->setProperty("name", juce::String(artifact.name.c_str())); item->setProperty("size", (juce::int64)artifact.bytes); item->setProperty("digest", juce::String(artifact.digest.c_str()));
        juce::DynamicObject::Ptr manifest = new juce::DynamicObject;
        manifest->setProperty("formatVersion", 1); manifest->setProperty("minimumLauncher", 1); manifest->setProperty("architecture", "x64"); manifest->setProperty("version", version); manifest->setProperty("keyId", "ephemeral-test-only");
        manifest->setProperty("portableInventorySha256", candidate.inventoryHash); manifest->setProperty("artifacts", juce::Array<juce::var>{juce::var(item.get())});
        const auto body = juce::JSON::toString(juce::var(manifest.get()), true).toStdString();
        juce::File((operation/L"update-manifest.json").c_str()).replaceWithText(body);
        juce::File((operation/L"update-manifest.sig").c_str()).replaceWithText(key.sign(body));
        const auto previous = layoutFixture(root, "1.0.0");
        PortableStore store(root, key.trust());
        { PortableUpdate update(key.trust()); update.prepare(package, artifact, root); update.apply(); }
        auto state = store.load(); scenarios::require(store.select(state).id == candidate.id, "Actual package was not activated");
        scenarios::require(std::filesystem::is_regular_file(root/L"versions"/candidate.id.toWideCharPointer()/L"LightHostModernWorker.exe"), "Worker missing after real package application");
        state.launching = true; store.save(state); state = store.load();
        scenarios::require(store.select(state).id == previous.id, "Actual package interruption lost rollback");
    });
}
void portableScenarios(scenarios::Runner& runner) {
    runner.run("Long staging paths preserve payload hashes and durable metadata without machine settings",[] {
        auto root=directory();
        while(root.wstring().size()<280)root/=L"long-portable-staging-segment";
        std::filesystem::create_directories(extendedFilePath(root));
        const auto file=root/L"payload.bin";
        {FileOutput output(file);output.write("payload",7);output.flush();}
        noReparsePath(file);
        scenarios::require(readSmallFile(file)=="payload","Long payload path was not readable");
        Sha256 expected;expected.append("payload",7);
        scenarios::require(fileDigest(file)==expected.finish(),"Long payload path changed its digest");
        juce::DynamicObject::Ptr state=new juce::DynamicObject;state->setProperty("revision",1);
        durableJson(root/L"state.json",juce::var(state.get()));
        state->setProperty("revision",2);durableJson(root/L"state.json",juce::var(state.get()));
        scenarios::require((int)readObject(root/L"state.json")["revision"]==2,"Long descriptor path was not replaced");
    });
    runner.run("Signed manifests reject altered bytes, unknown roots and forged keys",[] {
        TestSigningKey key,other;const std::string text="exact signed bytes";auto signature=key.sign(text);
        verifyManifestSignature(text,signature,key.trusted.id,&key.trusted,1);
        expect("signature_invalid",[&]{verifyManifestSignature(text+"!",signature,key.trusted.id,&key.trusted,1);});
        expect("signature_invalid",[&]{verifyManifestSignature(text,signature,other.trusted.id,&other.trusted,1);});
        expect("signature_untrusted",[&]{verifyManifestSignature(text,signature,key.trusted.id);});
    });
    runner.run("Legacy portable requires explicit migration and cannot replace its only entry point",[] {
        const auto root=directory(),package=zipFixture(directory());juce::File((root/L"LightHostModern.exe").c_str()).replaceWithText("old host");
        PortableUpdate update;expect("portable_migration_required",[&]{update.prepare(package,forFile(package),root);});
        scenarios::require(juce::File((root/L"LightHostModern.exe").c_str()).loadFileAsString()=="old host","Legacy entry changed");
        for(const auto* name:{"../host.exe","WinUI/CON.dll","WinUI/sub/file. ","WinUI/file:stream","user-settings.json"})expect("unsafe_update_path",[&]{PortableUpdate::safeRelative(name);});
        scenarios::require(!PortableUpdate::safeRelative("legacy-payload-files.json").empty(),"Release inventory rejected");
        for(const auto* name:{"THIRD-PARTY-NOTICES.txt","Licenses/JUCE.txt","Licenses/nested/NOTICE.md"})
            scenarios::require(!PortableUpdate::safeRelative(name).empty(),"License notice rejected");
        for(const auto* name:{"Licenses/runner.exe","Licenses/nested/payload.dll","Licenses/../README.md","Licenses/nested/../../README.md","Licenses/CON.txt","Licenses/secret.key"})
            expect("unsafe_update_path",[&]{PortableUpdate::safeRelative(name);});
    });
    runner.run("Versioned update stages complete payload and never rewrites launcher or settings",[] {
        TestSigningKey key;PayloadRef next;const auto package=signedZipFixture(key,next),root=directory();const auto first=layoutFixture(root,"1.2.1");
        juce::File((root/L"user-settings.json").c_str()).replaceWithText("keep");
        const auto orphan=root/L".lighthost-stage-00000000000000000000000000000000";
        const auto unrelated=root/L".lighthost-stage-11111111111111111111111111111111";
        std::filesystem::create_directory(orphan);std::filesystem::create_directory(unrelated);
        juce::File((orphan/L".lighthost-owned-stage").c_str()).replaceWithText("LightHostModern portable staging v1");
        juce::File((orphan/L"incomplete-download").c_str()).replaceWithText("partial");
        PortableStore store(root,key.trust());PortableUpdate update(key.trust());update.prepare(package,forFile(package),root);
        scenarios::require(!std::filesystem::exists(orphan)&&std::filesystem::exists(unrelated),"Orphan cleanup removed unrelated data or left owned partial payload");
        scenarios::require(store.load().confirmed.id==first.id&&store.load().candidate.empty(),"Preparation selected candidate early");
        expect("operation_conflict",[&]{auto other=store.lock();});update.apply();auto state=store.load();
        scenarios::require(store.select(state).id==next.id&&state.confirmed.id==first.id,"Activation lost previous version");
        scenarios::require(readSmallFile(root/L"versions"/next.id.toWideCharPointer()/L"THIRD-PARTY-NOTICES.txt")=="Fixture notices"
            &&readSmallFile(root/L"versions"/next.id.toWideCharPointer()/L"Licenses"/L"JUCE.txt")=="Fixture license","Update omitted authenticated license notices");
        scenarios::require(juce::File((root/L"LightHostModern.exe").c_str()).loadFileAsString()=="stable launcher"
            &&juce::File((root/L"user-settings.json").c_str()).loadFileAsString()=="keep","Root files changed");
    });
    runner.run("Interrupted attempt and corrupt state slots fall back to a complete version",[] {
        TestSigningKey key;PayloadRef next;const auto package=signedZipFixture(key,next),root=directory();const auto first=layoutFixture(root,"1.2.1");
        PortableStore store(root,key.trust());{PortableUpdate update(key.trust());update.prepare(package,forFile(package),root);update.apply();}
        auto state=store.load();state.launching=true;store.save(state);
        auto recovered=store.load();scenarios::require(store.select(recovered).id==first.id&&recovered.candidate.empty(),"Unconfirmed launch retried");
        juce::File((root/L"portable-state-0.json").c_str()).replaceWithText("{truncated");
        recovered=store.load();scenarios::require(store.select(recovered).id==first.id,"One truncated slot lost recovery");
        juce::File((root/L"portable-state-1.json").c_str()).replaceWithText("{truncated");
        recovered=store.load();scenarios::require(store.select(recovered).id==first.id,"Initial complete version unavailable");
    });
    runner.run("Tampered candidate, downgrade and failed descriptor write preserve confirmed payload",[] {
        TestSigningKey key;PayloadRef next;const auto package=signedZipFixture(key,next),root=directory();const auto first=layoutFixture(root,"1.2.1");
        PortableStore store(root,key.trust());{PortableUpdate update(key.trust());update.prepare(package,forFile(package),root);
            juce::File((root/L"versions"/next.id.toWideCharPointer()/L"LightHostModern.exe").c_str()).replaceWithText("tampered");
            expect("package_invalid",[&]{update.apply();});}
        auto state=store.load();scenarios::require(store.select(state).id==first.id,"Damaged candidate selected");
        expect("downgrade_refused",[&]{store.activate(first);});
        state.sequence=0;store.save(state);store.save(state);FileInput locked(root/L"portable-state-0.json");
        state.sequence=1;expect("storage_failed",[&]{store.save(state);});
        auto unchanged=store.load();scenarios::require(store.select(unchanged).id==first.id,"Failed descriptor write lost confirmed payload");
    });
    runner.run("Cancellation does not select or overwrite any payload",[] {
        TestSigningKey key;PayloadRef next;const auto package=signedZipFixture(key,next),root=directory();const auto first=layoutFixture(root,"1.2.1");
        {PortableUpdate update(key.trust());expect("cancelled",[&]{update.prepare(package,forFile(package),root,[]{return true;});});}
        PortableStore store(root,key.trust());auto state=store.load();scenarios::require(store.select(state).id==first.id,"Cancelled update changed selection");
    });
}
}
