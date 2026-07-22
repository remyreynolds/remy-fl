cmake_minimum_required(VERSION 3.22)

project(AIMidiGen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

# ---------------------------------------------------------------------------
# JUCE via FetchContent  -> no manual clone needed. CMake downloads it.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(AIMidiGen
    PRODUCT_NAME            "AI MIDI Gen"
    COMPANY_NAME            "YourStudio"
    BUNDLE_ID               "com.yourstudio.aimidigen"
    PLUGIN_MANUFACTURER_CODE Ymgn
    PLUGIN_CODE             Aimg
    FORMATS                 VST3 AU Standalone
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       TRUE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES         "Instrument" "Tools"
    AU_MAIN_TYPE            "kAudioUnitType_MusicDevice"
)

juce_generate_juce_header(AIMidiGen)

target_sources(AIMidiGen PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ai/AIClient.cpp
    src/engine/MidiDna.cpp
    src/engine/MidiGenerator.cpp
    src/gui/ChatPanel.cpp
    src/gui/InstrumentPanel.cpp
    src/gui/DrumKitPanel.cpp
)

target_include_directories(AIMidiGen PRIVATE src)

target_compile_definitions(AIMidiGen PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(AIMidiGen PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    juce::juce_dsp
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
