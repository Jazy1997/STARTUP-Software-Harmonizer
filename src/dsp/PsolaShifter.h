#pragma once

#include "PitchShifter.h"

#include <vector>

// TD-PSOLA (Time-Domain Pitch Synchronous Overlap-Add) in streaming.
//
// Algoritmo di dominio pubblico, portato da un'implementazione esterna
// verificata numericamente (5 test su segnale sintetico, errore di
// trasposizione < 0.01 cent — vedi handsoff.md sessione 8/9). Nessuna
// dipendenza JUCE nell'algoritmo stesso: solo questa classe conosce il
// dettaglio, come richiesto da CLAUDE.md regola 2 / FR-62.
//
// Principio: il pitch in uscita dipende ESCLUSIVAMENTE dalla spaziatura
// degli epoch di sintesi (periodo / alpha). Le formanti dipendono
// ESCLUSIVAMENTE dal ricampionamento del grano (beta). Con beta = 1 il
// grano non viene toccato e le formanti dell'originale sono preservate
// per costruzione: i due controlli sono ortogonali (FR-39/FR-41),
// verificato dai test 2/3 in tests/psola_test.cpp.
class PsolaShifter final : public PitchShifter
{
public:
    void prepare (double sampleRate, int maxBlockSize, int stabilityLevel) override;
    void reset() override;

    void setPitchShiftSemitones (float semitones) override;
    void setInputF0Hz (double f0Hz) override { currentF0Hz = f0Hz; }
    void setFormantRatio (double beta) override;

    int getLatencySamples() const override { return latency; }

    void process (const float* in, float* out, int numSamples) override;

private:
    // Elabora un'unica fetta di al piu' maxBlock campioni: e' la vera unita'
    // su cui la formula di latenza (2*maxPeriod + maxBlock) e' valida.
    // process() la richiama in un ciclo per svincolare la latenza
    // dichiarata dal maxBlockSize (potenzialmente grande, dimensionato sul
    // caso peggiore) che l'host passa a prepare().
    void processChunk (const float* in, float* out, int numSamples);

    static int nextPow2 (int v);
    static double clampd (double v, double lo, double hi);

    int currentPeriod() const;
    float readInterp (double pos) const;
    void detectEpochs();
    void synthesise();
    long long nearestEpoch (long long t) const;
    void emitGrain (long long analysisEpoch, long long synthEpoch, double frac, int P);

    double sr        = 48000.0;
    double minF0     = 70.0;
    double currentF0Hz = 0.0;
    double alpha     = 1.0;
    double beta      = 1.0;

    int  minPeriod   = 24;
    int  maxPeriod   = 686;
    int  maxBlock    = 512;
    int  latency     = 0;
    int  bufSize     = 0;
    long long mask   = 0;

    long long absWrite  = 0;
    long long absRead   = 0;
    long long lastEpoch = -1;
    double    synthPos  = -1.0;

    std::vector<float> inBuf, outBuf, envBuf;

    // Ring buffer di epoch a capacita' fissa, pre-allocato in prepare()
    // (message thread). Sostituisce un precedente std::deque<long long> che
    // allocava/deallocava a blocchi dentro process() (push_back/pop_front),
    // una violazione delle regole di threading di CLAUDE.md/PRD §9.4 — su
    // MSVC particolarmente grave perche' il blocco del deque e' da soli 16
    // byte. Capacita' dimensionata in prepare() su bufSize/minPeriod, con
    // ampio margine: gli epoch distano sempre >= minPeriod campioni e
    // vivono in una finestra di bufSize/2 (vedi detectEpochs()).
    std::vector<long long> epochRing;
    size_t epochCapacity = 0;
    size_t epochHead     = 0; // indice del piu' vecchio
    size_t epochCount    = 0;

    void epochPushBack (long long value) noexcept;
    void epochPopFront() noexcept;
    long long epochFront() const noexcept { return epochRing[epochHead]; }
    bool epochEmpty() const noexcept { return epochCount == 0; }
    void epochClear() noexcept { epochHead = 0; epochCount = 0; }
};
