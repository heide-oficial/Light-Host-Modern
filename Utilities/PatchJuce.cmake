set(LIGHTHOST_JUCE_BUILD_ROOT "${CMAKE_BINARY_DIR}")
cmake_path(IS_PREFIX LIGHTHOST_JUCE_BUILD_ROOT "${juce_SOURCE_DIR}" NORMALIZE LIGHTHOST_OWNS_JUCE)
if(NOT LIGHTHOST_OWNS_JUCE)
    message(FATAL_ERROR "Refusing to patch a JUCE checkout outside the build directory")
endif()
# Retain the complete VST3 class ID as optional verification metadata. Existing
# identifiers remain unchanged and old XML without this attribute still loads.
function(lighthost_patch_once path marker before after)
    file(READ "${path}" content)
    string(FIND "${content}" "${marker}" already)
    if(already EQUAL -1)
        string(FIND "${content}" "${before}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "JUCE adapter anchor changed: ${marker}")
        endif()
        string(REPLACE "${before}" "${after}" content "${content}")
        file(WRITE "${path}" "${content}")
    endif()
endfunction()
# Preferences must consume the bounded tree itself. A preflight followed by
# PropertiesFile reopening the file would leave an unsafe second read. Keep the
# hooks optional so other JUCE consumers retain their original behavior.
set(LH_PROPERTIES "${juce_SOURCE_DIR}/modules/juce_data_structures/app_properties")
lighthost_patch_once("${LH_PROPERTIES}/juce_PropertiesFile.h" "xmlFileReader;"
    "        InterProcessLock* processLock;"
    "        InterProcessLock* processLock;\n\n        std::function<std::unique_ptr<XmlElement>(const File&)> xmlFileReader;\n        std::function<std::unique_ptr<XmlElement>(const String&)> xmlValueParser;")
lighthost_patch_once("${LH_PROPERTIES}/juce_PropertiesFile.cpp" "options.xmlFileReader ? loadAsXml()"
    "    loadedOk = (! file.exists()) || loadAsBinary() || loadAsXml();"
    "    loadedOk = (! file.exists()) || (options.xmlFileReader ? loadAsXml() : (loadAsBinary() || loadAsXml()));")
lighthost_patch_once("${LH_PROPERTIES}/juce_PropertiesFile.cpp" "options.xmlFileReader (file)"
    "    if (auto doc = parseXMLIfTagMatches (file, PropertyFileConstants::fileTag))"
    "    auto doc = options.xmlFileReader ? options.xmlFileReader (file) : parseXMLIfTagMatches (file, PropertyFileConstants::fileTag);\n    if (doc != nullptr && doc->hasTagName (PropertyFileConstants::fileTag))")
lighthost_patch_once("${LH_PROPERTIES}/juce_PropertiesFile.cpp" "options.xmlValueParser (props"
    "        if (auto childElement = parseXML (props.getAllValues() [i]))"
    "        if (auto childElement = options.xmlValueParser ? options.xmlValueParser (props.getAllValues() [i]) : parseXML (props.getAllValues() [i]))")
set(LH_PROCESSORS "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/processors")
lighthost_patch_once("${LH_PROCESSORS}/juce_PluginDescription.h" "String vst3ClassId;"
    "    int uniqueId = 0;" "    int uniqueId = 0;\n    String vst3ClassId;")
lighthost_patch_once("${LH_PROCESSORS}/juce_PluginDescription.cpp" "setAttribute (\"vst3ClassId\""
    "    e->setAttribute (\"uniqueId\", String::toHexString (uniqueId));"
    "    e->setAttribute (\"uniqueId\", String::toHexString (uniqueId));\n    if (vst3ClassId.isNotEmpty()) e->setAttribute (\"vst3ClassId\", vst3ClassId);")
lighthost_patch_once("${LH_PROCESSORS}/juce_PluginDescription.cpp" "vst3ClassId = xml"
    "        uniqueId            = xml.getStringAttribute (\"uniqueId\", \"0\").getHexValue32();"
    "        uniqueId            = xml.getStringAttribute (\"uniqueId\", \"0\").getHexValue32();\n        vst3ClassId = xml.getStringAttribute (\"vst3ClassId\");")
set(LH_VST3_IMPL "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h")
lighthost_patch_once("${LH_VST3_IMPL}" "description.vst3ClassId = String::toHexString (uid"
    "        description.deprecatedUid       = getHashForRange (uid->data());"
    "        description.vst3ClassId = String::toHexString (uid->data(), 16, 0);\n        description.deprecatedUid       = getHashForRange (uid->data());")
lighthost_patch_once("${LH_VST3_IMPL}" "description.vst3ClassId = String::toHexString (info"
    "    description.deprecatedUid       = getHashForRange (info.cid);"
    "    description.vst3ClassId = String::toHexString (info.cid, 16, 0);\n    description.deprecatedUid       = getHashForRange (info.cid);")
lighthost_patch_once("${LH_VST3_IMPL}" "desc.vst3ClassId.isNotEmpty()"
    "            if (toString (info.name).trim() != desc.name)"
    "            if (desc.vst3ClassId.isNotEmpty() && desc.vst3ClassId != String::toHexString (info.cid, 16, 0)) continue;\n            if (toString (info.name).trim() != desc.name)")
# The isolated scanner can confirm a manifest against the real factory without
# modifying third-party metadata or instantiating every class.
set(LH_VST3_SOURCE "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatHeadless.cpp")
lighthost_patch_once("${LH_VST3_SOURCE}" "LIGHTHOST_SCANNER_FACTORY"
    "    if (const auto fast = DescriptionLister::findDescriptionsFast (File (fileOrIdentifier)); ! fast.empty())"
    "    if (const auto fast = SystemStats::getEnvironmentVariable (\"LIGHTHOST_SCANNER_FACTORY\", {}) == \"1\" ? std::vector<PluginDescription>{} : DescriptionLister::findDescriptionsFast (File (fileOrIdentifier)); ! fast.empty())")
# The host must observe actual MidiBuffer storage after third-party processing.
# A missing/invalid manifest must not fall through to the factory before the
# scanner's explicit factory pass. This flag is set only in that child process.
lighthost_patch_once("${LH_VST3_SOURCE}" "LIGHTHOST_SCANNER_MANIFEST_ONLY"
    "    for (const auto& file : getLibraryPaths (*this, fileOrIdentifier))"
    "    if (SystemStats::getEnvironmentVariable (\"LIGHTHOST_SCANNER_MANIFEST_ONLY\", {}) == \"1\") return;\n\n    for (const auto& file : getLibraryPaths (*this, fileOrIdentifier))")
# Scanner catalog mode avoids constructing every class before isolated validation.
# Opt-in environment flag is set only inside the scanner's catalog process.
set(LH_VST3_IMPL "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h")
file(READ "${LH_VST3_IMPL}" LH_VST3_TEXT)
if(NOT LH_VST3_TEXT MATCHES "LIGHTHOST_SCANNER_CATALOG")
    set(LH_CATALOG_ANCHOR "            {\n                VSTComSmartPtr<Vst::IComponent> component;")
    string(FIND "${LH_VST3_TEXT}" "${LH_CATALOG_ANCHOR}" LH_CATALOG_FOUND)
    if(LH_CATALOG_FOUND EQUAL -1)
        message(FATAL_ERROR "JUCE VST3 catalog changed; review scanner adapter")
    endif()
    string(REPLACE "${LH_CATALOG_ANCHOR}"
        "            if (SystemStats::getEnvironmentVariable (\"LIGHTHOST_SCANNER_CATALOG\", {}) == \"1\")\n                createPluginDescription (desc, file, companyName, name, info, info2.get(), infoW.get(), 0, 0);\n            else\n            {\n                VSTComSmartPtr<Vst::IComponent> component;"
        LH_VST3_TEXT "${LH_VST3_TEXT}")
    file(WRITE "${LH_VST3_IMPL}" "${LH_VST3_TEXT}")
endif()
set(LH_VST2_IMPL "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/juce_VSTPluginFormatHeadless.cpp")
file(READ "${LH_VST2_IMPL}" LH_VST2_TEXT)
if(NOT LH_VST2_TEXT MATCHES "LIGHTHOST_SCANNER_CATALOG")
    set(LH_SHELL_ANCHOR "            aboutToScanVSTShellPlugin (desc);")
    string(FIND "${LH_VST2_TEXT}" "${LH_SHELL_ANCHOR}" LH_SHELL_FOUND)
    if(LH_SHELL_FOUND EQUAL -1)
        message(FATAL_ERROR "JUCE VST2 shell catalog changed; review scanner adapter")
    endif()
    string(REPLACE "${LH_SHELL_ANCHOR}"
        "            if (SystemStats::getEnvironmentVariable (\"LIGHTHOST_SCANNER_CATALOG\", {}) == \"1\")\n            {\n                desc.hasSharedContainer = true;\n                results.add (new PluginDescription (desc));\n                continue;\n            }\n\n${LH_SHELL_ANCHOR}"
        LH_VST2_TEXT "${LH_VST2_TEXT}")
    file(WRITE "${LH_VST2_IMPL}" "${LH_VST2_TEXT}")
endif()
# JUCE Array has no public capacity query in 8.0.13. Add a read-only accessor to
# the build-owned copy; no layout, allocation policy or DSP behavior changes.


# Allow the user's diagnostics setting to stop JUCE's callback clock sampling,
# not just hide the resulting DSP-load value. The callback lock also serializes
# resets with a timer that was already admitted.
set(LIGHTHOST_DEVICE_HEADER "${juce_SOURCE_DIR}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.h")
set(LIGHTHOST_DEVICE_SOURCE "${juce_SOURCE_DIR}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")
file(READ "${LIGHTHOST_DEVICE_HEADER}" LIGHTHOST_DEVICE_HEADER_CONTENT)
if(NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "setDiagnosticsEnabled")
    if(NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "double getCpuUsage\\(\\) const;" OR
       NOT LIGHTHOST_DEVICE_HEADER_CONTENT MATCHES "AudioProcessLoadMeasurer loadMeasurer;")
        message(FATAL_ERROR "JUCE device manager changed; review diagnostics opt-out")
    endif()
    string(REPLACE "double getCpuUsage() const;"
        "double getCpuUsage() const;\n    void setDiagnosticsEnabled (bool enabled)\n    {\n        const ScopedLock lock (audioCallbackLock);\n        diagnosticsEnabled = enabled;\n        loadMeasurer.reset();\n        if (currentAudioDevice != nullptr)\n            loadMeasurer.reset (currentAudioDevice->getCurrentSampleRate(), currentAudioDevice->getCurrentBufferSizeSamples());\n    }"
        LIGHTHOST_DEVICE_HEADER_CONTENT "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
    string(REPLACE "AudioProcessLoadMeasurer loadMeasurer;" "AudioProcessLoadMeasurer loadMeasurer;\n    bool diagnosticsEnabled = true;"
        LIGHTHOST_DEVICE_HEADER_CONTENT "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
    file(WRITE "${LIGHTHOST_DEVICE_HEADER}" "${LIGHTHOST_DEVICE_HEADER_CONTENT}")
endif()
file(READ "${LIGHTHOST_DEVICE_SOURCE}" LIGHTHOST_DEVICE_SOURCE_CONTENT)
if(NOT LIGHTHOST_DEVICE_SOURCE_CONTENT MATCHES "if \\(diagnosticsEnabled\\) timer.emplace")
    set(LIGHTHOST_DEVICE_TIMER "AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);")
    string(FIND "${LIGHTHOST_DEVICE_SOURCE_CONTENT}" "${LIGHTHOST_DEVICE_TIMER}" LIGHTHOST_TIMER_POSITION)
    if(LIGHTHOST_TIMER_POSITION EQUAL -1)
        message(FATAL_ERROR "JUCE callback timer changed; review diagnostics opt-out")
    endif()
    string(REPLACE "${LIGHTHOST_DEVICE_TIMER}"
        "std::optional<AudioProcessLoadMeasurer::ScopedTimer> timer;\n        if (diagnosticsEnabled) timer.emplace (loadMeasurer, numSamples);"
        LIGHTHOST_DEVICE_SOURCE_CONTENT "${LIGHTHOST_DEVICE_SOURCE_CONTENT}")
    file(WRITE "${LIGHTHOST_DEVICE_SOURCE}" "${LIGHTHOST_DEVICE_SOURCE_CONTENT}")
endif()
set(LIGHTHOST_ARRAY_HEADER "${juce_SOURCE_DIR}/modules/juce_core/containers/juce_Array.h")
file(READ "${LIGHTHOST_ARRAY_HEADER}" LIGHTHOST_ARRAY_CONTENT)
if(NOT LIGHTHOST_ARRAY_CONTENT MATCHES "getAllocatedCapacity")
    set(LIGHTHOST_ARRAY_ANCHOR "    /** Increases the array's internal storage to hold a minimum number of elements.")
    string(FIND "${LIGHTHOST_ARRAY_CONTENT}" "${LIGHTHOST_ARRAY_ANCHOR}" LIGHTHOST_ARRAY_POSITION)
    if(LIGHTHOST_ARRAY_POSITION EQUAL -1)
        message(FATAL_ERROR "JUCE Array changed; review the bounded MIDI capacity adapter before building")
    endif()
    string(REPLACE "${LIGHTHOST_ARRAY_ANCHOR}"
        "    /** Light Host: read-only capacity query for allocation-free MIDI admission. */\n    int getAllocatedCapacity() const noexcept\n    {\n        const ScopedLockType lock (getLock());\n        return values.capacity();\n    }\n\n${LIGHTHOST_ARRAY_ANCHOR}"
        LIGHTHOST_ARRAY_CONTENT "${LIGHTHOST_ARRAY_CONTENT}")
    file(WRITE "${LIGHTHOST_ARRAY_HEADER}" "${LIGHTHOST_ARRAY_CONTENT}")
endif()
