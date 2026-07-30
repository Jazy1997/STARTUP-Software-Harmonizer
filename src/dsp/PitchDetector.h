#pragma once

#include <memory>

// Wrapper monofonico su Cycfi Q (bacf pitch detector). Sorgenti da ottimizzare
// in ordine (FR-14): voce, sax, tromba.
//
// prepare() alloca (message thread / prepareToPlay). pushSample() e i getter
// non allocano mai: rispettano le regole di threading di CLAUDE.md regola 1.
class PitchDetector
{
public:
    PitchDetector();
    ~PitchDetector();

    void prepare (double sampleRate);
    void reset();

    // Da chiamare una volta per sample. Ritorna true quando la stima e' stata
    // aggiornata in questo sample (vedi doc Cycfi Q: get_frequency()/periodicity()
    // vanno letti solo quando pushSample() ritorna true).
    bool pushSample (float sample) noexcept;

    // Valida solo se hasStableSignal() e' true nello stesso momento.
    float getMidiNote() const noexcept { return currentMidiNote; }
    float getConfidence() const noexcept { return currentConfidence; }
    bool hasStableSignal() const noexcept;

private:
    // unique_ptr (non optional): il pattern pimpl richiede che l'implicito
    // distruttore generato per unique_ptr<Impl> resti "differito" fino al .cpp
    // dove Impl e' completo — std::optional<T> instanzia i type-trait di T
    // (is_trivially_destructible ecc.) ovunque la classe venga inclusa, e non
    // compila con un tipo incompleto.
    struct Impl;
    std::unique_ptr<Impl> impl;

    float currentMidiNote = -1.0f;
    float currentConfidence = 0.0f;
};
