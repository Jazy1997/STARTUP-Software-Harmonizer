#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/PitchDetector.h"
#include "harmony/HarmonyEngine.h"
#include "harmony/PresetLibrary.h"
#include "voices/VoicePool.h"

#include <functional>
#include <memory>

class HarmonizerAudioProcessor : public juce::AudioProcessor
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    mutable juce::SpinLock presetLibraryLock;
    std::shared_ptr<const harmony::PresetLibrary> currentPresetLibrary;
    std::shared_ptr<const harmony::PresetLibrary> retiredPresetLibrary; // solo message thread

    PitchDetector pitchDetector;
    VoicePool voicePool;

    // Buffer di lavoro mono, dimensionati sul caso peggiore in prepareToPlay
    // (NFR-03): nessuna riallocazione in processBlock (CLAUDE.md regola 1).
    juce::AudioBuffer<float> monoInputScratch;
    juce::AudioBuffer<float> voicesMixScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessor)
};
