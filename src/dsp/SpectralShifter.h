#pragma once

#include "PitchShifter.h"
#include <signalsmith-stretch.h>

// Motore di pitch-shifting INTERINALE basato su Signalsmith Stretch (MIT),
// in attesa del PSOLA proprietario (PRD §9.2). E' uno shifter spettrale/STFT,
// non PSOLA: latenza e CPU non sono rappresentative del motore finale.
// Vive dietro PitchShifter (interfaccia astratta) — vedi createDefaultPitchShifter().
class SpectralShifter : public PitchShifter
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setPitchShiftSemitones (float semitones) override;
    int getLatencySamples() const override;
    void process (const float* in, float* out, int numSamples) override;

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    float currentSemitones = 0.0f;
};
