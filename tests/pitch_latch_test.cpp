// Verifica numerica dell'isteresi di intonazione (sessione 11, FR-16/FR-17).
// Nessuna dipendenza JUCE: si compila ed esegue in meno di un secondo,
// stesso principio di override_manager_test.cpp.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/pitch_latch_test.cpp -o pitch_latch_test

#include "harmony/PitchLatch.h"

#include <cstdio>
#include <cmath>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }

    // MIDI 60 = C. Un semitono = 100 cent = 1.0 in unita' MIDI frazionarie.
    constexpr float kCent = 0.01f;
}

int main()
{
    std::printf ("TEST 1 - il primo aggancio va sempre alla nota piu' vicina\n");
    {
        harmony::PitchLatch latch;
        check (latch.update (60.3f, false) == 60, "60.3 (non ancora agganciata) -> 60");
    }
    {
        harmony::PitchLatch latch;
        check (latch.update (60.3f, true) == 60, "60.3 con onAttack -> 60 comunque");
    }

    std::printf ("\nTEST 2 - entro +-25 cent dalla nota agganciata non cambia nulla\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true); // aggancio a C
        check (latch.update (60.0f + 20 * kCent, false) == 60, "+20 cent -> resta 60");
        check (latch.update (60.0f - 20 * kCent, false) == 60, "-20 cent -> resta 60");
        check (latch.update (60.0f + 25 * kCent, false) == 60, "esattamente +25 cent -> resta 60 (soglia non superata)");
    }

    std::printf ("\nTEST 3 - oltre la soglia si sposta di un semitono verso la nota vicina\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true);
        check (latch.update (60.0f + 26 * kCent, false) == 60, "26 cent: la nota piu' vicina e' ancora 60 (arrotondamento standard), nessun anticipo");
        check (latch.update (60.0f + 50 * kCent, false) == 61, "50 cent: la nota piu' vicina e' 61 (C#), scatta");
    }
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true);
        check (latch.update (60.0f - 26 * kCent, false) == 60, "-26 cent: la nota piu' vicina e' ancora 60, nessun anticipo");
        // -51 cent, non esattamente -50: allo scarto esatto di mezzo
        // semitono std::lround arrotonda sempre "lontano da zero" (quindi
        // verso 60, non verso 59) — un pareggio esatto che con dati di
        // pitch reali non si presenta mai. Un cent oltre lo evita.
        check (latch.update (60.0f - 51 * kCent, false) == 59, "-51 cent: la nota piu' vicina e' 59 (B), scatta");
    }

    std::printf ("\nTEST 4 - nessun rimbalzo: una volta scattato, non torna indietro se il\n"
                 "         pitch resta fermo li' (bug del design ingenuo 'passo incondizionato')\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true);
        const int afterJump = latch.update (60.0f + 50 * kCent, false);
        check (afterJump == 61, "scatta a 61 esattamente a +50 cent");
        // Stesso identico input al blocco successivo: NON deve tornare a 60.
        const int nextBlock = latch.update (60.0f + 50 * kCent, false);
        check (nextBlock == 61, "blocco successivo, stesso input: resta 61 (nessun rimbalzo)");
    }

    std::printf ("\nTEST 5 - onAttack forza l'aggancio immediato anche entro la tolleranza\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true);
        check (latch.update (65.1f, true) == 65, "onAttack=true salta subito alla nota nuova, nessuna isteresi");
    }

    std::printf ("\nTEST 6 - reset() fa ripartire da un aggancio pulito\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true);
        latch.reset();
        check (latch.update (72.4f, false) == 72, "dopo reset(), il primo update() successivo aggancia subito (come se non fosse mai stata agganciata)");
    }

    std::printf ("\nTEST 7 - un salto ampio si risolve un semitono alla volta su chiamate successive\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, false); // non ancora agganciata: aggancia subito a 60
        // Salto istantaneo (senza onset) a 65.0 (5 semitoni sopra), come in
        // uno slur/portamento strumentale rilevato blocco per blocco.
        int steps = 0;
        int note = 60;
        while (note != 65 && steps < 20)
        {
            note = latch.update (65.0f, false);
            ++steps;
        }
        check (note == 65, "converge a 65 dopo ripetute chiamate");
        check (steps == 5, "esattamente 5 passi da 60 a 65, un semitono a chiamata");
    }

    std::printf ("\nTEST 8 - replica dell'esempio dell'utente: vibrato +-50 cent su C alterna B/C/C#\n");
    {
        harmony::PitchLatch latch;
        latch.update (60.0f, true); // aggancio a C

        // Salita graduale fino al picco positivo (+50 cent).
        int noteAtPositivePeak = 60;
        for (int c = 0; c <= 50; c += 5)
            noteAtPositivePeak = latch.update (60.0f + (float) c * kCent, false);
        check (noteAtPositivePeak == 61, "picco positivo (+50 cent) -> C# (61)");

        // Discesa graduale oltre il picco negativo (fino a -55 cent: -50
        // esatto e' un pareggio di arrotondamento, vedi nota nel TEST 3).
        int noteAtNegativePeak = noteAtPositivePeak;
        for (int c = 50; c >= -55; c -= 5)
            noteAtNegativePeak = latch.update (60.0f + (float) c * kCent, false);
        check (noteAtNegativePeak == 59, "picco negativo (oltre -50 cent) -> B (59)");

        // Risalita al centro: torna a C.
        int noteBackAtCentre = latch.update (60.0f, false);
        check (noteBackAtCentre == 60, "di ritorno al centro -> C (60)");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
