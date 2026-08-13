#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Glide.h"
#include "dsp/PitchDetector.h"
#include "dsp/OnsetDetector.h"
#include "harmony/HarmonyEngine.h"
#include "harmony/PitchLatch.h"
#include "harmony/PresetLibrary.h"
#include "voices/PhraseScheduler.h"
#include "midi/CcRouter.h"
#include "midi/OverrideManager.h"
#include "midi/PlayModeInput.h"
#include "ui/UiScale.h"

#include <atomic>
#include <functional>
#include <memory>

class HarmonizerAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    HarmonizerAudioProcessor();
    ~HarmonizerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Tetto tecnico di voci fisiche pre-allocate (FR-51): il parametro
    // "Max Simultaneous Voices" regola quante di queste sono effettivamente
    // utilizzabili (1..hardVoiceSlotCapacity), senza mai riallocare.
    static constexpr int hardVoiceSlotCapacity = 32;

    // B-14/D-18: gamma ammessa per la nota piu' grave d'analisi.
    //
    // Il default e' E2 = MIDI 40 (Voice Male), scelto MISURANDO in s.32, non
    // per simmetria con la lista strumenti. Su materiale reale, a 1024
    // campioni di buffer: 44.3 ms di ritardo contro i 79.1 della soglia fissa
    // di 60 Hz usata fino a s.31, e ZERO offset di passaggio su tutte e
    // quattro le tabelle di prova. Un gradino piu' su (Ab2, Bb Tenor Sax)
    // scende a 32.7 ms ma su preset con TUTTI i gradi pieni lascia passare una
    // sbandata del rilevatore — cioe' riapre in piccolo B-13. Non vale 12 ms.
    //
    // Gli estremi lasciano margine per strumenti non ancora in lista senza
    // dover pubblicare un ID nuovo (regola 6).
    static constexpr int minAnalysisLowestNote = 28;     // E1, 41.2 Hz
    static constexpr int maxAnalysisLowestNote = 72;     // C5, 523.3 Hz
    static constexpr int defaultAnalysisLowestNote = 40; // E2, 82.4 Hz

    juce::AudioProcessorValueTreeState apvts;

    // Snapshot immutabile, utilizzabile da qualunque thread (audio incluso):
    // ritorna il puntatore corrente sotto un lock brevissimo (solo copia di
    // uno shared_ptr, nessuna allocazione/IO) — vedi PRD §9.4.
    std::shared_ptr<const harmony::PresetLibrary> getPresetLibrary() const noexcept;

    // Da chiamare SOLO dal message thread (bottoni UI). mutator riceve una
    // copia mutabile della libreria corrente; al termine la copia diventa la
    // nuova libreria "corrente" con uno scambio atomico del puntatore. La
    // vecchia libreria resta viva in un singolo slot "retired" (anch'esso
    // scritto solo dal message thread) finche' non viene sostituita dalla
    // modifica successiva: questo evita che la sua distruzione avvenga
    // sull'audio thread nel caso normale. Non e' una garanzia assoluta in
    // ogni possibile intreccio di timing (servirebbe hazard-pointer/epoch based
    // reclamation per quello) ma e' il compromesso pragmatico standard per
    // modifiche rare guidate dall'utente, non per hot-path a blocco.
    void editPresetLibrary (const std::function<void (harmony::PresetLibrary&)>& mutator);

    // FR-53: numero di voci fisiche attualmente in uso tra tutte le frasi.
    int getNumActiveVoices() const noexcept { return phraseScheduler.getNumActiveVoices(); }

    // FR-31/32/33: configurazione CC (numeri, canale) e MIDI Learn. Letta e
    // scritta dal message thread (UI); CcRouter la rende sicura anche
    // rispetto all'audio thread internamente (std::atomic).
    CcRouter& getCcRouter() noexcept { return ccRouter; }

    // Diagnostica (PRD §8.1, "Display della nota rilevata" — mai
    // implementato prima di sessione 10): snapshot dell'ultimo esito di
    // PitchDetector, scritto ogni blocco sull'audio thread, letto dal
    // message thread (UI, timer 15Hz). Nessun lock: solo atomici.
    float getLastDetectedMidiNote() const noexcept { return lastDetectedMidiNote.load (std::memory_order_relaxed); }
    float getLastDetectedConfidence() const noexcept { return lastDetectedConfidence.load (std::memory_order_relaxed); }
    bool  getLastInputStable() const noexcept { return lastInputStable.load (std::memory_order_relaxed); }
    // Sessione 12 (FR-43/45/46): stato del gate di OnsetDetector, distinto
    // da getLastInputStable() (che riflette la confidenza del pitch, non la
    // presenza del segnale) — vedi il commento su signalPresent in
    // processBlock per il perche' sono due domande diverse.
    bool  getLastGateOpen() const noexcept { return lastGateOpen.load (std::memory_order_relaxed); }
    // Sessione 13 (indagine click/wobbling): dimensione del blocco ricevuto
    // dall'host in questo processBlock. Tutta la catena di controllo (Glide,
    // parametri del PitchShifter) aggiorna una volta per blocco — con host
    // configurati su buffer grandi (es. MME/DirectX senza ASIO) questo puo'
    // essere la causa diretta di artefatti percepiti come click o
    // ondeggiamento, non un bug nel calcolo in se'. Va misurato, non assunto
    // (CLAUDE.md regola 12).
    int   getLastBlockSize() const noexcept { return lastBlockSize.load (std::memory_order_relaxed); }
    // Contatore cumulativo: quante volte PhraseScheduler ha dovuto completare
    // l'allocazione di uno slot dopo il trigger, perche' il pitch non era
    // ancora confidente al momento dell'onset. Serve a confermare all'ascolto
    // che il fix delle note saltate interviene davvero (CLAUDE.md regola 12).
    int   getNumLateBindings() const noexcept { return phraseScheduler.getNumLateBindings(); }

    // FR-59 (sessione 33): la scala della finestra, in percento. NON e' un
    // parametro APVTS, di proposito — e' una preferenza di visualizzazione, non
    // un valore musicale, e automatizzare "quanto e' grande la finestra" non
    // vorrebbe dire niente. Sta pero' nello stato del plugin (D-20), accanto ai
    // numeri CC che sono l'altro caso identico: configurazione da salvare ma
    // non da automatizzare.
    //
    // Non e' letta ne' scritta da processBlock: la regola 1 non la riguarda.
    // E' atomica perche' getStateInformation puo' essere chiamata da un thread
    // di salvataggio diverso dal message thread che la scrive in resized()
    // (stessa cautela gia' annotata su editPresetLibrary).
    int  getUiScalePercent() const noexcept { return uiScalePercent.load (std::memory_order_relaxed); }
    void setUiScalePercent (int percent) noexcept
    {
        uiScalePercent.store (ui::clampScalePercent (percent), std::memory_order_relaxed);
    }

    // ===== Build beta con scadenza (sessione 37, D-26) =====
    // Vedi src/licensing/BetaGate.h: NON e' il licensing di M6 (FR-63..70) e va
    // cancellato quando quello arrivera'. In una build normale HARMONIZER_BETA
    // e' OFF, isBetaBuild() e' false a tempo di compilazione e non resta niente.
    static constexpr bool isBetaBuild() noexcept
    {
       #if defined (HARMONIZER_BETA_BUILD_EPOCH)
        return true;
       #else
        return false;
       #endif
    }

    // L'UNICO membro di questo gruppo che l'audio thread puo' chiamare: legge un
    // flag atomico e nient'altro. L'ora di sistema (std::time, una chiamata di
    // sistema) e' letta sul message thread da updateBetaGate() — CLAUDE.md
    // regola 1 / PRD §9.4.
    bool isBetaExpired() const noexcept { return betaExpired.load (std::memory_order_relaxed); }

    // Solo message thread: leggono l'ora di sistema. Servono all'avviso
    // nell'editor, cosi' la scadenza non arriva come una sorpresa che il tester
    // segnala come difetto.
    int betaDaysRemaining() const;
    static const char* betaTesterName() noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void timerCallback() override;
    // Aggiorna betaExpired leggendo l'ora di sistema. SOLO message thread
    // (prepareToPlay e timerCallback). In una build non-beta e' un corpo vuoto.
    void updateBetaGate();
    bool canApplyStabilityChangeNow() const;
    bool isTransportPlaying() const;
    // B-14: valore corrente del parametro, gia' limitato alla gamma ammessa.
    int currentAnalysisLowestNote() const;

    mutable juce::SpinLock presetLibraryLock;
    std::shared_ptr<const harmony::PresetLibrary> currentPresetLibrary;
    std::shared_ptr<const harmony::PresetLibrary> retiredPresetLibrary; // solo message thread

    PitchDetector pitchDetector;
    OnsetDetector onsetDetector;
    // FR-16/17 (sessione 11): isteresi sulla nota usata per il lookup nella
    // tabella armonica — vedi PitchLatch.h per il perche'. Solo audio thread.
    harmony::PitchLatch pitchLatch;
    // FR-43/45/46 (sessione 12): fronte di discesa di signalPresent — vedi
    // processBlock per il perche' (reset di pitchDetector al vero silenzio,
    // cosi' un onset successivo non trova una stima confidente ma stantia
    // dalla nota precedente). Solo audio thread.
    bool signalPresentLastBlock = false;
    // B-14 (sessione 32): l'aggancio si aggiorna a ogni stima nuova del
    // rilevatore, non una volta per blocco — quindi il suo stato deve
    // sopravvivere al confine di blocco. samplesSinceLatchUpdate e' il tempo
    // VERO fra due stime consecutive (a buffer piccoli un blocco puo' non
    // contenerne nessuna); onsetPendingForLatch trasporta un onset rilevato
    // fino alla prima stima utile; lastLatchedNote e' cio' che il blocco legge
    // quando non e' arrivata alcuna stima nuova. Tutti e tre solo audio thread.
    int samplesSinceLatchUpdate = 0;
    bool onsetPendingForLatch = false;
    int lastLatchedNote = 0;
    // B-17 (sessione 35): fronte dell'interruttore di modalita'. Una frase
    // dell'Harmonizer nasce SOLO su un onset (PhraseScheduler: triggerNewPhrase
    // ha un unico call site), e uscendo da Play a meta' di una nota tenuta
    // quell'onset non arrivera' mai — il gate e' gia' aperto. Il fronte di
    // discesa prende il posto dell'onset mancante; il flag lo trasporta fino al
    // primo blocco in cui c'e' davvero di che fare una frase. Solo audio thread.
    bool playModeEnabledLastBlock = false;
    bool pendingHarmonizerRetrigger = false;
    PhraseScheduler phraseScheduler;

    // Sessione 12 (fix scricchiolii/click): dryLevel/wetLevel (e bypass, che
    // li sostituisce di netto) venivano letti come valore grezzo del
    // parametro e applicati per l'intero blocco — un cambio di automazione,
    // di CC bypass, o anche solo il fronte del bottone Bypass produceva un
    // salto di guadagno istantaneo al confine di blocco (un click classico,
    // indipendente da qualunque cosa succeda nelle voci). Stessa breve
    // rampa fissa anti-click delle voci (Voice::kDeclickMs), non il
    // glideTimeMs musicale dell'utente.
    Glide dryGlide, wetGlide;
    // Sessione 37 (D-26): scritto sul message thread da updateBetaGate(), letto
    // dall'audio thread in processBlock. In una build non-beta resta false per
    // sempre e nessuno lo scrive. Parte da false di proposito: se l'ora di
    // sistema fosse illeggibile, il plugin suona — non si blocca un tester per
    // un dubbio.
    std::atomic<bool> betaExpired { false };
    int lastKnownStabilityLevel = Stability::defaultLevel; // solo message thread (timerCallback)
    // B-14: come lastKnownStabilityLevel, per la nota piu' grave d'analisi.
    // Inizializzato a un valore impossibile cosi' il primo giro del timer
    // allinea comunque il rilevatore al parametro caricato dallo stato.
    int lastKnownAnalysisLowestNote = -1; // solo message thread (timerCallback)
    double preparedSampleRate = 44100.0;  // scritto in prepareToPlay, letto dal timer

    // FR-30/36/37/38: CcRouter interpreta i CC in ingresso, OverrideManager
    // decide se contano piu' di quello che dice l'automazione host. Entrambi
    // vivono e si usano solo sull'audio thread (dentro processBlock);
    // CcRouter espone comunque setter/getter atomici per l'UI (message
    // thread) tramite getCcRouter().
    CcRouter ccRouter;
    OverrideManager overrideManager;
    bool wasPlayingLastBlock = false; // solo audio thread: rileva il fronte di stop (FR-36)

    // Vedi i getter pubblici sopra.
    std::atomic<float> lastDetectedMidiNote { -1.0f };
    std::atomic<float> lastDetectedConfidence { 0.0f };
    std::atomic<bool> lastInputStable { false };
    std::atomic<bool> lastGateOpen { false }; // sessione 12, vedi getter pubblico
    std::atomic<int> lastBlockSize { 0 }; // sessione 13, vedi getter pubblico

    // FR-59, sessione 33 — vedi get/setUiScalePercent sopra. A differenza dei
    // cinque atomici qui sopra questo NON e' uno snapshot scritto dall'audio
    // thread: lo scrive l'editor (message thread) e lo legge il salvataggio
    // dello stato.
    std::atomic<int> uiScalePercent { ui::kDefaultScalePercent };

    // FR-24..28: modalita' Play, VoicePool dedicato separato (vedi
    // PlayModeInput.h per il perche' non condivide phraseScheduler).
    PlayModeInput playModeInput;

    // Buffer di lavoro, dimensionati sul caso peggiore in prepareToPlay
    // (NFR-03): nessuna riallocazione in processBlock (CLAUDE.md regola 1).
    // monoInputScratch resta mono (rilevamento pitch/onset e dry path
    // lavorano su un solo canale, PRD §3.1); i due mix wet sono STEREO
    // (FR-11/§8.1: gain/pan per voce, canale 0=L, 1=R).
    juce::AudioBuffer<float> monoInputScratch;
    juce::AudioBuffer<float> voicesMixScratch;
    juce::AudioBuffer<float> playVoicesMixScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessor)
};
