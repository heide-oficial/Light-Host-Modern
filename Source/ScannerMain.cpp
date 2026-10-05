#include <juce_audio_utils/juce_audio_utils.h>
#include "ScanProcess.h"
#include "ScannerProtocol.h"
#include "PluginInstances.h"
#include "PluginIdentity.h"
#include "ScanArchitecture.h"
#include <shellapi.h>

// This executable never constructs AudioEngine, AudioDeviceManager, application
// properties or a tray. Only the module named in this one-shot request is loaded.
int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count != 3) { if (arguments) LocalFree(arguments); return 2; }
    const juce::File requestFile { juce::String(arguments[1]) };
    const juce::File responseFile { juce::String(arguments[2]) };
    LocalFree(arguments);
    auto request = lightHostModern::scan::parseScannerXml(requestFile);
    if (!request || !request->hasTagName("SCAN") || request->getIntAttribute("version") != lightHostModern::scan::scannerProtocolVersion) return 3;
    if(request->hasAttribute("logRoot")) {
        const std::filesystem::path base(request->getStringAttribute("logRoot").toWideCharPointer());
        try{lightHostModern::verbose::logger().attach(base,"scanner");}
        catch(const std::exception& error){lightHostModern::verbose::reportFailure(base,error.what());}
    }
    lightHostModern::verbose::log("scanner.request", "id="+request->getStringAttribute("id").toStdString()+" mode="+request->getStringAttribute("mode").toStdString()+" path="+request->getStringAttribute("path").toStdString());
    if (request->getStringAttribute("mode") == "enumerate") return lightHostModern::scan::enumerate(*request, responseFile);
    try
    {
        juce::ScopedJuceInitialiser_GUI initialise;
        juce::AudioPluginFormatManager formats;
       #if JUCE_PLUGINHOST_VST
        formats.addFormat(std::make_unique<juce::VSTPluginFormat>());
       #endif
       #if JUCE_PLUGINHOST_VST3
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
       #endif
        juce::AudioPluginFormat* selected = nullptr;
        for (auto* format : formats.getFormats())
            if (format->getName() == request->getStringAttribute("format")) selected = format;
        if (!selected) return 4;
        const auto path = request->getStringAttribute("path");
        const juce::File module(path);
        const auto mode=request->getStringAttribute("mode");
        const auto fingerprint = request->getStringAttribute("fingerprint");
        if(mode=="verify" && lightHostModern::scan::fingerprint(module)!=fingerprint)return 7;
        if(mode=="verify")return 0;
        const auto architecture=lightHostModern::scan::architectureError(module);
        if(architecture.isNotEmpty())return 10;
        juce::OwnedArray<juce::PluginDescription> plugins;
        // JUCE uses VST3 moduleinfo when present. Every resulting class is still
        // instantiated and checked below before being accepted by the host.
        if(mode=="class") {
            auto description=std::make_unique<juce::PluginDescription>();auto* xml=request->getChildByName("PLUGIN");
            if(!xml||!description->loadFromXml(*xml)||!lightHostModern::scan::belongsToModule(description->fileOrIdentifier,path,selected->getName()))return 3;
            plugins.add(description.release());
        } else {
            lightHostModern::scan::StageTiming catalogTiming("catalog", "path=" + path.toStdString());
            SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_CATALOG",L"1");
            if (selected->getName()=="VST3") SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_MANIFEST_ONLY",L"1");
            {
                lightHostModern::scan::StageTiming declaredTiming("catalog_declared", "path=" + path.toStdString());
                selected->findAllTypesForFile(plugins, path);
            }
            SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_MANIFEST_ONLY",nullptr);
            if (selected->getName()=="VST3") {
                lightHostModern::scan::StageTiming factoryTiming("catalog_factory", "path=" + path.toStdString());
                juce::OwnedArray<juce::PluginDescription> factory;
                SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_FACTORY",L"1");
                selected->findAllTypesForFile(factory,path);
                if (!factory.isEmpty()) {
                    for(auto* actual:factory) {
                        const auto match=std::find_if(plugins.begin(),plugins.end(),[&](const auto* declared){return declared->vst3ClassId==actual->vst3ClassId;});
                        if(match==plugins.end()) lightHostModern::verbose::log("scanner.catalog","manifest_class_missing_or_stale cid="+actual->vst3ClassId.toStdString());
                        // Keep bundle identifiers stable while trusting the factory's class data.
                        if(match!=plugins.end())actual->fileOrIdentifier=(*match)->fileOrIdentifier;
                    }
                    plugins.swapWith(factory);
                }
            }
        }
        // A single-class VST3 can be validated in this already isolated process.
        // Multi-class modules still return their catalog for per-class children.
        const bool validateSingle = mode == "probe" && selected->getName() == "VST3"
            && request->getBoolAttribute("validateSingleClass") && plugins.size() == 1;
        if (validateSingle) {
            SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_CATALOG", nullptr);
            SetEnvironmentVariableW(L"LIGHTHOST_SCANNER_FACTORY", nullptr);
        }
        juce::XmlElement response("SCAN");
        response.setAttribute("version", lightHostModern::scan::scannerProtocolVersion);
        response.setAttribute("mode", "probe");
        response.setAttribute("fingerprint", fingerprint);
        response.setAttribute("id", request->getStringAttribute("id"));
        response.setAttribute("path", request->getStringAttribute("path"));
        response.setAttribute("format", selected->getName());
        response.setAttribute("catalog",mode!="class"&&!validateSingle);
        if(plugins.isEmpty()&&selected->getName()=="VST3") {
            const auto native=lightHostModern::scan::nativeLoadFailure(module);
            if(native.isNotEmpty())response.setAttribute("error",native);
        }
        for (const auto* plugin : plugins)
        {
            auto* item = response.createNewChildElement("ENTRY");
            item->setAttribute("knownId", lightHostModern::knownPluginId(*plugin));
            item->addChildElement(plugin->createXml().release());
            if(mode!="class"&&!validateSingle)continue;
            lightHostModern::scan::StageTiming classTiming("class", "class=" + item->getStringAttribute("knownId").toStdString());
            const auto moduleInfo = module.getChildFile("Contents/Resources/moduleinfo.json");
            item->setAttribute("declaredMetadata", moduleInfo.existsAsFile() ? "available" : "unavailable");
            try
            {
                juce::String error;
                auto instance = [&] {
                    lightHostModern::scan::StageTiming loadTiming("instantiate", "class=" + item->getStringAttribute("knownId").toStdString());
                    return formats.createPluginInstance(*plugin, 48000, 512, error);
                }();
                if (!instance) {
                    const auto native=selected->getName()=="VST3"?lightHostModern::scan::nativeLoadFailure(module):juce::String();
                    item->setAttribute("error",native.isNotEmpty()?native:(error.isEmpty()?"validation_failed":error));
                    item->setAttribute("detail",error);continue;
                }
                juce::PluginDescription actual;
                instance->fillInPluginDescription(actual);
                lightHostModern::verbose::log("scanner.identity","expected="+plugin->fileOrIdentifier.toStdString()+" actual="+actual.fileOrIdentifier.toStdString()+" expectedClass="+juce::String::toHexString(plugin->uniqueId).toStdString()+" actualClass="+juce::String::toHexString(actual.uniqueId).toStdString());
                if (!lightHostModern::samePluginClass(*plugin,actual)
                    || !lightHostModern::scan::belongsToModule(actual.fileOrIdentifier, path, selected->getName()))
                { item->setAttribute("error", "identity_mismatch"); continue; }
                // Preserve the requested identity while using live channel metadata.
                auto verified=*plugin;verified.numInputChannels=actual.numInputChannels;verified.numOutputChannels=actual.numOutputChannels;
                item->removeChildElement(item->getChildByName("PLUGIN"),true);item->addChildElement(verified.createXml().release());
                for (const bool input : {true, false})
                    for (int index = 0; index < instance->getBusCount(input); ++index)
                    {
                        const auto* bus = instance->getBus(input, index);
                        auto* info = item->createNewChildElement("BUS");
                        info->setAttribute("direction", input ? "input" : "output");
                        info->setAttribute("name", bus->getName());
                        info->setAttribute("channels", bus->getNumberOfChannels());
                        info->setAttribute("main", index == 0);
                        info->setAttribute("enabled", bus->isEnabled());
                        info->setAttribute("layout", bus->getDefaultLayout().getDescription());
                        info->setAttribute("defaultChannels", bus->getDefaultLayout().size());
                    }
                item->setAttribute("verifiedMetadata", "verified");
            }
            catch (...) { item->setAttribute("error", "validation_exception"); }
        }
        if(validateSingle||(mode=="class"&&request->getBoolAttribute("verifyAtEnd"))) {
            if(lightHostModern::scan::fingerprint(module)!=fingerprint)return 7;
            response.setAttribute("fingerprintVerified",true);
        }
        if(lightHostModern::verbose::logger().active())lightHostModern::verbose::log("scanner.result","id="+request->getStringAttribute("id").toStdString()+" "+response.toString().toStdString());
        return response.writeTo(responseFile) ? 0 : 5;
    }
    catch (const std::exception& error)
    {
        const juce::String reason(error.what());
        lightHostModern::verbose::log("scanner.failure",reason.toStdString());
        if (reason == "missing") return 8;
        if (reason == "metadata_unavailable") return 9;
        if (reason == "changed") return 7;
        return 6;
    }
    catch (...) { return 6; }
}
