#pragma once

#include "Voice.h"
#include <juce_core/juce_core.h>
#include <vector>

// Pool di slot fisici (Voice) riutilizzabili tra frasi diverse (FR-51/52).
// Non conosce il concetto di "frase": si limita a gestire un insieme fisso
// di Voice pre-allocate in prepare(); il chiamante (PhraseScheduler) traccia
// quali sono libere/occupate e quando rubarle.
class VoicePool
{
public:
    void prepare (int numSlots, double sampleRate, int maxBlockSize, int stabilityLevel);
    void reset();

    int getNumSlots() const noexcept { return (int) slots.size(); }
    Voice& getSlot (int index) noexcept { return slots[(size_t) index]; }

    int getLatencySamples() const { return slots.empty() ? 0 : slots.front().getLatencySamples(); }

    // FR-54/56 — stesso schema realtime-safe usato altrove nel progetto: il
    // message thread costruisce i nuovi shifter (alloca), l'audio thread li
    // applica solo quando sicuro con uno scambio senza allocazione, un
    // secondo passaggio sul message thread distrugge i vecchi.
    void requestStabilityChange (int newStabilityLevel); // SOLO message thread
    void collectGarbage();                               // SOLO message thread

    // Da chiamare in processBlock: applica lo scambio in attesa se
    // applyNow e' vero. Ritorna true se e' stato applicato un cambio (il
    // chiamante deve aggiornare setLatencySamples).
    bool applyPendingStabilityChangeIfSafe (bool applyNow);

private:
    std::vector<Voice> slots;

    double storedSampleRate = 48000.0;
    int storedMaxBlockSize = 8192;

    juce::SpinLock pendingLock;
    std::vector<std::unique_ptr<PitchShifter>> pendingShifters; // protetto da pendingLock
    bool hasPendingChange = false;                              // protetto da pendingLock

    juce::SpinLock retiredLock;
    std::vector<std::unique_ptr<PitchShifter>> retiredShifters; // protetto da retiredLock
};
