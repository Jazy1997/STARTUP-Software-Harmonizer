#pragma once

#include "Voice.h"
#include "../harmony/HarmonyPreset.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// M0/M1 — pool di voci "continue" (non a frase): ogni voce insegue in tempo
// reale l'offset del preset armonico corrente sulla nota rilevata. Il vero
// motore a frasi con trigger su onset, congelamento del voicing e furto
// (FR-43..53, PhraseScheduler) e' lavoro di M3: questa e' una semplificazione
// intenzionale per avere una catena udibile end-to-end il prima possibile.
class VoicePool
{
public:
    static constexpr int maxVoices = harmony::numVoices;

    void prepare (double sampleRate, int maxBlockSize, int stabilityLevel);
    void reset();

    void setVoiceMode (int voiceIndex, ShiftMode mode);
    void setGlideTimeMs (float ms);

    int getLatencySamples() const { return voices.front().getLatencySamples(); }

    // FR-54/56 — da chiamare SOLO dal message thread (mai in processBlock):
    // costruisce (con allocazione) 8 nuovi shifter per il livello indicato e
    // li mette in attesa. Verranno scambiati con quelli attivi da process()
    // solo quando applyStabilityChangesNow e' true (transport fermo o
    // standalone), mai a meta' riproduzione (FR-56/57).
    void requestStabilityChange (int newStabilityLevel);

    // Da chiamare periodicamente dal message thread (es. un juce::Timer):
    // distrugge gli shifter ritirati dallo scambio precedente. Mai sull'audio
    // thread (PRD §9.4).
    void collectGarbage();

    // mixOutput deve avere numSamples campioni; viene azzerato internamente
    // prima di sommare le voci attive. Ritorna true se in questa chiamata e'
    // stato applicato un cambio di Stability (il chiamante deve aggiornare
    // setLatencySamples subito dopo).
    bool process (const float* monoIn,
                  float* mixOutput,
                  int numSamples,
                  const std::array<harmony::Cell, harmony::numVoices>& offsets,
                  int numActiveVoices,
                  int quantizedPlayedNote,
                  float continuousInputMidiNote,
                  bool applyStabilityChangesNow);

private:
    std::array<Voice, maxVoices> voices;

    double storedSampleRate = 48000.0;
    int storedMaxBlockSize = 8192;

    juce::SpinLock pendingLock;
    std::vector<std::unique_ptr<PitchShifter>> pendingShifters; // protetto da pendingLock
    bool hasPendingChange = false;                              // protetto da pendingLock

    juce::SpinLock retiredLock;
    std::vector<std::unique_ptr<PitchShifter>> retiredShifters; // protetto da retiredLock
};
