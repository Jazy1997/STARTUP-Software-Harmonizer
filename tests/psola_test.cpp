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
#include "SampleAnalysis.h" // include gia' TestSignals.h; serve anche measureFrame (periodicita') per il Test 10

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>

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

    // NOTA (sessione 19): un primo Test 10 basato su TestSignals.h::
    // makeCompetingPulsesVowel (due impulsi per periodo che si scambiano
    // ampiezza) e' stato scritto e poi RITIRATO — CLAUDE.md regola 13, "un
    // test che fallisce potrebbe essere il test sbagliato": la periodicita'
    // dell'INGRESSO stesso di quel segnale sintetico crollava (0.90-0.98)
    // vicino al punto di scambio, quindi il test misurava in parte la non
    // perfetta periodicita' del segnale costruito, non un difetto puro
    // dell'algoritmo. Due tentativi di fix su detectEpochs (peso a coseno
    // sulla ricerca del picco, poi interpolazione parabolica sub-campione)
    // sono stati provati e poi RIMOSSI (tornati al codice del checkpoint
    // pre-sessione-19): nessuno dei due ha cambiato in modo misurabile il
    // sintomo osservato sul file audio reale (isolato con f0 fissa,
    // bypassando PitchDetector — vedi handsoff.md sessione 19).

    // -----------------------------------------------------------------------
    // TEST 10 (sessione 20 — ripresa dell'indagine wobbling). Sessione 19 ha
    // isolato il difetto DENTRO PsolaShifter (persiste con f0 fissa reale,
    // bypassando PitchDetector/PitchLatch/Glide) ed ESCLUSO detectEpochs
    // come causa (due fix mirati, zero effetto misurato sul file reale).
    // Il pezzo mai indagato e' la SINTESI. Ipotesi W-A: in synthesise(),
    // synthPos avanza del periodo QUANTIZZATO (currentPeriod(), un long
    // arrotondato a intero — vedi PsolaShifter.cpp), mentre gli epoch di
    // analisi seguono il periodo REALE del segnale. Se il periodo vero non
    // e' un multiplo intero del sample rate, lo scarto fra synthPos e
    // l'epoch piu' vicino deriva linearmente e periodicamente "scatta"
    // quando nearestEpoch() salta all'epoch adiacente: una discontinuita'
    // di fase interna al motore, invisibile solo quando il periodo e'
    // esattamente intero.
    //
    // Discriminante a costo quasi zero: stesso timbro (stesso generatore,
    // stessa formante), due f0 che a SR=48000 danno un periodo ESATTAMENTE
    // intero (200.0 campioni) e uno frazionario (200.5 campioni) — se solo
    // il caso frazionario degrada nel tempo, W-A e' confermata e
    // localizzata senza bisogno di altra strumentazione.
    //
    // RISULTATO (misurato, non assunto): su questo segnale sintetico W-A
    // NON SI RIPRODUCE. La traccia periodicita'-nel-tempo del caso
    // frazionario resta piatta (~0.996-0.997) per tutti i 2 secondi, senza
    // i cali periodici che la deriva sinthPos/epoch predirebbe — anzi
    // l'uscita e' leggermente PIU' periodica dell'ingresso stesso (che a
    // sua volta non e' perfettamente periodico: il generatore a treno di
    // impulsi con accumulatore di fase alterna gia' periodi interi 200/201
    // campioni per approssimare 200.5, quindi l'ingresso non ha una
    // struttura sub-campione "vera" su cui la deriva possa agire in modo
    // continuo — il meccanismo ipotizzato per synthesise() potrebbe
    // comunque essere reale su materiale diverso, ma questo test non lo
    // conferma). Per CLAUDE.md regola 13, il test resta come verifica di
    // trasparenza permanente (entrambi i casi devono restare stabili nel
    // tempo — vedi soglie sotto), ma la sua conclusione e' negativa su W-A:
    // la Fase 2 del piano (handsoff.md sessione 20) passa a strumentare
    // direttamente il file reale invece di forzare questa soglia a
    // "confermare" un'ipotesi che la misura non sostiene.
    std::printf ("\nTEST 10 - trasparenza in unisono nel tempo, periodo "
                 "intero vs frazionario (indagine wobbling)\n");
    {
        const double f0Int  = SR / 200.0; // periodo ESATTAMENTE 200 campioni
        const double f0Frac = SR / 200.5; // periodo 200.5 campioni, mai intero

        const double seconds = 2.0;
        const int    win     = (int) (0.020 * SR); // 20ms, ~4-5 periodi: serve margine per una stima di periodicita' stabile
        const int    hop     = win / 2;

        // measureOverTime ritorna { periodicita' minima INGRESSO, periodicita' minima USCITA }.
        auto measureOverTime = [&] (double f0) -> std::pair<double, double>
        {
            const auto in  = makeVowel (f0, seconds, SR);
            const auto out = runShifter (in, f0, 0.0, 1.0, Stability::numLevels - 1);

            const int maxLagCtx = (int) (SR / 50.0) + 2;

            // Controllo obbligatorio (CLAUDE.md regola 13, lezione diretta
            // del test viziato di sessione 19): l'INGRESSO deve essere
            // pulito prima di fidarsi di qualunque misura sull'uscita.
            double inMin = 1.0;
            const int settleIn = (int) (0.05 * SR); // oltre il transitorio del risonatore
            for (int from = settleIn; from + win + maxLagCtx < (int) in.size(); from += hop)
            {
                const auto fr = measureFrame (in, from, win, SR);
                if (fr.periodicity > 0.0) inMin = std::min (inMin, fr.periodicity);
            }

            double outMin = 1.0;
            const int settleOut = (int) (0.05 * SR) + 1536; // oltre la latenza dichiarata (Accurate: ~30ms)
            for (int from = settleOut; from + win + maxLagCtx < (int) out.size(); from += hop)
            {
                const auto fr = measureFrame (out, from, win, SR);
                if (fr.periodicity > 0.0) outMin = std::min (outMin, fr.periodicity);
            }

            return { inMin, outMin };
        };

        const auto [inMinInt,  outMinInt ] = measureOverTime (f0Int);
        const auto [inMinFrac, outMinFrac] = measureOverTime (f0Frac);

        std::printf ("  periodo intero  (200.0 camp., f0=%.3fHz): ingresso min %.4f  uscita min %.4f\n",
                     f0Int,  inMinInt,  outMinInt);
        std::printf ("  periodo frazion.(200.5 camp., f0=%.3fHz): ingresso min %.4f  uscita min %.4f\n",
                     f0Frac, inMinFrac, outMinFrac);

        const bool inputsClean = inMinInt > 0.98 && inMinFrac > 0.98;
        if (! inputsClean)
        {
            ++failures;
            std::printf ("  FALLITO: l'ingresso stesso non e' abbastanza periodico per fidarsi "
                         "della misura sull'uscita (regola 13) — test da rivedere, non l'algoritmo\n");
        }

        const bool intStaysClean = outMinInt > 0.95;
        if (! intStaysClean)
        {
            ++failures;
            std::printf ("  FALLITO: anche a periodo intero l'uscita degrada nel tempo — "
                         "il problema non e' (solo) la quantizzazione del periodo\n");
        }

        // Il caso frazionario deve restare trasparente quanto quello intero
        // (nessuna deriva/scatto misurabile nel tempo): e' l'esito
        // REALMENTE misurato (vedi nota sopra — W-A non si riproduce su
        // questo segnale), non un'ipotesi. Se in futuro qualcosa introduce
        // una vera deriva sinthPos/epoch, questa soglia la intercetta come
        // regressione.
        const bool fracStaysClean = outMinFrac > 0.95;
        if (! fracStaysClean) ++failures;
        std::printf ("  esito: %s (W-A non confermata su questo segnale — vedi Fase 2 in handsoff.md)\n",
                     fracStaysClean ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    // TEST 11 (sessione 20). La causa reale del wobbling e' stata trovata
    // strumentando temporaneamente PsolaShifter (PSOLA_DEBUG_SYNTH, non
    // presente in questo file: rimosso prima del commit) sul file reale
    // "Test 1 - Basic Silk Horns.wav" con f0 fissa, unisono: tracciando per
    // ogni grano lo scarto sp-epoch, si vedeva una spaziatura anomala fra
    // due epoch veri (un posizionamento sbagliato su un campione non
    // impulsivo, tipico di materiale reale) propagarsi per ~15 grani
    // (~30ms) prima di essere riassorbita — coincidente, a livello di
    // campione, con la finestra del glitch riportata da real_export_probe
    // sull'export reale (vedi handsoff.md sessione 20 per la traccia
    // completa e il ragionamento).
    //
    // NE' questo test (vibrato sintetico ±15 cent/5Hz) NE' il Test 10 (tono
    // perfettamente stazionario) riescono a riprodurre una degradazione
    // misurabile — provato con ENTRAMBI PRIMA di scrivere il fix: un
    // generatore a singola risonanza produce un picco per periodo troppo
    // netto e inequivocabile perche' detectEpochs lo posizioni mai male,
    // a differenza di un timbro reale (corno), piu' ricco armonicamente.
    // Questo test resta comunque utile come regressione: dopo il fix
    // (synthesise() usa il periodo di analisi LOCALE reale invece del
    // periodo quantizzato globale — vedi PsolaShifter.cpp) la trasparenza
    // deve restare alta anche con un vibrato naturale sovrapposto.
    std::printf ("\nTEST 11 - trasparenza in unisono con vibrato naturale\n");
    {
        const double f0Center   = 240.0;
        const double depthCents = 15.0;
        const double rateHz     = 5.0;
        const double seconds    = 2.0;

        const auto in = makeVibratoVowel (f0Center, depthCents, rateHz, seconds, SR);

        PsolaShifter ps;
        ps.prepare (SR, BLOCK, Stability::numLevels - 1);
        ps.setPitchShiftSemitones (0.0f);

        std::vector<float> out (in.size(), 0.0f);
        for (size_t i = 0; i + BLOCK <= in.size(); i += BLOCK)
        {
            const double t  = (double) i / SR;
            const double f0 = f0Center * std::pow (2.0, (depthCents * std::sin (2.0 * kPi * rateHz * t)) / 1200.0);
            ps.setInputF0Hz (f0);
            ps.process (&in[i], &out[i], BLOCK);
        }

        const int win       = (int) (0.020 * SR);
        const int hop       = win / 2;
        const int maxLagCtx = (int) (SR / 50.0) + 2;

        double inMin = 1.0;
        const int settleIn = (int) (0.05 * SR);
        for (int from = settleIn; from + win + maxLagCtx < (int) in.size(); from += hop)
        {
            const auto fr = measureFrame (in, from, win, SR);
            if (fr.periodicity > 0.0) inMin = std::min (inMin, fr.periodicity);
        }

        double outMin = 1.0;
        const int settleOut = (int) (0.05 * SR) + 1536;
        for (int from = settleOut; from + win + maxLagCtx < (int) out.size(); from += hop)
        {
            const auto fr = measureFrame (out, from, win, SR);
            if (fr.periodicity > 0.0) outMin = std::min (outMin, fr.periodicity);
        }

        std::printf ("  ingresso: periodicita' minima %.4f (vibrato reale, e' normale che non sia 1.0)\n", inMin);
        std::printf ("  uscita:   periodicita' minima %.4f\n", outMin);

        const bool inputPlausible = inMin > 0.90; // il vibrato stesso abbassa la periodicita' per costruzione
        if (! inputPlausible)
        {
            ++failures;
            std::printf ("  FALLITO: l'ingresso e' troppo instabile per essere un vibrato stretto — parametri da rivedere\n");
        }

        // Ne' prima ne' dopo il fix questo segnale sintetico mostra una
        // degradazione misurabile (vedi nota sopra): resta come verifica di
        // trasparenza permanente, non come conferma del meccanismo (quella
        // e' venuta dal file reale, Fase 2 in handsoff.md sessione 20).
        const bool staysClean = inputPlausible && (outMin > 0.95);
        if (! staysClean) ++failures;
        std::printf ("  esito: %s\n", staysClean ? "OK" : "FALLITO");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
