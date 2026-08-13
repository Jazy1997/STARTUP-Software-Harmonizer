cmake_minimum_required(VERSION 3.22)

project(Harmonizer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS Universal binary architectures")

# BUILD BETA CON SCADENZA (D-26, sessione 37) — vedi src/licensing/BetaGate.h.
#
# DEVE restare OFF per default: con ON le build di sviluppo di tutti i giorni
# smetterebbero di produrre wet dopo 30 giorni, che sarebbe una trappola e
# ucciderebbe lo standalone (CLAUDE.md regola 11). Si accende SOLO per
# confezionare il pacchetto da mandare ai tester:
#
#   cmake -B build-beta -DHARMONIZER_BETA=ON
#
# HARMONIZER_BETA_DAYS=0 e' il gancio per verificare all'ascolto che il wet
# scenda sulla rampa anti-click senza aspettare trenta giorni.
#
# NOTA: la data e' fissata al momento del CONFIGURE, non della compilazione.
# E' voluto — la scadenza appartiene al pacchetto che si spedisce, non
# all'ultima ricompilazione incrementale fatta sopra. Per rigenerarla, si
# riconfigura da zero.
option(HARMONIZER_BETA "Build beta: il wet si silenzia dopo HARMONIZER_BETA_DAYS giorni" OFF)
set(HARMONIZER_BETA_DAYS "30" CACHE STRING "Giorni di validita' di una build beta")
set(HARMONIZER_BETA_TESTER "" CACHE STRING "Nome del tester, mostrato nell'editor: rende tracciabile una copia che gira")

add_subdirectory(libs/JUCE)

# IDENTITA' DEL PLUGIN — due gruppi con regole opposte (D-24, sessione 37).
#
# CONGELATI, PER SEMPRE. Non erano piu' "placeholder da confermare prima della
# beta pubblica": dal momento in cui la prima build finisce in mano a un tester
# che salva un progetto, questi valori sono pubblici di fatto, e cambiarli fa
# perdere il plugin da DENTRO i progetti salvati degli artisti.
#   - PLUGIN_MANUFACTURER_CODE + PLUGIN_CODE -> identita' VST3 e AU
#   - AU_MAIN_TYPE                            -> CLAUDE.md regola 7
# Verificato in JUCE: l'UID VST3 e' derivato SOLO da questi due codici a 4
# lettere, non dal nome — libs/JUCE/modules/juce_audio_plugin_client/VST3/
# juce_VST3ModuleInfo.h:61 (jucePluginId), e il ramo che usa JucePlugin_Name
# (:58) e' dietro JUCE_VST3_CAN_REPLACE_VST2, che noi teniamo a 0 piu' sotto.
# Per l'AU, JUCEUtils.cmake:1612 -> JucePlugin_AUSubType = JucePlugin_PluginCode.
# Sono codici OPACHI: l'utente non li vede mai e non devono somigliare al nome
# commerciale. Congelarli ora non anticipa nessuna decisione di marchio.
#
# LIBERI, si cambiano quando A-02 chiude (nome/marchio/dominio). Non finiscono
# nei progetti salvati, quindi rinominare il prodotto NON rompe niente:
#   - PRODUCT_NAME, COMPANY_NAME, BUNDLE_ID
# COMPANY_NAME era "TBD", che i tester leggerebbero come nome del produttore nel
# browser dell'host: sostituito col nome legale di chi distribuisce la beta.
juce_add_plugin(Harmonizer
    COMPANY_NAME "Giacomo Cazzaro"
    BUNDLE_ID "com.giacomocazzaro.harmonizer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Sessione 37: senza questo, da macOS 10.14 il target STANDALONE non riceve
    # segnale in ingresso — e non lo dice, resta muto. Il plugin (VST3/AU) non
    # e' interessato: l'ingresso glielo passa l'host, che ha il suo permesso.
    # Riguarda solo lo standalone, che CLAUDE.md regola 11 impone di tenere
    # sempre vivo perche' e' lo strumento di iterazione sul DSP.
    # CORRETTO PER COSTRUZIONE, NON VERIFICATO: qui non c'e' un Mac. Vedi i
    # limiti noti in HANDOFF.md.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Harmonizer ha bisogno dell'ingresso audio per armonizzare cio' che suoni."
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

# La scadenza esiste SOLO se HARMONIZER_BETA e' ON: in una build normale nessuna
# di queste macro e' definita e il codice della scadenza non viene compilato
# affatto (vedi gli #if in PluginProcessor.cpp/.h). Non e' un flag a runtime che
# si puo' dimenticare acceso.
if(HARMONIZER_BETA)
    string(TIMESTAMP HARMONIZER_BUILD_EPOCH "%s" UTC)
    message(STATUS "Harmonizer: BUILD BETA — scade ${HARMONIZER_BETA_DAYS} giorni dopo ${HARMONIZER_BUILD_EPOCH} (epoch UTC)")
    target_compile_definitions(Harmonizer
        PUBLIC
            HARMONIZER_BETA_BUILD_EPOCH=${HARMONIZER_BUILD_EPOCH}
            HARMONIZER_BETA_DAYS=${HARMONIZER_BETA_DAYS}
            HARMONIZER_BETA_TESTER="${HARMONIZER_BETA_TESTER}"
    )
endif()

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

# Verifica numerica della scala della finestra (sessione 33, FR-59 — vedi
# src/ui/UiScale.h): header-only, nessuna dipendenza JUCE. Copre l'aritmetica
# della scala, NON la resa a schermo: che su un display HiDPI non ci siano
# testi sfocati resta una verifica visiva dell'utente.
add_executable(ui_scale_test
    tests/ui_scale_test.cpp
)
target_include_directories(ui_scale_test PRIVATE src)
target_compile_features(ui_scale_test PRIVATE cxx_std_20)

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

# Banco di misura dell'attacco di nota in modalita' Play (sessione 34, B-06
# riaperto): pilota il VERO PlayModeInput con veri juce::MidiBuffer, l'unico
# livello a cui esistono note-on/note-off e allocazione degli slot. Come
# phrase_scheduler_test sta nel secondo livello di D-16 — linka JUCE
# (juce_audio_basics per MidiBuffer), quindi gira in ctest ma NON nel gate a
# g++ nudo della CI (A-06).
add_executable(play_mode_input_test
    tests/play_mode_input_test.cpp
    src/midi/PlayModeInput.cpp
    src/voices/VoicePool.cpp
    src/voices/Voice.cpp
    src/dsp/PsolaShifter.cpp
    src/dsp/SpectralShifter.cpp
    src/dsp/PitchShifterFactory.cpp
)
target_include_directories(play_mode_input_test PRIVATE src tests libs/signalsmith-stretch)
target_compile_features(play_mode_input_test PRIVATE cxx_std_20)
target_link_libraries(play_mode_input_test PRIVATE juce::juce_audio_basics)

# Verifica numerica della scadenza delle build beta (sessione 37, D-26 — vedi
# src/licensing/BetaGate.h): header-only, nessuna dipendenza JUCE. Copre
# l'aritmetica della scadenza, incluso il caso dell'orologio spostato indietro;
# che il wet scenda senza click e' invece coperto da glide_test (la rampa e' la
# stessa) e resta da confermare all'ascolto.
add_executable(beta_expiry_test
    tests/beta_expiry_test.cpp
)
target_include_directories(beta_expiry_test PRIVATE src)
target_compile_features(beta_expiry_test PRIVATE cxx_std_20)

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

# Banco di misura del passaggio Harmonizer<->Play (sessione 35, B-16/B-17).
# TERZO livello di D-16: il passaggio di modalita' non esiste dentro nessun
# modulo isolato — vive in HarmonizerAudioProcessor::processBlock — quindi
# questo banco istanzia il PROCESSORE INTERO e muove il vero parametro
# "playModeEnabled". Costa le stesse sorgenti del plugin piu' juce_audio_utils:
# gira in ctest, non nel gate a g++ nudo della CI (A-06).
add_executable(mode_switch_test
    tests/mode_switch_test.cpp
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
target_include_directories(mode_switch_test PRIVATE
    src
    tests
    libs/q/q_lib/include
    libs/q/infra/include
    libs/signalsmith-stretch
)
target_compile_features(mode_switch_test PRIVATE cxx_std_20)
# Stesse definizioni del target del plugin. JucePlugin_Name e' l'unico simbolo
# del wrapper che le sorgenti usano (PluginProcessor::getName): qui non c'e'
# nessun wrapper, quindi si fornisce a mano invece di trascinarsi dietro
# juce_audio_plugin_client.
target_compile_definitions(mode_switch_test PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="Harmonizer"
)
target_link_libraries(mode_switch_test PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

# Verifica che la scadenza beta faccia tacere il WET e lasciare passare il DRY
# (sessione 37, D-26). QUARTO livello di D-16 come mode_switch_test: il cancello
# vive in HarmonizerAudioProcessor::processBlock, non in un modulo isolato,
# quindi servono le sorgenti del plugin intero.
#
# Il punto che rende questo banco diverso da tutti gli altri: le macro della
# beta sono forzate QUI, non prese dall'option HARMONIZER_BETA. Epoch fisso nel
# passato + DAYS=0 = build sempre scaduta, quindi la misura e' la stessa in
# qualunque configurazione e in qualunque giorno la si esegua. Senza questo, il
# cablaggio del cancello resterebbe scoperto in ctest (tutti gli altri banchi
# girano in configurazione NON beta) e un cancello scollegato spedirebbe agli
# artisti una versione illimitata senza che nulla lo segnali.
add_executable(beta_gate_audio_test
    tests/beta_gate_audio_test.cpp
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
target_include_directories(beta_gate_audio_test PRIVATE
    src
    tests
    libs/q/q_lib/include
    libs/q/infra/include
    libs/signalsmith-stretch
)
target_compile_features(beta_gate_audio_test PRIVATE cxx_std_20)
target_compile_definitions(beta_gate_audio_test PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="Harmonizer"
    # 1700000000 = un istante fisso del 2023: qualunque orologio ragionevole e'
    # oltre, e con DAYS=0 la scadenza coincide con quell'istante.
    HARMONIZER_BETA_BUILD_EPOCH=1700000000
    HARMONIZER_BETA_DAYS=0
)
target_link_libraries(beta_gate_audio_test PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

enable_testing()
add_test(NAME psola COMMAND psola_test)
add_test(NAME override_manager COMMAND override_manager_test)
add_test(NAME pitch_latch COMMAND pitch_latch_test)
add_test(NAME glide COMMAND glide_test)
add_test(NAME cell_input_parser COMMAND cell_input_parser_test)
add_test(NAME voice COMMAND voice_test)
add_test(NAME empty_cell_hold COMMAND empty_cell_hold_test)
add_test(NAME phrase_scheduler COMMAND phrase_scheduler_test)
add_test(NAME play_mode_input COMMAND play_mode_input_test)
add_test(NAME ui_scale COMMAND ui_scale_test)
add_test(NAME mode_switch COMMAND mode_switch_test)
add_test(NAME beta_expiry COMMAND beta_expiry_test)
add_test(NAME beta_gate_audio COMMAND beta_gate_audio_test)
