// Verifica numerica dell'isteresi di intonazione (sessione 11, FR-16/FR-17;
// riscritto in parte in sessione 31, B-13).
// Nessuna dipendenza JUCE: si compila ed esegue in meno di un secondo,
// stesso principio di override_manager_test.cpp.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/pitch_latch_test.cpp -o pitch_latch_test

#include "harmony/PitchLatch.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

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

    constexpr double kSampleRate = 44100.0;

    // Sessione 31/32: update() misura in CAMPIONI l'attesa prima di adottare
    // una nota nuova, e l'attesa arriva dal chiamante — e' un multiplo del
    // frame d'analisi del rilevatore (PitchLatch::settleSamplesForFrame), non
    // piu' una costante in millisecondi. Qui si usa il frame a 60 Hz (736
    // campioni = 16.7 ms), cioe' la configurazione fino a s.31: l'attesa che
    // ne esce e' 1104 campioni = 25.0 ms, esattamente la vecchia soglia, e i
    // test scritti allora restano confrontabili.
    constexpr int kFrameSamplesAt60Hz = 736;
    const int kSettleSamples = harmony::PitchLatch::settleSamplesForFrame (kFrameSamplesAt60Hz);

    // I test che vogliono osservare l'adozione in una sola chiamata passano un
    // blocco piu' lungo dell'attesa; quelli che vogliono osservare l'attesa
    // passano un blocco corto.
    constexpr int kBlockOverSettle = 2048;  // 46.4 ms a 44.1 kHz
    constexpr int kBlockShort = 128;        //  2.9 ms a 44.1 kHz

    harmony::PitchLatch makeLatch()
    {
        harmony::PitchLatch latch;
        latch.prepare (kSettleSamples);
        return latch;
    }
}

int main()
{
    std::printf ("TEST 1 - il primo aggancio va sempre alla nota piu' vicina\n");
    {
        auto latch = makeLatch();
        check (latch.update (60.3f, false, kBlockOverSettle) == 60, "60.3 (non ancora agganciata) -> 60");
    }
    {
        auto latch = makeLatch();
        check (latch.update (60.3f, true, kBlockOverSettle) == 60, "60.3 con onAttack -> 60 comunque");
    }

    std::printf ("\nTEST 2 - entro +-25 cent dalla nota agganciata non cambia nulla\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle); // aggancio a C
        check (latch.update (60.0f + 20 * kCent, false, kBlockOverSettle) == 60, "+20 cent -> resta 60");
        check (latch.update (60.0f - 20 * kCent, false, kBlockOverSettle) == 60, "-20 cent -> resta 60");
        check (latch.update (60.0f + 25 * kCent, false, kBlockOverSettle) == 60, "esattamente +25 cent -> resta 60 (soglia non superata)");
    }

    std::printf ("\nTEST 3 - oltre la soglia si sposta sulla nota piu' vicina\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle);
        check (latch.update (60.0f + 26 * kCent, false, kBlockOverSettle) == 60, "26 cent: la nota piu' vicina e' ancora 60 (arrotondamento standard), nessun anticipo");
        check (latch.update (60.0f + 50 * kCent, false, kBlockOverSettle) == 61, "50 cent: la nota piu' vicina e' 61 (C#), scatta");
    }
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle);
        check (latch.update (60.0f - 26 * kCent, false, kBlockOverSettle) == 60, "-26 cent: la nota piu' vicina e' ancora 60, nessun anticipo");
        // -51 cent, non esattamente -50: allo scarto esatto di mezzo
        // semitono std::lround arrotonda sempre "lontano da zero" (quindi
        // verso 60, non verso 59) — un pareggio esatto che con dati di
        // pitch reali non si presenta mai. Un cent oltre lo evita.
        check (latch.update (60.0f - 51 * kCent, false, kBlockOverSettle) == 59, "-51 cent: la nota piu' vicina e' 59 (B), scatta");
    }

    std::printf ("\nTEST 4 - nessun rimbalzo: una volta scattato, non torna indietro se il\n"
                 "         pitch resta fermo li' (bug del design ingenuo 'passo incondizionato')\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle);
        const int afterJump = latch.update (60.0f + 50 * kCent, false, kBlockOverSettle);
        check (afterJump == 61, "scatta a 61 esattamente a +50 cent");
        // Stesso identico input al blocco successivo: NON deve tornare a 60.
        const int nextBlock = latch.update (60.0f + 50 * kCent, false, kBlockOverSettle);
        check (nextBlock == 61, "blocco successivo, stesso input: resta 61 (nessun rimbalzo)");
    }

    std::printf ("\nTEST 5 - onAttack forza l'aggancio immediato anche entro la tolleranza\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle);
        check (latch.update (65.1f, true, kBlockShort) == 65, "onAttack=true salta subito alla nota nuova, nessuna isteresi e nessuna attesa");
    }

    std::printf ("\nTEST 6 - reset() fa ripartire da un aggancio pulito\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle);
        latch.reset();
        check (latch.update (72.4f, false, kBlockShort) == 72, "dopo reset(), il primo update() successivo aggancia subito (come se non fosse mai stata agganciata)");
    }

    // -------------------------------------------------------------------
    // SESSIONE 31 (B-13) — questo test asseriva l'esatto contrario:
    // "esattamente 5 passi da 60 a 65, un semitono a chiamata". Non era un
    // test sbagliato per distrazione, era la specifica di allora messa per
    // iscritto; ma quel comportamento e' il difetto misurato in s.31, perche'
    // ognuna delle 4 note intermedie e' una COLONNA DIVERSA della tabella
    // armonica, letta e suonata davvero per un blocco intero (23 ms a 1024
    // campioni). Vedi BUGS.md B-13 e PitchLatch.h. CLAUDE.md regola 13: la
    // sostituzione e' deliberata e motivata, non un adeguamento silenzioso
    // del test al nuovo codice.
    // -------------------------------------------------------------------
    std::printf ("\nTEST 7 - un salto ampio si risolve in UN COLPO, mai per note intermedie\n"
                 "         (riscritto in s.31: prima asseriva il passo di un semitono per chiamata)\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, false, kBlockOverSettle); // non ancora agganciata: aggancia subito a 60
        // Salto istantaneo (senza onset) a 65.0, come lo produce
        // PitchDetector fra due note staccate: la stima NON attraversa le
        // note intermedie, salta (misurato in s.31 su materiale reale).
        const int afterFirst = latch.update (65.0f, false, kBlockOverSettle);
        check (afterFirst == 65, "una sola chiamata (blocco piu' lungo dell'attesa) porta da 60 a 65");

        auto stepwise = makeLatch();
        stepwise.update (60.0f, false, kBlockOverSettle);
        bool anyIntermediate = false;
        for (int i = 0; i < 40; ++i)
        {
            const int note = stepwise.update (65.0f, false, kBlockShort);
            if (note != 60 && note != 65)
                anyIntermediate = true;
        }
        check (! anyIntermediate, "anche a blocchi corti non restituisce mai 61/62/63/64: o 60 o 65");
    }

    std::printf ("\nTEST 8 - replica dell'esempio dell'utente: vibrato +-50 cent su C alterna B/C/C#\n");
    {
        auto latch = makeLatch();
        latch.update (60.0f, true, kBlockOverSettle); // aggancio a C

        // Salita graduale fino al picco positivo (+50 cent).
        int noteAtPositivePeak = 60;
        for (int c = 0; c <= 50; c += 5)
            noteAtPositivePeak = latch.update (60.0f + (float) c * kCent, false, kBlockOverSettle);
        check (noteAtPositivePeak == 61, "picco positivo (+50 cent) -> C# (61)");

        // Discesa graduale oltre il picco negativo (fino a -55 cent: -50
        // esatto e' un pareggio di arrotondamento, vedi nota nel TEST 3).
        int noteAtNegativePeak = noteAtPositivePeak;
        for (int c = 50; c >= -55; c -= 5)
            noteAtNegativePeak = latch.update (60.0f + (float) c * kCent, false, kBlockOverSettle);
        check (noteAtNegativePeak == 59, "picco negativo (oltre -50 cent) -> B (59)");

        // Risalita al centro: torna a C.
        int noteBackAtCentre = latch.update (60.0f, false, kBlockOverSettle);
        check (noteBackAtCentre == 60, "di ritorno al centro -> C (60)");
    }

    // -------------------------------------------------------------------
    // SESSIONE 31 — i tre gruppi nuovi
    // -------------------------------------------------------------------
    std::printf ("\nTEST 9 - nessuna nota intermedia, per qualunque ampiezza di salto e in\n"
                 "         entrambe le direzioni (l'invariante che protegge la tabella armonica)\n");
    {
        bool allClean = true;
        int worstJump = 0;
        for (int jump = -12; jump <= 12; ++jump)
        {
            if (jump == 0)
                continue;

            const int from = 60;
            const int to = from + jump;

            auto latch = makeLatch();
            latch.update ((float) from, true, kBlockOverSettle);

            for (int i = 0; i < 40; ++i)
            {
                const int note = latch.update ((float) to, false, kBlockShort);
                const bool isEndpoint = (note == from || note == to);
                if (! isEndpoint)
                {
                    allClean = false;
                    worstJump = jump;
                }
            }
        }
        check (allClean, "salti da -12 a +12 semitoni: mai un valore diverso da partenza o arrivo");
        if (! allClean)
            std::printf ("    (primo salto che ha prodotto una nota intermedia: %+d semitoni)\n", worstJump);
    }

    std::printf ("\nTEST 10 - stesso esito a 128 e a 1024 campioni di buffer: la soglia e' in\n"
                 "          millisecondi, non in numero di chiamate (requisito dell'utente, s.31)\n");
    {
        // Traiettoria in TEMPO, non in chiamate: 200 ms fermi su 60, poi
        // 200 ms fermi su 64.2. Ogni block size vede lo stesso segnale.
        constexpr double kHoldMs = 200.0;
        const int blockSizes[] = { 128, 512, 1024, 4096 };

        bool sameSequence = true;
        bool latencyWithinBound = true;

        for (int block : blockSizes)
        {
            auto latch = makeLatch();
            std::vector<int> distinctNotes;
            double jumpAdoptedMs = -1.0;

            const double blockMs = 1000.0 * block / kSampleRate;
            const int numBlocks = (int) std::ceil (2.0 * kHoldMs / blockMs);

            for (int i = 0; i < numBlocks; ++i)
            {
                const double tMs = i * blockMs;
                const float pitch = tMs < kHoldMs ? 60.0f : 64.2f;
                const int note = latch.update (pitch, false, block);

                if (distinctNotes.empty() || distinctNotes.back() != note)
                {
                    distinctNotes.push_back (note);
                    if (note == 64 && jumpAdoptedMs < 0.0)
                        jumpAdoptedMs = tMs + blockMs - kHoldMs;
                }
            }

            const bool ok = distinctNotes.size() == 2 && distinctNotes[0] == 60 && distinctNotes[1] == 64;
            if (! ok)
                sameSequence = false;

            // Due limiti, e sono diversi fra loro di proposito.
            //
            // In basso l'attesa e' garantita SEMPRE: nessun buffer, per
            // quanto grande, puo' far adottare una stima piu' breve di
            // kNoteSettleMs. E' questa la proprieta' che protegge la tabella
            // armonica, ed e' davvero indipendente dal buffer.
            //
            // In alto resta la quantizzazione al blocco, che questo fix non
            // toglie e non prometteva di togliere: l'attesa si consuma un
            // blocco alla volta, quindi si arrotonda per eccesso a un numero
            // intero di blocchi, piu' un blocco per il disallineamento fra il
            // salto e la griglia. A 1024 campioni 25 ms non entrano in un
            // blocco da 23.2 e ne servono due — misurato, non stimato.
            const double settleMs = 1000.0 * kSettleSamples / kSampleRate;
            const double wholeBlocksOfSettle = std::ceil (settleMs * kSampleRate / 1000.0 / block) * blockMs;
            if (jumpAdoptedMs < settleMs - 1.0 || jumpAdoptedMs > wholeBlocksOfSettle + blockMs + 1.0)
                latencyWithinBound = false;

            std::printf ("    block %5d (%5.1f ms): note distinte=%zu, adozione a %+6.1f ms dal salto (limite %.1f)\n",
                         block, blockMs, distinctNotes.size(), jumpAdoptedMs, wholeBlocksOfSettle + blockMs);
        }

        check (sameSequence, "ogni block size produce esattamente la sequenza 60 -> 64, nessun intermedio");
        check (latencyWithinBound, "mai adottata prima dell'attesa, mai oltre l'attesa arrotondata a blocchi interi");
    }

    std::printf ("\nTEST 11 - una stima confidente ma sbagliata, piu' breve dell'attesa, non\n"
                 "          viene adottata (il caso E->C misurato in s.31 sul file reale)\n");
    {
        // Riproduce la terza transizione di "Test 1 - Basic Silk Horns.wav":
        // agganciata su E (64), il rilevatore riporta 60.696 — che arrotonda a
        // 61, cioe' il grado b2 — per 14.5 ms, poi si assesta su 60.4.
        auto latch = makeLatch();
        latch.update (64.0f, true, kBlockOverSettle);

        bool sawWrongNote = false;
        const int blocksOfWrongEstimate = (int) std::lround (14.5 * kSampleRate / 1000.0 / kBlockShort);
        for (int i = 0; i < blocksOfWrongEstimate; ++i)
            if (latch.update (60.696f, false, kBlockShort) != 64)
                sawWrongNote = true;

        check (! sawWrongNote, "i 14.5 ms a 60.696 (arrotonda a 61 = b2) non spostano l'aggancio da 64");

        int settled = 64;
        for (int i = 0; i < 20; ++i)
            settled = latch.update (60.397f, false, kBlockShort);

        check (settled == 60, "appena la stima si assesta su 60.4, l'aggancio va a 60 (mai a 61)");
    }

    std::printf ("\nTEST 12 - l'attesa e' un multiplo del frame d'analisi, e a 60 Hz vale\n"
                 "          esattamente i 25 ms cablati fino a s.31 (nessun cambio di suono)\n");
    {
        const double settleMsAt60 = 1000.0 * harmony::PitchLatch::settleSamplesForFrame (kFrameSamplesAt60Hz) / kSampleRate;
        check (std::fabs (settleMsAt60 - 25.0) < 0.1, "frame 736 campioni (60 Hz) -> attesa 25.0 ms");

        // Il punto del cambio di s.32: l'attesa segue la finestra invece di
        // restare ferma. Con la nota piu' grave a Ab2 (Bb Tenor Sax, default)
        // la finestra e' 896 campioni, quindi il frame e' 448.
        const double settleMsAtAb2 = 1000.0 * harmony::PitchLatch::settleSamplesForFrame (448) / kSampleRate;
        check (settleMsAtAb2 < settleMsAt60, "frame piu' corto -> attesa piu' corta, senza toccare altro");
        std::printf ("    60 Hz: %.1f ms · Ab2: %.1f ms\n", settleMsAt60, settleMsAtAb2);

        // Un'attesa piu' corta non deve riaprire B-13: nessuna nota intermedia,
        // mai, a nessuna attesa — nemmeno a zero.
        bool anyIntermediate = false;
        for (int settle : { 0, 128, 448, 1104, 4096 })
        {
            harmony::PitchLatch latch;
            latch.prepare (settle);
            latch.update (60.0f, true, kBlockShort);
            for (int i = 0; i < 40; ++i)
            {
                const int note = latch.update (67.0f, false, kBlockShort);
                if (note != 60 && note != 67)
                    anyIntermediate = true;
            }
        }
        check (! anyIntermediate, "a qualunque attesa (0 inclusa) il salto 60->67 non passa per note intermedie");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
