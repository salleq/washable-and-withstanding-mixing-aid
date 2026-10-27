cmake_minimum_required(VERSION 3.22)

project(WWMA VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Finnish texts are UTF-8 in the source
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Universal binary on macOS (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Use a local JUCE checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Audio Unit as well on macOS, for Logic and GarageBand
if(APPLE)
    set(WWMA_FORMATS VST3 AU Standalone)
else()
    set(WWMA_FORMATS VST3 Standalone)
endif()

juce_add_plugin(WWMA
    COMPANY_NAME "salleq"
    COMPANY_WEBSITE "https://github.com/salleq/washable-withstandable-mixing-aid"
    BUNDLE_ID "com.salleq.wwma"
    PLUGIN_MANUFACTURER_CODE Sllq
    PLUGIN_CODE Wwma
    PRODUCT_NAME "WWMA Mixing Aid"
    DESCRIPTION "Washable Withstandable Mixing Aid - tells you what is too much or too little in your mix"
    FORMATS ${WWMA_FORMATS}
    VST3_CATEGORIES Fx Analyzer Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(WWMA_CORE_SOURCES
    Source/analysis/Analyzer.cpp
    Source/advice/Profiles.cpp
    Source/advice/Advisor.cpp
    Source/reference/ReferenceTracks.cpp)

set(WWMA_UI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Components.cpp)

# Fonts (SIL Open Font License), compiled into the plugin
juce_add_binary_data(WWMAAssets SOURCES
    assets/fonts/Barlow-Regular.ttf
    assets/fonts/Barlow-SemiBold.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/BarlowCondensed-Bold.ttf
    assets/fonts/IBMPlexMono-Medium.ttf
    assets/fonts/IBMPlexMono-SemiBold.ttf)
set_target_properties(WWMAAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(WWMA PRIVATE ${WWMA_CORE_SOURCES} ${WWMA_UI_SOURCES})
target_include_directories(WWMA PRIVATE Source)

target_compile_definitions(WWMA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(WWMA
    PRIVATE
        WWMAAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Developer tool: measurement accuracy tests, advice scenarios and UI screenshots
option(WWMA_BUILD_TOOLS "Build the self test / screenshot tool" OFF)
if(WWMA_BUILD_TOOLS)
    juce_add_console_app(wwma_selftest PRODUCT_NAME "wwma_selftest")
    target_sources(wwma_selftest PRIVATE tools/SelfTest.cpp ${WWMA_CORE_SOURCES} ${WWMA_UI_SOURCES})
    target_include_directories(wwma_selftest PRIVATE Source)
    target_compile_definitions(wwma_selftest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="WWMA" JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(wwma_selftest PRIVATE
        WWMAAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
