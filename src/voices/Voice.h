#pragma once

#include "../dsp/PitchShifter.h"
#include <memory>
#include <vector>

class Voice
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setTargetSemitones (float semitones) noexcept { targetSemitones = semitones; }
    void setMuted (bool shouldBeMuted) noexcept { muted = shouldBeMuted; }
    bool isMuted() const noexcept { return muted; }

    int getLatencySamples() const;

    // Somma il segnale shiftato di questa voce dentro mixOutput (gia'
    // inizializzato dal chiamante). Non-op se la voce e' muta.
    void processAdd (const float* monoIn, float* mixOutput, int numSamples);

private:
    std::unique_ptr<PitchShifter> shifter;
    std::vector<float> scratch;
    float targetSemitones = 0.0f;
    bool muted = true;
};
