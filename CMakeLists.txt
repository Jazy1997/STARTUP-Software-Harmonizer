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
    # TRUE: copia il plugin compilato nelle cartelle di default dell'host dopo
    # ogni build (su Windows: VST3 in "C:\Program Files\Common Files\VST3", AU
    # in ~/Library/Audio/Plug-Ins/Components su macOS). Su Windows questa
    # cartella richiede permessi di amministratore: se il build gira in una
    # shell non elevata, il passo di copia fallisce con "Permission denied"
    # (il resto della build/l'artefatto in build/Harmonizer_artefacts restano
    # comunque validi). Per farla funzionare, lanciare la build da una shell
    # con privilegi di amministratore.
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
        src/dsp/PitchDetector.cpp
        src/dsp/SpectralShifter.cpp
        src/dsp/PsolaShifter.cpp
        src/dsp/PitchShifterFactory.cpp
        src/dsp/OnsetDetector.cpp
        src/harmony/HarmonyEngine.cpp
        src/harmony/PresetLibrary.cpp
        src/harmony/CsvIo.cpp
        src/voices/Voice.cpp
        src/voices/VoicePool.cpp
        src/voices/PhraseScheduler.cpp
        src/midi/CcRouter.cpp
        src/midi/OverrideManager.cpp
        src/midi/PlayModeInput.cpp
)

target_compile_definitions(Harmonizer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# Cycfi Q (pitch detection, Boost License) e Signalsmith Stretch (pitch
# shifting interinale, MIT) sono header-only: bastano gli include path.
target_include_directories(Harmonizer
    PRIVATE
        libs/q/q_lib/include
        libs/q/infra/include
        libs/signalsmith-stretch
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

# Suite di verifica numerica del motore PSOLA (CLAUDE.md, "non puoi
# ascoltare" — vedi tests/psola_test.cpp). Deliberatamente SENZA
# dipendenze da JUCE: si compila ed esegue in un secondo, senza aprire una
# DAW, cosi' una regressione nel motore si scopre prima di aspettare la
# build completa del plugin (vedi anche .github/workflows/build.yml).
add_executable(psola_test
    tests/psola_test.cpp
    src/dsp/PsolaShifter.cpp
)
target_include_directories(psola_test PRIVATE src)
target_compile_features(psola_test PRIVATE cxx_std_20)

# Verifica numerica della precedenza CC/automazione (FR-36/37/38): logica
# pura, nessuna dipendenza JUCE (OverrideManager non usa juce::MidiBuffer,
# solo CcRouter lo fa) — stesso principio del target psola_test sopra.
add_executable(override_manager_test
    tests/override_manager_test.cpp
    src/midi/OverrideManager.cpp
)
target_include_directories(override_manager_test PRIVATE src)
target_compile_features(override_manager_test PRIVATE cxx_std_20)

# Verifica numerica dell'isteresi di intonazione (sessione 11, FR-16/17):
# header-only, nessuna dipendenza JUCE.
add_executable(pitch_latch_test
    tests/pitch_latch_test.cpp
)
target_include_directories(pitch_latch_test PRIVATE src)
target_compile_features(pitch_latch_test PRIVATE cxx_std_20)

# Verifica numerica della rampa campione-per-campione di Glide (sessione 13,
# click residui): header-only, nessuna dipendenza JUCE.
add_executable(glide_test
    tests/glide_test.cpp
)
target_include_directories(glide_test PRIVATE src)
target_compile_features(glide_test PRIVATE cxx_std_20)

enable_testing()
add_test(NAME psola COMMAND psola_test)
add_test(NAME override_manager COMMAND override_manager_test)
add_test(NAME pitch_latch COMMAND pitch_latch_test)
add_test(NAME glide COMMAND glide_test)
