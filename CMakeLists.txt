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
        src/ui/PresetTableEditor.cpp
        src/ui/PresetListEditor.cpp
        src/ui/RootNoteGrid.cpp
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

# Verifica numerica del parser di input dell'editor tabella preset
# (sessione M5, §8.2 — CLAUDE.md regola 3, 0 vs cella vuota): header-only,
# nessuna dipendenza JUCE ne' da harmony/.
add_executable(cell_input_parser_test
    tests/cell_input_parser_test.cpp
)
target_include_directories(cell_input_parser_test PRIVATE src)
target_compile_features(cell_input_parser_test PRIVATE cxx_std_20)

# Verifica numerica dell'isteresi FR-17 (sessione 28, "ribattuto" — vedi
# src/voices/EmptyCellHold.h): header-only, nessuna dipendenza JUCE.
add_executable(empty_cell_hold_test
    tests/empty_cell_hold_test.cpp
)
target_include_directories(empty_cell_hold_test PRIVATE src)
target_compile_features(empty_cell_hold_test PRIVATE cxx_std_20)

# Strumento di misura del click a inizio nota (sessione 16, ripartenza da
# zero dopo che il fix di sessione 14 e' stato smentito all'ascolto — vedi
# LOG/archivio-s01-s28.md §6 e tests/voice_test.cpp per il ragionamento completo).
# Serve Voice + il motore PSOLA reale (non un doppio finto), ma resta
# headless come le altre suite: nessuna dipendenza JUCE, si compila ed
# esegue senza aprire una DAW.
add_executable(voice_test
    tests/voice_test.cpp
    src/voices/Voice.cpp
    src/dsp/PsolaShifter.cpp
    src/dsp/SpectralShifter.cpp
    src/dsp/PitchShifterFactory.cpp
)
target_include_directories(voice_test PRIVATE src libs/signalsmith-stretch)
target_compile_features(voice_test PRIVATE cxx_std_20)

# Verifica numerica di PhraseScheduler (sessione 30, B-10/FR-19). PRIMO test
# su questo modulo, rimasto scoperto fino a oggi. A differenza delle 7 suite
# sopra, questa NON e' JUCE-free: PhraseScheduler dipende da juce_core
# (juce::SpinLock in VoicePool.h, juce::jmin, juce::Uuid/String via
# HarmonyPreset.h), quindi il livello "nessuna dipendenza, runner minimo" di
# D-11 non la puo' contenere. Gira in ctest ma NON nel gate a g++ nudo della
# CI — vedi D-16 (che emenda D-11) e A-06.
add_executable(phrase_scheduler_test
    tests/phrase_scheduler_test.cpp
    src/voices/PhraseScheduler.cpp
    src/voices/VoicePool.cpp
    src/voices/Voice.cpp
    src/dsp/PsolaShifter.cpp
    src/dsp/SpectralShifter.cpp
    src/dsp/PitchShifterFactory.cpp
)
target_include_directories(phrase_scheduler_test PRIVATE src tests libs/signalsmith-stretch)
target_compile_features(phrase_scheduler_test PRIVATE cxx_std_20)
target_link_libraries(phrase_scheduler_test PRIVATE juce::juce_core)

# Strumento diagnostico OFFLINE per il click "in mezzo alla nota" (sessione
# 17, segnalato dopo aver riascoltato il fix di sessione 16): legge un file
# WAV vero fornito dall'utente e cerca discontinuita' anomale con la stessa
# logica di voice_test.cpp ma su segnale reale, non sintetico — vedi
# tests/sample_click_finder.cpp per il ragionamento completo. Usa lo stesso
# PitchDetector reale del plugin (Cycfi Q, nessuna dipendenza JUCE). NON in
# ctest: dipende da file WAV esterni non versionati nel repo, si lancia a
# mano con il percorso del file come argomento.
add_executable(sample_click_finder
    tests/sample_click_finder.cpp
    src/voices/Voice.cpp
    src/dsp/PsolaShifter.cpp
    src/dsp/SpectralShifter.cpp
    src/dsp/PitchShifterFactory.cpp
    src/dsp/PitchDetector.cpp
    src/dsp/OnsetDetector.cpp
)
target_include_directories(sample_click_finder PRIVATE
    src
    libs/signalsmith-stretch
    libs/q/q_lib/include
    libs/q/infra/include
)
target_compile_features(sample_click_finder PRIVATE cxx_std_20)

# Strumento diagnostico OFFLINE che confronta DRY reale e WET reale (un
# export vero dal plugin, non un'approssimazione offline) — sessione 18,
# "una prima voce perfetta". Vedi tests/real_export_probe.cpp. Serve solo
# PsolaShifter (per la latenza dichiarata) oltre a SampleAnalysis.h/
# TestSignals.h, entrambi header-only: nessuna dipendenza JUCE. NON in
# ctest: dipende da file WAV esterni forniti dall'utente, non versionati.
add_executable(real_export_probe
    tests/real_export_probe.cpp
    src/dsp/PsolaShifter.cpp
)
target_include_directories(real_export_probe PRIVATE src)
target_compile_features(real_export_probe PRIVATE cxx_std_20)

# Strumento diagnostico OFFLINE — sessione 25/26, indagine sul ritorno del
# "wobbling"/timbro granuloso sulle voci con offset profondo. A differenza di
# real_export_probe (che PREDICE il pitch atteso applicando la tabella
# armonica al dry), qui il target e' gia' noto per costruzione: confronta
# direttamente l'export reale di una voce isolata con la nota "ideale" suonata
# dal synth via MIDI (nessuna elaborazione del plugin). Vedi
# tests/voice_reference_probe.cpp. Stessa dipendenza minima di
# real_export_probe (solo PsolaShifter, per SampleAnalysis.h/TestSignals.h
# header-only). NON in ctest: dipende da file WAV esterni forniti
# dall'utente, non versionati (SAMPLE TEST/DBG Timbro/).
add_executable(voice_reference_probe
    tests/voice_reference_probe.cpp
    src/dsp/PsolaShifter.cpp
)
target_include_directories(voice_reference_probe PRIVATE src)
target_compile_features(voice_reference_probe PRIVATE cxx_std_20)

# Strumento diagnostico OFFLINE minimale — sessione 28 (piano "ribattuto"):
# dump dell'inviluppo RMS fine di UN SOLO file WAV, per misurare con
# precisione buchi gia' individuati a occhio in un DAW. Vedi
# tests/envelope_probe.cpp. NON in ctest: dipende da file WAV esterni.
add_executable(envelope_probe
    tests/envelope_probe.cpp
)
target_include_directories(envelope_probe PRIVATE src)
target_compile_features(envelope_probe PRIVATE cxx_std_20)

# Strumento diagnostico OFFLINE — sessione 31 ("attacchi sporchi", B-13):
# quale SEQUENZA di offset la catena di controllo chiede al motore mentre la
# melodia cambia nota. Nessun Voice e nessun PsolaShifter dentro, di proposito:
# serve a dire se il difetto nasce PRIMA della sintesi o dentro. Gira i moduli
# veri (PitchDetector, OnsetDetector, PitchLatch, stepEmptyCellHold). Vedi
# tests/degree_trace_probe.cpp. NON in ctest: dipende da WAV esterni.
add_executable(degree_trace_probe
    tests/degree_trace_probe.cpp
    src/dsp/PitchDetector.cpp
    src/dsp/OnsetDetector.cpp
)
target_include_directories(degree_trace_probe PRIVATE
    src
    tests
    libs/q/q_lib/include
    libs/q/infra/include
)
target_compile_features(degree_trace_probe PRIVATE cxx_std_20)

enable_testing()
add_test(NAME psola COMMAND psola_test)
add_test(NAME override_manager COMMAND override_manager_test)
add_test(NAME pitch_latch COMMAND pitch_latch_test)
add_test(NAME glide COMMAND glide_test)
add_test(NAME cell_input_parser COMMAND cell_input_parser_test)
add_test(NAME voice COMMAND voice_test)
add_test(NAME empty_cell_hold COMMAND empty_cell_hold_test)
add_test(NAME phrase_scheduler COMMAND phrase_scheduler_test)
