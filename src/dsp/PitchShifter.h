#pragma once

#include <memory>

// FR-54: 5 posizioni discrete, da Fast (reattivo, meno accurato) a Accurate
// (accurato, piu' lento). Determina la finestra di analisi e quindi la
// latenza dichiarata all'host (FR-55).
namespace Stability
{
    constexpr int numLevels = 5;
    constexpr const char* names[numLevels] = { "Fast", "Fast+", "Balanced", "Accurate-", "Accurate" };
    constexpr int defaultLevel = 2; // "Balanced"
}

// Interfaccia astratta (FR-62, CLAUDE.md regola 2). Nessun modulo fuori da
// src/dsp/ puo' conoscere l'implementazione concreta: oggi SpectralShifter
// (Signalsmith Stretch, motore interinale), domani PsolaShifter proprietario,
// dietro la stessa interfaccia, senza toccare voices/ o harmony/.
class PitchShifter
{
public:
    virtual ~PitchShifter() = default;

    // Alloca le risorse interne per il livello di Stability indicato
    // (0..Stability::numLevels-1). Da chiamare SOLO su message thread: mai in
    // processBlock. Per cambiare livello a runtime si prepara una nuova
    // istanza (vedi VoicePool::requestStabilityChange) e la si scambia con
    // Voice::swapShifterNoAlloc, mai riconfigurando quella in uso.
    virtual void prepare (double sampleRate, int maxBlockSize, int stabilityLevel) = 0;
    virtual void reset() = 0;

    // Sicuro da chiamare ad ogni blocco, anche con un valore diverso ogni
    // volta: nell'implementazione corrente (SpectralShifter) e' una semplice
    // assegnazione di due float, nessuna allocazione. Necessario per la
    // modalita' Fix (FR-22), che ricalcola il rapporto ogni blocco.
    virtual void setPitchShiftSemitones (float semitones) = 0;

    virtual int getLatencySamples() const = 0;

    // in e out sono buffer mono distinti (non alias) di numSamples campioni.
    virtual void process (const float* in, float* out, int numSamples) = 0;
};

std::unique_ptr<PitchShifter> createDefaultPitchShifter();
