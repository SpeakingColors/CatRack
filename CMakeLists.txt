cmake_minimum_required(VERSION 3.22)
# The release workflow rewrites this version from the tag (sed), so menu, installer and release agree.
project(CatRack VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(CatRAssets SOURCES
    assets/fonts/BarlowCondensed-Medium.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/BarlowCondensed-Bold.ttf
    assets/fonts/BarlowCondensed-ExtraBoldItalic.ttf
    assets/fonts/JetBrainsMono-Regular.ttf)
set_target_properties(CatRAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(CatRack
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Stu8
    PLUGIN_CODE Rack
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    PRODUCT_NAME "CatRack")

set(CATR_SOURCES
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/core/Dsp.h
    src/core/Units.h
    src/core/Rack.h
    src/core/UnitData.h
    src/core/Fmt.h
    src/editor/PluginEditor.cpp
    src/editor/PluginEditor.h
    src/editor/Presets.cpp
    src/editor/Presets.h
    src/editor/Dialogs.cpp
    src/editor/Dialogs.h
    src/editor/Theme.h
    src/editor/Layout.h
    src/editor/Widgets.h
    src/editor/Views.h
    src/editor/Panels.h
    src/editor/FactoryData.h)
target_sources(CatRack PRIVATE ${CATR_SOURCES})

target_compile_definitions(CatRack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(CatRack
    PRIVATE CatRAssets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offscreen window renderer for checking the editor against the mockup (not part of releases).
option(CATR_BUILD_SNAPSHOT "Build the editor snapshot tool" OFF)
if(CATR_BUILD_SNAPSHOT)
    juce_add_console_app(CatRSnapshot PRODUCT_NAME "CatRSnapshot")
    target_sources(CatRSnapshot PRIVATE tools/snapshot.cpp ${CATR_SOURCES})
    target_compile_definitions(CatRSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="CatRack" JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(CatRSnapshot PRIVATE CatRAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
