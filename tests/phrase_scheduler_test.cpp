// Verifica numerica di PhraseScheduler (sessione 30, B-10 — FR-19).
//
// PERCHE' QUESTO FILE ESISTE, E PERCHE' NON ESISTEVA PRIMA
// -------------------------------------------------------
// PhraseScheduler e' stato fino a oggi l'UNICO modulo del progetto senza
// alcun test. Il motivo era tecnico e sta scritto in
// tests/empty_cell_hold_test.cpp: dipende da juce_core (juce::SpinLock in
// VoicePool.h, juce::jmin, juce::Uuid/String via HarmonyPreset.h), quindi non
// e' linkabile nel livello JUCE-free delle altre 7 suite (D-11). Questa suite
// linka juce::juce_core e gira in ctest ma NON nel gate a g++ nudo della CI —
// vedi D-16, che emenda D-11, e A-06.
//
// Il costo di non averlo si e' visto: il bug B-10 e' il SECONDO difetto della
// stessa identica famiglia ("una voce smette di essere richiesta ma nessuno la
// mette in mute"). Il primo, corretto in sessione 12, e' documentato in
// Phrase.h:37-45. Nessuno dei due sarebbe arrivato all'ascolto con una misura
// come quella qui sotto.
//
// COSA MISURA
// -----------
// FR-19 [MUST]: "Il numero di voci attive e' selezionabile da 1 a 8. Le voci
// oltre il numero selezionato sono mute anche se il preset contiene offset per
// loro." Letto insieme a FR-17 (un cambio su nota tenuta si applica SUBITO,
// senza ribattere la nota), significa che abbassare il selettore deve spegnere
// le voci in eccesso DAL VIVO, su una frase gia' in suono.
//
// Il sintomo riportato dall'utente era asimmetrico: alzando le voci si
// sentivano entrare, abbassandole non uscivano; tornavano a tacere solo
// fermando l'audio e facendolo ripartire. P-1 e P-2 sono i due test che
// FALLISCONO sul codice pre-fix (verificato prima di scrivere il fix, non
// dopo: un test che passa anche su codice rotto non e' un test). P-3 e' il
// controllo di regressione sulla direzione che gia' funzionava, P-4 protegge
// dal rischio che il fix introduce davvero — un salto di ampiezza, cioe' il
// click di B-03/sessioni 12-13.
// ---------------------------------------------------------------------------

#include "voices/PhraseScheduler.h"
#include "TestSignals.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace
{
    constexpr double SR             = 44100.0;
    constexpr int    kBlockSize     = 256;
    constexpr int    kSlotCapacity  = 32;   // == HarmonizerAudioProcessor::hardVoiceSlotCapacity
    constexpr int    kPlayedNote    = 57;   // A3, coerente con f0 = 220 Hz della sorgente

    // Otto colonne TUTTE compilate: cosi' il numero di voci che suonano
    // dipende solo dal selettore, mai dalle celle vuote (che hanno un
    // percorso separato — EmptyCellHold, FR-17/sessione 28).
    std::array<harmony::Cell, harmony::numVoices> fullOffsets()
    {
        return { 0, 3, 7, 10, 14, 17, 21, 24 };
    }

    // Fa girare `numBlocks` blocchi di PhraseScheduler consumando la sorgente
    // in modo continuo. Se outL/outR non sono nulli, vi APPENDE l'uscita
    // (PhraseScheduler azzera internamente i suoi mix a ogni blocco, quindi
    // qui si accumula copiando, non sommando).
    void run (PhraseScheduler& sched,
              const std::vector<float>& src, size_t& srcPos,
              int numBlocks, int numRequestedVoices, bool onsetOnFirstBlock,
              std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
    {
        const auto offsets = fullOffsets();
        std::vector<float> blockL ((size_t) kBlockSize), blockR ((size_t) kBlockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            sched.process (&src[srcPos], blockL.data(), blockR.data(), kBlockSize,
                           /*onsetDetectedThisBlock*/ onsetOnFirstBlock && b == 0,
                           /*signalPresent*/  true,
                           /*inputIsStable*/  true,
                           kPlayedNote,
                           (float) kPlayedNote,
                           offsets,
                           numRequestedVoices,
                           /*applyStabilityChangeNow*/ false);

            if (outL != nullptr) outL->insert (outL->end(), blockL.begin(), blockL.end());
            if (outR != nullptr) outR->insert (outR->end(), blockR.begin(), blockR.end());

            srcPos += (size_t) kBlockSize;
        }
    }

    // Numero di blocchi ampiamente sufficiente perche' la dissolvenza
    // anti-click (kDeclickMs = 8 ms = 353 campioni a questa SR) sia finita e
    // lo slot sia stato restituito al pool.
    constexpr int kFadeBlocks = 24; // ~139 ms
    // Assestamento dopo il trigger: offsetGlide (30 ms di default) + latenza
    // dichiarata del motore, con margine.
    constexpr int kSettleBlocks = 60; // ~348 ms
}

int main()
{
    int failures = 0;

    const auto carrier = makeVowel (220.0, 12.0, SR);

    std::printf ("PhraseScheduler — FR-19: le voci oltre il numero selezionato devono tacere\n");
    std::printf ("SR=%.0f Hz, blocco=%d campioni, sorgente f0=220 Hz\n\n", SR, kBlockSize);

    // -----------------------------------------------------------------------
    // Misura di riferimento: RMS a regime di una frase nata direttamente con N
    // voci. Serve come bersaglio a P-2/P-3 — il punto non e' "l'ampiezza
    // scende un po'", e' "scende ESATTAMENTE al livello di N voci".
    auto steadyStateRms = [&] (int numVoices) -> double
    {
        PhraseScheduler sched;
        sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
        sched.setVoiceCap (kSlotCapacity);

        size_t pos = 0;
        run (sched, carrier, pos, 1, numVoices, /*onset*/ true);
        run (sched, carrier, pos, kSettleBlocks, numVoices, false);

        std::vector<float> L, R;
        run (sched, carrier, pos, kFadeBlocks, numVoices, false, &L, &R);
        return rms (L, 0, (int) L.size());
    };

    const double rms1 = steadyStateRms (1);
    const double rms2 = steadyStateRms (2);
    const double rms4 = steadyStateRms (4);

    std::printf ("Riferimenti a regime (frase nata con N voci): "
                 "1 voce = %.5f | 2 voci = %.5f | 4 voci = %.5f\n", rms1, rms2, rms4);

    // Controllo di sanita' della misura stessa (regola 13): se questi tre
    // valori non fossero separati, P-2 non potrebbe distinguere nulla e
    // "passerebbe" per un motivo sbagliato.
    if (! (rms1 < rms2 && rms2 < rms4))
    {
        std::printf ("  FALLITO: i riferimenti non sono monotoni — la misura non discrimina\n");
        ++failures;
    }
    else
    {
        std::printf ("  OK: riferimenti monotoni e separati (rapporto 4voci/2voci = %.2f)\n",
                     rms4 / rms2);
    }

    // -----------------------------------------------------------------------
    std::printf ("\nP-1 — abbassando il selettore, il conteggio delle voci attive scende\n");
    {
        PhraseScheduler sched;
        sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
        sched.setVoiceCap (kSlotCapacity);

        size_t pos = 0;
        run (sched, carrier, pos, 1, 4, /*onset*/ true);
        run (sched, carrier, pos, kSettleBlocks, 4, false);
        const int activeAt4 = sched.getNumActiveVoices();

        // Nessun onset qui: e' esattamente il caso del reclamo — nota tenuta,
        // selettore girato, nessuna ribattuta (FR-17).
        run (sched, carrier, pos, kFadeBlocks, 2, false);
        const int activeAt2 = sched.getNumActiveVoices();

        run (sched, carrier, pos, kFadeBlocks, 1, false);
        const int activeAt1 = sched.getNumActiveVoices();

        std::printf ("  voci attive: con selettore a 4 = %d | poi a 2 = %d | poi a 1 = %d\n",
                     activeAt4, activeAt2, activeAt1);

        const bool ok = activeAt4 == 4 && activeAt2 == 2 && activeAt1 == 1;
        std::printf ("  atteso 4 / 2 / 1  %s\n", ok ? "OK" : "FALLITO (le voci non si spengono)");
        if (! ok) ++failures;
    }

    // -----------------------------------------------------------------------
    std::printf ("\nP-2 — e l'ampiezza scende davvero al livello giusto (e' cio' che si sente)\n");
    {
        PhraseScheduler sched;
        sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
        sched.setVoiceCap (kSlotCapacity);

        size_t pos = 0;
        run (sched, carrier, pos, 1, 4, /*onset*/ true);
        run (sched, carrier, pos, kSettleBlocks, 4, false);

        run (sched, carrier, pos, kFadeBlocks, 2, false); // dissolvenza in corso: non si misura
        std::vector<float> L, R;
        run (sched, carrier, pos, kFadeBlocks, 2, false, &L, &R);

        const double after = rms (L, 0, (int) L.size());
        const double devFrom2 = 100.0 * (after - rms2) / rms2;
        const double devFrom4 = 100.0 * (after - rms4) / rms4;

        std::printf ("  RMS dopo 4->2 = %.5f | bersaglio 2 voci = %.5f (scarto %+.1f%%) "
                     "| 4 voci = %.5f (scarto %+.1f%%)\n",
                     after, rms2, devFrom2, rms4, devFrom4);

        // Il criterio e' comparativo, non assoluto: deve somigliare al
        // riferimento a 2 voci MOLTO piu' che a quello a 4.
        const bool ok = std::fabs (devFrom2) < std::fabs (devFrom4) * 0.5;
        std::printf ("  deve somigliare a 2 voci, non a 4  %s\n",
                     ok ? "OK" : "FALLITO (l'ampiezza e' rimasta quella di 4 voci)");
        if (! ok) ++failures;
        if (! allFinite (L) || ! allFinite (R)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nP-3 — regressione: risalire continua a funzionare come prima del fix\n");
    {
        PhraseScheduler sched;
        sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
        sched.setVoiceCap (kSlotCapacity);

        size_t pos = 0;
        run (sched, carrier, pos, 1, 2, /*onset*/ true);
        run (sched, carrier, pos, kSettleBlocks, 2, false);
        const int activeAt2 = sched.getNumActiveVoices();

        // Scende e RISALE senza mai ribattere la nota: e' il giro completo che
        // l'utente fa col knob. Verifica anche che liberare gli slot in P-1
        // non abbia reso impossibile riprenderli.
        run (sched, carrier, pos, kFadeBlocks, 1, false);
        run (sched, carrier, pos, kSettleBlocks, 4, false);
        const int activeAt4 = sched.getNumActiveVoices();

        std::vector<float> L, R;
        run (sched, carrier, pos, kFadeBlocks, 4, false, &L, &R);
        const double after = rms (L, 0, (int) L.size());
        const double devFrom4 = 100.0 * (after - rms4) / rms4;

        std::printf ("  voci attive: partenza = %d -> (giu' a 1) -> risalito = %d | RMS %.5f "
                     "contro riferimento 4 voci %.5f (scarto %+.1f%%)\n",
                     activeAt2, activeAt4, after, rms4, devFrom4);

        const bool ok = activeAt2 == 2 && activeAt4 == 4 && std::fabs (devFrom4) < 25.0;
        std::printf ("  %s\n", ok ? "OK" : "FALLITO (risalire non ripristina le voci)");
        if (! ok) ++failures;
        if (! allFinite (L) || ! allFinite (R)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nP-4 — la voce si spegne SFUMANDO, non tagliata (anti-click, B-03/s.12-13)\n");
    {
        PhraseScheduler sched;
        sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
        sched.setVoiceCap (kSlotCapacity);

        size_t pos = 0;
        run (sched, carrier, pos, 1, 4, /*onset*/ true);
        run (sched, carrier, pos, kSettleBlocks, 4, false);

        // Cattura continua a cavallo dell'abbassamento: prima meta' a 4 voci,
        // seconda meta' a 2. Il confine cade esattamente a meta' del buffer.
        std::vector<float> L, R;
        run (sched, carrier, pos, kFadeBlocks, 4, false, &L, &R);
        const int boundary = (int) L.size();
        run (sched, carrier, pos, kFadeBlocks, 2, false, &L, &R);

        // Stessa convenzione di voice_test T-5/H1/H4: si confronta il salto
        // massimo campione-per-campione attorno al confine con quello a
        // regime, lontano da qualunque cambio.
        const int win = (int) (0.05 * SR);
        const double jumpAtBoundary = maxJump (L, boundary - win / 2, win);
        const double jumpRegime     = maxJump (L, win / 2, win);
        const double ratio = (jumpRegime > 0.0) ? jumpAtBoundary / jumpRegime : 1.0e9;

        std::printf ("  salto a regime = %.5f | salto al passaggio 4->2 = %.5f | rapporto = %.2f\n",
                     jumpRegime, jumpAtBoundary, ratio);

        const bool ok = ratio < 3.0; // stessa soglia diagnostica di voice_test
        std::printf ("  rapporto < 3.0  %s\n", ok ? "OK" : "FALLITO (salto di ampiezza = click)");
        if (! ok) ++failures;
        if (! allFinite (L) || ! allFinite (R)) { ++failures; std::printf ("  FALLITO: uscita non finita\n"); }
    }

    // -----------------------------------------------------------------------
    // P-5 — sessione 30, secondo giro: l'utente conferma che TOGLIERE voci ora
    // e' fluido (B-10 risolto all'ascolto per la discesa) ma segnala "un
    // piccolo click ogni volta che AGGIUNGO una nuova voce".
    //
    // La domanda da sciogliere PRIMA di toccare qualsiasi cosa e' se il click
    // sia stato introdotto dal fix di B-10 o se fosse gia' li'. I due
    // scenari qui sotto lo distinguono per costruzione:
    //   (a) SALITA PURA da una frase nata con 1 voce: il ramo aggiunto per
    //       B-10 non scatta MAI (le colonne oltre il selettore non hanno
    //       slot, quindi escono dal loop su slotIndex < 0). Un click qui e'
    //       PREESISTENTE.
    //   (b) DISCESA e poi RISALITA: gli slot vengono riciclati dopo goCold().
    //       Un click solo qui sarebbe invece imputabile al fix.
    // Stessa metrica di P-4, cosi' i numeri sono confrontabili fra loro.
    std::printf ("\nP-5 — aggiungere una voce non deve produrre un salto di ampiezza\n");
    {
        auto jumpOnAdd = [&] (const char* what, bool descendFirst) -> double
        {
            PhraseScheduler sched;
            sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
            sched.setVoiceCap (kSlotCapacity);

            size_t pos = 0;
            const int startVoices = descendFirst ? 4 : 1;
            run (sched, carrier, pos, 1, startVoices, /*onset*/ true);
            run (sched, carrier, pos, kSettleBlocks, startVoices, false);

            if (descendFirst)
            {
                run (sched, carrier, pos, kFadeBlocks, 1, false);
                run (sched, carrier, pos, kFadeBlocks, 1, false);
            }

            // Una voce in piu' per volta, con una cattura continua: ogni
            // confine e' l'istante esatto in cui la voce N+1 viene aggiunta.
            std::vector<float> L, R;
            std::vector<int> boundaries;

            run (sched, carrier, pos, kFadeBlocks, 1, false, &L, &R);
            for (int target = 2; target <= 4; ++target)
            {
                boundaries.push_back ((int) L.size());
                run (sched, carrier, pos, kFadeBlocks, target, false, &L, &R);
            }

            const int win = (int) (0.05 * SR);
            const double jumpRegime = maxJump (L, win / 2, win);

            double worst = 0.0;
            for (size_t i = 0; i < boundaries.size(); ++i)
            {
                const double j = maxJump (L, boundaries[i] - win / 2, win);
                worst = std::max (worst, j);
                std::printf ("      +1 voce (-> %d): salto %.5f | rapporto %.2f\n",
                             (int) i + 2, j, jumpRegime > 0.0 ? j / jumpRegime : 1.0e9);
            }

            const double ratio = (jumpRegime > 0.0) ? worst / jumpRegime : 1.0e9;
            std::printf ("    %s: salto a regime %.5f | peggiore in aggiunta %.5f | rapporto %.2f\n",
                         what, jumpRegime, worst, ratio);
            if (! allFinite (L) || ! allFinite (R)) { ++failures; std::printf ("    FALLITO: uscita non finita\n"); }
            return ratio;
        };

        std::printf ("  (a) salita pura 1->2->3->4 (il ramo di B-10 non scatta mai)\n");
        const double ratioPure = jumpOnAdd ("salita pura", false);

        std::printf ("  (b) discesa 4->1 e poi risalita 1->2->3->4 (slot riciclati)\n");
        const double ratioRecycled = jumpOnAdd ("dopo riciclo", true);

        const bool okPure      = ratioPure < 3.0;      // stessa soglia di P-4
        const bool okRecycled  = ratioRecycled < 3.0;
        std::printf ("  rapporto < 3.0 in entrambi gli scenari  %s\n",
                     (okPure && okRecycled) ? "OK" : "FALLITO (click in aggiunta)");
        if (! okPure)     ++failures;
        if (! okRecycled) ++failures;

        std::printf ("  DIAGNOSI: %s\n",
                     (! okPure && ! okRecycled) ? "click in ENTRAMBI -> preesistente, non introdotto da B-10"
                     : (okPure && ! okRecycled) ? "click SOLO col riciclo -> imputabile al fix di B-10"
                     : (! okPure && okRecycled) ? "click SOLO in salita pura -> preesistente"
                                                : "nessun salto di ampiezza misurabile in aggiunta");
    }

    // -----------------------------------------------------------------------
    // P-6 — P-5 non ha trovato nulla, ma la sua metrica e' inadatta e va detto
    // (CLAUDE.md regola 13). maxJump misura il MIX: il salto della singola
    // voce che entra e' diluito fra le altre 2-3 gia' in suono, e il "salto a
    // regime" di riferimento cresce col numero di voci — il rapporto sale da
    // 1.06 a 1.80 semplicemente perche' il segnale e' piu' grande, non perche'
    // ci sia un artefatto. P-5 resta come guardia contro un salto GROSSOLANO,
    // non come prova che non ci sia un click.
    //
    // Qui si isola il contributo della SOLA voce aggiunta, per differenza fra
    // due rig identici (uno aggiunge la voce, l'altro no: tutto il resto e'
    // deterministico e si cancella). Sul residuo si misura la forma
    // dell'attacco, che e' la grandezza giusta: quanto ci mette a salire.
    //
    // Perche' proprio questa: la dissolvenza anti-click di Voice dura
    // kDeclickMs = 8 ms, ma il motore appena riattivato ha un ritardo pari
    // alla propria latenza dichiarata prima di produrre segnale (B-04/s.27).
    // Se ampGlide finisce la propria rampa DENTRO quel silenzio, quando il
    // segnale vero arriva la voce e' gia' a guadagno pieno: entra di netto.
    // Un tempo di salita molto piu' corto di 8 ms e' esattamente questa
    // firma. E' anche il motivo per cui H1 in voice_test.cpp non lo vedeva:
    // li' si misurava il rapporto di slew, non il tempo di salita.
    std::printf ("\nP-6 — forma dell'attacco della SOLA voce aggiunta (isolata per differenza)\n");
    {
        auto captureWith = [&] (int voicesAfter) -> std::vector<float>
        {
            PhraseScheduler sched;
            sched.prepare (kSlotCapacity, SR, kBlockSize, Stability::defaultLevel);
            sched.setVoiceCap (kSlotCapacity);

            size_t pos = 0;
            run (sched, carrier, pos, 1, 2, /*onset*/ true);
            run (sched, carrier, pos, kSettleBlocks, 2, false);

            std::vector<float> L, R;
            run (sched, carrier, pos, kFadeBlocks * 3, voicesAfter, false, &L, &R);
            return L;
        };

        const auto base = captureWith (2); // resta a 2 voci
        const auto more = captureWith (3); // ne aggiunge una nello stesso istante

        std::vector<float> added (base.size());
        for (size_t i = 0; i < base.size(); ++i)
            added[i] = more[i] - base[i];

        // Inviluppo RMS scorrevole corto (1 ms): abbastanza fine da vedere una
        // rampa di 8 ms, abbastanza liscio da non inseguire le singole creste.
        const int envWin = (int) (0.001 * SR);
        std::vector<double> env (added.size(), 0.0);
        {
            double sumSq = 0.0;
            for (size_t i = 0; i < added.size(); ++i)
            {
                sumSq += (double) added[i] * (double) added[i];
                if (i >= (size_t) envWin)
                    sumSq -= (double) added[i - (size_t) envWin] * (double) added[i - (size_t) envWin];
                env[i] = std::sqrt (sumSq / std::min<size_t> (i + 1, (size_t) envWin));
            }
        }

        // Livello a regime della voce aggiunta: ultimo quarto della cattura.
        const int tailFrom = (int) (added.size() * 3 / 4);
        const double steady = rms (added, tailFrom, (int) added.size() - tailFrom);

        auto firstAtLeast = [&] (double frac) -> int
        {
            for (size_t i = 0; i < env.size(); ++i)
                if (env[i] >= frac * steady)
                    return (int) i;
            return -1;
        };

        const int t10 = firstAtLeast (0.10);
        const int t90 = firstAtLeast (0.90);
        const double riseMs  = (t90 >= 0 && t10 >= 0) ? 1000.0 * (t90 - t10) / SR : -1.0;
        const double delayMs = (t10 >= 0) ? 1000.0 * t10 / SR : -1.0;

        std::printf ("  livello a regime della voce aggiunta = %.5f\n", steady);
        std::printf ("  ritardo prima che parta (10%%) = %.2f ms | tempo di salita 10%%->90%% = %.2f ms\n",
                     delayMs, riseMs);
        std::printf ("  atteso: salita ~ kDeclickMs (8 ms). Molto meno = entra di netto (click)\n");

        // Soglia: la rampa anti-click esiste per durare 8 ms. Sotto 4 ms
        // (meta') la dissolvenza e' di fatto saltata — stessa logica con cui
        // sessione 13 ha dichiarato no-op la dissolvenza a blocchi da 4096.
        const bool ok = riseMs >= 4.0;
        std::printf ("  salita >= 4 ms  %s\n", ok ? "OK" : "FALLITO (la dissolvenza d'ingresso non c'e')");
        if (! ok) ++failures;
        if (! allFinite (added)) { ++failures; std::printf ("  FALLITO: residuo non finito\n"); }
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
