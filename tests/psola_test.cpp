// Suite di verifica numerica di PsolaShifter.
//
// Ragione d'essere di questo file (CLAUDE.md, "non puoi ascoltare"): non
// esiste modo di sapere se il pitch shifter "suona bene" senza ascoltarlo.
// Qui il criterio di completamento e' numerico. Deliberatamente SENZA
// dipendenze JUCE: si compila ed esegue in un secondo, senza aprire una DAW
// (CLAUDE.md regola 11 — lo stesso principio del target standalone).
//
// Portato da un'implementazione esterna verificata (vedi handsoff.md,
// sessioni 8/9) e adattato alla nostra interfaccia PitchShifter (namespace
// globale, setPitchShiftSemitones in semitoni invece di setPitchRatio in
// alpha, setInputF0Hz invece di setF0, prepare con stabilityLevel invece di
// minF0Hz diretto).
//
// Compilazione (vedi anche il target CMake `psola_test`):
//   g++ -O2 -std=c++20 -Isrc tests/psola_test.cpp src/dsp/PsolaShifter.cpp -o psola_test

#include "dsp/PsolaShifter.h"
#include "TestSignals.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

static constexpr double SR    = 48000.0;
static constexpr int    BLOCK = 256;

// makeVowel/makeSweptVowel/measureF0/formantPeak/maxJump/rms/minShortTimeRms/
// allFinite/centsError: estratte in tests/TestSignals.h (sessione 16) per
// essere condivise con tests/voice_test.cpp — stessa identica matematica,
// solo il sample rate e' diventato un parametro esplicito invece della
// costante globale SR di questo file. Vedi TestSignals.h per i dettagli.

// ---------------------------------------------------------------------------
static std::vector<float> runShifter (const std::vector<float>& in,
                                      double f0, double semitones, double beta,
                                      int stabilityLevel = Stability::numLevels - 1)
{
    PsolaShifter ps;
    ps.prepare (SR, BLOCK, stabilityLevel);
    ps.setInputF0Hz (f0);
    ps.setPitchShiftSemitones ((float) semitones);
    ps.setFormantRatio (beta);

    std::vector<float> out (in.size(), 0.0f);

    for (size_t i = 0; i + BLOCK <= in.size(); i += BLOCK)
        ps.process (&in[i], &out[i], BLOCK);

    return out;
}

// ---------------------------------------------------------------------------
int main()
{
    int failures = 0;
    const double f0in = 200.0;
    const auto input = makeVowel (f0in, 1.5, SR);

    const int anaFrom = (int) (0.8 * SR);
    const int anaLen  = 8192;

    const double inPeak = formantPeak (input, anaFrom, anaLen, SR);
    const double inRms  = rms (input, anaFrom, anaLen);

    std::printf ("Segnale di test: vocale sintetica, f0 = %.1f Hz, "
                 "formante a 1100 Hz\n", f0in);
    std::printf ("Picco formantico in ingresso: %.0f Hz   RMS: %.4f\n\n", inPeak, inRms);

    // -----------------------------------------------------------------------
    std::printf ("TEST 1 - accuratezza di trasposizione (beta = 1)\n");
    std::printf ("%10s %12s %12s %10s %10s\n",
                 "semitoni", "atteso Hz", "misurato Hz", "errore c", "esito");

    const int semis[] = { -12, -7, -5, -3, 0, 4, 7, 12 };

    for (int st : semis)
    {
        const double alpha = std::pow (2.0, st / 12.0);
        const auto   out   = runShifter (input, f0in, st, 1.0);

        const double expected = f0in * alpha;
        const double measured = measureF0 (out, anaFrom, anaLen, SR);
        const double err      = centsError (measured, expected);
        const bool   ok       = std::fabs (err) < 10.0;

        if (! ok) ++failures;
        std::printf ("%10d %12.2f %12.2f %10.2f %10s\n",
                     st, expected, measured, err, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 2 - ortogonalita': con beta = 1 le formanti non "
                 "devono seguire il pitch\n");
    std::printf ("%10s %14s %14s %10s\n",
                 "semitoni", "picco Hz", "scostamento", "esito");

    for (int st : semis)
    {
        const auto out = runShifter (input, f0in, st, 1.0);

        const double c    = formantPeak (out, anaFrom, anaLen, SR);
        const double dev  = 100.0 * (c - inPeak) / inPeak;

        const bool   ok   = std::fabs (dev) < 15.0;

        if (! ok) ++failures;
        std::printf ("%10d %14.1f %13.1f%% %10s\n",
                     st, c, dev, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 3 - beta sposta le formanti senza toccare il pitch "
                 "(semitoni = 0)\n");
    std::printf ("%10s %14s %14s %12s %10s\n",
                 "beta", "picco Hz", "atteso Hz", "f0 misurata", "esito");

    for (double beta : { 0.7, 0.85, 1.0, 1.2, 1.5 })
    {
        const auto out = runShifter (input, f0in, 0.0, beta);

        const double c        = formantPeak (out, anaFrom, anaLen, SR);
        const double expected = inPeak * beta;
        const double devC     = 100.0 * (c - expected) / expected;
        const double measured = measureF0 (out, anaFrom, anaLen, SR);
        const double errF0    = centsError (measured, f0in);

        const bool ok = std::fabs (devC) < 12.0 && std::fabs (errF0) < 10.0;
        if (! ok) ++failures;

        std::printf ("%10.2f %14.1f %14.1f %12.2f %10s\n",
                     beta, c, expected, measured, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 4 - assenza di discontinuita' e stabilita' di livello\n");
    const double inJump = maxJump (input, anaFrom, anaLen);

    for (int st : { -12, -5, 7 })
    {
        const double alpha = std::pow (2.0, st / 12.0);
        const auto   out   = runShifter (input, f0in, st, 1.0);

        const double j  = maxJump (out, anaFrom, anaLen);
        const double r  = rms (out, anaFrom, anaLen);
        const double dB = 20.0 * std::log10 (r / inRms);

        const double allowed = inJump * std::max (1.0, alpha) * 3.0;
        const bool   ok      = (j < allowed) && std::fabs (dB) < 6.0;

        if (! ok) ++failures;
        std::printf ("  %+3d st: salto max %.4f (limite %.4f), livello %+.2f dB  %s\n",
                     st, j, allowed, dB, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 5 - la latenza dichiarata cala con Stability piu' "
                 "reattivo (minF0 piu' alto)\n");
    {
        int prevLatency = -1;
        bool monotonic = true;
        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PsolaShifter ps;
            ps.prepare (SR, BLOCK, level);
            const int lat = ps.getLatencySamples();
            std::printf ("  Stability[%d] (%s) -> %d campioni = %.1f ms\n",
                         level, Stability::names[level], lat, 1000.0 * lat / SR);
            if (prevLatency >= 0 && lat < prevLatency) monotonic = false;
            prevLatency = lat;
        }
        if (! monotonic) { ++failures; std::printf ("  FALLITO: la latenza deve crescere da Fast ad Accurate\n"); }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 6 - inviluppo minimo di sovrapposizione (rischio: "
                 "vuoti periodici sotto -12 semitoni)\n");
    {
        const int win = (int) (0.005 * SR); // ~5 ms
        for (double st : { -12.0, -14.5, -17.0 }) // alpha ~ 0.5, 0.43, 0.35
        {
            const auto out = runShifter (input, f0in, st, 1.0);

            const double minR = minShortTimeRms (out, anaFrom, anaLen, win);
            const double avgR = rms (out, anaFrom, anaLen);
            const double ratio = (avgR > 0.0) ? minR / avgR : 0.0;

            // Un motore che smette di sovrapporre i grani produce vuoti
            // profondi: senza, la variazione naturale del treno di impulsi
            // di test non fa scendere il minimo sotto ~1/4 della media.
            const bool ok = ratio > 0.25;
            if (! ok) ++failures;
            std::printf ("  %+.1f st: RMS minimo/medio = %.3f  %s\n",
                         st, ratio, ok ? "OK" : "FALLITO");
        }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 7 - tenuta con f0 variabile nel tempo (ricambio "
                 "continuo di epoch)\n");
    {
        const auto swept = makeSweptVowel (150.0, 260.0, 1.5, SR);
        PsolaShifter ps;
        ps.prepare (SR, BLOCK, Stability::numLevels - 1);
        ps.setPitchShiftSemitones (0.0f);

        std::vector<float> out (swept.size(), 0.0f);
        bool finiteThroughout = true;

        for (size_t i = 0; i + BLOCK <= swept.size(); i += BLOCK)
        {
            const double t  = (double) i / (double) swept.size();
            const double f0 = 150.0 + (260.0 - 150.0) * t;
            ps.setInputF0Hz (f0);
            ps.process (&swept[i], &out[i], BLOCK);

            for (int s = 0; s < BLOCK; ++s)
                if (! std::isfinite (out[i + (size_t) s])) finiteThroughout = false;
        }

        const bool ok = finiteThroughout && allFinite (out);
        if (! ok) ++failures;
        std::printf ("  uscita finita per tutta la durata (f0 150->260 Hz): %s\n",
                     ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    // TEST 8 (sessione 12 — feedback utente "granuloso, non fedele al
    // sorgente"): il test 6 misura la sovrapposizione dei grani SOLO a
    // beta = 1, ma Voice.cpp applica la correzione formantica automatica
    // (FR-39) di DEFAULT (formantSpread = 1.0), quindi beta != 1 su
    // qualunque voce shiftata e' il caso normale in produzione, non un
    // caso limite. La formula qui e' una copia intenzionale di quella in
    // Voice::processAdd — se k cambia la', va aggiornata anche qui.
    std::printf ("\nTEST 8 - inviluppo minimo di sovrapposizione CON la "
                 "correzione formantica automatica di Voice.cpp (beta != 1)\n");
    {
        constexpr double kFormantSpreadK = 0.3; // deve restare uguale a Voice.cpp
        const int win = (int) (0.005 * SR);
        for (double st : { -12.0, -14.5, -17.0 })
        {
            const double autoFormantSemitones = -kFormantSpreadK * 1.0 * st; // spread=1.0 default
            const double beta = std::pow (2.0, autoFormantSemitones / 12.0);

            const auto out = runShifter (input, f0in, st, beta);

            const double minR = minShortTimeRms (out, anaFrom, anaLen, win);
            const double avgR = rms (out, anaFrom, anaLen);
            const double ratio = (avgR > 0.0) ? minR / avgR : 0.0;

            const bool ok = ratio > 0.25;
            if (! ok) ++failures;
            std::printf ("  %+.1f st (beta %.3f): RMS minimo/medio = %.3f  %s\n",
                         st, beta, ratio, ok ? "OK" : "FALLITO");
        }
    }

    // -----------------------------------------------------------------------
    // TEST 9 (sessione 14 — feedback utente "un click solo a inizio nota,
    // specialmente legato"). Ipotesi: quando una voce fisica smette di
    // essere processata (dissolvenza anti-click completata, isSilent()),
    // Voice::processAdd smette di chiamare shifter->process() — il PSOLA
    // resta congelato con qualunque contenuto avesse in quel momento nella
    // sua pipeline interna (inBuf/outBuf/envBuf, epoch). La dissolvenza dura
    // solo kDeclickMs=8ms (353 campioni a 44.1kHz), ma la latenza dichiarata
    // del motore (2*maxPeriod+maxBlock) e' SEMPRE piu' lunga — 13.6ms a
    // Fast, 21.5ms a Balanced, 30ms ad Accurate: la pipeline non fa in tempo
    // a svuotarsi del tutto prima che la voce venga considerata silenziosa.
    // Se quello stesso slot fisico viene poi riassegnato a una nuova nota
    // (routine, mai resettato fra una frase e l'altra — vedi handsoff.md),
    // i primi campioni della nuova nota sono ancora, in parte, contenuto
    // residuo della nota PRECEDENTE che quello slot stava sintetizzando.
    std::printf ("\nTEST 9 - riattivazione di uno slot fisico dopo un periodo di inattivita'\n"
                 "         (nessuna chiamata a process() durante il 'mute', nessun reset)\n");
    {
        const auto signalA = makeVowel (350.0, 0.3, SR, 1800.0); // nota "precedente", timbro diverso
        const auto signalB = makeVowel (220.0, 0.3, SR, 1100.0); // nota "nuova"

        // Baseline: uno shifter MAI usato prima (equivalente a uno slot
        // fisico fresco, o a uno slot correttamente resettato) che processa
        // solo il segnale B dall'inizio.
        PsolaShifter fresh;
        fresh.prepare (SR, BLOCK, Stability::defaultLevel);
        fresh.setPitchShiftSemitones (0.0f);
        std::vector<float> outFresh (signalB.size(), 0.0f);
        for (size_t i = 0; i + BLOCK <= signalB.size(); i += BLOCK)
        {
            fresh.setInputF0Hz (220.0);
            fresh.process (&signalB[i], &outFresh[i], BLOCK);
        }

        // "Sporco": uno shifter che ha gia' processato il segnale A (stato
        // interno reale, non banale), poi smette di essere chiamato per un
        // po' (il 'mute' — nessuna chiamata a process(), esattamente come
        // Voice::processAdd quando isSilent()), poi riprende sul segnale B
        // SENZA reset() — il bug come si presenta oggi.
        PsolaShifter stale;
        stale.prepare (SR, BLOCK, Stability::defaultLevel);
        stale.setPitchShiftSemitones (0.0f);
        for (size_t i = 0; i + BLOCK <= signalA.size(); i += BLOCK)
        {
            stale.setInputF0Hz (350.0);
            std::vector<float> scratch (BLOCK, 0.0f);
            stale.process (&signalA[i], scratch.data(), BLOCK);
        }
        // (il "mute" e' semplicemente il fatto di non chiamare process()
        // per un intervallo: nessun tempo passa internamente per lo shifter,
        // esattamente come una Voice inattiva)
        std::vector<float> outStale (signalB.size(), 0.0f);
        for (size_t i = 0; i + BLOCK <= signalB.size(); i += BLOCK)
        {
            stale.setInputF0Hz (220.0);
            stale.process (&signalB[i], &outStale[i], BLOCK);
        }

        // Stessa storia di "stale", ma con reset() prima di riprendere sul
        // segnale B — questo e' il fix proposto (Voice::setMuted chiamera'
        // shifter->reset() alla riattivazione da uno stato silenzioso).
        PsolaShifter resetThenResume;
        resetThenResume.prepare (SR, BLOCK, Stability::defaultLevel);
        resetThenResume.setPitchShiftSemitones (0.0f);
        for (size_t i = 0; i + BLOCK <= signalA.size(); i += BLOCK)
        {
            resetThenResume.setInputF0Hz (350.0);
            std::vector<float> scratch (BLOCK, 0.0f);
            resetThenResume.process (&signalA[i], scratch.data(), BLOCK);
        }
        resetThenResume.reset();
        std::vector<float> outReset (signalB.size(), 0.0f);
        for (size_t i = 0; i + BLOCK <= signalB.size(); i += BLOCK)
        {
            resetThenResume.setInputF0Hz (220.0);
            resetThenResume.process (&signalB[i], &outReset[i], BLOCK);
        }

        double maxDiffStaleVsFresh = 0.0, maxDiffResetVsFresh = 0.0;
        for (size_t i = 0; i < outFresh.size(); ++i)
        {
            maxDiffStaleVsFresh = std::max (maxDiffStaleVsFresh, (double) std::fabs (outStale[i] - outFresh[i]));
            maxDiffResetVsFresh = std::max (maxDiffResetVsFresh, (double) std::fabs (outReset[i] - outFresh[i]));
        }

        // reset() deve riportare lo shifter ESATTAMENTE allo stesso stato di
        // uno mai usato: stessa sequenza di ingresso, stessi parametri ->
        // uscita bit-per-bit identica (nessuna dipendenza residua nascosta).
        const bool resetMatchesFresh = maxDiffResetVsFresh < 1.0e-6;
        std::printf ("  con reset() prima di riprendere: scostamento massimo dal riferimento pulito %.8f  %s\n",
                     maxDiffResetVsFresh, resetMatchesFresh ? "OK" : "FALLITO");
        if (! resetMatchesFresh) ++failures;

        // Senza reset(), la nota precedente lascia una traccia misurabile:
        // questo e' esattamente il click "a inizio nota" segnalato
        // dall'utente, riprodotto qui numericamente.
        const bool staleDiffersMeasurably = maxDiffStaleVsFresh > 0.02;
        std::printf ("  SENZA reset() (comportamento attuale): scostamento massimo dal riferimento pulito %.6f  %s\n",
                     maxDiffStaleVsFresh, staleDiffersMeasurably ? "OK (bug confermato)" : "FALLITO (bug non riprodotto)");
        if (! staleDiffersMeasurably) ++failures;
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
