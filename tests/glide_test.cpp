// Verifica numerica della rampa campione-per-campione di Glide (sessione 13,
// indagine "click residui" segnalata dall'utente dopo il fix di dissolvenza
// di sessione 12). Nessuna dipendenza JUCE: Glide.h e' header-only, stesso
// principio di pitch_latch_test.cpp.
//
// Perche' questo test esiste: Glide::process(numSamples) ritorna UN SOLO
// valore per l'intero blocco (per costruzione, documentato nella classe
// stessa) — corretto per un parametro come l'offset armonico (FR-17), che
// per l'interfaccia di PitchShifter deve comunque essere un unico rapporto
// per chiamata a process(). Ma sessione 12 lo ha riusato anche per la
// dissolvenza anti-click (ampGlide) e per dry/wet, applicando il valore
// risultante come un guadagno COSTANTE su tutto il buffer — corretto solo se
// il buffer e' piu' corto della rampa. Con host configurati su buffer grandi
// (es. MME/DirectX senza ASIO, 4096 campioni osservato dall'utente in
// Ableton) una rampa di 8ms (353 campioni a 44.1kHz) diventa un salto in un
// solo campione: esattamente il click che il fix di sessione 12 doveva
// eliminare. Vedi handsoff.md sessione 13 per i numeri completi.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/glide_test.cpp -o glide_test

#include "dsp/Glide.h"

#include <cstdio>
#include <cmath>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }

    // Soglia di "salto udibile" fra due campioni consecutivi: un decimo
    // dell'ampiezza totale della rampa (start=1, target=0 in tutti i test
    // sotto) per campione e' gia' generoso — una vera rampa lineare su
    // centinaia di campioni resta ben al di sotto.
    constexpr float kMaxAllowedStep = 0.01f;

    // Ricostruisce il segnale campione-per-campione prodotto da processRamp
    // per un'intera chiamata (rampa + coda piatta al target), accodandolo a
    // 'out'. Riflette esattamente cio' che Voice::processAdd e
    // PluginProcessor::processBlock fanno dopo il fix di sessione 13.
    void appendRampBlock (Glide& g, int numSamples, std::vector<float>& out)
    {
        const auto ramp = g.processRamp (numSamples);
        float v = ramp.startValue;
        for (int i = 0; i < ramp.rampSamples; ++i)
        {
            out.push_back (v);
            v += ramp.increment;
        }
        for (int i = ramp.rampSamples; i < numSamples; ++i)
            out.push_back (g.getCurrentValue());
    }

    float maxConsecutiveStep (const std::vector<float>& v)
    {
        float worst = 0.0f;
        for (size_t i = 1; i < v.size(); ++i)
            worst = std::max (worst, std::fabs (v[i] - v[i - 1]));
        return worst;
    }
}

int main()
{
    constexpr double kSampleRate = 44100.0; // dallo screenshot Ableton dell'utente, sessione 13
    constexpr float  kDeclickMs  = 8.0f;    // Voice::kDeclickMs / kMixDeclickMs

    std::printf ("TEST 1 - documentazione del bug: process() applicato come costante di blocco\n"
                 "         (comportamento REALE pre-sessione-13, per confronto numerico)\n");
    {
        // Riproduce esattamente cosa faceva Voice::processAdd prima del fix:
        // un solo scalare per l'intero blocco.
        Glide g;
        g.prepare (kSampleRate);
        g.setGlideTimeMs (kDeclickMs);
        g.reset (1.0f);
        g.setTarget (0.0f);

        const float scalarBlock1 = g.process (4096); // 4096: buffer reale osservato dall'utente
        // Un secondo blocco identico non produce piu' nulla (gia' assestato),
        // ma il salto rilevante e' gia' avvenuto DENTRO il primo blocco: il
        // buffer intero (voce.processAdd moltiplica ogni campione per lo
        // stesso scalare) passa da 1.0 a scalarBlock1 in un solo campione.
        check (std::fabs (scalarBlock1 - 0.0f) < 1e-6f,
               "process(4096) con rampa da 353 campioni si assesta gia' nello stesso blocco");
        check (std::fabs (1.0f - scalarBlock1) > 0.99f,
               "il salto di ampiezza applicato a TUTTO il buffer e' quasi pieno scala (bug che processRamp risolve)");
    }

    std::printf ("\nTEST 2 - processRamp: rampa intera dentro un singolo blocco grande (4096 campioni)\n");
    {
        Glide g;
        g.prepare (kSampleRate);
        g.setGlideTimeMs (kDeclickMs);
        g.reset (1.0f);
        g.setTarget (0.0f);

        const int expectedRampSamples = (int) std::lround (kDeclickMs * 0.001 * kSampleRate); // 353

        std::vector<float> signal;
        appendRampBlock (g, 4096, signal);

        check ((int) signal.size() == 4096, "il segnale ricostruito ha la stessa lunghezza del blocco");
        check (std::fabs (signal.front() - 1.0f) < 1e-6f, "il primo campione parte esattamente dal valore corrente (1.0)");
        check (maxConsecutiveStep (signal) <= kMaxAllowedStep,
               "nessun salto fra campioni consecutivi supera la soglia anti-click, nemmeno al confine rampa/coda piatta");
        check (std::fabs (signal.back() - 0.0f) < 1e-6f, "l'ultimo campione del blocco e' al target (0.0)");
        check (g.isSettled(), "la rampa risulta assestata a fine blocco");
        (void) expectedRampSamples;
    }

    std::printf ("\nTEST 3 - processRamp: stessa rampa spezzata su molti blocchi piccoli (64 campioni)\n"
                 "         deve ricostruire un segnale IDENTICO (a meno di arrotondamento) al caso a blocco unico\n");
    {
        Glide gRef;
        gRef.prepare (kSampleRate);
        gRef.setGlideTimeMs (kDeclickMs);
        gRef.reset (1.0f);
        gRef.setTarget (0.0f);
        std::vector<float> reference;
        appendRampBlock (gRef, 4096, reference);

        Glide gChunked;
        gChunked.prepare (kSampleRate);
        gChunked.setGlideTimeMs (kDeclickMs);
        gChunked.reset (1.0f);
        gChunked.setTarget (0.0f);
        std::vector<float> chunked;
        int remaining = 4096;
        while (remaining > 0)
        {
            const int n = std::min (64, remaining);
            appendRampBlock (gChunked, n, chunked);
            remaining -= n;
        }

        check (chunked.size() == reference.size(), "stessa lunghezza totale indipendentemente dal chunking");

        float worstDrift = 0.0f;
        for (size_t i = 0; i < chunked.size() && i < reference.size(); ++i)
            worstDrift = std::max (worstDrift, std::fabs (chunked[i] - reference[i]));
        check (worstDrift < 1e-4f, "nessuna deriva fra la rampa a blocco unico e quella spezzata (stessa retta, ricalcolata ogni blocco senza accumulare errore)");

        check (maxConsecutiveStep (chunked) <= kMaxAllowedStep,
               "nessun salto fra campioni consecutivi supera la soglia, nemmeno ai confini fra i 64 blocchi da 64 campioni");
    }

    std::printf ("\nTEST 4 - processRamp: blocco piu' corto della rampa (64 campioni), un solo blocco\n"
                 "         tutta la rampa resta ancora in corso: nessuna coda piatta in questo blocco\n");
    {
        Glide g;
        g.prepare (kSampleRate);
        g.setGlideTimeMs (kDeclickMs);
        g.reset (1.0f);
        g.setTarget (0.0f);

        const auto ramp = g.processRamp (64);
        check (ramp.rampSamples == 64, "con blocco (64) piu' corto della rampa totale (353), rampSamples copre l'intero blocco");
        check (! g.isSettled(), "la rampa non e' ancora assestata dopo un solo blocco da 64 campioni");
    }

    std::printf ("\nTEST 5 - processRamp: nessun salto al momento del re-target a meta' rampa\n"
                 "         (es. una voce mutata e subito ri-smutata mentre stava ancora sfumando)\n");
    {
        Glide g;
        g.prepare (kSampleRate);
        g.setGlideTimeMs (kDeclickMs);
        g.reset (1.0f);
        g.setTarget (0.0f);

        std::vector<float> signal;
        appendRampBlock (g, 128, signal); // rampa in corso, non ancora assestata
        const float valueBeforeRetarget = g.getCurrentValue();

        g.setTarget (1.0f); // re-target a meta' rampa
        appendRampBlock (g, 128, signal);

        check (std::fabs (signal[128] - valueBeforeRetarget) < 1e-6f,
               "il primo campione dopo il re-target riparte esattamente da dove si trovava la rampa precedente");
        check (maxConsecutiveStep (signal) <= kMaxAllowedStep,
               "nessun salto anche attraverso il punto di re-target");
    }

    std::printf ("\nTEST 6 - process() invariata (offsetGlide, FR-17, resta un valore per blocco di proposito)\n");
    {
        Glide g;
        g.prepare (kSampleRate);
        g.setGlideTimeMs (30.0f); // default musicale FR-17, non l'anti-click
        g.reset (0.0f);
        g.setTarget (12.0f);

        const float v1 = g.process (256);
        const float v2 = g.process (256);
        check (v1 != 0.0f && v1 < 12.0f, "process() continua a restituire un unico valore intermedio per blocco");
        check (v2 > v1, "e progredisce verso il target chiamata dopo chiamata, come richiesto da PitchShifter::setPitchShiftSemitones");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
