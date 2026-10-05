cmake_minimum_required(VERSION 3.22)

project(LightHostModern VERSION 2.0.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LightHostModern is now maintained as a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LIGHTHOST_REALTIME_AUDIT "Instrument this host executable's callback allocations in Release validation builds" OFF)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME release-performance-report-analysis COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_performance_analysis.py")
        set_tests_properties(release-performance-report-analysis PROPERTIES TIMEOUT 30)
    endif()
    add_executable(LightHostModernCallbackMeasurementTests Tests/CallbackMeasurementTests.cpp)
    target_include_directories(LightHostModernCallbackMeasurementTests PRIVATE Source)
    add_test(NAME callback-performance-measurement COMMAND LightHostModernCallbackMeasurementTests)
    set_tests_properties(callback-performance-measurement PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernMeterTests Tests/MeterTests.cpp)
    target_include_directories(LightHostModernMeterTests PRIVATE Source)
    add_test(NAME audio-rms-peak-and-clipping COMMAND LightHostModernMeterTests)
    add_executable(LightHostModernRuntimeProfileTests Tests/RuntimeProfileTests.cpp)
    target_include_directories(LightHostModernRuntimeProfileTests PRIVATE Source)
    add_test(NAME runtime-profile-isolation COMMAND LightHostModernRuntimeProfileTests)
    set_tests_properties(runtime-profile-isolation PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernOperationRegistryTests Tests/OperationRegistryTests.cpp)
    target_include_directories(LightHostModernOperationRegistryTests PRIVATE Source)
    add_test(NAME ipc-operation-retention COMMAND LightHostModernOperationRegistryTests)
    set_tests_properties(ipc-operation-retention PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernIpcTests Tests/IpcTests.cpp)
    target_include_directories(LightHostModernIpcTests PRIVATE Source)
    add_test(NAME ipc-lifetime-and-transport COMMAND LightHostModernIpcTests)
    set_tests_properties(ipc-lifetime-and-transport PROPERTIES TIMEOUT 15)
    if(MSVC)
        add_executable(LightHostModernWinUiTransportTests Tests/WinUiTransportTests.cpp)
        set(_program_files_x86 "ProgramFiles(x86)")
        find_path(LIGHTHOST_CPPWINRT_INCLUDE_DIR winrt/Windows.Foundation.h
            HINTS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
                  "$ENV{${_program_files_x86}}/Windows Kits/10/Include/${CMAKE_SYSTEM_VERSION}/cppwinrt"
            REQUIRED)
        target_include_directories(LightHostModernWinUiTransportTests PRIVATE WinUI/LightHostModern.WinUI "${LIGHTHOST_CPPWINRT_INCLUDE_DIR}")
        target_link_libraries(LightHostModernWinUiTransportTests PRIVATE windowsapp)
        set_property(TARGET LightHostModernWinUiTransportTests PROPERTY CXX_STANDARD 20)
        target_compile_options(LightHostModernWinUiTransportTests PRIVATE /utf-8)
        add_test(NAME winui-json-and-async-transport COMMAND LightHostModernWinUiTransportTests)
        set_tests_properties(winui-json-and-async-transport PROPERTIES TIMEOUT 20)
        add_executable(LightHostModernLateTransportTests Tests/LateTransportTests.cpp)
        target_include_directories(LightHostModernLateTransportTests PRIVATE WinUI/LightHostModern.WinUI "${LIGHTHOST_CPPWINRT_INCLUDE_DIR}")
        target_link_libraries(LightHostModernLateTransportTests PRIVATE windowsapp)
        set_property(TARGET LightHostModernLateTransportTests PROPERTY CXX_STANDARD 20)
        target_compile_options(LightHostModernLateTransportTests PRIVATE /utf-8)
        add_test(NAME winui-late-operation-reconciliation COMMAND LightHostModernLateTransportTests)
        set_tests_properties(winui-late-operation-reconciliation PROPERTIES TIMEOUT 20)
    endif()
endif()

include(FetchContent)

set(LIGHTHOST_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
set(LIGHTHOST_VST3_SDK_DIR "" CACHE PATH "Optional local VST3 SDK checkout")
set(LIGHTHOST_VST2_SDK_DIR "" CACHE PATH "Optional local VST2 SDK root. Must contain pluginterfaces/vst2.x/aeffect.h")
set(LIGHTHOST_ENABLE_VST2 "AUTO" CACHE STRING "VST2 host support: AUTO, ON, or OFF")
set_property(CACHE LIGHTHOST_ENABLE_VST2 PROPERTY STRINGS AUTO ON OFF)
set(LIGHTHOST_VST2_PROVIDER "XAYMAR" CACHE STRING "VST2 header provider: LEGACY or XAYMAR")
set_property(CACHE LIGHTHOST_VST2_PROVIDER PROPERTY STRINGS LEGACY XAYMAR)
set(LIGHTHOST_ASIO_SDK_DIR "" CACHE PATH "Optional local ASIO SDK checkout")

if(LIGHTHOST_JUCE_DIR)
    # Local source checkouts remain untouched by the bounded MIDI accessor.
    set(juce_SOURCE_DIR "${CMAKE_BINARY_DIR}/juce-lighthost")
    file(COPY "${LIGHTHOST_JUCE_DIR}/" DESTINATION "${juce_SOURCE_DIR}" PATTERN ".git" EXCLUDE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Utilities/PatchJuce.cmake")
    add_subdirectory("${juce_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2) # JUCE 8.0.13
    FetchContent_MakeAvailable(juce)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Utilities/PatchJuce.cmake")
endif()

if(LIGHTHOST_VST3_SDK_DIR)
    set(vst3sdk_SOURCE_DIR "${LIGHTHOST_VST3_SDK_DIR}")
else()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG 9fad9770f2ae8542ab1a548a68c1ad1ac690abe0 # VST3 3.8.0 build 66
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(vst3sdk)
endif()

juce_set_vst3_sdk_path("${vst3sdk_SOURCE_DIR}")

if(BUILD_TESTING)
    add_subdirectory(Tests/ScenarioPlugin scn EXCLUDE_FROM_ALL)
endif()

if(BUILD_TESTING)
    add_executable(LightHostModernPluginInstanceTests Tests/PluginInstanceTests.cpp)
    add_executable(LightHostModernPreferenceMigrationTests Tests/PreferenceMigrationTests.cpp)
    target_include_directories(LightHostModernPreferenceMigrationTests PRIVATE Source)
    target_compile_definitions(LightHostModernPreferenceMigrationTests PRIVATE JUCE_USE_CURL=0 NOMINMAX)
    target_link_libraries(LightHostModernPreferenceMigrationTests PRIVATE juce::juce_data_structures juce::juce_cryptography)
    add_test(NAME product-preference-migration COMMAND LightHostModernPreferenceMigrationTests)
    target_include_directories(LightHostModernPluginInstanceTests PRIVATE Source)
    target_compile_definitions(LightHostModernPluginInstanceTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_compile_options(LightHostModernPluginInstanceTests PRIVATE /utf-8)
    target_link_libraries(LightHostModernPluginInstanceTests PRIVATE juce::juce_audio_processors_headless juce::juce_cryptography)
    add_test(NAME plugin-instance-identity-and-legacy-adapter COMMAND LightHostModernPluginInstanceTests)
    set_tests_properties(plugin-instance-identity-and-legacy-adapter PROPERTIES TIMEOUT 20)
    add_executable(LightHostModernSessionTests Tests/SessionPersistenceTests.cpp Source/SessionStore.cpp)
    target_include_directories(LightHostModernSessionTests PRIVATE Source)
    target_compile_definitions(LightHostModernSessionTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_compile_options(LightHostModernSessionTests PRIVATE /utf-8)
    target_link_libraries(LightHostModernSessionTests PRIVATE juce::juce_audio_processors_headless juce::juce_cryptography)
    add_test(NAME session-atomic-persistence-and-recovery COMMAND LightHostModernSessionTests)
    set_tests_properties(session-atomic-persistence-and-recovery PROPERTIES TIMEOUT 30)
    add_executable(LightHostModernProtocolTests Tests/ProtocolTests.cpp)
    target_include_directories(LightHostModernProtocolTests PRIVATE Source)
    target_compile_definitions(LightHostModernProtocolTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostModernProtocolTests PRIVATE juce::juce_core)
    add_test(NAME ipc-versioned-protocol COMMAND LightHostModernProtocolTests)

    add_executable(LightHostModernStateSyncTests Tests/StateSyncTests.cpp)
    target_include_directories(LightHostModernStateSyncTests PRIVATE Source)
    target_compile_definitions(LightHostModernStateSyncTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostModernStateSyncTests PRIVATE juce::juce_core)
    add_test(NAME state-events-and-immutable-snapshots COMMAND LightHostModernStateSyncTests)
    set_tests_properties(ipc-versioned-protocol PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernDevicePolicyTests Tests/DevicePolicyTests.cpp)
    target_include_directories(LightHostModernDevicePolicyTests PRIVATE Source)
    target_compile_definitions(LightHostModernDevicePolicyTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostModernDevicePolicyTests PRIVATE juce::juce_audio_devices)
    add_test(NAME device-creation-policy COMMAND LightHostModernDevicePolicyTests)
    set_tests_properties(device-creation-policy PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernDeviceRecoveryTests Tests/DeviceRecoveryTests.cpp Source/DeviceController.cpp)
    target_include_directories(LightHostModernDeviceRecoveryTests PRIVATE Source)
    target_compile_definitions(LightHostModernDeviceRecoveryTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostModernDeviceRecoveryTests PRIVATE juce::juce_audio_devices juce::juce_data_structures juce::juce_cryptography)
    add_test(NAME device-recovery-scenarios COMMAND LightHostModernDeviceRecoveryTests)
    set_tests_properties(device-recovery-scenarios PROPERTIES TIMEOUT 20)
endif()

string(TOUPPER "${LIGHTHOST_VST2_PROVIDER}" LIGHTHOST_VST2_PROVIDER_UPPER)
if(NOT LIGHTHOST_VST2_PROVIDER_UPPER MATCHES "^(LEGACY|XAYMAR)$")
    message(FATAL_ERROR "LIGHTHOST_VST2_PROVIDER must be LEGACY or XAYMAR.")
endif()

set(LIGHTHOST_VST2_INCLUDE_DIRS "")
set(LIGHTHOST_VST2_SEARCH_DIRS)
if(LIGHTHOST_VST2_PROVIDER_UPPER STREQUAL "XAYMAR")
    FetchContent_Declare(xaymar_vst2sdk
        URL https://github.com/Xaymar/vst2sdk/archive/refs/tags/v0.4.0.zip
        URL_HASH SHA256=de2bad3db73f5e062f3b9a258207a7eacec396ecf069f2f029f7c6424f85896b
        SOURCE_SUBDIR cmake-skip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(xaymar_vst2sdk)

    set(LIGHTHOST_VST2_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/XaymarVST2JuceShim/include"
        "${xaymar_vst2sdk_SOURCE_DIR}/include")

    if(NOT EXISTS "${xaymar_vst2sdk_SOURCE_DIR}/include/vst.h")
        message(FATAL_ERROR "Xaymar vst2sdk was not fetched correctly. Expected include/vst.h.")
    endif()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/XaymarVST2JuceShim/include/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "Xaymar VST2 JUCE shim headers are missing.")
    endif()
else()
    if(LIGHTHOST_VST2_SDK_DIR)
        list(APPEND LIGHTHOST_VST2_SEARCH_DIRS "${LIGHTHOST_VST2_SDK_DIR}")
    endif()
    if(DEFINED ENV{LIGHTHOST_VST2_SDK_DIR})
        list(APPEND LIGHTHOST_VST2_SEARCH_DIRS "$ENV{LIGHTHOST_VST2_SDK_DIR}")
    endif()
    list(APPEND LIGHTHOST_VST2_SEARCH_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/VST2_SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/vst2"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VST2_SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2"
        "${vst3sdk_SOURCE_DIR}")

    foreach(LIGHTHOST_VST2_CANDIDATE IN LISTS LIGHTHOST_VST2_SEARCH_DIRS)
        if(LIGHTHOST_VST2_INCLUDE_DIRS)
            break()
        endif()

        if(EXISTS "${LIGHTHOST_VST2_CANDIDATE}/pluginterfaces/vst2.x/aeffect.h"
                AND EXISTS "${LIGHTHOST_VST2_CANDIDATE}/pluginterfaces/vst2.x/aeffectx.h")
            set(LIGHTHOST_VST2_INCLUDE_DIRS "${LIGHTHOST_VST2_CANDIDATE}")
        elseif(EXISTS "${LIGHTHOST_VST2_CANDIDATE}/aeffect.h"
                AND EXISTS "${LIGHTHOST_VST2_CANDIDATE}/aeffectx.h"
                AND LIGHTHOST_VST2_CANDIDATE MATCHES "pluginterfaces[/\\]vst2\\.x$")
            get_filename_component(LIGHTHOST_VST2_PLUGINTERFACES_DIR "${LIGHTHOST_VST2_CANDIDATE}" DIRECTORY)
            get_filename_component(LIGHTHOST_VST2_INCLUDE_DIRS "${LIGHTHOST_VST2_PLUGINTERFACES_DIR}" DIRECTORY)
        endif()
    endforeach()
endif()

if(LIGHTHOST_VST2_INCLUDE_DIRS AND LIGHTHOST_VST2_PROVIDER_UPPER STREQUAL "LEGACY")
    list(GET LIGHTHOST_VST2_INCLUDE_DIRS 0 LIGHTHOST_VST2_LEGACY_INCLUDE_DIR)
    set(LIGHTHOST_VST2_SDK_DIR "${LIGHTHOST_VST2_LEGACY_INCLUDE_DIR}" CACHE PATH "Optional local VST2 SDK root. Must contain pluginterfaces/vst2.x/aeffect.h" FORCE)
endif()

string(TOUPPER "${LIGHTHOST_ENABLE_VST2}" LIGHTHOST_ENABLE_VST2_UPPER)
if(NOT LIGHTHOST_ENABLE_VST2_UPPER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "LIGHTHOST_ENABLE_VST2 must be AUTO, ON, or OFF.")
endif()

set(LIGHTHOST_PLUGINHOST_VST2 0)
if(LIGHTHOST_ENABLE_VST2_UPPER STREQUAL "ON")
    if(NOT LIGHTHOST_VST2_INCLUDE_DIRS)
        message(FATAL_ERROR "VST2 support was requested, but aeffect.h/aeffectx.h were not found. Set LIGHTHOST_VST2_SDK_DIR to a folder containing pluginterfaces/vst2.x.")
    endif()
    set(LIGHTHOST_PLUGINHOST_VST2 1)
elseif(LIGHTHOST_ENABLE_VST2_UPPER STREQUAL "AUTO" AND LIGHTHOST_VST2_INCLUDE_DIRS)
    set(LIGHTHOST_PLUGINHOST_VST2 1)
endif()

if(LIGHTHOST_PLUGINHOST_VST2)
    message(STATUS "VST2 host support enabled using ${LIGHTHOST_VST2_PROVIDER_UPPER}: ${LIGHTHOST_VST2_INCLUDE_DIRS}")
else()
    message(STATUS "VST2 host support disabled. Set LIGHTHOST_ENABLE_VST2=ON and configure LIGHTHOST_VST2_PROVIDER/LIGHTHOST_VST2_SDK_DIR to enable it.")
endif()

if(LIGHTHOST_ASIO_SDK_DIR)
    set(asio_sdk_SOURCE_DIR "${LIGHTHOST_ASIO_SDK_DIR}")
else()
    FetchContent_Declare(asio_sdk
        GIT_REPOSITORY https://github.com/audiosdk/asio.git
        GIT_TAG 496a0765b8bb9c26f764f22f9a9712a937177db2
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(asio_sdk)
endif()

set(LIGHTHOST_ASIO_INCLUDE_DIR "${asio_sdk_SOURCE_DIR}/common")
if(NOT EXISTS "${LIGHTHOST_ASIO_INCLUDE_DIR}/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK not found. Expected iasiodrv.h in ${LIGHTHOST_ASIO_INCLUDE_DIR}")
endif()

add_library(LightHostModernCore STATIC)

target_sources(LightHostModernCore
    PRIVATE
        Source/AudioEngine.cpp
        Source/IsolatedPlugin.cpp
        Source/OperatingProfiles.cpp
        Source/SessionStore.cpp
        Source/DeviceController.cpp
        Source/LightHostModernController.cpp
        Source/PluginWindow.cpp
        Source/RealtimeHostProcessor.cpp
        Source/PluginScanController.cpp)

target_include_directories(LightHostModernCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

target_compile_definitions(LightHostModernCore
    PUBLIC
        JUCE_ASIO=1
        JUCE_ASIO_USE_EXTERNAL_SDK=1
        JUCE_CONTENT_SHARING=0
        JUCE_DIRECTSOUND=1
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=${LIGHTHOST_PLUGINHOST_VST2}
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_CAMERA=0
        JUCE_USE_CDBURNER=0
        JUCE_USE_CDREADER=0
        JUCE_USE_CURL=0
        JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
        JUCE_WASAPI=1
        JUCE_WEB_BROWSER=0)

target_link_libraries(LightHostModernCore
    PUBLIC
        juce::juce_audio_processors
        juce::juce_cryptography
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_gui_app(LightHostModern
    BUNDLE_ID "com.lighthost.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Icon/logo.png"
    COMPANY_NAME "LightHostModern"
    PRODUCT_NAME "LightHostModern")

if(BUILD_TESTING)
    add_executable(LightHostModernRealtimeTests Tests/RealtimeTests.cpp Source/RealtimeAllocationAudit.cpp)
    target_link_libraries(LightHostModernRealtimeTests PRIVATE LightHostModernCore)
    target_include_directories(LightHostModernRealtimeTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_test(NAME realtime-channels-segments-and-bypass COMMAND LightHostModernRealtimeTests)
    set_tests_properties(realtime-channels-segments-and-bypass PROPERTIES TIMEOUT 30)
    # Opt-in real modules run in their own process under the Python deadline.
    # CTest remains self-contained and never downloads third-party binaries.
    add_executable(LightHostModernRealPluginTests EXCLUDE_FROM_ALL Tests/RealPluginTests.cpp Source/RealtimeAllocationAudit.cpp)
    target_link_libraries(LightHostModernRealPluginTests PRIVATE LightHostModernCore)
    target_include_directories(LightHostModernRealPluginTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
endif()

juce_add_console_app(LightHostModernScanner PRODUCT_NAME "LightHostModernScanner")
target_sources(LightHostModernScanner PRIVATE Source/ScannerMain.cpp)
target_link_libraries(LightHostModernScanner PRIVATE LightHostModernCore)
target_include_directories(LightHostModernScanner PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
set_target_properties(LightHostModernScanner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHostModern_artefacts/$<CONFIG>")
add_dependencies(LightHostModern LightHostModernScanner)

juce_add_console_app(LightHostModernWorker PRODUCT_NAME "LightHostModernWorker")
target_sources(LightHostModernWorker PRIVATE Source/PluginWorkerMain.cpp)
target_link_libraries(LightHostModernWorker PRIVATE LightHostModernCore avrt)
target_include_directories(LightHostModernWorker PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
target_compile_definitions(LightHostModernWorker PRIVATE LIGHTHOST_WORKER_TEST_FIXTURES=0)
set_target_properties(LightHostModernWorker PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHostModern_artefacts/$<CONFIG>")
add_dependencies(LightHostModern LightHostModernWorker)
if(BUILD_TESTING)
    juce_add_console_app(LightHostModernWorkerFixture PRODUCT_NAME "LightHostModernWorkerFixture")
    target_sources(LightHostModernWorkerFixture PRIVATE Source/PluginWorkerMain.cpp)
    target_link_libraries(LightHostModernWorkerFixture PRIVATE LightHostModernCore avrt)
    target_include_directories(LightHostModernWorkerFixture PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    target_compile_definitions(LightHostModernWorkerFixture PRIVATE LIGHTHOST_WORKER_TEST_FIXTURES=1)
    add_executable(LightHostModernIsolatedPluginTests Tests/IsolatedPluginTests.cpp Source/RealtimeAllocationAudit.cpp)
    target_link_libraries(LightHostModernIsolatedPluginTests PRIVATE LightHostModernCore)
    target_include_directories(LightHostModernIsolatedPluginTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_dependencies(LightHostModernIsolatedPluginTests LightHostModernWorkerFixture)
    add_test(NAME isolated-plugin-crash-hang-and-audio COMMAND LightHostModernIsolatedPluginTests "$<TARGET_FILE:LightHostModernWorkerFixture>")
    set_tests_properties(isolated-plugin-crash-hang-and-audio PROPERTIES TIMEOUT 100)
    add_executable(LightHostModernIsolatedRealPluginTests EXCLUDE_FROM_ALL Tests/IsolatedRealPluginTests.cpp)
    target_link_libraries(LightHostModernIsolatedRealPluginTests PRIVATE LightHostModernCore)
    target_include_directories(LightHostModernIsolatedRealPluginTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_dependencies(LightHostModernIsolatedRealPluginTests LightHostModernWorker)
endif()

add_library(LightHostModernInstallerActions SHARED Source/InstallerActions.cpp)
target_link_libraries(LightHostModernInstallerActions PRIVATE msi)
target_compile_options(LightHostModernInstallerActions PRIVATE /utf-8)
set_target_properties(LightHostModernInstallerActions PROPERTIES
    CXX_STANDARD 17 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/installer-actions/$<CONFIG>")
add_dependencies(LightHostModern LightHostModernInstallerActions)

juce_add_console_app(LightHostModernUpdateHelper PRODUCT_NAME "LightHostModernUpdateHelper")
target_sources(LightHostModernUpdateHelper PRIVATE Source/UpdateHelperMain.cpp)
target_include_directories(LightHostModernUpdateHelper PRIVATE Source)
target_compile_definitions(LightHostModernUpdateHelper PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX LIGHTHOST_APP_VERSION="${PROJECT_VERSION}")
target_compile_options(LightHostModernUpdateHelper PRIVATE /utf-8)
target_link_libraries(LightHostModernUpdateHelper PRIVATE juce::juce_core msi bcrypt winhttp shell32 version)
set_target_properties(LightHostModernUpdateHelper PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHostModern_artefacts/$<CONFIG>")
add_dependencies(LightHostModern LightHostModernUpdateHelper)

juce_add_gui_app(LightHostModernLauncher PRODUCT_NAME "LightHostModernLauncher" ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Icon/logo.png")
target_sources(LightHostModernLauncher PRIVATE Source/PortableLauncherMain.cpp)
target_include_directories(LightHostModernLauncher PRIVATE Source)
target_compile_definitions(LightHostModernLauncher PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX)
target_compile_options(LightHostModernLauncher PRIVATE /utf-8)
target_link_libraries(LightHostModernLauncher PRIVATE juce::juce_core msi bcrypt winhttp shell32)
set_target_properties(LightHostModernLauncher PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHostModern_artefacts/$<CONFIG>")
add_dependencies(LightHostModern LightHostModernLauncher)

add_executable(LightHostModernLegacyLauncher WIN32 Source/LegacyLauncher.cpp)
set_target_properties(LightHostModernLegacyLauncher PROPERTIES OUTPUT_NAME "Light Host Modern"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHostModern_artefacts/$<CONFIG>")
foreach(legacy_name IN ITEMS LightHostScanner LightHostUpdateHelper LightHostWinUI)
    add_custom_command(TARGET LightHostModernLegacyLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:LightHostModernLegacyLauncher>"
            "$<TARGET_FILE_DIR:LightHostModernLegacyLauncher>/${legacy_name}.exe")
endforeach()
add_dependencies(LightHostModern LightHostModernLegacyLauncher)

# JUCE's resource command does not track Info.txt itself. Regenerate version
# resources when project metadata changes, including in incremental builds.
foreach(app_target IN ITEMS LightHostModern LightHostModernScanner LightHostModernWorker LightHostModernUpdateHelper LightHostModernLauncher)
    get_target_property(app_generated_sources ${app_target} JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(app_info_file ${app_target} JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${app_generated_sources}/${app_target}_resources.rc"
        APPEND DEPENDS "${app_info_file}")
endforeach()

if(BUILD_TESTING)
    add_executable(LightHostModernUpdateTests Tests/UpdateTests.cpp)
    # Local portable upgrade integration only: the release helper refuses its
    # own version, so exercise its unchanged source with an older fixture gate.
    # This target has no production dependency or packaging/staging rule.
    add_executable(LightHostModernUpdateHelperBaselineFixture Source/UpdateHelperMain.cpp)
    target_include_directories(LightHostModernUpdateHelperBaselineFixture PRIVATE Source)
    target_compile_definitions(LightHostModernUpdateHelperBaselineFixture PRIVATE
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX LIGHTHOST_APP_VERSION="1.9.9")
    target_compile_options(LightHostModernUpdateHelperBaselineFixture PRIVATE /utf-8)
    target_link_libraries(LightHostModernUpdateHelperBaselineFixture PRIVATE juce::juce_core msi bcrypt winhttp shell32 version)
    set_target_properties(LightHostModernUpdateHelperBaselineFixture PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-fixtures/$<CONFIG>")
    add_executable(LightHostModernLegacyMigrationTests Tests/LegacyMigrationTests.cpp)
    target_include_directories(LightHostModernLegacyMigrationTests PRIVATE Source)
    target_compile_definitions(LightHostModernLegacyMigrationTests PRIVATE JUCE_USE_CURL=0 NOMINMAX)
    target_link_libraries(LightHostModernLegacyMigrationTests PRIVATE juce::juce_core msi bcrypt winhttp shell32 version)
    add_test(NAME legacy-cleanup-boundaries COMMAND LightHostModernLegacyMigrationTests)
    target_include_directories(LightHostModernUpdateTests PRIVATE Source)
    target_compile_definitions(LightHostModernUpdateTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX)
    target_compile_options(LightHostModernUpdateTests PRIVATE /utf-8)
    target_link_libraries(LightHostModernUpdateTests PRIVATE juce::juce_core msi bcrypt winhttp shell32)
    add_test(NAME update-streaming-validation-and-application COMMAND LightHostModernUpdateTests)
    set_tests_properties(update-streaming-validation-and-application PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    add_executable(LightHostModernScanProcessTests Tests/ScanProcessTests.cpp)
    add_executable(LightHostModernUiLifetimeTests Tests/UiProcessLifetimeTests.cpp)
    target_include_directories(LightHostModernUiLifetimeTests PRIVATE Source)
    add_test(NAME ui-process-lifetime COMMAND LightHostModernUiLifetimeTests)
    set_tests_properties(ui-process-lifetime PROPERTIES TIMEOUT 20)
    add_executable(LightHostModernVerboseLogTests Tests/VerboseLogTests.cpp)
    target_include_directories(LightHostModernVerboseLogTests PRIVATE Source)
    add_test(NAME verbose-capture-lifecycle-and-export COMMAND LightHostModernVerboseLogTests)
    target_include_directories(LightHostModernScanProcessTests PRIVATE Source)
    add_test(NAME scanner-process-lifetime COMMAND LightHostModernScanProcessTests)
    set_tests_properties(scanner-process-lifetime PROPERTIES TIMEOUT 15)
    add_executable(LightHostModernScanControllerTests Tests/ScanControllerTests.cpp)
    add_executable(LightHostModernScanCacheTests Tests/ScanCacheJournalTests.cpp)
    target_link_libraries(LightHostModernScanCacheTests PRIVATE LightHostModernCore)
    target_include_directories(LightHostModernScanCacheTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_test(NAME scanner-cache-journal-recovery COMMAND LightHostModernScanCacheTests)
    target_link_libraries(LightHostModernScanControllerTests PRIVATE LightHostModernCore Advapi32)
    target_include_directories(LightHostModernScanControllerTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_test(NAME scanner-controller-results COMMAND LightHostModernScanControllerTests)
    set_tests_properties(scanner-controller-results PROPERTIES TIMEOUT 30)
endif()

juce_generate_juce_header(LightHostModern)
set_target_properties(LightHostModern PROPERTIES OUTPUT_NAME "LightHostModern")

target_sources(LightHostModern
    PRIVATE
        Source/DebugLog.cpp
        Source/HostIpcServer.cpp
        Source/HostStartup.cpp
        Source/IconMenu.cpp
        Source/PluginTrayMenu.cpp)

if(LIGHTHOST_REALTIME_AUDIT)
    target_sources(LightHostModern PRIVATE Source/RealtimeAllocationAudit.cpp)
    target_compile_definitions(LightHostModern PRIVATE LIGHTHOST_REALTIME_AUDIT=1)
endif()

target_include_directories(LightHostModern
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

juce_add_binary_data(LightHostModernData
    SOURCES
        Icon/logo.png
        Icon/logo-black.png
        Icon/logo-white.png)

juce_add_binary_data(LightHostModernLocales
    NAMESPACE LightHostModernLocales
    HEADER_NAME LightHostModernLocales.h
    SOURCES WinUI/LightHostModern.WinUI/Locales/en-us.json WinUI/LightHostModern.WinUI/Locales/pt-br.json)

target_compile_definitions(LightHostModern
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LightHostModern,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LightHostModern,JUCE_VERSION>")

target_link_libraries(LightHostModern
    PRIVATE
        LightHostModernCore
        LightHostModernData
        LightHostModernLocales
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    set(LIGHTHOST_WINUI_TARGET_DIR
        "$<TARGET_FILE_DIR:LightHostModern>/WinUI/x64/$<CONFIG>/LightHostModern.WinUI")

    # Staging must also run when only WinUI changed and the host does not relink.
    # CMP0112 (NEW through cmake_minimum_required) makes TARGET_FILE_DIR a
    # directory query, so this prerequisite does not create a target cycle.
    add_custom_target(LightHostModernStageWinUI
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/WinUI Output.ps1" -Mode Stage -Platform x64 -Configuration $<CONFIG>
            -Destination "${LIGHTHOST_WINUI_TARGET_DIR}"
        COMMENT "Verifying and staging the current WinUI shell"
        VERBATIM)
    add_dependencies(LightHostModern LightHostModernStageWinUI)
endif()
