// Strumento diagnostico OFFLINE: confronta DRY reale e WET reale (un export
// vero dal plugin, non un'approssimazione offline) — sessione 18, "una prima
// voce perfetta". Vedi handsoff.md per il contesto completo: l'utente ha
// isolato empiricamente in Ableton che il difetto (click + instabilita'
// timbrica) c'e' gia' con Voices=1, e ha fornito
// "SAMPLE TEST/Export V1.wav" (Voices=1, Dry=0, Wet=1) accanto al dry
// sorgente "SAMPLE TEST/Test 1 - Basic Silk Horns.wav".
//
// CLAUDE.md regola 12/13: questo file non presume il meccanismo (ipotesi:
// PsolaShifter::currentPeriod() arrotona il periodo a un intero, vedi
// PsolaShifter.cpp righe 108-113) — lo MISURA sul file reale, PRIMA di
// toccare src/. Se i numeri qui non confermano l'ipotesi, l'ipotesi va
// abbandonata o rivista, non forzata.
//
// NON in ctest: dipende da file WAV esterni forniti dall'utente, non
// versionati nel repo.
//
// Uso: real_export_probe <dry.wav> <wet.wav> [stability=2] [rootPitchClass=0]
//
// Compilazione (vedi anche il target CMake `real_export_probe`):
//   g++ -O2 -std=c++20 -Isrc tests/real_export_probe.cpp src/dsp/PsolaShifter.cpp
//       -o real_export_probe

#include "SampleAnalysis.h"
#include "dsp/PsolaShifter.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace
{
    struct Plateau
    {
        int startFrame, endFrame; // indici nella griglia di frame del DRY
        double dryF0Hz;
    };

    std::vector<FrameStat> computeFrames (const std::vector<float>& x, double sr, int winLen, int hop)
    {
        std::vector<FrameStat> frames;
        for (int from = 0; from + winLen <= (int) x.size(); from += hop)
            frames.push_back (measureFrame (x, from, winLen, sr));
        return frames;
    }

    // Un plateau e' una corsa massimale di frame "tonali" (periodicita' alta,
    // f0 valida) la cui f0 resta entro centsTol dal primo frame della corsa —
    // approssima "una nota tenuta", senza pretendere di sapere dove comincia
    // e finisce esattamente (i bordi si tagliano dopo, vedi coreStart/coreEnd
    // nel chiamante).
    std::vector<Plateau> segmentPlateaus (const std::vector<FrameStat>& frames,
                                          double centsTol, double minPeriodicity, int minFrames)
    {
        std::vector<Plateau> result;
        const int n = (int) frames.size();
        int i = 0;
        while (i < n)
        {
            if (frames[i].periodicity < minPeriodicity || frames[i].f0Hz <= 0.0) { ++i; continue; }

            int j = i;
            const double refF0 = frames[i].f0Hz;
            while (j + 1 < n && frames[j + 1].periodicity >= minPeriodicity && frames[j + 1].f0Hz > 0.0
                   && std::fabs (centsError (frames[j + 1].f0Hz, refF0)) < centsTol)
                ++j;

            if (j - i + 1 >= minFrames)
                result.push_back ({ i, j, refF0 });

            i = j + 1;
        }
        return result;
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("Uso: real_export_probe <dry.wav> <wet.wav> [stability=2] [rootPitchClass=0] [traceStartSec traceEndSec]\n");
        return 1;
    }

    const std::string dryPath = argv[1];
    const std::string wetPath = argv[2];
    const int stabilityLevel = argc > 3 ? std::stoi (argv[3]) : Stability::defaultLevel;
    const int rootPitchClass = argc > 4 ? std::stoi (argv[4]) : 0;
    const bool doTrace = argc > 6;
    const double traceStartSec = doTrace ? std::stod (argv[5]) : 0.0;
    const double traceEndSec   = doTrace ? std::stod (argv[6]) : 0.0;

    WavFile dryWav, wetWav;
    std::string error;
    if (! readWav (dryPath, dryWav, error)) { std::printf ("Errore leggendo il DRY '%s': %s\n", dryPath.c_str(), error.c_str()); return 1; }
    if (! readWav (wetPath, wetWav, error)) { std::printf ("Errore leggendo il WET '%s': %s\n", wetPath.c_str(), error.c_str()); return 1; }

    if (dryWav.sampleRate != wetWav.sampleRate)
        std::printf ("ATTENZIONE: sample rate diversi (dry=%.0f, wet=%.0f) — i risultati sotto non sono affidabili\n",
                     dryWav.sampleRate, wetWav.sampleRate);

    const auto dry = downmix (dryWav);
    const auto wet = downmix (wetWav);
    const double sr = dryWav.sampleRate;

    std::printf ("DRY: %s (%.2fs, %zu campioni)\n", dryPath.c_str(), dry.size() / sr, dry.size());
    std::printf ("WET: %s (%.2fs, %zu campioni)\n", wetPath.c_str(), wet.size() / sr, wet.size());
    std::printf ("Parametri: Stability=%s, rootPitchClass=%d, preset=Maj (V1: %.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f/%.0f)\n\n",
                 Stability::names[std::clamp (stabilityLevel, 0, Stability::numLevels - 1)], rootPitchClass,
                 kMajV1Table[0], kMajV1Table[1], kMajV1Table[2], kMajV1Table[3], kMajV1Table[4], kMajV1Table[5],
                 kMajV1Table[6], kMajV1Table[7], kMajV1Table[8], kMajV1Table[9], kMajV1Table[10], kMajV1Table[11]);

    // =========================================================================
    // 1. ALLINEAMENTO — latenza attesa dal codice, poi lag di massa
    //    sull'inviluppo (non sulla forma d'onda: su un segnale periodico la
    //    correlazione di forma d'onda e' ambigua a meno di multipli del
    //    periodo, vedi SampleAnalysis.h).
    // =========================================================================
    std::printf ("=== 1. ALLINEAMENTO ===\n");
    PsolaShifter latencyProbe;
    latencyProbe.prepare (sr, 64, stabilityLevel);
    const int expectedLatency = latencyProbe.getLatencySamples();
    std::printf ("  Latenza dichiarata dal motore: %d campioni = %.1f ms\n",
                 expectedLatency, 1000.0 * expectedLatency / sr);

    const int hopAlign = 64;
    const int maxLagSearch = (int) (0.15 * sr); // 150ms, generoso
    const auto massLag = bestLagByEnvelope (dry, wet, hopAlign, maxLagSearch);
    std::printf ("  Lag di massa (inviluppo RMS, hop=%d camp.): %ld campioni = %.1f ms (correlazione %.3f)\n",
                 hopAlign, massLag.samples, 1000.0 * massLag.samples / sr, massLag.correlation);
    if (std::labs (massLag.samples) < 2 * hopAlign)
        std::printf ("  -> compatibile con PDC gia' compensata dall'host (lag ~0)\n");
    else
        std::printf ("  -> lag non nullo: le sezioni successive useranno questo lag per confrontare dry e wet allo stesso istante\n");

    const long lag = massLag.samples;

    // =========================================================================
    // 2/3. TRAIETTORIA PER PLATEAU + ACCURATEZZA DI INTONAZIONE
    // =========================================================================
    std::printf ("\n=== 2/3. TRAIETTORIA PER PLATEAU E ACCURATEZZA DI INTONAZIONE ===\n");
    const int winLen = (int) (0.030 * sr); // 30ms
    const int hop    = (int) (0.010 * sr); // 10ms
    const auto dryFrames = computeFrames (dry, sr, winLen, hop);
    const auto plateaus = segmentPlateaus (dryFrames, 25.0, 0.90, (int) (0.15 * sr) / hop);

    std::printf ("%8s %8s %10s %6s %6s %6s %10s %12s %12s\n",
                 "t_ini", "t_fin", "f0dry", "nota", "grado", "V1att", "f0wet", "err_ideale_c", "err_modP_c");

    for (const auto& p : plateaus)
    {
        const int span = p.endFrame - p.startFrame + 1;
        const int trim = std::max (1, span / 5); // taglia il 20% ai bordi (transizioni)
        const int coreStart = p.startFrame + trim;
        const int coreEnd   = p.endFrame - trim;
        if (coreEnd < coreStart) continue;

        double sumF0Dry = 0.0; int nDry = 0;
        for (int k = coreStart; k <= coreEnd; ++k)
            if (dryFrames[(size_t) k].f0Hz > 0.0) { sumF0Dry += dryFrames[(size_t) k].f0Hz; ++nDry; }
        if (nDry == 0) continue;
        const double f0Dry = sumF0Dry / nDry;

        double sumF0Wet = 0.0; int nWet = 0;
        for (int k = coreStart; k <= coreEnd; ++k)
        {
            const long wetFrom = (long) k * hop + lag;
            if (wetFrom < 0 || wetFrom + winLen > (long) wet.size()) continue;
            const auto fs = measureFrame (wet, (int) wetFrom, winLen, sr);
            if (fs.f0Hz > 0.0) { sumF0Wet += fs.f0Hz; ++nWet; }
        }
        if (nWet == 0) continue;
        const double f0Wet = sumF0Wet / nWet;

        const int quantizedNote = (int) std::lround (69.0 + 12.0 * std::log2 (f0Dry / 440.0));
        const int degree = degreeOf (quantizedNote, rootPitchClass);
        const double v1Expected = kMajV1Table[degree];
        const double alpha = std::pow (2.0, v1Expected / 12.0);

        const double expectedIdeal = alpha * f0Dry;
        const int    pRounded      = (int) std::lround (sr / f0Dry);
        const double expectedModelP = alpha * sr / (double) pRounded;

        const double errIdealCent = centsError (f0Wet, expectedIdeal);
        const double errModelPCent = centsError (f0Wet, expectedModelP);

        std::printf ("%8.2f %8.2f %10.1f %6d %6d %6.1f %10.1f %12.1f %12.1f\n",
                     (double) (p.startFrame * hop) / sr, (double) (p.endFrame * hop + winLen) / sr,
                     f0Dry, quantizedNote, degree, v1Expected, f0Wet, errIdealCent, errModelPCent);
    }
    std::printf ("  (err_ideale_c = scostamento in cent da alpha*f0dry; err_modP_c = scostamento da\n"
                 "   alpha*sr/round(sr/f0dry) — se err_modP_c e' sistematicamente piu' vicino a 0\n"
                 "   di err_ideale_c, il meccanismo di quantizzazione del periodo e' confermato)\n");

    // =========================================================================
    // 4. INSTABILITA' TIMBRICA — periodicita' WET vs DRY sulla stessa griglia
    // =========================================================================
    std::printf ("\n=== 4. INSTABILITA' TIMBRICA (periodicita' WET quando il DRY e' stabile) ===\n");
    struct InstabilityEvent { double startSec, endSec, minPeriodicityWet; };
    std::vector<InstabilityEvent> instab;
    bool inEvent = false; double evStart = 0.0, evMin = 1.0;
    int framesConsidered = 0, framesUnstable = 0;

    for (size_t k = 0; k < dryFrames.size(); ++k)
    {
        const auto& df = dryFrames[k];
        const double tSec = (double) (k * (size_t) hop) / sr;

        if (df.periodicity < 0.95 || df.f0Hz <= 0.0)
        {
            if (inEvent) { instab.push_back ({ evStart, tSec, evMin }); inEvent = false; }
            continue;
        }

        const long wetFrom = (long) (k * (size_t) hop) + lag;
        if (wetFrom < 0 || wetFrom + winLen > (long) wet.size()) continue;
        const auto wf = measureFrame (wet, (int) wetFrom, winLen, sr);
        ++framesConsidered;

        const bool unstableHere = wf.periodicity < 0.90; // il dry qui e' gia' garantito > 0.95
        if (unstableHere) ++framesUnstable;

        if (unstableHere && ! inEvent) { inEvent = true; evStart = tSec; evMin = wf.periodicity; }
        else if (unstableHere) { evMin = std::min (evMin, wf.periodicity); }
        else if (inEvent) { instab.push_back ({ evStart, tSec, evMin }); inEvent = false; }
    }
    if (inEvent) instab.push_back ({ evStart, (double) dryFrames.size() * hop / sr, evMin });

    std::printf ("  finestre tonali confrontabili: %d, instabili (periodicita' wet<0.90): %d (%.1f%%)\n",
                 framesConsidered, framesUnstable,
                 framesConsidered > 0 ? 100.0 * framesUnstable / framesConsidered : 0.0);
    for (const auto& e : instab)
        std::printf ("    t=%.2f-%.2fs  durata=%.0fms  periodicita' minima wet=%.3f\n",
                     e.startSec, e.endSec, 1000.0 * (e.endSec - e.startSec), e.minPeriodicityWet);
    if (instab.empty()) std::printf ("    nessuno\n");

    // =========================================================================
    // 5. TRANSIZIONI E BUCHI
    // =========================================================================
    std::printf ("\n=== 5. TRANSIZIONI E BUCHI ===\n");

    const int holeWin = (int) (0.05 * sr); // 50ms
    std::printf ("  Buchi (wet quasi a zero mentre il dry suona):\n");
    int holesFound = 0;
    for (int s = 0; s + holeWin <= (int) dry.size(); s += holeWin)
    {
        const double rDry = rms (dry, s, holeWin);
        const long wetFrom = s + lag;
        if (wetFrom < 0 || wetFrom + holeWin > (long) wet.size()) continue;
        const double rWet = rms (wet, (int) wetFrom, holeWin);
        if (rDry > 0.01 && rWet < 0.1 * rDry)
        {
            std::printf ("    t=%.2fs  rmsDry=%.4f  rmsWet=%.4f\n", s / sr, rDry, rWet);
            ++holesFound;
        }
    }
    if (holesFound == 0) std::printf ("    nessuno\n");

    std::printf ("\n  Tempo di recupero ad ogni cambio nota (fino a |errore|<10 cent E periodicita'>0.95):\n");
    for (size_t pi = 1; pi < plateaus.size(); ++pi)
    {
        const auto& cur = plateaus[pi];
        const double changeTimeSec = (double) (cur.startFrame * hop) / sr;
        const double f0Dry = cur.dryF0Hz;

        const int quantizedNote = (int) std::lround (69.0 + 12.0 * std::log2 (f0Dry / 440.0));
        const int degree = degreeOf (quantizedNote, rootPitchClass);
        const double alpha = std::pow (2.0, kMajV1Table[degree] / 12.0);
        const double expected = alpha * f0Dry;

        double recoverySec = -1.0;
        for (int k = cur.startFrame; k <= cur.endFrame; ++k)
        {
            const long wetFrom = (long) k * hop + lag;
            if (wetFrom < 0 || wetFrom + winLen > (long) wet.size()) continue;
            const auto wf = measureFrame (wet, (int) wetFrom, winLen, sr);
            if (wf.f0Hz <= 0.0) continue;
            if (std::fabs (centsError (wf.f0Hz, expected)) < 10.0 && wf.periodicity > 0.95)
            { recoverySec = (double) (k * hop) / sr - changeTimeSec; break; }
        }

        if (recoverySec >= 0.0)
            std::printf ("    cambio a t=%.2fs -> stabile dopo %.0fms\n", changeTimeSec, 1000.0 * recoverySec);
        else
            std::printf ("    cambio a t=%.2fs -> non tornato entro soglia in questo plateau\n", changeTimeSec);
    }

    {
        const int checkWin = (int) (0.01 * sr);
        int dryStart = -1, wetStart = -1;
        for (int s = 0; s + checkWin <= (int) dry.size(); s += checkWin)
            if (rms (dry, s, checkWin) > 0.01) { dryStart = s; break; }
        for (int s = 0; s + checkWin <= (int) wet.size(); s += checkWin)
            if (rms (wet, s, checkWin) > 0.01) { wetStart = s; break; }
        if (dryStart >= 0 && wetStart >= 0)
            std::printf ("\n  Primo attacco: dry a %.3fs, wet a %.3fs (ritardo %.0fms)\n",
                         dryStart / sr, wetStart / sr, 1000.0 * (wetStart - dryStart) / sr);
    }

    // =========================================================================
    // 6. SCANSIONE CLICK — rete di sicurezza, non criterio principale (vedi
    //    il commento sopra findClicks in SampleAnalysis.h)
    // =========================================================================
    std::printf ("\n=== 6. SCANSIONE CLICK (findClicks — rete di sicurezza, vedi SampleAnalysis.h) ===\n");
    const auto dryClicks = findClicks (dry, sr);
    const auto wetClicks = findClicks (wet, sr);
    printReport ("DRY", dryClicks);
    printReport ("WET", wetClicks);
    printWetOnly (wetClicks, dryClicks);

    // =========================================================================
    // TRACCIA FINE (opzionale) — sessione 18: prima di ipotizzare un
    // meccanismo per un evento trovato in sezione 4/5, guardare cosa fa
    // DAVVERO il segnale dry in quell'istante (una vera pausa fra due note?
    // un calo naturale di livello? niente di particolare?) invece di
    // presumerlo. Stampa dry e wet fianco a fianco sulla stessa griglia di
    // frame gia' calcolata sopra (nessun ricalcolo).
    // =========================================================================
    if (doTrace)
    {
        std::printf ("\n=== TRACCIA FINE %.3f-%.3fs ===\n", traceStartSec, traceEndSec);
        std::printf ("%8s %10s %8s %8s | %10s %8s %8s\n",
                     "t", "f0dry", "perDry", "rmsDry", "f0wet", "perWet", "rmsWet");
        const int fFrom = std::max (0, (int) (traceStartSec * sr / hop) - 2);
        const int fTo   = std::min ((int) dryFrames.size() - 1, (int) (traceEndSec * sr / hop) + 2);
        for (int k = fFrom; k <= fTo; ++k)
        {
            const auto& df = dryFrames[(size_t) k];
            const long wetFrom = (long) k * hop + lag;
            FrameStat wf;
            if (wetFrom >= 0 && wetFrom + winLen <= (long) wet.size())
                wf = measureFrame (wet, (int) wetFrom, winLen, sr);

            std::printf ("%8.3f %10.1f %8.3f %8.4f | %10.1f %8.3f %8.4f\n",
                         (double) (k * hop) / sr, df.f0Hz, df.periodicity, df.rmsValue,
                         wf.f0Hz, wf.periodicity, wf.rmsValue);
        }
    }

    return 0;
}
