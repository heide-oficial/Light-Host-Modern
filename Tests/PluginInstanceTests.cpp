#include "PluginInstances.h"
#include "PluginStateCapture.h"
#include "KnownPluginNames.h"
#include "OperatingProfiles.h"
#include "ScenarioRunner.h"

using namespace juce;
using namespace lightHostModern;
using scenarios::require;

static PluginDescription plugin(int uid = 42)
{
    PluginDescription description;
    description.name = "Effect";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "C:\\Plugins\\Effect.vst3";
    description.uniqueId = uid;
    description.deprecatedUid = 100;
    return description;
}

int main(int argc, char** argv)
{
    const bool legacyLoadedFixture = argc == 5 && String(argv[1]) == "--write-legacy-loaded-ui-fixture";
    const bool loadedFixture = legacyLoadedFixture || (argc == 5 && String(argv[1]) == "--write-loaded-ui-fixture");
    if ((argc == 4 && String(argv[1]) == "--write-ui-fixture") || loadedFixture)
    {
        const int count = String(argv[3]).getIntValue();
        if (count < 1 || count > 1000) return 2;
        const File destination(String::fromUTF8(argv[2]));
        PluginDescription simulatedProcessor;
        if (loadedFixture)
        {
            const auto cache = XmlDocument::parse(File(String::fromUTF8(argv[4])));
            const auto* entry = cache ? cache->getChildByName("ENTRY") : nullptr;
            const auto* description = entry ? entry->getChildByName("PLUGIN") : nullptr;
            if (!description || entry->getStringAttribute("verifiedMetadata") != "verified"
                || !simulatedProcessor.loadFromXml(*description)
                || simulatedProcessor.name != "LightHostModern Scenario Fixture" || simulatedProcessor.manufacturerName != "LightHostModern Tests"
                || !File(simulatedProcessor.fileOrIdentifier).existsAsFile()) return 3;
        }
        XmlElement properties("PROPERTIES");
        auto* known = properties.createNewChildElement("VALUE");
        known->setAttribute("name", "pluginList");
        auto* database = known->createNewChildElement("KNOWNPLUGINS");
        XmlElement* legacy = nullptr;
        if (legacyLoadedFixture)
        {
            auto* active = properties.createNewChildElement("VALUE");
            active->setAttribute("name", "pluginListActive");
            legacy = active->createNewChildElement("KNOWNPLUGINS");
        }
        PluginInstances instances;
        for (int index = 0; index < count; ++index)
        {
            PluginInstanceRecord record;
            record.id = Uuid().toString();
            record.description = plugin(index + 1);
            record.description.name = "Test Effect " + String(index + 1).paddedLeft('0', 4);
            record.description.manufacturerName = index % 2 ? "Factory A" : String::fromUTF8("Fábrica B");
            record.description.fileOrIdentifier = destination.getParentDirectory().getChildFile("Simulated")
                .getChildFile(String(index + 1) + ".vst3").getFullPathName();
            database->addChildElement((loadedFixture && index == 0 ? simulatedProcessor : record.description).createXml().release());
            if (loadedFixture)
            {
                record.customName = record.description.name;
                record.description = simulatedProcessor;
            }
            record.originalIdentity = knownPluginId(record.description);
            record.loading = loadedFixture ? "unloaded" : "missing";
            // A VST3 host state wraps the processor state. Let the real host
            // capture that wrapper after loading each independent instance.
            if (!loadedFixture) record.lastValidState = MemoryBlock(&index, sizeof(index)).toBase64Encoding();
            if (legacy)
            {
                auto description = simulatedProcessor;
                description.deprecatedUid += index;
                legacy->addChildElement(description.createXml().release());
                auto* id = properties.createNewChildElement("VALUE");
                id->setAttribute("name", PluginInstances::legacyKey("instance-id", description));
                id->setAttribute("val", record.id);
                auto* order = properties.createNewChildElement("VALUE");
                order->setAttribute("name", PluginInstances::legacyKey("order", description));
                order->setAttribute("val", index + 1);
            }
            instances.records.push_back(std::move(record));
        }
        if (!legacy)
        {
            auto* session = properties.createNewChildElement("VALUE");
            session->setAttribute("name", "pluginInstancesV1");
            session->addChildElement(instances.serialize(1).release());
        }
        return properties.writeTo(destination) ? 0 : 1;
    }
    if (argc == 3 && String(argv[1]) == "--write-legacy-fixture")
    {
        auto original = plugin();
        auto duplicate = original;
        ++duplicate.deprecatedUid;
        XmlElement properties("PROPERTIES");
        auto* known = properties.createNewChildElement("VALUE");
        known->setAttribute("name", "pluginList");
        known->createNewChildElement("KNOWNPLUGINS")->addChildElement(original.createXml().release());
        auto* active = properties.createNewChildElement("VALUE");
        active->setAttribute("name", "pluginListActive");
        auto* legacy = active->createNewChildElement("KNOWNPLUGINS");
        legacy->addChildElement(original.createXml().release());
        legacy->addChildElement(duplicate.createXml().release());
        const auto value = [&](const String& key, const String& text) {
            auto* item = properties.createNewChildElement("VALUE");
            item->setAttribute("name", key); item->setAttribute("val", text);
        };
        value(PluginInstances::legacyKey("instance-id", original), "11111111111111111111111111111111");
        value(PluginInstances::legacyKey("instance-id", duplicate), "22222222222222222222222222222222");
        value(PluginInstances::legacyKey("order", original), "2");
        value(PluginInstances::legacyKey("order", duplicate), "1");
        value(PluginInstances::legacyKey("state", original), MemoryBlock("one", 3).toBase64Encoding());
        value(PluginInstances::legacyKey("state", duplicate), MemoryBlock("two", 3).toBase64Encoding());
        return properties.writeTo(File(String::fromUTF8(argv[2]))) ? 0 : 1;
    }
    scenarios::Runner runner;
    runner.run("profile catalogue recovers readable entries without overwriting the original", [] {
        const auto directory=File::getSpecialLocation(File::tempDirectory).getChildFile("lhm-profile-recovery-"+Uuid().toString());
        require(directory.createDirectory().wasOk(),"Create isolated profile directory");
        struct Cleanup { File directory; ~Cleanup(){directory.deleteRecursively();} } cleanup{directory};
        const auto preferences=directory.getChildFile("settings.xml"), file=directory.getChildFile("settings.xml.profiles.xml");
        OperatingProfiles profiles(preferences);require(profiles.ensureDefaults(),"Create defaults");
        auto next=profiles.profiles;auto custom=OperatingProfiles::makeDefault("list");custom.id=Uuid().toString();custom.session.profileId=custom.id;custom.name="My setup";next.push_back(custom);
        require(profiles.commit(next),"Commit profiles with backup");
        require(file.getSiblingFile(file.getFileName()+".bak").existsAsFile(),"No profile backup created");
        auto xml=juce::parseXML(file);xml->createNewChildElement("PROFILE")->setAttribute("id","invalid");
        require(file.replaceWithText(xml->toString()),"Write corrupt fixture");const auto original=file.loadFileAsString();
        OperatingProfiles partial(preferences);require(!partial.writable&&partial.find(custom.id),"One invalid entry erased valid profiles");
        require(file.loadFileAsString()==original,"Loading partial catalogue overwrote original");
        require(partial.recover(),"Explicit recovery failed");OperatingProfiles recovered(preferences);
        require(recovered.writable&&recovered.find(custom.id),"Recovered profiles were not readable");
        require(!directory.findChildFiles(File::findFiles,false,"*.recovery-*").isEmpty(),"Corrupt original not retained");
        require(file.replaceWithText("truncated"),"Corrupt primary fixture");OperatingProfiles backup(preferences);
        require(!backup.profiles.empty()&&!backup.writable,"No backup recovery offered");
    });

    runner.run("exact legacy keys precede normalization and preserve distinct duplicate states", [] {
        const auto original = plugin();
        auto duplicate = original;
        ++duplicate.deprecatedUid;
        PropertySet settings;
        const auto firstId = Uuid().toString(), secondId = Uuid().toString();
        const auto firstState = MemoryBlock("one", 3).toBase64Encoding(), secondState = MemoryBlock("two", 3).toBase64Encoding();
        settings.setValue(PluginInstances::legacyKey("instance-id", original), firstId);
        settings.setValue(PluginInstances::legacyKey("instance-id", duplicate), secondId);
        settings.setValue(PluginInstances::legacyKey("state", original), firstState);
        settings.setValue(PluginInstances::legacyKey("state", duplicate), secondState);
        settings.setValue(PluginInstances::legacyKey("order", original), 2);
        settings.setValue(PluginInstances::legacyKey("order", duplicate), 1);
        settings.setValue(PluginInstances::legacyKey("bypass", duplicate), 1);
        XmlElement legacy("KNOWNPLUGINS");
        legacy.addChildElement(original.createXml().release());
        legacy.addChildElement(duplicate.createXml().release());
        PluginInstances instances;
        instances.migrate(legacy, settings, {original});
        require(instances.records.size() == 2, "duplicate was collapsed");
        const auto& first = instances.records[0];
        const auto& second = instances.records[1];
        require(first.id == secondId && second.id == firstId, "legacy order or UUID changed");
        require(first.lastValidState == secondState && second.lastValidState == firstState && first.bypassed, "states were reassigned");
        require(first.description.deprecatedUid == original.deprecatedUid && first.originalIdentity == second.originalIdentity, "original identity was not restored");
        require(first.legacyDescription != second.legacyDescription, "exact recovery material lost");
        PluginInstances reloaded;
        require(reloaded.deserialize(*instances.serialize(9)) && reloaded.records.size() == 2, "adapter lost duplicates");
        require(reloaded.records[0].id == first.id && reloaded.records[0].lastValidState == first.lastValidState, "adapter changed state");
        instances.migrate(legacy, settings, {original});
        require(instances.records.size() == 2 && instances.records[0].id == secondId, "migration is not idempotent");
    });
    runner.run("ambiguous identities and states remain recoverable", [] {
        const auto original = plugin(0);
        auto unknown = original;
        ++unknown.deprecatedUid;
        PropertySet settings;
        const auto exact = PluginInstances::legacyKey("state", unknown);
        settings.setValue(exact, "not valid base64 !");
        XmlElement legacy("KNOWNPLUGINS");
        legacy.addChildElement(unknown.createXml().release());
        PluginInstances instances;
        instances.migrate(legacy, settings, {original});
        const auto& record = instances.records.front();
        require(!record.identityResolved && record.description.deprecatedUid == unknown.deprecatedUid, "unknown identity guessed");
        require(record.recoveryState == settings.getValue(exact) && !record.stateCaptureAllowed, "corrupt original discarded");
        require(settings.containsKey(exact), "legacy state was deleted");
    });
    runner.run("identity is independent of metadata and installed sort order", [] {
        const auto original = plugin();
        auto changed = original;
        changed.name = "New display name";
        changed.version = "9";
        changed.deprecatedUid++;
        require(knownPluginId(original) == knownPluginId(changed), "metadata changed class identity");
        changed.uniqueId++;
        require(knownPluginId(original) != knownPluginId(changed), "different class got same identity");
        changed = original;
        changed.pluginFormatName = "VST";
        require(knownPluginId(original) != knownPluginId(changed), "formats got same identity");
    });
    runner.run("invalid adapter load is transactional and rejects duplicate UUIDs", [] {
        PluginInstances instances;
        PluginInstanceRecord record;
        record.id = Uuid().toString();
        record.description = plugin();
        record.originalIdentity = knownPluginId(record.description);
        record.customName = String::fromUTF8("  Voz • 日本語  ");
        instances.records.push_back(record);
        auto xml = instances.serialize();
        xml->addChildElement(new XmlElement(*xml->getFirstChildElement()));
        require(!instances.deserialize(*xml) && instances.records.size() == 1, "duplicate UUID accepted or existing records lost");
        xml = instances.serialize();
        xml->setAttribute("version", 99);
        require(!instances.deserialize(*xml) && instances.records.front().id == record.id, "unsupported data overwrote session");
    });
    runner.run("custom names count Unicode code points and reject multiline input", [] {
        String normalized;
        require(normalizeInstanceName(String::fromUTF8("  Voz • 日本語 🎵  "), normalized)
            && normalized == String::fromUTF8("Voz • 日本語 🎵"), "Unicode name or trim changed");
        require(normalizeInstanceName("   ", normalized) && normalized.isEmpty(), "Empty name must restore original");
        require(normalizeInstanceName(String::repeatedString(String::fromUTF8("🎵"), 128), normalized), "128 supplementary characters rejected");
        require(!normalizeInstanceName(String::repeatedString("x", 129), normalized), "Overlong name accepted");
        for (const auto& bad : {String("a\nb"), String("a\rb"), String::fromUTF8("a\xe2\x80\xa8" "b"), String("a\tb")})
            require(!normalizeInstanceName(bad, normalized), "Multiline/control name accepted");
    });
    runner.run("catalogue aliases survive persistence and seed independent instances", [] {
        PropertySet settings;
        const auto original = plugin();
        const auto identity = knownPluginId(original);
        const auto alias = String::fromUTF8("  Voz \xe2\x80\xa2 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e  ");
        require(setKnownPluginCustomName(settings, original, alias), "valid catalogue alias rejected");
        PropertySet reopened;
        reopened.restoreFromXml(*settings.createXml("SETTINGS"));
        auto updatedMetadata = original; updatedMetadata.version = "2"; updatedMetadata.name = "Vendor renamed effect";
        require(knownPluginCustomName(reopened, updatedMetadata) == alias.trim(), "rescan or reload lost alias");
        auto first = newKnownPluginInstance(reopened, original), second = newKnownPluginInstance(reopened, original);
        require(first.id != second.id && first.displayName() == alias.trim() && first.originalIdentity == identity,
                "add did not inherit alias or changed identity/UUID");
        require(first.description.name == original.name && second.description.name == original.name, "original description was renamed");
        require(!setKnownPluginCustomName(reopened, original, "bad\nname") && knownPluginCustomName(reopened, original) == alias.trim(),
                "invalid rename damaged the existing alias");
        require(knownPluginCustomName(reopened, plugin(999)).isEmpty(), "alias leaked to another class");
        require(setKnownPluginCustomName(reopened, original, original.name) && knownPluginCustomName(reopened, original).isEmpty(),
                "original name was retained as a custom alias");
        require(first.displayName() == alias.trim() && newKnownPluginInstance(reopened, original).displayName() == original.name,
                "catalogue restore changed a running instance or affected new additions");
    });
    runner.run("Visual colors survive list and graph persistence without changing routing", [] {
        PluginInstances session;PluginInstanceRecord record;record.id=Uuid().toString();record.description=plugin();record.originalIdentity=knownPluginId(record.description);
        record.cardColor="#FF10B981";session.records.push_back(record);
        PluginInstances restored;require(restored.deserialize(*session.serialize()),"Session reload failed");
        require(restored.records[0].cardColor==record.cardColor,"List color lost");
        auto graph=RoutingGraph::empty(2,2);graph.nodes[0].cardColor="#803C83F6";
        graph.nodes[0].outputColors.getDynamicObject()->setProperty("0","#FFFF0000");
        graph.nodes[1].inputColors.getDynamicObject()->setProperty("0","#FF0000FF");
        RoutingGraph roundtrip;String error;require(RoutingGraph::parse(graph.json(),roundtrip,error),"Graph color reload failed");
        require(roundtrip.nodes[0].cardColor==graph.nodes[0].cardColor && roundtrip.nodes[0].outputColors["0"].toString()=="#FFFF0000","Graph color lost");
        auto bad=graph.json();bad["nodes"].getArray()->getReference(0).getDynamicObject()->setProperty("cardColor","not a color");
        require(!RoutingGraph::parse(bad,roundtrip,error),"Invalid color accepted");
        bad=graph.json();bad["nodes"].getArray()->getReference(0)["outputColors"].getDynamicObject()->setProperty("999","#FFFFFFFF");
        require(!RoutingGraph::parse(bad,roundtrip,error),"Invalid channel color index accepted");
    });
    runner.run("Mixer pair edits preserve surviving wires and metadata", [] {
        auto graph=RoutingGraph::empty(2,2);RouteNode mixer;mixer.id="mix";mixer.kind="mixer";mixer.inputs=8;mixer.outputs=2;
        mixer.inputColors.getDynamicObject()->setProperty("6","#FF123456");mixer.inputAliases.getDynamicObject()->setProperty("6:2","Keep this pair");
        mixer.gains[3]=.25f;graph.nodes.push_back(mixer);
        graph.edges.push_back({"keep-in","audio-in","mix",0,6,2,2});
        graph.edges.push_back({"remove-in","audio-in","mix",0,2,2,2});
        graph.edges.push_back({"keep-out","mix","audio-out",0,0,2,2});
        require(graph.editMixerPair("mix",false,1).isEmpty(),"Remove failed");
        const auto* updated=graph.find("mix");require(updated->inputs==6&&updated->gains.size()==3&&updated->gains[2]==.25f,"Mixer controls shifted incorrectly");
        require(updated->inputColors["4"].toString()=="#FF123456"&&updated->inputAliases["4:2"].toString()=="Keep this pair","Surviving metadata lost");
        require(graph.edges.size()==2&&graph.edges[0].input==4&&graph.validate().isEmpty(),"Surviving wires changed");
        require(graph.editMixerPair("mix",true).isEmpty()&&graph.find("mix")->outputs==4,"Output add failed");
        require(graph.editMixerPair("mix",true,0).isEmpty()&&graph.edges.size()==1,"Deleted output wires retained");
        require(graph.editMixerPair("mix",true,0).isNotEmpty(),"Last output pair removed");
        require(graph.editMixerPair("audio-in",true).isNotEmpty(),"Hardware topology was editable");
        RoutingGraph restored;String error;require(RoutingGraph::parse(graph.json(),restored,error),"Dynamic mixer roundtrip failed");
        while(graph.find("mix")->inputs<256)require(graph.editMixerPair("mix",false).isEmpty(),"Valid input add failed");
        require(graph.editMixerPair("mix",false).isNotEmpty(),"Mixer exceeded channel bound");
        require(graph.editMixerPair("mix",false,2147483647).isNotEmpty(),"Invalid pair index accepted");
    });
    runner.run("Aggregate capture limit keeps previous state before encoding", [] {
        PluginInstanceRecord record;record.lastValidState="previous state";
        const bool captured=capturePluginState(record,[](juce::MemoryBlock& block){block.setSize(2048,true);},1024);
        require(!captured&&record.lastValidState=="previous state","Aggregate capture limit erased previous state");
    });
    runner.run("Isolation persists by instance and is not silently accepted by older session schemas", [] {
        PropertySet settings; PluginInstances session;
        session.records.push_back(newKnownPluginInstance(settings, plugin()));
        const auto original = session.serialize(); require(original->getIntAttribute("version") == 1, "Direct sessions lost compatibility");
        PluginInstances migrated; require(migrated.deserialize(*original) && !migrated.records[0].isolated, "Old session enabled isolation implicitly");
        session.records[0].isolated = true;
        const auto isolated = session.serialize(); require(isolated->getIntAttribute("version") == 2, "Older host would silently run isolated plugin directly");
        require(migrated.deserialize(*isolated) && migrated.records[0].isolated && migrated.records[0].id == session.records[0].id,
            "Isolated reload lost mode or instance identity");
    });
    runner.run("Tray flattens chain dependencies without changing list order", [] {
        PluginInstances session;session.mode="chain";session.graph=RoutingGraph::empty(2,2);session.graph.edges.clear();
        for(const auto* id:{"c","b","a","unavailable"}){PluginInstanceRecord r;r.id=id;session.records.push_back(r);}
        for(const auto* id:{"c","b","a"}){RouteNode n;n.id=id;session.graph.nodes.push_back(n);}
        RouteNode mixer;mixer.id="mix";mixer.kind="mixer";mixer.inputs=8;session.graph.nodes.push_back(mixer);
        session.graph.edges={{"in","audio-in","a",0,0,2,2},{"ab","a","b",0,0,2,2},{"ac","a","c",0,0,2,2},
            {"bm","b","mix",0,0,2,2},{"cm","c","mix",0,2,2,2},{"out","mix","audio-out",0,0,2,2}};
        require(session.graph.validate().isEmpty(),"Tray test graph is invalid");
        require(session.processingOrder()==std::vector<size_t>{2,1,0,3},"Tray ignored chain dependencies or lost an unavailable instance");
        session.graph.find("a")->x=1400;session.graph.find("b")->x=-800;
        require(session.processingOrder()==std::vector<size_t>{2,1,0,3},"Moving cards changed the tray order");
        session.mode="list";
        require(session.processingOrder()==std::vector<size_t>{0,1,2,3},"List-mode instance order changed");
    });
    runner.run("Channel configurations require a compatible host and survive persistence", [] {
        PropertySet settings; PluginInstances session;session.records.push_back(newKnownPluginInstance(settings,plugin()));
        AudioProcessor::BusesLayout layout;layout.inputBuses.add(AudioChannelSet::stereo());layout.inputBuses.add(AudioChannelSet::disabled());layout.outputBuses.add(AudioChannelSet::mono());
        session.records[0].busLayout=pluginBuses::encode(layout);const auto xml=session.serialize();
        require(xml->getIntAttribute("version")==3,"Old host would reinterpret bus connections");
        PluginInstances read;require(read.deserialize(*xml),"Cannot restore configured buses");AudioProcessor::BusesLayout decoded;
        require(pluginBuses::decode(read.records[0].busLayout,decoded)&&decoded==layout,"Configured buses changed after restart");
        xml->getChildByName("INSTANCE")->getChildByName("BUSES")->deleteAllTextElements();
        require(!read.deserialize(*xml)&&read.records.size()==1,"Invalid layout damaged the previous session");
    });
    return runner.result();
}
