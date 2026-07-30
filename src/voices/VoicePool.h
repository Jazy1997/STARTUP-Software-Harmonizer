#pragma once

#include "Voice.h"
#include "../harmony/HarmonyPreset.h"
#include <array>

// M0/M1 — pool di voci "continue" (non a frase): ogni voce insegue in tempo
// reale l'offset del preset armonico corrente sulla nota rilevata. Il vero
// motore a frasi con trigger su onset, congelamento del voicing e furto
// (FR-43..53, PhraseScheduler) e' lavoro di M3: questa e' una semplificazione
// intenzionale per avere una catena udibile end-to-end il prima possibile.
class VoicePool
{
public:
    static constexpr int maxVoices = harmony::numVoices;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    int getLatencySamples() const { return voices.front().getLatencySamples(); }

    // mixOutput deve avere numSamples campioni; viene azzerato internamente
    // prima di sommare le voci attive.
    void process (const float* monoIn,
                  float* mixOutput,
                  int numSamples,
                  const std::array<harmony::Cell, harmony::numVoices>& offsets,
                  int numActiveVoices);

private:
    std::array<Voice, maxVoices> voices;
};
