// Verifica numerica della scala della finestra (sessione 33, FR-59).
// Nessuna dipendenza JUCE: si compila ed esegue in meno di un secondo, stesso
// principio di cell_input_parser_test.cpp e glide_test.cpp.
//
// COSA PUO' E COSA NON PUO' DIRE QUESTA SUITE. Puo' dire che l'aritmetica della
// scala e' coerente: che il giro percentuale -> pixel -> percentuale torna al
// punto di partenza, che gli estremi di FR-59 danno i numeri attesi, e che i
// limiti di ridimensionamento non contraddicono il rapporto d'aspetto bloccato.
// NON puo' dire nulla su come la finestra appare davvero: che i testi non siano
// sfocati su un display HiDPI e' una verifica visiva dell'utente, l'analogo per
// gli occhi di quello che CLAUDE.md regola 12 dice per le orecchie.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/ui_scale_test.cpp -o ui_scale_test

#include "ui/UiScale.h"

#include <cstdio>

namespace
{
    int failures = 0;

    // NB: "description" e' un ARGOMENTO di printf, non la sua stringa di
    // formato — dentro va un singolo '%', non '%%'.
    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }
}

int main()
{
    std::printf ("TEST 1 - gli estremi di FR-59 danno esattamente i limiti della finestra\n");
    {
        check (ui::physicalWidthForPercent (70) == 630,    "70% -> larghezza 630");
        check (ui::physicalHeightForPercent (70) == 462,   "70% -> altezza 462");
        check (ui::physicalWidthForPercent (100) == 900,   "100% -> larghezza 900 (identica al vecchio setSize)");
        check (ui::physicalHeightForPercent (100) == 660,  "100% -> altezza 660 (identica al vecchio setSize)");
        check (ui::physicalWidthForPercent (200) == 1800,  "200% -> larghezza 1800");
        check (ui::physicalHeightForPercent (200) == 1320, "200% -> altezza 1320");
    }

    std::printf ("\nTEST 2 - round-trip esatto su TUTTO il range, non solo sulle tacche del menu\n"
                 "         (percentuale -> larghezza -> percentuale). E' la proprieta' che si\n"
                 "         rompe se la larghezza logica diventa un valore che rende la mappa\n"
                 "         non iniettiva: sotto i 100 px logici due percentuali diverse\n"
                 "         darebbero la stessa larghezza e la finestra 'scatterebbe' da sola.\n");
    {
        int worst = -1;
        for (int p = ui::kMinScalePercent; p <= ui::kMaxScalePercent; ++p)
            if (ui::scalePercentFromWidth (ui::physicalWidthForPercent (p)) != p)
                worst = p;

        check (worst < 0, "ogni percentuale in [70, 200] torna identica a se stessa");
        if (worst >= 0)
            std::printf ("      ultimo valore fallito: %d%% -> %d px -> %d%%\n",
                         worst, ui::physicalWidthForPercent (worst),
                         ui::scalePercentFromWidth (ui::physicalWidthForPercent (worst)));

        check (ui::kLogicalWidth >= 100, "la larghezza logica da' almeno 1 px per punto percentuale");
    }

    std::printf ("\nTEST 3 - i limiti di setResizeLimits hanno lo STESSO rapporto d'aspetto del\n"
                 "         layout logico. E' l'ipotesi su cui poggia setFixedAspectRatio: se i\n"
                 "         due si contraddicessero, il constrainer non potrebbe soddisfarli\n"
                 "         entrambi e la finestra resterebbe incastrata a un estremo.\n");
    {
        const double logical = (double) ui::kLogicalWidth / (double) ui::kLogicalHeight;
        const double atMin = (double) ui::physicalWidthForPercent (ui::kMinScalePercent)
                             / (double) ui::physicalHeightForPercent (ui::kMinScalePercent);
        const double atMax = (double) ui::physicalWidthForPercent (ui::kMaxScalePercent)
                             / (double) ui::physicalHeightForPercent (ui::kMaxScalePercent);

        check (atMin > logical - 1.0e-9 && atMin < logical + 1.0e-9, "rapporto al 70% identico a quello logico");
        check (atMax > logical - 1.0e-9 && atMax < logical + 1.0e-9, "rapporto al 200% identico a quello logico");
        std::printf ("      logico %.6f   al 70%% %.6f   al 200%% %.6f\n", logical, atMin, atMax);
    }

    std::printf ("\nTEST 4 - il clamp regge su stato di sessione corrotto o troncato\n"
                 "         (la percentuale arriva da XML che qualcuno puo' aver modificato:\n"
                 "         va riportata dentro il range, mai rifiutata lasciando la finestra\n"
                 "         a dimensione zero)\n");
    {
        check (ui::clampScalePercent (0) == 70,       "0 -> 70 (nodo presente ma vuoto)");
        check (ui::clampScalePercent (-5) == 70,      "-5 -> 70");
        check (ui::clampScalePercent (69) == 70,      "69 -> 70 (appena sotto il minimo)");
        check (ui::clampScalePercent (201) == 200,    "201 -> 200 (appena sopra il massimo)");
        check (ui::clampScalePercent (100000) == 200, "100000 -> 200");
        check (ui::clampScalePercent (100) == 100,    "100 -> 100 (valore valido, invariato)");

        // Stessa difesa dal lato larghezza: e' il percorso che gira davvero a
        // ogni resized(), anche prima che la finestra abbia una dimensione.
        check (ui::scalePercentFromWidth (0) == 70,       "larghezza 0 -> 70% (finestra non ancora dimensionata)");
        check (ui::scalePercentFromWidth (-100) == 70,    "larghezza negativa -> 70%");
        check (ui::scalePercentFromWidth (100000) == 200, "larghezza assurda -> 200%");
    }

    std::printf ("\nTEST 5 - le tacche del menu sono ordinate e dentro il range\n"
                 "         (il menu su Impostazioni le mostra nell'ordine in cui stanno\n"
                 "         nell'array: un valore fuori posto sarebbe una voce che\n"
                 "         rimpicciolisce scendendo nella lista)\n");
    {
        bool inRange = true, ascending = true;
        for (int i = 0; i < ui::kNumScalePresets; ++i)
        {
            if (ui::kScalePresets[i] < ui::kMinScalePercent || ui::kScalePresets[i] > ui::kMaxScalePercent)
                inRange = false;
            if (i > 0 && ui::kScalePresets[i] <= ui::kScalePresets[i - 1])
                ascending = false;
        }

        check (ui::kNumScalePresets > 0, "il menu non e' vuoto");
        check (inRange, "ogni tacca sta dentro [70, 200]");
        check (ascending, "le tacche sono strettamente crescenti");
        check (ui::clampScalePercent (ui::kDefaultScalePercent) == ui::kDefaultScalePercent,
               "il default (100%) e' esso stesso un valore valido");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
