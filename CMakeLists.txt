cmake_minimum_required(VERSION 3.22)

project(Harmonizer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS Universal binary architectures")

add_subdirectory(libs/JUCE)

# PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE sono placeholder: nome prodotto e azienda
# non sono ancora stati decisi (PRD §16). Vedi CLAUDE.md — vanno confermati prima
# della beta pubblica perché non sono piu' modificabili dopo il rilascio.
juce_add_plugin(Harmonizer
    COMPANY_NAME "TBD"
    BUNDLE_ID "com.tbd.harmonizer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Hzso
    PLUGIN_CODE Hmz1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Harmonizer"
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
)

juce_generate_juce_header(Harmonizer)

target_sources(Harmonizer
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
)

target_compile_definitions(Harmonizer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(Harmonizer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
