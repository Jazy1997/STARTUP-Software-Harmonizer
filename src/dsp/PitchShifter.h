#pragma once

#include <memory>

// Interfaccia astratta (FR-62, CLAUDE.md regola 2). Nessun modulo fuori da
// src/dsp/ puo' conoscere l'implementazione concreta: oggi SpectralShifter
// (Signalsmith Stretch, motore interinale), domani PsolaShifter proprietario,
// dietro la stessa interfaccia, senza toccare voices/ o harmony/.
class PitchShifter
{
public:
    virtual ~PitchShifter() = default;

    // Alloca le risorse interne. Da chiamare su message thread / prepareToPlay.
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    // Cambiare lo shift e' economico solo se il valore cambia davvero: le
    // implementazioni correnti possono ricalcolare tabelle interne quando
    // l'offset cambia, quindi va chiamato solo su cambi di accordo/nota,
    // non ad ogni blocco audio con lo stesso valore.
    virtual void setPitchShiftSemitones (float semitones) = 0;

    virtual int getLatencySamples() const = 0;

    // in e out sono buffer mono distinti (non alias) di numSamples campioni.
    virtual void process (const float* in, float* out, int numSamples) = 0;
};

std::unique_ptr<PitchShifter> createDefaultPitchShifter();
