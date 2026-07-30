#pragma once

#include <memory>

// Rilevamento onset (FR-43): inviluppo di picco + gate con doppia soglia
// (livello assoluto O pendenza rapida — cattura anche attacchi morbidi).
// Un EVENTO di onset e' il fronte di salita del gate (chiuso -> aperto):
// pushSample() ritorna true esattamente nel sample in cui questo accade.
//
// pimpl con unique_ptr (non optional): vedi il commento in PitchDetector.h
// sul perche' std::optional<Impl> non funziona con un tipo incompleto.
class OnsetDetector
{
public:
    OnsetDetector();
    ~OnsetDetector();

    void prepare (double sampleRate);
    void reset();

    bool pushSample (float sample) noexcept;

    bool isGateOpen() const noexcept { return gateOpen; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool gateOpen = false;
};
