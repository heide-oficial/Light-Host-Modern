cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.1.1 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Light Host Modern is now maintained as a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    add_subdirectory("${LIGHTHOST_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(LIGHTHOST_VST3_SDK_DIR)
    set(vst3sdk_SOURCE_DIR "${LIGHTHOST_VST3_SDK_DIR}")
else()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(vst3sdk)
endif()

juce_set_vst3_sdk_path("${vst3sdk_SOURCE_DIR}")

string(TOUPPER "${LIGHTHOST_VST2_PROVIDER}" LIGHTHOST_VST2_PROVIDER_UPPER)
if(NOT LIGHTHOST_VST2_PROVIDER_UPPER MATCHES "^(LEGACY|XAYMAR)$")
    message(FATAL_ERROR "LIGHTHOST_VST2_PROVIDER must be LEGACY or XAYMAR.")
endif()

set(LIGHTHOST_VST2_INCLUDE_DIRS "")
set(LIGHTHOST_VST2_SEARCH_DIRS)
if(LIGHTHOST_VST2_PROVIDER_UPPER STREQUAL "XAYMAR")
    FetchContent_Declare(xaymar_vst2sdk
        URL https://github.com/Xaymar/vst2sdk/archive/refs/tags/v0.4.0.zip
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
        GIT_TAG 496a076
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(asio_sdk)
endif()

set(LIGHTHOST_ASIO_INCLUDE_DIR "${asio_sdk_SOURCE_DIR}/common")
if(NOT EXISTS "${LIGHTHOST_ASIO_INCLUDE_DIR}/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK not found. Expected iasiodrv.h in ${LIGHTHOST_ASIO_INCLUDE_DIR}")
endif()

add_library(LightHostCore STATIC)

target_sources(LightHostCore
    PRIVATE
        Source/AudioEngine.cpp
        Source/LightHostController.cpp
        Source/PluginWindow.cpp
        Source/RealtimeHostProcessor.cpp)

target_include_directories(LightHostCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

target_compile_definitions(LightHostCore
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

target_link_libraries(LightHostCore
    PUBLIC
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_gui_app(LightHost
    BUNDLE_ID "com.lighthost.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Icon/logo.png"
    COMPANY_NAME "Light Host Modern"
    PRODUCT_NAME "Light Host Modern")

juce_generate_juce_header(LightHost)
set_target_properties(LightHost PROPERTIES OUTPUT_NAME "Light Host Modern")

target_sources(LightHost
    PRIVATE
        Source/DebugLog.cpp
        Source/HostIpcServer.cpp
        Source/HostStartup.cpp
        Source/IconMenu.cpp)

target_include_directories(LightHost
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

juce_add_binary_data(LightHostData
    SOURCES
        Icon/logo.png
        Icon/logo-black.png
        Icon/logo-white.png)

target_compile_definitions(LightHost
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LightHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LightHost,JUCE_VERSION>")

target_link_libraries(LightHost
    PRIVATE
        LightHostCore
        LightHostData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    set(LIGHTHOST_WINUI_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/WinUI/LightHost.WinUI/x64/$<CONFIG>/LightHost.WinUI")
    set(LIGHTHOST_WINUI_TARGET_DIR
        "$<TARGET_FILE_DIR:LightHost>/WinUI/x64/$<CONFIG>/LightHost.WinUI")

    add_custom_command(TARGET LightHost POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "if (Test-Path -LiteralPath '${LIGHTHOST_WINUI_SOURCE_DIR}') { New-Item -ItemType Directory -Force -Path '${LIGHTHOST_WINUI_TARGET_DIR}' | Out-Null; Copy-Item -Path '${LIGHTHOST_WINUI_SOURCE_DIR}\\*' -Destination '${LIGHTHOST_WINUI_TARGET_DIR}' -Recurse -Force } else { Write-Host 'WinUI output not found; build WinUI/LightHost.WinUI.sln before packaging release.' }"
        COMMENT "Copying Light Host Modern WinUI shell into the host output folder")
endif()
