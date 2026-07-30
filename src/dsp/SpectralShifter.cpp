#include "SpectralShifter.h"
#include <algorithm>

namespace
{
    // Finestra di analisi in secondi per ciascun livello di Stability
    // (Fast..Accurate). L'intervallo e' 1/4 della finestra, come nel preset
    // di default di Signalsmith Stretch (block=0.12s, interval=0.03s).
    //
    // NOTA: anche il livello piu' reattivo (30 ms) e' ben oltre il target di
    // <=15 ms del PRD (§1.3): e' un limite del motore STFT interinale, non
    // raggiungibile finche' non si passa al PSOLA proprietario (§9.2 del PRD
    // spiega perche' PSOLA su sorgente monofonica arriva a 7-13 ms).
    constexpr float windowSeconds[Stability::numLevels] = { 0.03f, 0.06f, 0.09f, 0.12f, 0.18f };
}

void SpectralShifter::prepare (double sampleRate, int /*maxBlockSize*/, int stabilityLevel)
{
    const int level = std::clamp (stabilityLevel, 0, Stability::numLevels - 1);
    const float windowSize = windowSeconds[level] * (float) sampleRate;
    const float interval = windowSize * 0.25f;

    stretch.configure (1, (int) windowSize, (int) interval);
}

void SpectralShifter::reset()
{
    stretch.reset();
}

void SpectralShifter::setPitchShiftSemitones (float semitones)
{
    // Verificato leggendo signalsmith-stretch.h: assegna solo due float e
    // azzera un puntatore a funzione gia' nullo (non usiamo mai setFreqMap).
    // Nessuna allocazione: sicuro da chiamare ogni blocco (serve alla
    // modalita' Fix, FR-22, che ricalcola il rapporto in continuo).
    stretch.setTransposeSemitones (semitones);
}

int SpectralShifter::getLatencySamples() const
{
    return stretch.inputLatency() + stretch.outputLatency();
}

void SpectralShifter::process (const float* in, float* out, int numSamples)
{
    const float* const inputs[1] = { in };
    float* const outputs[1] = { out };
    stretch.process (inputs, numSamples, outputs, numSamples);
}

std::unique_ptr<PitchShifter> createDefaultPitchShifter()
{
    return std::make_unique<SpectralShifter>();
}
