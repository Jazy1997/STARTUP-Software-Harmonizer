// Verifica della logica di precedenza CC vs automazione (FR-36/37/38).
// Logica deterministica, nessuna dipendenza JUCE: si compila ed esegue in
// meno di un secondo (CLAUDE.md, "non puoi ascoltare" si applica anche a
// "non hai un controller MIDI qui" — questo e' il criterio verificabile al
// posto di premere davvero un pad hardware).
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/override_manager_test.cpp src/midi/OverrideManager.cpp -o override_manager_test

#include "midi/OverrideManager.h"

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
    // TEST 1 - senza nessun CC mai ricevuto, i valori effettivi sono
    // sempre quelli host (nessun override attivo di default).
    std::printf ("TEST 1 - pass-through senza override\n");
    {
        OverrideManager mgr;
        const auto eff = mgr.resolve ({}, /*hostRoot*/ 4, /*hostPreset*/ 2, /*hostBypass*/ false);
        check (eff.rootPitchClass == 4, "root = valore host");
        check (eff.presetOneBased == 2, "preset = valore host");
        check (eff.bypassed == false, "bypass = valore host");
    }

    // TEST 2 - un CC sulla root attiva l'override SOLO per la root; preset
    // e bypass restano governati dall'host (FR-36, "per quel parametro").
    std::printf ("\nTEST 2 - override indipendente per parametro\n");
    {
        OverrideManager mgr;
        OverrideManager::CcEvents ev;
        ev.root = { true, 7 };
        const auto eff = mgr.resolve (ev, 4, 2, false);
        check (eff.rootPitchClass == 7, "root = valore CC");
        check (eff.presetOneBased == 2, "preset resta = valore host (non toccato)");
        check (eff.bypassed == false, "bypass resta = valore host (non toccato)");
    }

    // TEST 3 - l'override persiste sui blocchi successivi anche se l'host
    // continua a scrivere automazione (nessun nuovo evento CC in quei
    // blocchi): il valore CC resta quello che vince.
    std::printf ("\nTEST 3 - l'override persiste senza nuovi eventi CC\n");
    {
        OverrideManager mgr;
        OverrideManager::CcEvents withCc;
        withCc.preset = { true, 5 };
        mgr.resolve (withCc, 4, 2, false); // blocco con CC

        // blocchi successivi: nessun evento CC, ma l'host "automatizza" un
        // valore diverso -> deve restare ignorato finche' l'override e' attivo.
        const auto eff1 = mgr.resolve ({}, 4, 9, false);
        const auto eff2 = mgr.resolve ({}, 4, 11, false);
        check (eff1.presetOneBased == 5, "blocco N+1: ancora il valore CC, non l'automazione host");
        check (eff2.presetOneBased == 5, "blocco N+2: ancora il valore CC");
    }

    // TEST 4 - FR-36: clearOverrides() (simula lo stop del transport) fa
    // tornare il controllo all'automazione host.
    std::printf ("\nTEST 4 - clearOverrides() restituisce il controllo all'host\n");
    {
        OverrideManager mgr;
        OverrideManager::CcEvents ev;
        ev.bypass = { true, 127 };
        mgr.resolve (ev, 4, 2, false);
        check (mgr.resolve ({}, 4, 2, false).bypassed == true, "override bypass attivo prima dello stop");

        mgr.clearOverrides();
        const auto eff = mgr.resolve ({}, 4, 2, /*hostBypass*/ false);
        check (eff.bypassed == false, "dopo clearOverrides(): torna il valore host");
    }

    // TEST 5 - FR-38: in caso di piu' sorgenti CC in conflitto, l'ultimo
    // ricevuto vince — soddisfatto per costruzione (ogni evento sovrascrive
    // il precedente), verificato con due eventi in blocchi consecutivi.
    std::printf ("\nTEST 5 - l'ultimo CC ricevuto vince (FR-38)\n");
    {
        OverrideManager mgr;
        OverrideManager::CcEvents first;
        first.root = { true, 3 };
        mgr.resolve (first, 0, 1, false);

        OverrideManager::CcEvents second;
        second.root = { true, 9 };
        const auto eff = mgr.resolve (second, 0, 1, false);
        check (eff.rootPitchClass == 9, "il secondo CC (piu' recente) sovrascrive il primo");
    }

    // TEST 6 - la soglia bypass (booleano) e' quella che il chiamante
    // passa gia' interpretata (CcRouter applica la soglia 0-63/64-127,
    // FR-30): OverrideManager si limita a propagare value != 0.
    std::printf ("\nTEST 6 - bypass: value != 0 -> true, value == 0 -> false\n");
    {
        OverrideManager mgr;
        OverrideManager::CcEvents on;
        on.bypass = { true, 1 };
        check (mgr.resolve (on, 0, 1, false).bypassed == true, "value=1 -> bypassed=true");

        OverrideManager mgr2;
        OverrideManager::CcEvents off;
        off.bypass = { true, 0 };
        check (mgr2.resolve (off, 0, 1, true).bypassed == false, "value=0 -> bypassed=false (anche con hostBypass=true)");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
