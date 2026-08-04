#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/PitchDetector.h"
#include "dsp/OnsetDetector.h"
#include "harmony/HarmonyEngine.h"
#include "harmony/PitchLatch.h"
#include "harmony/PresetLibrary.h"
#include "voices/PhraseScheduler.h"
#include "midi/CcRouter.h"
#include "midi/OverrideManager.h"
#include "midi/PlayModeInput.h"

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
    // Contatore cumulativo: quante volte PhraseScheduler ha dovuto completare
    // l'allocazione di uno slot dopo il trigger, perche' il pitch non era
    // ancora confidente al momento dell'onset. Serve a confermare all'ascolto
    // che il fix delle note saltate interviene davvero (CLAUDE.md regola 12).
    int   getNumLateBindings() const noexcept { return phraseScheduler.getNumLateBindings(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void timerCallback() override;
    bool canApplyStabilityChangeNow() const;
    bool isTransportPlaying() const;

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
    PhraseScheduler phraseScheduler;
    int lastKnownStabilityLevel = Stability::defaultLevel; // solo message thread (timerCallback)

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

    // FR-24..28: modalita' Play, VoicePool dedicato separato (vedi
    // PlayModeInput.h per il perche' non condivide phraseScheduler).
    PlayModeInput playModeInput;

    // Buffer di lavoro mono, dimensionati sul caso peggiore in prepareToPlay
    // (NFR-03): nessuna riallocazione in processBlock (CLAUDE.md regola 1).
    juce::AudioBuffer<float> monoInputScratch;
    juce::AudioBuffer<float> voicesMixScratch;
    juce::AudioBuffer<float> playVoicesMixScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessor)
};
