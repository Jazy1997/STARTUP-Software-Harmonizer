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
// essere piu' corto). Ritorna SOLO il canale L (FR-11: processAdd e' ora
// stereo, ma tutti i test H1/H3/H4/controllo negativo preesistenti misurano
// un solo canale — con gain/pan al default L ed R sono bit-identici, vedi
// T-3 piu' sotto, quindi restare su L basta a preservare esattamente le
// stesse misure di prima senza toccarne le soglie, CLAUDE.md regola 13). Il
// buffer R viene comunque prodotto (processAdd lo richiede) e scartato.
// mixL/mixR devono essere pre-azzerati (Voice::processAdd ACCUMULA, come da
// contratto in Voice.h) — un vector<float> appena costruito e' gia' a zero.
// ---------------------------------------------------------------------------
static std::vector<float> captureOutput (Voice& voice, const std::vector<float>& src, size_t srcOffset,
                                         int blockSize, int totalSamples,
                                         int quantizedPlayedNote, float continuousMidi)
{
    std::vector<float> outL ((size_t) totalSamples, 0.0f);
    std::vector<float> outR ((size_t) totalSamples, 0.0f);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        voice.processAdd (&src[srcOffset + (size_t) done], &outL[(size_t) done], &outR[(size_t) done], n,
                          quantizedPlayedNote, continuousMidi);
        done += n;
    }
    return outL;
}

// Come captureOutput, ma ritorna ENTRAMBI i canali — usato dai test T-1..T-5
// (FR-11), che devono ispezionare L ed R separatamente.
struct StereoOutput { std::vector<float> left, right; };

static StereoOutput captureStereoOutput (Voice& voice, const std::vector<float>& src, size_t srcOffset,
                                          int blockSize, int totalSamples,
                                          int quantizedPlayedNote, float continuousMidi)
{
    StereoOutput out;
    out.left.assign ((size_t) totalSamples, 0.0f);
    out.right.assign ((size_t) totalSamples, 0.0f);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        voice.processAdd (&src[srcOffset + (size_t) done], &out.left[(size_t) done], &out.right[(size_t) done], n,
                          quantizedPlayedNote, continuousMidi);
        done += n;
    }
    return out;
}

// Fa avanzare lo stato di `voice` di `totalSamples` campioni senza
// conservare l'uscita (usato per le fasi di "assestamento" prima della
// misura vera e propria: portare offsetGlide/gainGlide/panGlide a
// convergenza, o svuotare ampGlide fino al silenzio).
static void runSilently (Voice& voice, const std::vector<float>& src, size_t srcOffset,
                         int blockSize, int totalSamples,
                         int quantizedPlayedNote, float continuousMidi)
{
    std::vector<float> scratchL ((size_t) blockSize, 0.0f);
    std::vector<float> scratchR ((size_t) blockSize, 0.0f);
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        std::fill (scratchL.begin(), scratchL.begin() + n, 0.0f);
        std::fill (scratchR.begin(), scratchR.begin() + n, 0.0f);
        voice.processAdd (&src[srcOffset + (size_t) done], scratchL.data(), scratchR.data(), n,
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

    // =========================================================================
    // FR-11/§8.1 — gain e pan per voce (sessione 23). Cinque test, T-1..T-5,
    // pianificati PRIMA di scrivere il fix (CLAUDE.md regola 12/13): T-3 in
    // particolare e' la garanzia che il default (gain 0dB, pan centro) non
    // cambi il suono rispetto a prima di questo lavoro.
    // =========================================================================

    // T-1 — pan hard left/right: un canale deve restare ESATTAMENTE a zero
    // (non solo piccolo), l'altro deve portare tutto il segnale.
    std::printf ("\nT-1 — pan tutto a sinistra/destra: il canale opposto deve restare ESATTAMENTE zero\n");
    {
        auto measureHardPan = [&] (float panValue, bool expectRightSilent) -> bool
        {
            Voice voice;
            voice.prepare (SR, maxBlockPrepare, Stability::defaultLevel);
            voice.setMuted (false);
            voice.setTargetOffsetSemitones (0.0f);
            voice.setPan (panValue);

            // Lascia assestare la rampa anti-click di pan (kDeclickMs=8ms =
            // 353 campioni a questa SR) ben oltre la sua durata prima di
            // misurare — altrimenti si misurerebbe un pan ancora in transito.
            runSilently (voice, carrier, 0, 256, 2000, 0, continuousMidi);

            const int total = 4096;
            const auto stereo = captureStereoOutput (voice, carrier, 2000, 256, total, 0, continuousMidi);

            const auto& silentChannel = expectRightSilent ? stereo.right : stereo.left;
            const auto& fullChannel   = expectRightSilent ? stereo.left  : stereo.right;

            double maxAbsSilent = 0.0, maxAbsFull = 0.0;
            for (float s : silentChannel) maxAbsSilent = std::max (maxAbsSilent, (double) std::fabs (s));
            for (float s : fullChannel)   maxAbsFull   = std::max (maxAbsFull,   (double) std::fabs (s));

            const bool ok = maxAbsSilent == 0.0 && maxAbsFull > 0.01;
            std::printf ("  pan=%+.0f: canale %s max|.|=%.3e (atteso 0 esatto), canale %s max|.|=%.4f (atteso >0)  %s\n",
                         panValue, expectRightSilent ? "R" : "L", maxAbsSilent,
                         expectRightSilent ? "L" : "R", maxAbsFull, ok ? "OK" : "FALLITO");
            if (! allFinite (stereo.left) || ! allFinite (stereo.right)) return false;
            return ok;
        };

        if (! measureHardPan (-1.0f, /*expectRightSilent*/ true))  ++failures;
        if (! measureHardPan (+1.0f, /*expectRightSilent*/ false)) ++failures;
    }

    // T-2 — potenza costante: L^2+R^2 sommati sull'intero buffer non deve
    // dipendere dalla posizione del pan (nessun calo/salto di volume
    // ruotando il knob). Stesso segmento sorgente per ogni pan testato, cosi'
    // l'unica variabile e' il fattore di pan, non il contenuto del segnale.
    std::printf ("\nT-2 — potenza costante: energia L+R non deve dipendere dalla posizione del pan\n");
    {
        const std::vector<float> panPositions { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        const int total = 4096;
        double referenceEnergy = -1.0;
        bool ok = true;

        for (float pan : panPositions)
        {
            Voice voice;
            voice.prepare (SR, maxBlockPrepare, Stability::defaultLevel);
            voice.setMuted (false);
            voice.setTargetOffsetSemitones (0.0f);
            voice.setPan (pan);
            runSilently (voice, carrier, 0, 256, 2000, 0, continuousMidi);

            const auto stereo = captureStereoOutput (voice, carrier, 2000, 256, total, 0, continuousMidi);

            double energy = 0.0;
            for (int i = 0; i < total; ++i)
                energy += (double) stereo.left[(size_t) i] * stereo.left[(size_t) i]
                        + (double) stereo.right[(size_t) i] * stereo.right[(size_t) i];

            if (referenceEnergy < 0.0)
                referenceEnergy = energy;

            const double ratio = energy / referenceEnergy;
            const bool thisOk = ratio > 0.98 && ratio < 1.02;
            if (! thisOk) ok = false;
            std::printf ("  pan=%+.2f  energia L+R=%.4f  rapporto al primo campione=%.4f  %s\n",
                         pan, energy, ratio, thisOk ? "OK" : "FALLITO");
        }
        if (! ok) ++failures;
    }

    // T-3 — regressione bit-per-bit: con gain/pan al DEFAULT (mai toccati),
    // L ed R devono essere ESATTAMENTE identici campione per campione. E'
    // la garanzia che introdurre questo lavoro non cambi il suono prodotto
    // finora dal plugin (vedi Voice.cpp, panToLR: caso pan==0.0f esplicito,
    // non lasciato al calcolo trigonometrico, proprio per questa garanzia).
    std::printf ("\nT-3 — con gain/pan di default L ed R devono essere IDENTICI campione per campione\n");
    {
        Voice voice;
        voice.prepare (SR, maxBlockPrepare, Stability::defaultLevel);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (3.0f); // shift reale, non unisono: non banale

        const int total = 8192;
        const auto stereo = captureStereoOutput (voice, carrier, 0, 300, total, 0, continuousMidi);

        bool identical = true;
        double maxDiff = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double diff = std::fabs ((double) stereo.left[(size_t) i] - (double) stereo.right[(size_t) i]);
            maxDiff = std::max (maxDiff, diff);
            if (stereo.left[(size_t) i] != stereo.right[(size_t) i])
                identical = false;
        }
        std::printf ("  differenza massima L-R = %.3e (atteso 0 esatto)  %s\n",
                     maxDiff, identical ? "OK (bit-identici)" : "FALLITO");
        if (! identical) ++failures;
        if (! allFinite (stereo.left) || ! allFinite (stereo.right)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    // T-4 — gain al minimo (-60dB, gia' convertito in lineare da
    // PluginProcessor prima di arrivare qui — vedi juce::Decibels::
    // decibelsToGain nel commento di ParamIDs::voiceGain): l'uscita deve
    // essere esattamente nulla, nessun residuo.
    std::printf ("\nT-4 — gain lineare 0.0 (equivalente a -60dB) deve produrre uscita ESATTAMENTE nulla\n");
    {
        Voice voice;
        voice.prepare (SR, maxBlockPrepare, Stability::defaultLevel);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (0.0f);
        voice.setGainLinear (0.0f);
        runSilently (voice, carrier, 0, 256, 2000, 0, continuousMidi); // assesta gainGlide

        const int total = 4096;
        const auto stereo = captureStereoOutput (voice, carrier, 2000, 256, total, 0, continuousMidi);

        double maxAbsL = 0.0, maxAbsR = 0.0;
        for (float s : stereo.left)  maxAbsL = std::max (maxAbsL, (double) std::fabs (s));
        for (float s : stereo.right) maxAbsR = std::max (maxAbsR, (double) std::fabs (s));

        const bool ok = maxAbsL == 0.0 && maxAbsR == 0.0;
        std::printf ("  max|L|=%.3e  max|R|=%.3e (atteso 0 esatto su entrambi)  %s\n",
                     maxAbsL, maxAbsR, ok ? "OK" : "FALLITO");
        if (! ok) ++failures;
    }

    // T-5 — anti-click con un blocco LUNGO (4096 campioni, il caso reale
    // misurato in sessione 13 con MME/DirectX in Ableton): un salto brusco
    // di gain e di pan, impostato esattamente al confine fra due blocchi da
    // 4096 campioni (come farebbe processBlock leggendo un nuovo valore di
    // parametro a inizio blocco), non deve produrre una discontinuita'
    // campione-per-campione AL CONFINE. E' il rischio piu' concreto di
    // questo lavoro: se le rampe di gain/pan fossero applicate una volta per
    // blocco invece che per campione (esattamente il bug di sessione 13 su
    // ampGlide/dry/wet), il campione all'inizio del secondo blocco
    // salterebbe gia' al valore di regime finale, e questo test lo deve
    // rilevare. Cattura UNICA e continua (non due chiamate separate): solo
    // cosi' maxJump puo' vedere il confine fra gli ultimi campioni del primo
    // blocco e i primi del secondo.
    std::printf ("\nT-5 — salto di gain/pan al confine fra due blocchi da 4096 campioni non deve clickare\n");
    {
        Voice voice;
        voice.prepare (SR, 4096, Stability::defaultLevel);
        voice.setMuted (false);
        voice.setTargetOffsetSemitones (0.0f);
        voice.setGainLinear (1.0f);
        voice.setPan (0.0f);

        // Assesta tutto (offset/gain/pan glide, PSOLA a regime) ben oltre
        // l'attacco — stessa convenzione di regimeFrom=0.30s usata in H1.
        runSilently (voice, carrier, 0, 4096, (int) (0.30 * SR), 0, continuousMidi);

        constexpr int blockLen = 4096;
        const size_t srcOffset = (size_t) (0.30 * SR);
        std::vector<float> outL ((size_t) (2 * blockLen), 0.0f);
        std::vector<float> outR ((size_t) (2 * blockLen), 0.0f);

        // Primo blocco: ancora a gain=1/pan=0 (regime).
        voice.processAdd (&carrier[srcOffset], outL.data(), outR.data(), blockLen, 0, continuousMidi);

        // Salto brusco A CONFINE DI BLOCCO: -20dB circa (0.1 lineare) e pan
        // tutto a destra, impostati PRIMA di chiamare processAdd sul secondo
        // blocco — esattamente come processBlock legge un parametro cambiato
        // a inizio blocco.
        voice.setGainLinear (0.1f);
        voice.setPan (1.0f);
        voice.processAdd (&carrier[srcOffset + (size_t) blockLen], outL.data() + blockLen, outR.data() + blockLen,
                          blockLen, 0, continuousMidi);

        // Finestra CENTRATA sul confine (indice blockLen-1 -> blockLen):
        // misura esattamente il salto che l'anti-click deve evitare.
        const int winLen = (int) (0.05 * SR);
        const int boundaryFrom = blockLen - winLen / 2;
        const double slewAtBoundary = maxJump (outL, boundaryFrom, winLen);

        // Riferimento "a regime": stessa larghezza di finestra, ma lontano
        // da qualunque confine o cambio di parametro (dentro il primo blocco).
        const double slewRegime = maxJump (outL, blockLen / 2 - winLen / 2, winLen);
        const double slewRatio = (slewRegime > 0.0) ? slewAtBoundary / slewRegime : 1.0e9;

        // Soglia diagnostica: stesso ordine di grandezza usato per H1/H4 sopra
        // (rapporto < 3x fra lo slew al confine e lo slew a regime).
        const bool ok = slewRatio < 3.0;
        std::printf ("  slew a regime = %.5f | slew al confine fra i due blocchi = %.5f | rapporto = %.2f  %s\n",
                     slewRegime, slewAtBoundary, slewRatio, ok ? "OK" : "FALLITO (click)");
        if (! ok) ++failures;

        if (! allFinite (outL) || ! allFinite (outR)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
