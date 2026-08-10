// Verifica numerica dell'isteresi FR-17 (sessione 28 — "ribattuto").
// Nessuna dipendenza JUCE: EmptyCellHold.h e' header-only, stesso principio
// di pitch_latch_test.cpp/glide_test.cpp.
//
// Vedi src/voices/EmptyCellHold.h per il ragionamento completo: Fase 0
// (LOG/archivio-s01-s28.md sessione 28) ha misurato col codice vero che il "ribattuto"
// sul materiale di riferimento non e' un ri-attacco vero (il gate non si
// chiude mai, un solo binding di slot per l'intero file) ma il mute/unmute
// di Voice::setMuted() sulla STESSA frase viva ogni volta che la melodia
// attraversa un grado non compilato. Questo test verifica solo la funzione
// pura che decide QUANDO scattare il mute — l'integrazione con
// PhraseScheduler resta verificata a lettura + con sample_click_finder.cpp
// (Passata 6) sul materiale reale, dato che PhraseScheduler dipende da
// juce_core e non e' linkabile qui.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/empty_cell_hold_test.cpp -o empty_cell_hold_test

#include "voices/EmptyCellHold.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }
}

int main()
{
    std::printf ("Verifica di EmptyCellHold — isteresi del mute su cella vuota (FR-17, sessione 28)\n\n");

    constexpr int holdSamples = 3528; // 80ms a 44100Hz, stesso ordine di grandezza discusso per L1

    // ---------------------------------------------------------------------
    std::printf ("Cella piena: il conteggio resta a zero, nessun mute, a prescindere dal valore precedente\n");
    {
        auto r1 = stepEmptyCellHold (0, 512, /*cellHasValue*/ true, holdSamples);
        check (r1.emptySamplesAfter == 0 && ! r1.shouldMuteNow, "da conteggio zero, cella piena -> resta zero, nessun mute");

        auto r2 = stepEmptyCellHold (holdSamples + 1000, 512, /*cellHasValue*/ true, holdSamples);
        check (r2.emptySamplesAfter == 0 && ! r2.shouldMuteNow, "da conteggio gia' oltre soglia, cella piena -> azzera comunque, nessun mute");
    }

    // ---------------------------------------------------------------------
    std::printf ("\nPassaggio BREVE (sotto soglia): mai shouldMuteNow, anche su piu' blocchi consecutivi\n");
    {
        int empty = 0;
        bool anyMute = false;
        const int blockSize = 256;
        int totalEmpty = 0;
        while (totalEmpty + blockSize < holdSamples) // resta rigorosamente sotto soglia
        {
            auto r = stepEmptyCellHold (empty, blockSize, false, holdSamples);
            empty = r.emptySamplesAfter;
            totalEmpty += blockSize;
            if (r.shouldMuteNow) anyMute = true;
        }
        check (! anyMute, "nessun blocco sotto soglia ha fatto scattare il mute");
        check (empty == totalEmpty, "il conteggio segue esattamente i campioni vuoti accumulati");

        // Il passaggio finisce PRIMA della soglia: la cella torna piena.
        auto rBack = stepEmptyCellHold (empty, blockSize, true, holdSamples);
        check (rBack.emptySamplesAfter == 0 && ! rBack.shouldMuteNow,
              "il ritorno a una cella piena prima della soglia azzera tutto, mai mutato");
    }

    // ---------------------------------------------------------------------
    std::printf ("\nVuoto PERSISTENTE (oltre soglia): scatta shouldMuteNow esattamente quando la soglia e' superata\n");
    {
        int empty = 0;
        const int blockSize = 256;
        bool sawMute = false;
        int muteAtSample = -1;
        for (int i = 0; i < 40; ++i) // 40*256 = 10240 campioni, ben oltre holdSamples=3528
        {
            auto r = stepEmptyCellHold (empty, blockSize, false, holdSamples);
            const bool crossedThisBlock = r.shouldMuteNow && ! sawMute;
            if (crossedThisBlock) { sawMute = true; muteAtSample = r.emptySamplesAfter; }
            empty = r.emptySamplesAfter;
        }
        check (sawMute, "un vuoto che persiste ben oltre la soglia fa scattare il mute prima o poi");
        check (muteAtSample >= holdSamples, "il mute scatta solo a soglia raggiunta o superata, mai prima");
        check (muteAtSample < holdSamples + blockSize, "il mute scatta al PRIMO blocco che supera la soglia, non tardi");
    }

    // ---------------------------------------------------------------------
    std::printf ("\nSoglia zero (comportamento precedente a questa sessione): muta al primo blocco vuoto, sempre\n");
    {
        auto r = stepEmptyCellHold (0, 1, false, /*holdThresholdSamples*/ 0);
        check (r.shouldMuteNow, "soglia zero -> mute immediato, coerente col comportamento pre-isteresi");
    }

    // ---------------------------------------------------------------------
    std::printf ("\nEsattezza del conteggio con block size non multipli della soglia\n");
    {
        // holdSamples=3528 non e' multiplo di 4096 (block size reale
        // osservato in Ableton, sessione 13): la soglia deve scattare dentro
        // il blocco che la attraversa, non essere ignorata per un resto.
        auto r = stepEmptyCellHold (0, 4096, false, holdSamples);
        check (r.emptySamplesAfter == 4096, "il conteggio e' esatto anche con un blocco piu' grande della soglia");
        check (r.shouldMuteNow, "un blocco singolo piu' grande della soglia fa scattare il mute nello stesso blocco");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
