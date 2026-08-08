#pragma once

// Strumenti di analisi condivisi per la diagnosi su audio VERO (sessione 17-18
// — "una prima voce perfetta", vedi handsoff.md). Header-only, nessuna
// dipendenza JUCE, stesso principio di TestSignals.h (che include, per
// riusare rms/maxJump/centsError/allFinite invece di duplicarli — sessione
// 16 ha gia' pagato il prezzo di NON farlo con psola_test/voice_test).
//
// Perche' un header separato da TestSignals.h e non un'estensione di esso:
// TestSignals.h genera segnali SINTETICI e li misura con un sample rate
// costante passato esplicitamente (SR del chiamante). Qui invece si legge
// audio VERO da file (formato/durata sconosciuti a priori) e si confrontano
// DUE segnali fra loro (dry vs wet) — un problema diverso (lettura WAV,
// allineamento temporale, confronto), non solo un'altra misura sullo stesso
// segnale.
//
// CLAUDE.md regola 12/13: ogni funzione qui e' un tentativo di tradurre in
// numero una domanda percettiva ("questo tratto suona instabile?", "c'e' un
// buco qui?") — nessuna è garantita infallibile alla prima stesura. findClicks
// in particolare si e' gia' dimostrata CIECA al difetto reale su un file
// dell'utente (sessione 18): vedi il commento sopra la sua definizione.

#include "TestSignals.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Lettura WAV
// ---------------------------------------------------------------------------
struct WavFile
{
    double sampleRate = 0.0;
    int numChannels = 0;
    int bitsPerSample = 0;
    std::vector<float> interleaved; // normalizzato in [-1, 1]
};

// Lettore minimale, deliberatamente SENZA dipendenza JUCE (stesso principio
// degli altri strumenti in tests/): cerca i chunk "fmt " e "data" per
// posizione, non per offset fisso — robusto a chunk extra come "JUNK"
// (presente nei file dell'utente, verificato con xxd prima di scrivere questo
// lettore). Supporta solo PCM 16-bit: e' il formato di tutti i file forniti
// finora; rifiuta esplicitamente qualunque altro formato invece di leggerlo
// in modo sbagliato in silenzio (stesso principio di CellInputParser —
// CLAUDE.md regola 3).
inline bool readWav (const std::string& path, WavFile& out, std::string& error)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { error = "impossibile aprire il file"; return false; }

    std::vector<uint8_t> bytes;
    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    bytes.resize ((size_t) size);
    const size_t readCount = std::fread (bytes.data(), 1, (size_t) size, f);
    std::fclose (f);
    if (readCount != (size_t) size) { error = "lettura incompleta"; return false; }

    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0
                          || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
    { error = "non e' un file RIFF/WAVE"; return false; }

    auto readU32 = [&] (size_t off) { return (uint32_t) bytes[off] | ((uint32_t) bytes[off+1] << 8)
                                            | ((uint32_t) bytes[off+2] << 16) | ((uint32_t) bytes[off+3] << 24); };
    auto readU16 = [&] (size_t off) { return (uint16_t) bytes[off] | ((uint16_t) bytes[off+1] << 8); };

    bool haveFmt = false, haveData = false;
    uint16_t audioFormat = 0;
    size_t dataOffset = 0, dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        char id[5] = {0};
        std::memcpy (id, bytes.data() + pos, 4);
        const uint32_t chunkSize = readU32 (pos + 4);
        const size_t chunkDataStart = pos + 8;

        if (std::strcmp (id, "fmt ") == 0 && chunkDataStart + 16 <= bytes.size())
        {
            audioFormat        = readU16 (chunkDataStart + 0);
            out.numChannels    = readU16 (chunkDataStart + 2);
            out.sampleRate     = (double) readU32 (chunkDataStart + 4);
            out.bitsPerSample  = readU16 (chunkDataStart + 14);
            haveFmt = true;
        }
        else if (std::strcmp (id, "data") == 0)
        {
            dataOffset = chunkDataStart;
            dataSize = chunkSize;
            haveData = true;
        }

        pos = chunkDataStart + chunkSize + (chunkSize % 2); // i chunk sono allineati a 2 byte
    }

    if (! haveFmt || ! haveData) { error = "chunk fmt/data non trovati"; return false; }
    if (audioFormat != 1 || out.bitsPerSample != 16)
    { error = "supportato solo PCM 16-bit (questo file e' formato=" + std::to_string (audioFormat)
              + ", " + std::to_string (out.bitsPerSample) + "-bit)"; return false; }
    if (dataOffset + dataSize > bytes.size()) { error = "chunk data troncato"; return false; }

    const size_t numSamples = dataSize / 2;
    out.interleaved.resize (numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        const int16_t s = (int16_t) (bytes[dataOffset + i * 2] | (bytes[dataOffset + i * 2 + 1] << 8));
        out.interleaved[i] = (float) s / 32768.0f;
    }
    return true;
}

// Scrittore minimale (PCM 16-bit mono), simmetrico a readWav: serve a
// salvare l'uscita di una riproduzione offline (es. una passata di
// sample_click_finder.cpp) su disco, cosi' la si puo' rianalizzare con lo
// stesso strumento rigoroso usato sull'export reale (real_export_probe),
// invece di duplicare la strumentazione di misura in due posti diversi.
inline bool writeWavMono (const std::string& path, const std::vector<float>& mono, double sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;

    const uint32_t dataSize = (uint32_t) (mono.size() * 2);
    const uint32_t sr = (uint32_t) sampleRate;
    const uint16_t bitsPerSample = 16, numChannels = 1, audioFormat = 1;
    const uint32_t byteRate = sr * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign = (uint16_t) (numChannels * bitsPerSample / 8);
    const uint32_t riffSize = 36 + dataSize;

    std::fwrite ("RIFF", 1, 4, f);
    std::fwrite (&riffSize, 4, 1, f);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);
    { const uint32_t fmtSize = 16; std::fwrite (&fmtSize, 4, 1, f); }
    std::fwrite (&audioFormat, 2, 1, f);
    std::fwrite (&numChannels, 2, 1, f);
    std::fwrite (&sr, 4, 1, f);
    std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&blockAlign, 2, 1, f);
    std::fwrite (&bitsPerSample, 2, 1, f);
    std::fwrite ("data", 1, 4, f);
    std::fwrite (&dataSize, 4, 1, f);

    for (float s : mono)
    {
        const float clamped = std::clamp (s, -1.0f, 1.0f);
        const int16_t sample = (int16_t) std::lround (clamped * 32767.0f);
        std::fwrite (&sample, 2, 1, f);
    }

    std::fclose (f);
    return true;
}

// Downmix identico a HarmonizerAudioProcessor::processBlock (PluginProcessor.cpp):
// 0.5*(L+R) se stereo, copia diretta se mono.
inline std::vector<float> downmix (const WavFile& w)
{
    std::vector<float> mono;
    if (w.numChannels == 2)
    {
        mono.resize (w.interleaved.size() / 2);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (w.interleaved[2 * i] + w.interleaved[2 * i + 1]);
    }
    else if (w.numChannels == 1)
    {
        mono = w.interleaved;
    }
    return mono;
}

// ---------------------------------------------------------------------------
// Click grossolani (slew anomalo) — RETE DI SICUREZZA, NON CRITERIO PRINCIPALE
// ---------------------------------------------------------------------------
//
// ATTENZIONE (sessione 18): eseguita sull'export reale dell'utente
// (Export V1.wav, che l'utente sente difettoso), questa metrica ha dato 0
// anomalie. Il difetto reale (vedi PsolaShifter::currentPeriod, sessione 18)
// non e' un salto di ampiezza in un campione: e' un errore sistematico di
// intonazione e cali episodici di periodicita', invisibili a una misura di
// slew. Tenuta per completezza (le tre passate "pulite" di sessione 17 su
// meccanismi diversi restano valide per QUEI meccanismi), ma NON usarla come
// prova di assenza del problema principale — vedi measureFrame/findInstability
// piu' sotto per la misura che conta davvero.
struct ClickEvent
{
    double timeSeconds;
    double magnitude;   // slew locale / baseline robusta
};

// Rileva discontinuita' anomale: per ogni campione calcola lo slew
// |x[n]-x[n-1]|, stima una baseline ROBUSTA (percentile 99, non la media: un
// segnale reale ha gia' transienti naturali forti — la percentile alta li
// assorbe nella "normalita'" invece di farli sembrare tutti anomali) e
// segnala solo i campioni che superano la baseline di un fattore
// kOutlierFactor. Eventi vicini (entro kClusterGapSamples) vengono fusi in un
// solo report.
inline std::vector<ClickEvent> findClicks (const std::vector<float>& x, double sampleRate,
                                           double outlierFactor = 6.0, int clusterGapSamples = 512)
{
    if (x.size() < 3) return {};

    std::vector<float> slew (x.size(), 0.0f);
    for (size_t i = 1; i < x.size(); ++i)
        slew[i] = std::fabs (x[i] - x[i - 1]);

    std::vector<float> sorted (slew.begin(), slew.end());
    std::sort (sorted.begin(), sorted.end());
    const double baseline = sorted[(size_t) (0.99 * (double) (sorted.size() - 1))];

    std::vector<ClickEvent> events;
    if (baseline <= 1.0e-9) return events; // segnale silenzioso o degenerato: niente da confrontare

    const double threshold = baseline * outlierFactor;

    long lastEventSample = -1000000;
    for (size_t i = 0; i < slew.size(); ++i)
    {
        if (slew[i] < threshold) continue;

        if ((long) i - lastEventSample <= clusterGapSamples && ! events.empty())
        {
            events.back().magnitude = std::max (events.back().magnitude, (double) slew[i] / baseline);
            lastEventSample = (long) i;
            continue;
        }

        events.push_back ({ (double) i / sampleRate, (double) slew[i] / baseline });
        lastEventSample = (long) i;
    }
    return events;
}

inline void printReport (const char* label, const std::vector<ClickEvent>& events)
{
    std::printf ("  %s: %zu anomalie\n", label, events.size());
    for (const auto& e : events)
        std::printf ("    t=%.3fs  slew/baseline=%.1fx\n", e.timeSeconds, e.magnitude);
}

inline void printWetOnly (const std::vector<ClickEvent>& wetEvents, const std::vector<ClickEvent>& dryEvents)
{
    std::printf ("\n  WET-ONLY (introdotti dal plugin, nessun corrispettivo nel dry entro 5ms):\n");
    int count = 0;
    for (const auto& w : wetEvents)
    {
        bool matchedInDry = false;
        for (const auto& d : dryEvents)
            if (std::fabs (w.timeSeconds - d.timeSeconds) < 0.005)
            { matchedInDry = true; break; }

        if (! matchedInDry)
        {
            std::printf ("    t=%.3fs  slew/baseline=%.1fx\n", w.timeSeconds, w.magnitude);
            ++count;
        }
    }
    if (count == 0)
        std::printf ("    nessuna\n");
}

// ---------------------------------------------------------------------------
// Analisi per finestra: f0, periodicita', livello
// ---------------------------------------------------------------------------
//
// periodicity e' il valore di correlazione normalizzata al miglior lag (1.0 =
// perfettamente periodico, valori piu' bassi = il segnale in quella finestra
// si discosta da una ripetizione esatta del proprio periodo — esattamente la
// grandezza che un ascoltatore descrive come "granuloso"/"instabile"/
// "ondeggiante"). E' la stessa idea di measureF0 in TestSignals.h
// (autocorrelazione normalizzata con correzione d'ottava), ma quella funzione
// non espone il valore di picco — reimplementata qui deliberatamente
// affiancata, per non rischiare di alterare una funzione gia' verificata da
// 9 sessioni di test (psola_test.cpp).
struct FrameStat
{
    double f0Hz = 0.0;
    double periodicity = 0.0;
    double rmsValue = 0.0;
};

inline FrameStat measureFrame (const std::vector<float>& x, int from, int len, double sr)
{
    FrameStat result;
    if (from < 0 || len <= 2 || from + len > (int) x.size())
        return result;

    result.rmsValue = rms (x, from, len);

    // Range piu' ampio di measureF0 (che assume voce/fiati fino a ~800Hz):
    // qui il materiale reale (es. corni) puo' arrivare oltre E5 (~659Hz).
    const int minLag = (int) (sr / 1200.0);
    const int maxLag = (int) (sr / 50.0);

    if (from + len + maxLag + 2 > (int) x.size() || minLag < 1 || maxLag <= minLag)
        return result; // non abbastanza contesto per l'autocorrelazione in questa finestra

    std::vector<double> v ((size_t) (len + maxLag + 2));
    double mean = 0.0;
    for (size_t i = 0; i < v.size(); ++i) { v[i] = x[(size_t) from + i]; mean += v[i]; }
    mean /= (double) v.size();
    for (auto& s : v) s -= mean;

    double e0 = 0.0;
    for (int i = 0; i < len; ++i) e0 += v[(size_t) i] * v[(size_t) i];
    if (e0 <= 0.0) return result; // silenzio: f0/periodicity restano 0

    std::vector<double> ac ((size_t) (maxLag + 2), 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double num = 0.0, el = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double b = v[(size_t) (i + lag)];
            num += v[(size_t) i] * b;
            el  += b * b;
        }
        ac[(size_t) lag] = (el > 0.0) ? num / std::sqrt (e0 * el) : 0.0;
    }

    double acMax = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) acMax = std::max (acMax, ac[(size_t) lag]);
    if (acMax <= 0.0) return result;

    int best = -1;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (ac[(size_t) lag] >= 0.90 * acMax
            && ac[(size_t) lag] > ac[(size_t) (lag - 1)]
            && ac[(size_t) lag] >= ac[(size_t) (lag + 1)])
        { best = lag; break; }

    if (best <= minLag || best >= maxLag) return result;

    const double a = ac[(size_t) (best - 1)];
    const double b = ac[(size_t) best];
    const double c = ac[(size_t) (best + 1)];
    const double denom = (a - 2.0 * b + c);
    const double delta = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;

    result.f0Hz = sr / ((double) best + delta);
    result.periodicity = b; // valore di correlazione al lag scelto, non interpolato: basta per la stabilita'
    return result;
}

// ---------------------------------------------------------------------------
// Energia armonica / energia spettrale totale in banda — sessione 25/26,
// timbro granuloso/robotico sulle voci con offset profondo.
// ---------------------------------------------------------------------------
//
// Riusa goertzelMag (TestSignals.h, la stessa DFT a frequenza singola gia'
// in uso da formantPeak): sulla stessa griglia di bin da loHz a hiHz, somma
// l'energia (magnitudine al quadrato) SOLO nei bin piu' vicini alle
// armoniche di f0Hz e la confronta con l'energia totale della griglia.
// Bounded in [0,1] PER COSTRUZIONE (harmonicEnergy e' un sottoinsieme dei
// bin che formano totalEnergy, non una normalizzazione indipendente sul
// dominio del tempo che avrebbe dovuto tener conto del guadagno della
// finestra di Hann) — un valore vicino a 1 vuol dire "quasi tutta l'energia
// sta sulle armoniche attese", un valore basso vuol dire energia sparsa
// altrove: la firma numerica di "granuloso"/"robotico" (rumore inarmonico)
// quando f0Hz e' la f0 REALE della voce, e la firma di "periodicita'
// originale che filtra" (l'artefatto descritto nel commento ATTENZIONE di
// PsolaShifter::emitGrain) quando f0Hz e' invece la f0 del DRY non
// trasposto per lo stesso istante.
inline double harmonicEnergyRatio (const std::vector<float>& x, int from, int len, double sr,
                                    double f0Hz, double loHz = 60.0, double hiHz = 4000.0,
                                    double stepHz = 20.0)
{
    if (f0Hz <= 0.0 || from < 0 || len <= 2 || from + len > (int) x.size())
        return 0.0;

    const int nbins = (int) ((hiHz - loHz) / stepHz) + 1;
    std::vector<double> mag2 ((size_t) nbins);
    double totalEnergy = 0.0;
    for (int k = 0; k < nbins; ++k)
    {
        const double m = goertzelMag (x, from, len, sr, loHz + k * stepHz);
        mag2[(size_t) k] = m * m;
        totalEnergy += mag2[(size_t) k];
    }
    if (totalEnergy <= 1.0e-12) return 0.0;

    double harmonicEnergy = 0.0;
    for (int h = 1; ; ++h)
    {
        const double freq = f0Hz * (double) h;
        if (freq > hiHz) break;
        if (freq < loHz) continue;
        const int k = (int) std::lround ((freq - loHz) / stepHz);
        if (k >= 0 && k < nbins) harmonicEnergy += mag2[(size_t) k];
    }

    return std::min (1.0, harmonicEnergy / totalEnergy);
}

// ---------------------------------------------------------------------------
// Allineamento temporale fra due segnali
// ---------------------------------------------------------------------------
//
// Su un segnale periodico la correlazione di FORMA D'ONDA e' ambigua a meno
// di multipli del periodo (un lag sbagliato di N periodi da' comunque
// correlazione alta) — non e' lo strumento giusto per trovare un ritardo
// grande come una latenza di plugin. L'INVILUPPO (RMS a finestre) non ha
// questa ambiguita' (non e' periodico all'interno di una nota) ed e'
// insensibile alla trasposizione di pitch: e' la scelta giusta per
// l'allineamento di massa. La forma d'onda resta utile SOLO per una
// rifinitura sub-periodo dentro una singola finestra breve (vedi fitWindow).
inline std::vector<double> envelopeRms (const std::vector<float>& x, int hop)
{
    std::vector<double> env;
    if (hop <= 0) return env;
    for (size_t i = 0; i + (size_t) hop <= x.size(); i += (size_t) hop)
        env.push_back (rms (x, (int) i, hop));
    return env;
}

struct Lag
{
    long samples;      // positivo: b e' in ritardo rispetto ad a
    double correlation; // Pearson sull'inviluppo al lag scelto, in [-1,1]
};

inline Lag bestLagByEnvelope (const std::vector<float>& a, const std::vector<float>& b,
                              int hop, int maxLagSamples)
{
    const auto envA = envelopeRms (a, hop);
    const auto envB = envelopeRms (b, hop);
    const int maxLagWindows = std::max (1, maxLagSamples / hop);

    auto meanOf = [] (const std::vector<double>& v)
    { double s = 0.0; for (double x : v) s += x; return v.empty() ? 0.0 : s / (double) v.size(); };
    const double meanA = meanOf (envA), meanB = meanOf (envB);

    long bestLagWindows = 0;
    double bestCorr = -2.0;

    for (long lag = -maxLagWindows; lag <= maxLagWindows; ++lag)
    {
        double num = 0.0, denA = 0.0, denB = 0.0;
        int count = 0;
        for (size_t i = 0; i < envA.size(); ++i)
        {
            const long j = (long) i + lag;
            if (j < 0 || j >= (long) envB.size()) continue;
            const double da = envA[i] - meanA, db = envB[(size_t) j] - meanB;
            num += da * db; denA += da * da; denB += db * db;
            ++count;
        }
        if (count < 4 || denA <= 0.0 || denB <= 0.0) continue;

        const double corr = num / std::sqrt (denA * denB);
        if (corr > bestCorr) { bestCorr = corr; bestLagWindows = lag; }
    }

    return { bestLagWindows * (long) hop, bestCorr };
}

// ---------------------------------------------------------------------------
// Conoscenza del dominio armonico condivisa fra sample_click_finder.cpp e
// real_export_probe.cpp — cosi' le due uscite usano la STESSA tabella e sono
// confrontabili riga a riga.
// ---------------------------------------------------------------------------
//
// Colonna V1 VERA del preset di fabbrica "Maj" (toni accordo {0,4,7,11}),
// derivata a mano da PresetLibrary.cpp::generateDropVoicingTable (sessione
// 18) e confermata dalla sequenza di offset misurata sull'export reale
// dell'utente (0/-2/0/0 su C4->D4->E4->C4, root C). V1 non e' MAI muta in un
// accordo a 4+ toni: la regola "voci oltre numTones restano mute"
// (PresetLibrary.cpp) colpisce solo v>=numTones=4, mai v=0.
constexpr float kMajV1Table[12] = {
    0.0f, -1.0f, -2.0f, -3.0f, 0.0f, -1.0f, -2.0f, 0.0f, -1.0f, -2.0f, -3.0f, 0.0f
};

// d = (notaMIDI - fondamentale) mod 12, sempre nel range [0,11] anche per
// operandi negativi (a differenza del solo operatore % di C++) — stessa
// invariante di HarmonyEngine::degreeOf, reimplementata qui per non dover
// linkare harmony/HarmonyEngine.cpp (dipende da HarmonyPreset.h -> juce_core).
inline int degreeOf (int playedMidiNote, int rootPitchClass) noexcept
{
    return ((playedMidiNote - rootPitchClass) % 12 + 12) % 12;
}

// Rifinitura per finestra: cerca il lag (entro searchRange campioni) e il
// guadagno che meglio spiegano wet come gain*dry ritardato, e riporta il
// residuo (energia non spiegata dal modello lineare) in dB rispetto
// all'energia del wet in quella finestra — misura diretta di "quanto il
// motore sporca" un tratto che dovrebbe essere trasparente (offset 0,
// formanti invariate). Va usata SOLO su finestre brevi e in tratti a
// offset 0 (altrimenti wet e dry hanno pitch diversi e il modello lineare
// gain*dry non e' quello giusto per definizione).
struct FitResult
{
    long lag = 0;
    double gain = 0.0;
    double correlation = 0.0; // 0..1, 1 = wet spiegato perfettamente da gain*dry ritardato
    double residualDb = -999.0;
};

inline FitResult fitWindow (const std::vector<float>& wet, const std::vector<float>& dry,
                            int from, int len, int searchRange)
{
    FitResult best;
    if (from < 0 || from + len > (int) dry.size()) return best;

    for (long lag = -searchRange; lag <= searchRange; ++lag)
    {
        const long wetFrom = from + lag;
        if (wetFrom < 0 || wetFrom + len > (long) wet.size()) continue;

        double num = 0.0, den = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double d = dry[(size_t) (from + i)];
            const double w = wet[(size_t) (wetFrom + i)];
            num += d * w; den += d * d;
        }
        if (den <= 1.0e-12) continue;
        const double gain = num / den;

        double residual = 0.0, wetEnergy = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double d = dry[(size_t) (from + i)];
            const double w = wet[(size_t) (wetFrom + i)];
            const double e = w - gain * d;
            residual += e * e;
            wetEnergy += w * w;
        }
        const double corr = (wetEnergy > 1.0e-12)
            ? std::sqrt (std::max (0.0, 1.0 - residual / wetEnergy)) : 0.0;

        if (corr > best.correlation)
        {
            best.lag = lag;
            best.gain = gain;
            best.correlation = corr;
            best.residualDb = (wetEnergy > 1.0e-12 && residual > 1.0e-12)
                ? 10.0 * std::log10 (residual / wetEnergy) : -999.0;
        }
    }
    return best;
}
