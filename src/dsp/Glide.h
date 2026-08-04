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

    // Sessione 13: process() ritorna un solo valore per l'intero blocco, per
    // costruzione (necessario per l'offset armonico, FR-17: PitchShifter
    // accetta un unico rapporto per chiamata a process()). Ma un chiamante
    // che deve applicare quel valore come GUADAGNO campione-per-campione
    // (dissolvenza anti-click, dry/wet) non puo' usarlo direttamente: con un
    // blocco host piu' lungo della rampa (es. 4096 campioni a 44.1kHz contro
    // una rampa di 353), process() fa scattare il salto intero in un solo
    // campione — lo stesso identico problema che la rampa doveva eliminare.
    //
    // processRamp() espone la retta INTERA che process() gia' calcola
    // internamente (start/target/durata fissati da setTarget, mai
    // ricalcolati per blocco), cosi' il chiamante puo' interpolare campione
    // per campione. Il calcolo dell'incremento usa i valori CORRENTI
    // (current/remainingSamples) invece che quelli originali del setTarget:
    // per costruzione current e' sempre esattamente sulla retta originale
    // (vedi process()), quindi (target-current)/remainingSamples e'
    // algebricamente identico a (target-start)/totalGlideSamples in
    // qualunque punto della rampa — nessuna deriva quando la stessa rampa e'
    // spezzata su piu' blocchi di dimensione diversa (verificato in
    // tests/glide_test.cpp, TEST 3).
    //
    // rampSamples puo' essere minore di numSamples (la rampa finisce dentro
    // questo blocco: il chiamante deve tenere il valore fermo al target per
    // i campioni restanti) o uguale a numSamples (la rampa continua oltre
    // la fine del blocco). Muta lo stato interno esattamente come process()
    // (infatti lo richiama): va chiamata una sola volta per blocco.
    struct Ramp
    {
        float startValue;
        float increment;
        int rampSamples;
    };

    Ramp processRamp (int numSamples) noexcept
    {
        Ramp r { current, 0.0f, 0 };
        if (remainingSamples <= 0)
            return r;

        r.rampSamples = remainingSamples < numSamples ? remainingSamples : numSamples;
        r.increment = (target - current) / (float) remainingSamples;
        process (numSamples); // avanza lo stato interno esattamente come prima
        return r;
    }

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
