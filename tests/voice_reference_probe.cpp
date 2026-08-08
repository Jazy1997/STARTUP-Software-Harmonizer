// Strumento diagnostico OFFLINE — sessione 25/26, indagine sul ritorno del
// "wobbling timbrico" (formanti che "respirano" in una nota tenuta,
// segnalato dall'utente dopo aver isolato le singole voci del preset "Maj"
// su Test 1 - Basic Silk Horns.wav). Confronta direttamente:
//   - "Ideal Voice N.wav": la nota target suonata DAVVERO dal synth via MIDI
//     (nessuna elaborazione del plugin) — il riferimento "perfetto" di cosa
//     dovrebbe uscire dalla voce N;
//   - "Voice N.wav": l'export reale del plugin con solo la voce N udibile.
//
// A differenza di real_export_probe.cpp (che deve PREDIRE il pitch atteso
// applicando una tabella armonica al dry originale), qui il target e' gia'
// noto per costruzione — niente predizione, solo confronto diretto. Stessa
// filosofia di misura (periodicita' per finestra, CLAUDE.md regola 12): non
// presume il meccanismo, lo misura.
//
// NON in ctest: dipende da file WAV esterni forniti dall'utente
// (SAMPLE TEST/DBG Timbro/, fuori da git per scelta, vedi handsoff.md).
//
// NOTA METODOLOGICA (CLAUDE.md regola 13, da non dimenticare in una sessione
// futura): "Ideal Voice N.wav" NON e' un target bit-per-bit. E' il synth che
// suona quella nota, con il timbro che quel patch ha IN QUEL REGISTRO — un
// armonizzatore che scende di 8-10 semitoni non puo' e non deve produrre lo
// stesso spettro esatto (nessun motore di pitch-shift lo fa). L'Ideal serve
// come PAVIMENTO DI RIFERIMENTO per gli artefatti (periodicita', energia
// armonica: li' il confronto e' legittimo, sono proprieta' che un tono pulito
// del synth deve avere comunque) e come indicazione di DIREZIONE sulle
// formanti — non come errore assoluto da azzerare. Inseguire una differenza
// di colore fra Voice e Ideal che e' fisiologica (registro diverso) sarebbe
// un errore diverso da quello di sessione 18 ma della stessa famiglia:
// misurare la cosa sbagliata pensando di misurare quella giusta.
//
// Uso: voice_reference_probe <ideal.wav> <voice.wav> [label]

#include "SampleAnalysis.h"

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
        int startFrame, endFrame;
        double idealF0Hz;
    };

    std::vector<FrameStat> computeFrames (const std::vector<float>& x, double sr, int winLen, int hop)
    {
        std::vector<FrameStat> frames;
        for (int from = 0; from + winLen <= (int) x.size(); from += hop)
            frames.push_back (measureFrame (x, from, winLen, sr));
        return frames;
    }

    // Stessa logica di real_export_probe.cpp: una corsa massimale di frame
    // "tonali" (periodicita' alta, f0 valida) la cui f0 resta entro centsTol
    // dal primo frame della corsa — approssima "una nota tenuta".
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

    // Tabella degli offset (in semitoni) delle 4 voci del preset "Maj" (root
    // C) sulle 4 note del dry "Test 1 - Basic Silk Horns.wav" (C4/D4/E4/C4,
    // ~2s l'una — stessa struttura gia' usata da real_export_probe.cpp),
    // riportata testualmente dall'utente a fine sessione 25. Serve a
    // ricostruire la f0 ORIGINALE (non trasposta) per ogni istante: dryF0 =
    // idealF0 / 2^(offset/12) — la firma diretta della periodicita' che
    // l'artefatto descritto in PsolaShifter::emitGrain (commento ATTENZIONE)
    // lascerebbe filtrare se il grano fosse piu' lungo della spaziatura di
    // sintesi. Indice riga = voce-1, indice colonna = indice nota (0..3).
    constexpr float kVoiceOffsetsSemitones[4][4] = {
        {  0.0f, -2.0f,  0.0f,  0.0f }, // Voice 1
        { -1.0f, -3.0f, -4.0f, -1.0f }, // Voice 2
        { -5.0f, -7.0f, -5.0f, -5.0f }, // Voice 3
        { -8.0f,-10.0f, -9.0f, -8.0f }, // Voice 4
    };

    // Le 4 note durano ~2s l'una nel dry sorgente: stesso confine usato per
    // segmentare "Test 1" nelle sessioni precedenti (17-20).
    int noteIndexAtTime (double tSec) noexcept
    {
        const int idx = (int) (tSec / 2.0);
        return std::clamp (idx, 0, 3);
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("Uso: voice_reference_probe <ideal.wav> <voice.wav> [label]\n");
        return 1;
    }

    const std::string idealPath = argv[1];
    const std::string voicePath = argv[2];
    const std::string label = argc > 3 ? argv[3] : "?";

    WavFile idealWav, voiceWav;
    std::string error;
    if (! readWav (idealPath, idealWav, error)) { std::printf ("Errore leggendo IDEAL '%s': %s\n", idealPath.c_str(), error.c_str()); return 1; }
    if (! readWav (voicePath, voiceWav, error)) { std::printf ("Errore leggendo VOICE '%s': %s\n", voicePath.c_str(), error.c_str()); return 1; }

    if (idealWav.sampleRate != voiceWav.sampleRate)
        std::printf ("ATTENZIONE: sample rate diversi (ideal=%.0f, voice=%.0f) — i risultati sotto non sono affidabili\n",
                     idealWav.sampleRate, voiceWav.sampleRate);

    const auto ideal = downmix (idealWav);
    const auto voice = downmix (voiceWav);
    const double sr = idealWav.sampleRate;

    std::printf ("=== VOICE %s ===\n", label.c_str());
    std::printf ("IDEAL: %s (%.2fs)\n", idealPath.c_str(), ideal.size() / sr);
    std::printf ("VOICE: %s (%.2fs)\n\n", voicePath.c_str(), voice.size() / sr);

    // =========================================================================
    // 1. ALLINEAMENTO (inviluppo RMS — vedi SampleAnalysis.h per il perche')
    // =========================================================================
    std::printf ("=== 1. ALLINEAMENTO ===\n");
    const int hopAlign = 64;
    const int maxLagSearch = (int) (0.15 * sr);
    const auto massLag = bestLagByEnvelope (ideal, voice, hopAlign, maxLagSearch);
    std::printf ("  Lag di massa (inviluppo RMS, hop=%d camp.): %ld campioni = %.1f ms (correlazione %.3f)\n",
                 hopAlign, massLag.samples, 1000.0 * massLag.samples / sr, massLag.correlation);
    const long lag = massLag.samples;

    // =========================================================================
    // 2. TRAIETTORIA PER PLATEAU E ACCURATEZZA DI INTONAZIONE
    // =========================================================================
    std::printf ("\n=== 2. TRAIETTORIA PER PLATEAU E ACCURATEZZA DI INTONAZIONE ===\n");
    const int winLen = (int) (0.030 * sr);
    const int hop    = (int) (0.010 * sr);
    const auto idealFrames = computeFrames (ideal, sr, winLen, hop);
    const auto plateaus = segmentPlateaus (idealFrames, 25.0, 0.90, (int) (0.15 * sr) / hop);

    std::printf ("%8s %8s %10s %10s %12s\n", "t_ini", "t_fin", "f0ideal", "f0voice", "err_c");

    for (const auto& p : plateaus)
    {
        const int span = p.endFrame - p.startFrame + 1;
        const int trim = std::max (1, span / 5);
        const int coreStart = p.startFrame + trim;
        const int coreEnd   = p.endFrame - trim;
        if (coreEnd < coreStart) continue;

        double sumF0Ideal = 0.0; int nIdeal = 0;
        for (int k = coreStart; k <= coreEnd; ++k)
            if (idealFrames[(size_t) k].f0Hz > 0.0) { sumF0Ideal += idealFrames[(size_t) k].f0Hz; ++nIdeal; }
        if (nIdeal == 0) continue;
        const double f0Ideal = sumF0Ideal / nIdeal;

        double sumF0Voice = 0.0; int nVoice = 0;
        for (int k = coreStart; k <= coreEnd; ++k)
        {
            const long voiceFrom = (long) k * hop + lag;
            if (voiceFrom < 0 || voiceFrom + winLen > (long) voice.size()) continue;
            const auto fs = measureFrame (voice, (int) voiceFrom, winLen, sr);
            if (fs.f0Hz > 0.0) { sumF0Voice += fs.f0Hz; ++nVoice; }
        }
        if (nVoice == 0) continue;
        const double f0Voice = sumF0Voice / nVoice;

        std::printf ("%8.2f %8.2f %10.1f %10.1f %12.1f\n",
                     (double) (p.startFrame * hop) / sr, (double) (p.endFrame * hop + winLen) / sr,
                     f0Ideal, f0Voice, centsError (f0Voice, f0Ideal));
    }

    // =========================================================================
    // 3. INSTABILITA' TIMBRICA — periodicita' VOICE quando IDEAL e' stabile
    // =========================================================================
    std::printf ("\n=== 3. INSTABILITA' TIMBRICA (periodicita' VOICE quando IDEAL e' stabile) ===\n");
    struct InstabilityEvent { double startSec, endSec, minPeriodicityVoice; };
    std::vector<InstabilityEvent> instab;
    bool inEvent = false; double evStart = 0.0, evMin = 1.0;
    int framesConsidered = 0, framesUnstable = 0;

    for (size_t k = 0; k < idealFrames.size(); ++k)
    {
        const auto& idf = idealFrames[k];
        const double tSec = (double) (k * (size_t) hop) / sr;

        if (idf.periodicity < 0.95 || idf.f0Hz <= 0.0)
        {
            if (inEvent) { instab.push_back ({ evStart, tSec, evMin }); inEvent = false; }
            continue;
        }

        const long voiceFrom = (long) (k * (size_t) hop) + lag;
        if (voiceFrom < 0 || voiceFrom + winLen > (long) voice.size()) continue;
        const auto vf = measureFrame (voice, (int) voiceFrom, winLen, sr);
        ++framesConsidered;

        const bool unstableHere = vf.periodicity < 0.90;
        if (unstableHere) ++framesUnstable;

        if (unstableHere && ! inEvent) { inEvent = true; evStart = tSec; evMin = vf.periodicity; }
        else if (unstableHere) { evMin = std::min (evMin, vf.periodicity); }
        else if (inEvent) { instab.push_back ({ evStart, tSec, evMin }); inEvent = false; }
    }
    if (inEvent) instab.push_back ({ evStart, (double) idealFrames.size() * hop / sr, evMin });

    std::printf ("  finestre tonali confrontabili: %d, instabili (periodicita' voice<0.90): %d (%.1f%%)\n",
                 framesConsidered, framesUnstable,
                 framesConsidered > 0 ? 100.0 * framesUnstable / framesConsidered : 0.0);
    for (const auto& e : instab)
        std::printf ("    t=%.2f-%.2fs  durata=%.0fms  periodicita' minima voice=%.3f\n",
                     e.startSec, e.endSec, 1000.0 * (e.endSec - e.startSec), e.minPeriodicityVoice);
    if (instab.empty()) std::printf ("    nessuno\n");

    // =========================================================================
    // 3b. CADENZA DEI DISTURBI (stessa logica di real_export_probe.cpp §4b —
    // sessione 19/20: capire se le micro-instabilita' ricorrono a intervallo
    // regolare, firma di un aggiornamento "una volta per blocco host").
    // =========================================================================
    std::printf ("\n=== 3b. CADENZA DEI DISTURBI (intervalli fra un calo di periodicita' e il successivo) ===\n");
    std::vector<long> glitchOnsetSamples;
    bool inGlitch = false;
    for (size_t k = 0; k < idealFrames.size(); ++k)
    {
        const auto& idf = idealFrames[k];
        if (idf.periodicity < 0.98 || idf.f0Hz <= 0.0) { inGlitch = false; continue; }

        const long voiceFrom = (long) (k * (size_t) hop) + lag;
        if (voiceFrom < 0 || voiceFrom + winLen > (long) voice.size()) continue;
        const auto vf = measureFrame (voice, (int) voiceFrom, winLen, sr);

        const bool unstableHere = vf.periodicity < 0.98;
        if (unstableHere && ! inGlitch)
            glitchOnsetSamples.push_back ((long) (k * (size_t) hop));
        inGlitch = unstableHere;
    }

    if (glitchOnsetSamples.size() < 3)
    {
        std::printf ("  troppo pochi eventi (%zu) per stimare una cadenza\n", glitchOnsetSamples.size());
    }
    else
    {
        std::vector<long> gaps;
        for (size_t i = 1; i < glitchOnsetSamples.size(); ++i)
            gaps.push_back (glitchOnsetSamples[i] - glitchOnsetSamples[i - 1]);

        std::vector<long> sortedGaps = gaps;
        std::sort (sortedGaps.begin(), sortedGaps.end());
        const long median = sortedGaps[sortedGaps.size() / 2];

        std::printf ("  %zu eventi, %zu intervalli — mediana=%ld campioni (%.1fms), min=%ld (%.1fms), max=%ld (%.1fms)\n",
                     glitchOnsetSamples.size(), gaps.size(), median, 1000.0 * median / sr,
                     sortedGaps.front(), 1000.0 * sortedGaps.front() / sr,
                     sortedGaps.back(), 1000.0 * sortedGaps.back() / sr);

        static constexpr int kCommonBlockSizes[] = { 64, 128, 256, 512, 1024, 2048, 4096 };
        std::printf ("  compatibilita' con block size comuni (intervalli entro +-15%% di un multiplo):\n");
        for (int blockSize : kCommonBlockSizes)
        {
            int matches = 0;
            for (long g : gaps)
            {
                if (g <= 0) continue;
                const double multiple = std::round ((double) g / blockSize);
                if (multiple < 1.0) continue;
                const double expected = multiple * blockSize;
                if (std::fabs ((double) g - expected) / expected < 0.15)
                    ++matches;
            }
            std::printf ("    %5d campioni (%5.1fms): %d/%zu intervalli compatibili\n",
                         blockSize, 1000.0 * blockSize / sr, matches, gaps.size());
        }
    }

    // =========================================================================
    // 4. SCANSIONE CLICK — rete di sicurezza
    // =========================================================================
    std::printf ("\n=== 4. SCANSIONE CLICK (rete di sicurezza) ===\n");
    const auto idealClicks = findClicks (ideal, sr);
    const auto voiceClicks = findClicks (voice, sr);
    printReport ("IDEAL", idealClicks);
    printReport ("VOICE", voiceClicks);
    printWetOnly (voiceClicks, idealClicks);

    // =========================================================================
    // 5. TIMBRO PER PLATEAU — energia armonica (HNR), residuo al pitch
    //    ORIGINALE non trasposto, formante. Vedi la NOTA METODOLOGICA in
    //    testa al file: HNR_ideal e' il pavimento del synth in quel
    //    registro, NON zero — il confronto legittimo e' HNR_voice contro
    //    HNR_ideal (quanto si allontana dal proprio pavimento), non
    //    HNR_voice contro 1.0. res_dryF0 invece DEVE restare basso a
    //    prescindere dal registro: e' energia alle armoniche di una nota che
    //    la voce non dovrebbe piu' contenere.
    // =========================================================================
    std::printf ("\n=== 5. TIMBRO PER PLATEAU (HNR, residuo al pitch originale, formante) ===\n");

    int voiceIndex = -1;
    try { voiceIndex = std::stoi (label) - 1; } catch (...) {}
    if (voiceIndex < 0 || voiceIndex > 3)
        std::printf ("  ATTENZIONE: label '%s' non e' un indice voce 1..4 valido — res_dryF0 sara' -1 "
                     "(serve l'indice per leggere la tabella di offset e ricostruire la f0 originale)\n",
                     label.c_str());

    std::printf ("%8s %8s %10s %10s %10s %10s %10s %10s\n",
                 "t_ini", "t_fin", "HNR_idl", "HNR_voc", "res_dry0", "fmt_idl", "fmt_voc", "d_fmt");

    for (const auto& p : plateaus)
    {
        const int span = p.endFrame - p.startFrame + 1;
        const int trim = std::max (1, span / 5);
        const int coreStart = p.startFrame + trim;
        const int coreEnd   = p.endFrame - trim;
        if (coreEnd < coreStart) continue;

        // Finestra di misura: il centro del nucleo del plateau — un singolo
        // punto rappresentativo, non una media (le misure spettrali qui
        // ripetono una scansione di banda intera per finestra, molto piu'
        // costose delle autocorrelazioni di sezione 2).
        const int midFrame = (coreStart + coreEnd) / 2;
        const int idealFrom = midFrame * hop;
        const long voiceFrom = (long) midFrame * hop + lag;
        if (idealFrom < 0 || idealFrom + winLen > (int) ideal.size()) continue;
        if (voiceFrom < 0 || voiceFrom + winLen > (long) voice.size()) continue;

        const double tSec = (double) (p.startFrame * hop) / sr;
        const double hnrIdeal = harmonicEnergyRatio (ideal, idealFrom, winLen, sr, p.idealF0Hz);
        const double hnrVoice = harmonicEnergyRatio (voice, (int) voiceFrom, winLen, sr, p.idealF0Hz);

        double resDryF0 = -1.0;
        if (voiceIndex >= 0 && voiceIndex <= 3)
        {
            const int noteIdx = noteIndexAtTime (tSec);
            const double offsetSemitones = (double) kVoiceOffsetsSemitones[voiceIndex][noteIdx];
            const double dryF0Hz = p.idealF0Hz / std::exp2 (offsetSemitones / 12.0);
            resDryF0 = harmonicEnergyRatio (voice, (int) voiceFrom, winLen, sr, dryF0Hz);
        }

        const double fmtIdeal = formantPeak (ideal, idealFrom, winLen, sr);
        const double fmtVoice = formantPeak (voice, (int) voiceFrom, winLen, sr);

        std::printf ("%8.2f %8.2f %10.3f %10.3f %10.3f %10.0f %10.0f %10.0f\n",
                     (double) (p.startFrame * hop) / sr, (double) (p.endFrame * hop + winLen) / sr,
                     hnrIdeal, hnrVoice, resDryF0, fmtIdeal, fmtVoice, fmtVoice - fmtIdeal);
    }

    std::printf ("\n");
    return 0;
}
