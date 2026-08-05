// Strumento di misura del "click a inizio nota" (sessione 14 SMENTITA
// all'ascolto, sessione 16 ripartenza da zero — vedi handsoff.md §6).
//
// CLAUDE.md regola 12/13: prima di scrivere un altro fix "plausibile" come
// quello di sessione 14 (che l'utente ha poi smentito all'ascolto), questo
// file MISURA il sintomo alla granularita' di Voice (non del solo
// PsolaShifter, gia' coperto da tests/psola_test.cpp) — l'integrazione fra
// Voice::setMuted, Glide (ampGlide/offsetGlide) e PitchShifter e' esattamente
// il livello a cui il fix di sessione 14 e' intervenuto senza risolvere
// nulla, quindi e' il livello a cui va misurato.
//
// Nuovi dati raccolti dall'utente in sessione 16, prima di scrivere questo
// file: il click si sente SOLO sul segnale wet (mai sul dry — esclude
// dry/wet glide, bypass, downmix, sorgente) e persiste identico con un
// buffer ASIO piccolo (1024 campioni, non solo coi 4096 di MME/DirectX di
// sessione 13 — indebolisce fortemente l'ipotesi che sia un artefatto di
// block size). La causa e' quindi dentro Voice/PitchShifter, indipendente
// dal blocco dell'host: esattamente il perimetro di questo file.
//
// Ipotesi misurate (vedi il piano approvato in sessione 16 per il
// ragionamento completo dietro ciascuna) — ESITO della Fase 1, misurato
// PRIMA di scrivere qualunque fix (CLAUDE.md regola 12/13):
//   H1 - REFUTATA. Ipotesi: la dissolvenza anti-click (ampGlide, 8ms) e'
//        spesa per intero sul PREFISSO DI SILENZIO che PsolaShifter::reset()
//        introduce deliberatamente (absWrite=latency, absRead=0), quindi
//        l'uscita salterebbe da 0 a piena scala in un grano invece che in
//        8ms. Misurato: slewAtt/Regime = 1.00 su tutti e 5 i livelli di
//        Stability e su entrambi i block size (64/1024) — nessuna
//        discontinuita'. L'attacco e' in realta' una salita LISCIA, solo
//        RITARDATA (18-35ms secondo Stability) rispetto a dove ampGlide
//        aveva gia' finito la propria rampa. Nessun fix scritto per questa
//        ipotesi: non c'era nulla da correggere.
//   H3 - CONFERMATA, e' la causa del click. offsetGlide non veniva mai
//        "agganciato" al nuovo target quando uno slot silenzioso viene
//        riassegnato a una nuova nota: l'intonazione del nuovo attacco
//        partiva da dove si trovava la nota PRECEDENTE sullo stesso slot
//        fisico e ci scivolava sopra in glideTimeMs (FR-17, 30ms di
//        default), invece di scattare subito. Misurato PRIMA del fix:
//        ~195 cent di scostamento dal nuovo target appena dopo il
//        riattacco. Fix: Voice::justReactivated (vedi Voice.h/Voice.cpp).
//   H4 - ESCLUSA. t1 identico (differenza 0 campioni) fra block size 64 e
//        1024 su tutti i livelli di Stability — coerente con la prova
//        dell'utente (click identico con ASIO 1024 e MME 4096): la causa
//        non dipende dalla dimensione del blocco host.
//
// Il fix e' ora nel codice: le assert su H1/H3/H4 sono il cancello di
// regressione della Fase 3, non piu' solo strumenti di misura esplorativa.
//
// Compilazione (vedi anche il target CMake `voice_test`):
//   g++ -O2 -std=c++20 -Isrc -Ilibs/signalsmith-stretch tests/voice_test.cpp
//       src/voices/Voice.cpp src/dsp/PsolaShifter.cpp src/dsp/SpectralShifter.cpp
//       src/dsp/PitchShifterFactory.cpp -o voice_test

#include "voices/Voice.h"
#include "TestSignals.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

static constexpr double SR = 44100.0; // stessa SR della config reale dell'utente (screenshot ASIO)

// ---------------------------------------------------------------------------
// Fa processare a `voice` `totalSamples` campioni consecutivi presi da `src`
// a partire da `srcOffset`, in blocchi da `blockSize` (l'ultimo blocco puo'
// essere piu' corto). mixOutput deve essere pre-azzerato (Voice::processAdd
// ACCUMULA, come da contratto in Voice.h) — un vector<float> appena
// costruito e' gia' a zero, nessuna inizializzazione esplicita necessaria.
// ---------------------------------------------------------------------------
static std::vector<float> captureOutput (Voice& voice, const std::vector<float>& src, size_t srcOffset,
                                         int blockSize, int totalSamples,
                                         int quantizedPlayedNote, float continuousMidi)
{
    std::vector<float> out ((size_t) totalSamples, 0.0f);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        voice.processAdd (&src[srcOffset + (size_t) done], &out[(size_t) done], n,
                          quantizedPlayedNote, continuousMidi);
        done += n;
    }
    return out;
}

// Fa avanzare lo stato di `voice` di `totalSamples` campioni senza
// conservare l'uscita (usato per le fasi di "assestamento" prima della
// misura vera e propria: portare offsetGlide a convergenza, o svuotare
// ampGlide fino al silenzio).
static void runSilently (Voice& voice, const std::vector<float>& src, size_t srcOffset,
                         int blockSize, int totalSamples,
                         int quantizedPlayedNote, float continuousMidi)
{
    std::vector<float> scratch ((size_t) blockSize, 0.0f);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        std::fill (scratch.begin(), scratch.begin() + n, 0.0f);
        voice.processAdd (&src[srcOffset + (size_t) done], scratch.data(), n,
                          quantizedPlayedNote, continuousMidi);
        done += n;
    }
}

// Inviluppo RMS a finestra scorrevole, causale (usa solo campioni passati):
// serve a localizzare CON PRECISIONE quando l'uscita "parte", non solo se e'
// finita a zero o a regime — a differenza di minShortTimeRms in
// TestSignals.h (pensato per trovare vuoti, non per misurare un fronte).
static std::vector<double> movingRms (const std::vector<float>& x, int win)
{
    std::vector<double> env (x.size(), 0.0);
    double sumSq = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        sumSq += (double) x[i] * (double) x[i];
        if (i >= (size_t) win)
            sumSq -= (double) x[i - (size_t) win] * (double) x[i - (size_t) win];
        const int n = (int) std::min<size_t> (i + 1, (size_t) win);
        env[i] = std::sqrt (sumSq / n);
    }
    return env;
}

// Primo indice i tale che env[i] >= threshold, a partire da `from`. -1 se mai.
static int firstCrossing (const std::vector<double>& env, int from, double threshold)
{
    for (size_t i = (size_t) from; i < env.size(); ++i)
        if (env[i] >= threshold)
            return (int) i;
    return -1;
}

// ---------------------------------------------------------------------------
int main()
{
    int failures = 0;

    const double f0in = 220.0;
    const int    maxBlockPrepare = 1024; // >= il piu' grande blockSize usato sotto (vedi Voice::scratch)
    const int    envWin = (int) (0.005 * SR); // ~5ms, stessa convenzione di minShortTimeRms

    // Segnale di prova: stessa famiglia (treno di impulsi + risonanza) di
    // tests/psola_test.cpp, riusata via TestSignals.h invece di riscritta.
    const auto carrier = makeVowel (f0in, 2.0, SR);
    const float continuousMidi = (float) (69.0 + 12.0 * std::log2 (f0in / 440.0));

    std::printf ("Strumento di misura del click a inizio nota — SR=%.0f Hz, f0 sorgente=%.0f Hz\n\n", SR, f0in);

    // =========================================================================
    // CONTROLLO NEGATIVO (obbligatorio prima di fidarsi di qualunque altra
    // misura in questo file, CLAUDE.md regola 13): la metrica "slew di picco"
    // che uso sotto per rilevare un salto di ampiezza non deve segnalare nulla
    // quando applicata a due finestre PRESE ENTRAMBE da un tratto di uscita
    // gia' a regime (nessuna riattivazione, nessun reset in mezzo) — altrimenti
    // la misura e' rotta, non il codice, e va corretta la misura prima di
    // procedere.
    // =========================================================================
    std::printf ("CONTROLLO NEGATIVO — la misura di slew non deve segnalare nulla su uscita gia' stabile\n");
    {
        Voice voice;
        voice.prepare (SR, maxBlockPrepare, Stability::defaultLevel);
        voice.setMuted (false); // singola attivazione, mai piu' rimutata: nessun evento da rilevare
        voice.setTargetOffsetSemitones (0.0f);

        const int total = (int) (0.5 * SR);
        const auto out = captureOutput (voice, carrier, 0, 256, total, 0, continuousMidi);

        const int winLen = (int) (0.05 * SR); // 50ms, stessa larghezza usata per le finestre H1 sotto
        const int fromA = (int) (0.30 * SR);
        const int fromB = (int) (0.40 * SR);

        const double slewA = maxJump (out, fromA, winLen);
        const double slewB = maxJump (out, fromB, winLen);
        const double ratio = (slewB > 0.0) ? slewA / slewB : 1.0e9;

        const bool ok = ratio > 0.5 && ratio < 2.0;
        if (! ok) ++failures;
        std::printf ("  slew [%.2fs] / slew [%.2fs] = %.3f (atteso ~1)  %s\n",
                     0.30, 0.40, ratio, ok ? "OK" : "FALLITO — la misura stessa non e' affidabile, fermarsi qui");

        if (! allFinite (out)) { ++failures; std::printf ("  FALLITO: uscita non finita (NaN/Inf)\n"); }
    }

    // =========================================================================
    // H1/H4 — attacco dopo riattivazione da silenzio, su tutti i livelli di
    // Stability e due block size molto diversi (64 e 1024 campioni).
    // =========================================================================
    std::printf ("\nH1/H4 — attacco della voce alla riattivazione (setMuted(false) su una voce silenziosa)\n");
    std::printf ("%10s %10s %12s %14s %14s %10s\n",
                 "Stability", "block", "t1 (camp)", "salita t1->90%", "slewAtt/Regime", "esito");

    for (int level = 0; level < Stability::numLevels; ++level)
    {
        for (int blockSize : { 64, 1024 })
        {
            Voice voice;
            voice.prepare (SR, maxBlockPrepare, level);
            // Voice appena preparata e' gia' isSilent()==true (muted=true,
            // ampGlide.reset(0) in Voice::prepare): setMuted(false) qui
            // esegue esattamente lo stesso percorso di una riattivazione di
            // uno slot fisico riusato (vedi Voice.h — reset() dello shifter
            // scatta comunque sulla transizione, che lo slot sia "fresco" o
            // "usato" non cambia lo stato dopo reset(), verificato in
            // tests/psola_test.cpp Test 9).
            voice.setMuted (false);
            voice.setTargetOffsetSemitones (0.0f); // unisono: isola il fenomeno da qualunque artefatto di ricampionamento

            const int total = (int) (0.5 * SR);
            const auto out = captureOutput (voice, carrier, 0, blockSize, total, 0, continuousMidi);

            const auto env = movingRms (out, envWin);

            const int regimeFrom = (int) (0.30 * SR);
            const int regimeLen  = (int) (0.15 * SR);
            double regimeRmsSum = 0.0;
            for (int i = 0; i < regimeLen; ++i) regimeRmsSum += env[(size_t) (regimeFrom + i)];
            const double regimeEnv = regimeRmsSum / regimeLen;

            const int t1  = firstCrossing (env, 0, 0.01 * regimeEnv);
            const int t90 = (t1 >= 0) ? firstCrossing (env, t1, 0.90 * regimeEnv) : -1;
            const int riseSamples = (t1 >= 0 && t90 >= 0) ? (t90 - t1) : -1;

            const int attackWinLen = (int) (0.05 * SR); // 50ms: copre il caso peggiore (Accurate, 30ms+8ms)
            const double slewAttack = maxJump (out, 0, attackWinLen);
            const double slewRegime = maxJump (out, regimeFrom, attackWinLen);
            const double slewRatio  = (slewRegime > 0.0) ? slewAttack / slewRegime : 1.0e9;

            // Soglia diagnostica: un attacco "morbido" (rampa vera su 8ms)
            // non dovrebbe produrre uno slew di picco piu' di ~3x quello a
            // regime. Attesa FALLIRE oggi (H1): documenta il bug prima del
            // fix, stesso schema di Test 8/9 in psola_test.cpp.
            const bool ok = slewRatio < 3.0;
            if (! ok) ++failures;

            std::printf ("%10s %10d %12d %14d %14.2f %10s\n",
                         Stability::names[level], blockSize, t1, riseSamples, slewRatio,
                         ok ? "OK" : "FALLITO (click)");

            if (! allFinite (out)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
        }
    }

    // H4, isolato: per lo stesso livello di Stability, il salto (t1) non deve
    // dipendere in modo sostanziale dal block size dell'host — se dipendesse,
    // la causa sarebbe (anche) architetturale, non solo interna a Voice.
    std::printf ("\nH4 — la posizione del salto (t1) non deve dipendere dal block size dell'host\n");
    {
        bool blockSizeIndependent = true;
        for (int level = 0; level < Stability::numLevels; ++level)
        {
            int t1_64 = -1, t1_1024 = -1;
            for (int blockSize : { 64, 1024 })
            {
                Voice voice;
                voice.prepare (SR, maxBlockPrepare, level);
                voice.setMuted (false);
                voice.setTargetOffsetSemitones (0.0f);

                const int total = (int) (0.1 * SR);
                const auto out = captureOutput (voice, carrier, 0, blockSize, total, 0, continuousMidi);
                const auto env = movingRms (out, envWin);

                // soglia assoluta piccola e fissa (non relativa al regime,
                // per non ripetere il calcolo del regime qui): basta per
                // confrontare la POSIZIONE del salto fra i due block size.
                const int t1 = firstCrossing (env, 0, 0.02);
                if (blockSize == 64) t1_64 = t1; else t1_1024 = t1;
            }

            // Se la soglia assoluta non viene mai raggiunta (t1 < 0) la
            // misura non e' conclusiva per questo livello, non un successo:
            // niente da confrontare, quindi non entra nel verdetto finale
            // ne' come OK ne' come FALLITO silenzioso.
            if (t1_64 < 0 || t1_1024 < 0)
            {
                std::printf ("  %10s: t1(block=64)=%d  t1(block=1024)=%d  NON CONCLUSIVO (soglia mai raggiunta)\n",
                             Stability::names[level], t1_64, t1_1024);
                continue;
            }

            const int diff = std::abs (t1_64 - t1_1024);
            // Tollerante alla granularita' del block size stesso (il salto
            // puo' spostarsi di al piu' un blockSize per via dell'allineamento
            // ai confini di chiamata): soglia = 2x il block size piu' grande.
            const bool ok = diff <= 2 * 1024;
            if (! ok) { blockSizeIndependent = false; }
            std::printf ("  %10s: t1(block=64)=%d  t1(block=1024)=%d  differenza=%d  %s\n",
                         Stability::names[level], t1_64, t1_1024, diff, ok ? "OK" : "FALLITO");
        }
        if (! blockSizeIndependent) ++failures;
    }

    // =========================================================================
    // H3 — offsetGlide non si aggancia al nuovo target quando uno slot
    // silenzioso viene riassegnato: replica esatta della sequenza di
    // PhraseScheduler.cpp (righe 326-328) — setMuted(false) POI
    // setTargetOffsetSemitones(nuovo valore) — su una voce che aveva un
    // offset diverso PRIMA di essere ammutolita.
    // =========================================================================
    std::printf ("\nH3 — intonazione all'attacco quando uno slot riassegnato aveva un offset diverso prima\n");
    {
        constexpr float kOldOffset = 7.0f;  // "nota precedente" sullo stesso slot fisico
        constexpr float kNewOffset = -5.0f; // nuova nota, nuovo offset armonico

        // Glide esagerato SOLO per avere una finestra di misura comoda
        // (migliaia di campioni in cui la rampa e' ancora ben lontana dal
        // target): il meccanismo dimostrato — nessun aggancio immediato al
        // riattivarsi — e' lo stesso identico a qualunque durata di glide,
        // inclusi i 30ms di default di FR-17.
        constexpr float kTestGlideMs = 200.0f;

        Voice voice;
        voice.prepare (SR, maxBlockPrepare, 0 /*Fast: latenza minima, piu' finestra utile prima che il glide converga*/);
        voice.setGlideTimeMs (kTestGlideMs);

        // 1) attiva, converge sul vecchio offset.
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (kOldOffset);
        runSilently (voice, carrier, 0, 64, (int) (0.25 * SR), 0, continuousMidi); // >> kTestGlideMs

        // 2) si ammutolisce (fine frase/nota): la dissolvenza di 8ms la
        // porta a isSilent()==true in poche decine di blocchi.
        voice.setMuted (true);
        runSilently (voice, carrier, (size_t) (0.25 * SR), 64, (int) (0.02 * SR), 0, continuousMidi);

        // 3) riassegnata a una nuova nota con un offset diverso — stessa
        // sequenza esatta di PhraseScheduler.cpp.
        const size_t attackOffset = (size_t) (0.27 * SR);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (kNewOffset);

        const int captureLen = 4096;
        const auto attackOut = captureOutput (voice, carrier, attackOffset, 64, captureLen, 0, continuousMidi);

        // Finestra di misura: dopo la latenza del motore (Fast, ~600 campioni
        // a questa SR) ma ben dentro la rampa di 200ms (8820 campioni) —
        // segnale vero, ma il glide e' solo a pochi punti percentuale.
        const int measFrom = 800;
        const int measLen  = 2048;
        const double measuredHz = measureF0 (attackOut, measFrom, measLen, SR);

        const double fIfSnapped = f0in * std::pow (2.0, kNewOffset / 12.0);
        const double fIfStale   = f0in * std::pow (2.0, kOldOffset / 12.0);
        const double errVsSnapped = centsError (measuredHz, fIfSnapped);
        const double errVsStale   = centsError (measuredHz, fIfStale);

        std::printf ("  atteso se scattasse subito: %.1f Hz | atteso se restasse sul vecchio: %.1f Hz | misurato: %.1f Hz\n",
                     fIfSnapped, fIfStale, measuredHz);
        std::printf ("  scostamento dal nuovo target: %.0f cent | scostamento dal vecchio: %.0f cent\n",
                     errVsSnapped, errVsStale);

        // Sessione 16: prima del fix (Voice::justReactivated) questa
        // asserzione era invertita — "scostamento > 100 cent" era il
        // risultato ATTESO, misurato e documentato come "OK (bug
        // confermato)" (mirror di Test 9 in psola_test.cpp) prima di
        // scrivere il fix. Ora che il fix e' nel codice, l'attacco deve
        // agganciare il nuovo target IMMEDIATAMENTE (offsetGlide.reset,
        // nessuna rampa): la soglia si stringe a 15 cent, stesso ordine di
        // grandezza della tolleranza di Test 1 in psola_test.cpp (10 cent),
        // leggermente piu' larga per la finestra di misura piu' breve qui.
        const bool snapsImmediately = std::fabs (errVsSnapped) < 15.0;
        std::printf ("  %s\n", snapsImmediately
                     ? "OK (l'attacco scatta subito al nuovo offset)"
                     : "FALLITO (l'attacco resta ancorato, in tutto o in parte, al vecchio offset)");
        if (! snapsImmediately) ++failures;

        // Controllo positivo: NON riusa il glide esagerato di sopra (quello
        // servirebbe solo a dilatare la rampa, non a dimostrare che la
        // misura e' sana). Qui il glide e' disattivato del tutto
        // (glideTimeMs=0 -> Glide::setTarget salta subito a current=target,
        // vedi Glide.h): una voce mai riattivata prima, con la rampa
        // spenta, deve leggere il target fin dal primo campione utile. Se
        // anche questo controllo scostasse molto dal target, il problema
        // sarebbe nella misura di F0 (measureF0), non nel meccanismo di
        // staleness che si vuole dimostrare sopra.
        Voice freshVoice;
        freshVoice.prepare (SR, maxBlockPrepare, 0);
        freshVoice.setGlideTimeMs (0.0f);
        freshVoice.setMuted (false);
        freshVoice.setTargetOffsetSemitones (kNewOffset);

        // captureLen (non solo measFrom+measLen): measureF0 legge internamente
        // fino a from+len+maxLag+2 campioni (vedi TestSignals.h) — usare lo
        // stesso margine di captureLen gia' scelto sopra per attackOut evita
        // di leggere oltre la fine del vettore.
        const auto freshOut = captureOutput (freshVoice, carrier, 0, 64, captureLen, 0, continuousMidi);
        const double freshHz = measureF0 (freshOut, measFrom, measLen, SR);
        const double freshErr = centsError (freshHz, fIfSnapped);

        const bool freshOk = std::fabs (freshErr) < 10.0;
        if (! freshOk) ++failures;
        std::printf ("  controllo positivo (voce mai riattivata, stesso target da sempre): scostamento %.2f cent  %s\n",
                     freshErr, freshOk ? "OK" : "FALLITO — la misura di F0 stessa non e' affidabile qui");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
