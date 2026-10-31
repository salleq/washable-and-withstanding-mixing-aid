cmake_minimum_required(VERSION 3.22)

project(WWMA VERSION 0.3.1 LANGUAGES C CXX)

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
elseif(UNIX)
    set(WWMA_FORMATS VST3 LV2 Standalone) # LV2 for Ardour, Carla, Qtractor and friends
else()
    set(WWMA_FORMATS VST3 Standalone)
endif()

juce_add_plugin(WWMA
    COMPANY_NAME "salleq"
    COMPANY_WEBSITE "https://github.com/salleq/washable-and-withstanding-mixing-aid"
    BUNDLE_ID "com.salleq.wwma"
    PLUGIN_MANUFACTURER_CODE Sllq
    PLUGIN_CODE Wwma
    PRODUCT_NAME "WWMA Mixing Aid"
    DESCRIPTION "Washable and Withstanding Mixing Aid - tells you what is too much or too little in your mix"
    FORMATS ${WWMA_FORMATS}
    VST3_CATEGORIES Fx Analyzer Tools
    LV2URI "https://github.com/salleq/washable-withstandable-mixing-aid"  # fixed plugin ID, kept from the first repo name so LV2 hosts still find the plugin
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# CLAP (Bitwig, Reaper, FL Studio...) through clap-juce-extensions
option(WWMA_CLAP "Build the CLAP format" ON)
set(CJE_DIR "" CACHE PATH "Path to a local clap-juce-extensions checkout (optional)")
if(WWMA_CLAP)
    if(CJE_DIR)
        add_subdirectory(${CJE_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d # main, 2026-10-06 (JUCE 8 support)
            GIT_SUBMODULES_RECURSE ON)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()
    clap_juce_extensions_plugin(TARGET WWMA
        CLAP_ID "com.salleq.wwma"
        CLAP_FEATURES audio-effect analyzer utility stereo)
endif()

set(WWMA_CORE_SOURCES
    Source/analysis/Analyzer.cpp
    Source/advice/Profiles.cpp
    Source/advice/Advisor.cpp
    Source/advice/StyleGuess.cpp
    Source/advice/MixReport.cpp
    Source/advice/Lessons.cpp
    Source/advice/ChainAdvice.cpp
    Source/advice/TrackAdvice.cpp
    Source/advice/Passes.cpp
    Source/dsp/MasterChain.cpp
    Source/reference/ReferenceTracks.cpp)

set(WWMA_UI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Components.cpp
    Source/ui/ChainPanel.cpp)

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
    target_include_directories(wwma_selftest PRIVATE Source tools)
    target_compile_definitions(wwma_selftest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="WWMA" JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(wwma_selftest PRIVATE
        WWMAAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    # main() keeps many processors (each with two analysers) on the stack; Windows' default 1 MB is too small
    if(MSVC)
        target_link_options(wwma_selftest PRIVATE /STACK:16777216)
    endif()

    juce_add_console_app(wwma_calibrate PRODUCT_NAME "wwma_calibrate")
    target_sources(wwma_calibrate PRIVATE tools/Calibrate.cpp ${WWMA_CORE_SOURCES})
    target_include_directories(wwma_calibrate PRIVATE Source tools)
    target_compile_definitions(wwma_calibrate PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(wwma_calibrate PRIVATE juce::juce_audio_formats juce::juce_events juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
