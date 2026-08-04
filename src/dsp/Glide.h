#pragma once

#include <cmath>

// Rampa lineare a DURATA fissa (non a velocita' fissa): un salto del target
// impiega sempre glideTimeMs per essere completato, qualunque sia la sua
// ampiezza. Un valore per blocco, guidato dai campioni trascorsi (robusto a
// buffer di dimensione variabile, NFR-03).
//
// Usato per FR-17: quando l'offset armonico cambia (cambio accordo/fondamentale
// su nota tenuta), il movimento delle voci e' morbido invece di un salto
// istantaneo che clicca. Non alloca mai: utilizzabile in processBlock.
class Glide
{
public:
    void prepare (double sampleRateIn) noexcept { sampleRate = sampleRateIn; }
    void setGlideTimeMs (float ms) noexcept { glideTimeMs = ms; }

    // Inizializza (o re-inizializza) il valore corrente senza rampa, es. al
    // prepareToPlay o al primo utilizzo.
    void reset (float value) noexcept
    {
        current = value;
        target = value;
        remainingSamples = 0;
        totalGlideSamples = 0;
    }

    void setTarget (float newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;
        startValueForCurrentGlide = current; // riparte da dove si trova ora, anche a meta' di una rampa precedente
        totalGlideSamples = glideTimeMs > 0.0f
            ? juce_max1 ((int) std::lround (glideTimeMs * 0.001 * sampleRate))
            : 0;
        remainingSamples = totalGlideSamples;

        if (remainingSamples <= 0)
            current = target; // glide disattivato: salto immediato
    }

    // Da chiamare una volta per blocco con la durata del blocco in campioni;
    // ritorna il valore da applicare per l'intero blocco.
    float process (int numSamples) noexcept
    {
        if (remainingSamples <= 0)
            return current;

        remainingSamples -= numSamples;
        if (remainingSamples <= 0)
        {
            current = target;
            return current;
        }

        const float progress = 1.0f - (float) remainingSamples / (float) totalGlideSamples;
        current = startValueForCurrentGlide + (target - startValueForCurrentGlide) * progress;
        return current;
    }

    float getCurrentValue() const noexcept { return current; }
    // Sessione 12: usato per sapere se una rampa (es. di ampiezza) ha
    // raggiunto stabilmente il target, senza dover confrontare current/target
    // dall'esterno (confronto in virgola mobile fragile su un valore calcolato).
    bool isSettled() const noexcept { return remainingSamples <= 0; }

private:
    static int juce_max1 (int v) noexcept { return v > 1 ? v : 1; }

    double sampleRate = 48000.0;
    float glideTimeMs = 30.0f; // default (FR-17)
    float current = 0.0f;
    float target = 0.0f;
    float startValueForCurrentGlide = 0.0f;
    int remainingSamples = 0;
    int totalGlideSamples = 0;
};
