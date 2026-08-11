// Strumento diagnostico OFFLINE — sessione 31 ("attacchi sporchi", B-13).
//
// Misura UNA SOLA COSA: quale sequenza di offset la catena di controllo chiede
// al motore mentre la melodia passa da una nota alla successiva. Non c'e'
// nessun Voice e nessun PsolaShifter qui dentro, di proposito — il sospetto di
// sessione 31 non e' sul motore (il timbro e' chiuso, B-02/s.26) ma su cosa
// gli viene CHIESTO di suonare. Isolare la catena di controllo dal motore e'
// l'unico modo di dire se il difetto nasce prima o dentro la sintesi.
//
// Girano i moduli VERI, non modelli: PitchDetector (Cycfi Q), OnsetDetector,
// PitchLatch, HarmonyEngine::degreeOf e stepEmptyCellHold. La riproduzione del
// ramo "cella vuota su frase viva" ricalca PhraseScheduler::process (vedi il
// commento sul posto): sotto la soglia di attesa la voce NON viene ri-targetta
// e resta sull'ultimo offset valido.
//
// Il numero che conta e' in fondo: quante CORSE distinte di offset applicato
// esistono nel file. Una melodia di N note dovrebbe produrne N. Ogni corsa in
// piu' e' un offset che il motore ha davvero suonato e che il preset non
// prevedeva su nessuna delle note eseguite — un grado di passaggio letto per
// sbaglio.
//
// NON in ctest: dipende da file WAV esterni forniti dall'utente
// (SAMPLE TEST/, fuori da git — vedi D-14).
//
// Uso:
//   degree_trace_probe <dry.wav> <tabella> [root=0] [block=1024] [tIni tFin]
//
//   <tabella>  12 celle separate da virgola, nell'ordine R b2 2 b3 3 4 b5 5 b6
//              6 b7 7. Una cella VUOTA e' la cella vuota del preset (regola 3
//              di CLAUDE.md: diversa da "0"). Va quotata nella shell.
//              Test#1: "-7,,-1,,-7,,,,,,,"
//              Test#2: "-7,,-7,,-7,,,,,,,"
//              Test#3: "-7,-2,-7,-2,-7,,,,,,,"
//   [tIni tFin] se forniti, stampa anche la traccia blocco per blocco su
//              quell'intervallo. Senza, stampa solo le corse e il riepilogo.

#include "harmony/PitchLatch.h"
#include "voices/EmptyCellHold.h"
#include "dsp/PitchDetector.h"
#include "dsp/OnsetDetector.h"
#include "SampleAnalysis.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <algorithm>
#include <cmath>

namespace
{
    // degreeOf arriva da SampleAnalysis.h, che lo reimplementa gia' per non
    // linkare harmony/HarmonyEngine.cpp (dipende da HarmonyPreset.h ->
    // juce_core). Stessa invariante di HarmonyEngine::degreeOf.

    // Stessa soglia di PhraseScheduler.h (kEmptyCellHoldMs): la sonda deve
    // riprodurre il comportamento reale, non una sua variante.
    constexpr float kEmptyCellHoldMs = 80.0f;

    std::array<std::optional<int>, 12> parseTable (const std::string& spec, bool& ok)
    {
        std::array<std::optional<int>, 12> table {};
        int index = 0;
        std::string field;
        ok = true;

        auto commit = [&] ()
        {
            if (index >= 12) { ok = false; return; }
            if (! field.empty())
            {
                try { table[(size_t) index] = std::stoi (field); }
                catch (...) { ok = false; }
            }
            ++index;
            field.clear();
        };

        for (char c : spec)
        {
            if (c == ',') commit();
            else if (c != ' ') field += c;
        }
        commit();

        if (index != 12)
            ok = false;
        return table;
    }

    const char* kDegreeNames[12] = { "R", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };

    // Una corsa di offset applicato: valore, quando comincia, quanto dura.
    struct Run
    {
        std::optional<int> offset; // nullopt = voce muta (cella vuota oltre la soglia)
        bool muted = false;
        double startSec = 0.0;
        double endSec = 0.0;
        int degreeAtStart = -1;
    };
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("Uso: degree_trace_probe <dry.wav> <tabella 12 celle> [root=0] [block=1024]"
                     " [notaMinimaMidi=35] [frameAttesa=1.5] [tIni tFin]\n");
        std::printf ("  es.: degree_trace_probe dry.wav \"-7,-2,-7,-2,-7,,,,,,,\" 0 1024 44\n");
        return 1;
    }

    const std::string path = argv[1];
    bool tableOk = false;
    const auto table = parseTable (argv[2], tableOk);
    if (! tableOk)
    {
        std::printf ("Tabella non valida: servono esattamente 12 celle separate da virgola (vuoto = cella vuota).\n");
        return 1;
    }

    const int rootPitchClass = argc > 3 ? std::stoi (argv[3]) : 0;
    const int block = argc > 4 ? std::stoi (argv[4]) : 1024;
    const int lowestNoteMidi = argc > 5 ? std::stoi (argv[5]) : PitchDetector::kDefaultLowestNoteMidi;
    // Quanti frame d'analisi deve reggere un candidato prima di essere
    // adottato. Argomento CLI per poterlo TARARE misurando invece che
    // indovinando: e' il compromesso fra prontezza dell'armonia e immunita'
    // alle sbandate del rilevatore (che con finestre corte aumentano).
    const float settleFrames = argc > 6 ? std::stof (argv[6]) : harmony::PitchLatch::kSettleFrames;
    const bool doTrace = argc > 8;
    const double traceStartSec = doTrace ? std::stod (argv[7]) : 0.0;
    const double traceEndSec = doTrace ? std::stod (argv[8]) : 0.0;

    WavFile wav;
    std::string error;
    if (! readWav (path, wav, error))
    {
        std::printf ("Errore leggendo '%s': %s\n", path.c_str(), error.c_str());
        return 1;
    }

    const auto mono = downmix (wav);
    const double sr = wav.sampleRate;
    const int emptyCellHoldSamples = (int) std::lround (kEmptyCellHoldMs * sr / 1000.0);

    PitchDetector pitchDetector;
    pitchDetector.prepare (sr, lowestNoteMidi);
    const int frameSamples = pitchDetector.getAnalysisFrameSamples();

    std::printf ("File: %s (%.0f Hz, %.2fs)\n", path.c_str(), sr, mono.size() / sr);
    std::printf ("Root: %s, block: %d campioni (%.1f ms)\n",
                 kDegreeNames[((rootPitchClass % 12) + 12) % 12], block, 1000.0 * block / sr);
    std::printf ("Nota minima: MIDI %d = %.1f Hz -> finestra %d campioni (%.1f ms), una stima ogni %.1f ms\n",
                 lowestNoteMidi, PitchDetector::noteToHz (lowestNoteMidi),
                 frameSamples * 2, 2000.0 * frameSamples / sr, 1000.0 * frameSamples / sr);
    std::printf ("Tabella: ");
    for (int d = 0; d < 12; ++d)
        if (table[(size_t) d].has_value())
            std::printf ("%s=%d ", kDegreeNames[d], *table[(size_t) d]);
        else
            std::printf ("%s=. ", kDegreeNames[d]);
    std::printf ("\n\n");

    OnsetDetector onsetDetector;
    onsetDetector.prepare (sr);
    harmony::PitchLatch pitchLatch;
    pitchLatch.prepare (harmony::PitchLatch::settleSamplesForFrame (pitchDetector.getAnalysisFrameSamples(), settleFrames));

    // Stato della "voce": ultimo offset davvero applicato al motore.
    std::optional<int> appliedOffset;
    bool muted = true;
    int emptySamples = 0;
    bool signalPresentLastBlock = false;
    // Stesso stato che PluginProcessor tiene fra un blocco e l'altro (B-14).
    int samplesSinceLatchUpdate = 0;
    bool onsetPendingForLatch = false;
    int lastLatchedNote = 0;

    std::vector<Run> runs;
    bool haveCurrentRun = false;
    Run current {};

    if (doTrace)
    {
        std::printf ("=== TRACCIA BLOCCO PER BLOCCO %.3f-%.3fs ===\n", traceStartSec, traceEndSec);
        std::printf ("%9s %9s %7s %6s %6s %7s %5s %8s %9s\n",
                     "t", "midi", "conf", "stab", "onset", "held", "grado", "cella", "applicato");
    }

    int done = 0;
    while (done + block <= (int) mono.size())
    {
        // Identico a PluginProcessor::processBlock (B-14): l'aggancio si
        // aggiorna a ogni STIMA NUOVA del rilevatore, non una volta per
        // blocco, cosi' l'attesa di PitchLatch misura il tempo vero fra due
        // stime e non i campioni del buffer dell'host. Se questa sonda
        // aggiornasse per blocco misurerebbe un altro programma.
        bool onsetThisBlock = false;
        for (int i = 0; i < block; ++i)
        {
            const bool estimateUpdated = pitchDetector.pushSample (mono[(size_t) (done + i)]);

            if (onsetDetector.pushSample (mono[(size_t) (done + i)]))
            {
                onsetThisBlock = true;
                onsetPendingForLatch = true;
            }

            ++samplesSinceLatchUpdate;

            if (estimateUpdated && pitchDetector.hasStableSignal())
            {
                lastLatchedNote = pitchLatch.update (pitchDetector.getMidiNote(),
                                                     onsetPendingForLatch,
                                                     samplesSinceLatchUpdate);
                onsetPendingForLatch = false;
                samplesSinceLatchUpdate = 0;
            }
        }

        const double tSec = (double) done / sr;
        const float continuousMidi = pitchDetector.getMidiNote();
        const bool inputIsStable = pitchDetector.hasStableSignal();
        const bool signalPresent = onsetDetector.isGateOpen();

        // PluginProcessor.cpp: al fronte di discesa del gate si azzera anche il
        // rilevatore, non solo il latch.
        if (signalPresentLastBlock && ! signalPresent)
            pitchDetector.reset();
        signalPresentLastBlock = signalPresent;

        int heldNote = -1;
        int degree = -1;
        std::optional<int> cell;

        if (inputIsStable)
        {
            heldNote = lastLatchedNote; // gia' aggiornato dal ciclo di campioni
            degree = degreeOf (heldNote, rootPitchClass);
            cell = table[(size_t) degree];
        }
        else if (! signalPresent)
        {
            pitchLatch.reset();
        }

        // Ramo di PhraseScheduler::process per UNA voce di una frase viva.
        if (! signalPresent)
        {
            muted = true;
            appliedOffset.reset();
            emptySamples = 0;
        }
        else if (inputIsStable)
        {
            if (! cell.has_value())
            {
                const auto hold = stepEmptyCellHold (emptySamples, block, false, emptyCellHoldSamples);
                emptySamples = hold.emptySamplesAfter;
                if (hold.shouldMuteNow)
                    muted = true;
                // sotto soglia: nessun mute e nessun ri-target, appliedOffset resta com'e'
            }
            else
            {
                emptySamples = 0;
                muted = false;
                appliedOffset = cell;
            }
        }
        // else: ne' onset ne' pitch confidente — PhraseScheduler non tocca nulla.

        const bool sameAsCurrent = haveCurrentRun
                                   && current.muted == muted
                                   && current.offset.has_value() == appliedOffset.has_value()
                                   && (! appliedOffset.has_value() || *current.offset == *appliedOffset);

        if (! sameAsCurrent)
        {
            if (haveCurrentRun)
            {
                current.endSec = tSec;
                runs.push_back (current);
            }
            current = Run { appliedOffset, muted, tSec, tSec, degree };
            haveCurrentRun = true;
        }

        if (doTrace && tSec >= traceStartSec && tSec <= traceEndSec)
        {
            char cellText[8], appliedText[8];
            if (cell.has_value()) std::snprintf (cellText, sizeof cellText, "%d", *cell);
            else std::snprintf (cellText, sizeof cellText, ".");
            if (muted) std::snprintf (appliedText, sizeof appliedText, "MUTA");
            else if (appliedOffset.has_value()) std::snprintf (appliedText, sizeof appliedText, "%d", *appliedOffset);
            else std::snprintf (appliedText, sizeof appliedText, "-");

            std::printf ("%9.4f %9.3f %7.3f %6s %6s %7d %5s %8s %9s\n",
                         tSec, continuousMidi, pitchDetector.getConfidence(),
                         inputIsStable ? "si" : "no", onsetThisBlock ? "SI" : "",
                         heldNote, degree >= 0 ? kDegreeNames[degree] : "-",
                         cellText, appliedText);
        }

        done += block;
    }

    if (haveCurrentRun)
    {
        current.endSec = (double) done / sr;
        runs.push_back (current);
    }

    std::printf ("\n=== CORSE DI OFFSET APPLICATO ===\n");
    std::printf ("%9s %9s %9s %9s %7s\n", "t_ini", "t_fin", "durata", "offset", "grado");
    for (const auto& r : runs)
    {
        char what[8];
        if (r.muted) std::snprintf (what, sizeof what, "MUTA");
        else if (r.offset.has_value()) std::snprintf (what, sizeof what, "%d", *r.offset);
        else std::snprintf (what, sizeof what, "-");
        std::printf ("%9.4f %9.4f %8.1fms %9s %7s\n",
                     r.startSec, r.endSec, 1000.0 * (r.endSec - r.startSec), what,
                     r.degreeAtStart >= 0 ? kDegreeNames[r.degreeAtStart] : "-");
    }

    // =====================================================================
    // RITARDO DI ADOZIONE (sessione 32, B-14) — quanto tempo passa fra
    // l'attacco di una nota nel DRY e il momento in cui il motore riceve
    // davvero l'offset che quella nota deve avere. E' il numero che
    // corrisponde alla "flem" riportata dall'utente: finche' non scade, la
    // nota nuova sta suonando con l'offset della precedente.
    //
    // L'attacco si prende dall'inviluppo del dry, non da OnsetDetector: su
    // materiale legato il gate non si richiude (misurato in s.28) e non
    // segnalerebbe nulla, mentre qui serve la verita' sul segnale, non cio'
    // che il plugin riesce a vedere.
    // =====================================================================
    const int envHop = std::max (1, (int) std::lround (0.001 * sr)); // 1 ms
    const auto env = envelopeRms (mono, envHop);
    double envMax = 0.0;
    for (double e : env)
        envMax = std::max (envMax, e);

    std::vector<double> attackSec;
    {
        const int lookBack = 20;                   // 20 ms indietro
        const double minGapSec = 0.30;             // due attacchi non piu' vicini di 300 ms
        const double floorLevel = 0.15 * envMax;   // sotto questo e' coda o silenzio
        for (int i = lookBack; i < (int) env.size(); ++i)
        {
            if (env[(size_t) i] < floorLevel)
                continue;
            if (env[(size_t) (i - lookBack)] > 0.5 * env[(size_t) i])
                continue; // non sta salendo abbastanza in fretta
            const double tSec = (double) (i - lookBack) * envHop / sr;
            if (! attackSec.empty() && tSec - attackSec.back() < minGapSec)
                continue;
            attackSec.push_back (tSec);
        }
    }

    auto appliedAt = [&runs] (double tSec) -> std::optional<int>
    {
        for (const auto& r : runs)
            if (tSec >= r.startSec && tSec < r.endSec)
                return r.muted ? std::nullopt : r.offset;
        return std::nullopt;
    };

    std::printf ("\n=== RITARDO DI ADOZIONE ===\n");
    std::printf ("%10s %10s %10s %10s %12s\n", "cambio", "attacco", "off_prec", "off_nuovo", "ritardo");

    // Si parte dai CAMBI D'OFFSET e si cerca l'attacco piu' vicino, non il
    // contrario: l'inviluppo di un patch con tremolo (come "Basic Silk Horns")
    // produce parecchi attacchi dentro la stessa nota, e partendo da quelli si
    // finisce per accoppiare un cambio all'attacco sbagliato — con blocchi
    // grandi, dove il cambio puo' cadere anche PRIMA dell'attacco vero, si
    // ottengono numeri privi di senso (misurato: 398 ms fasulli a 4096).
    // Il segno conta: negativo = il cambio e' caduto prima dell'attacco, cioe'
    // il blocco lo ha applicato retroattivamente a un pezzo della nota
    // precedente. E' la quantizzazione al blocco, non un anticipo reale.
    double worstDelayMs = 0.0, sumDelayMs = 0.0;
    int numChanges = 0;

    for (size_t i = 1; i < runs.size(); ++i)
    {
        const auto& prev = runs[i - 1];
        const auto& curr = runs[i];
        if (curr.muted || ! curr.offset.has_value())
            continue;
        if (prev.muted || ! prev.offset.has_value())
            continue; // ripartenza dal silenzio: non e' un cambio d'offset
        if (*prev.offset == *curr.offset)
            continue;

        double nearestAttack = -1.0, bestDistance = 1e9;
        for (double tA : attackSec)
        {
            const double distance = std::fabs (curr.startSec - tA);
            if (distance < bestDistance) { bestDistance = distance; nearestAttack = tA; }
        }
        if (nearestAttack < 0.0)
            continue;

        const double delayMs = 1000.0 * (curr.startSec - nearestAttack);
        worstDelayMs = std::max (worstDelayMs, std::fabs (delayMs));
        sumDelayMs += std::fabs (delayMs);
        ++numChanges;

        std::printf ("%10.4f %10.4f %10d %10d %10.1fms\n",
                     curr.startSec, nearestAttack, *prev.offset, *curr.offset, delayMs);
    }

    if (numChanges > 0)
        std::printf ("  cambi d'offset: %d — scarto MEDIO dall'attacco %.1f ms, PEGGIORE %.1f ms\n",
                     numChanges, sumDelayMs / numChanges, worstDelayMs);
    else
        std::printf ("  nessun cambio d'offset in questo file con questa tabella\n");

    // Riepilogo: una corsa "di passaggio" e' una corsa udibile (non muta) piu'
    // breve di kTransientMs, cioe' troppo breve per essere una nota della
    // melodia. E' la definizione operativa di "grado letto per sbaglio".
    constexpr double kTransientMs = 250.0;
    int transientRuns = 0;
    double transientMs = 0.0;
    for (const auto& r : runs)
    {
        const double ms = 1000.0 * (r.endSec - r.startSec);
        if (! r.muted && r.offset.has_value() && ms < kTransientMs)
        {
            ++transientRuns;
            transientMs += ms;
        }
    }

    std::printf ("\n=== RIEPILOGO ===\n");
    std::printf ("  corse totali: %zu\n", runs.size());
    std::printf ("  corse di passaggio (udibili, piu' brevi di %.0f ms): %d, per %.1f ms complessivi\n",
                 kTransientMs, transientRuns, transientMs);
    std::printf ("  block size: %d campioni (%.1f ms)\n", block, 1000.0 * block / sr);

    return 0;
}
