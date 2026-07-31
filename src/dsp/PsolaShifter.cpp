#include "PsolaShifter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    // Con PSOLA il parametro fisico dietro Stability non e' piu' una finestra
    // di analisi STFT (come in SpectralShifter) ma la fondamentale piu' bassa
    // che il motore sa ancora inseguire (minF0Hz): sotto quella soglia la
    // latenza dichiarata (vedi prepare()) non basta piu' a garantire la
    // correttezza degli epoch. E' uno scostamento deliberato dalla lettera
    // di FR-54 ("seleziona la dimensione della finestra di analisi") che
    // resta comunque coerente con lo spirito del requisito: 5 posizioni
    // discrete, Fast=piu' reattivo/meno accurato, Accurate=il contrario.
    // Il PRD non viene modificato; lo scostamento e' documentato qui e in
    // handsoff.md. Valori di partenza, da tarare all'ascolto (una riga):
    constexpr double minF0PerLevel[Stability::numLevels] = { 165.0, 130.0, 100.0, 85.0, 70.0 };

    // process() elabora internamente a fette di al piu' questa dimensione,
    // indipendentemente da quanto e' grande il blocco ricevuto dall'host:
    // e' cio' che permette alla latenza dichiarata (2*maxPeriod + maxBlock)
    // di restare piccola anche quando PluginProcessor dimensiona gli scratch
    // buffer sul caso peggiore (8192 campioni, vedi PluginProcessor.cpp).
    constexpr int kInternalChunk = 64;
}

int PsolaShifter::nextPow2 (int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

double PsolaShifter::clampd (double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void PsolaShifter::prepare (double sampleRate, int /*maxBlockSize*/, int stabilityLevel)
{
    const int level = std::clamp (stabilityLevel, 0, Stability::numLevels - 1);

    sr        = sampleRate;
    minF0     = minF0PerLevel[level];
    maxPeriod = (int) std::ceil (sr / minF0);
    minPeriod = (int) std::floor (sr / 2000.0);
    if (minPeriod < 4) minPeriod = 4;
    // maxBlockSize ricevuto dal chiamante e' ignorato qui: la formula di
    // latenza sotto vale per fetta interna (vedi processChunk/kInternalChunk
    // in process()), non per il blocco esterno dell'host. PluginProcessor
    // dimensiona maxBlockSize sul caso peggiore (8192 campioni) per gli
    // scratch buffer, cifra che farebbe esplodere la latenza se usata qui.
    maxBlock  = kInternalChunk;

    // Vincolo di correttezza: perche' nessun grano possa scrivere in una
    // zona gia' letta, serve latency >= 2 * maxPeriod + maxBlock (dimostrato
    // nella spec sorgente, riportata in handsoff.md sessione 8/9).
    latency   = 2 * maxPeriod + maxBlock;

    bufSize   = nextPow2 (4 * maxPeriod + 4 * maxBlock + 16);
    mask      = bufSize - 1;

    inBuf .assign ((size_t) bufSize, 0.0f);
    outBuf.assign ((size_t) bufSize, 0.0f);
    envBuf.assign ((size_t) bufSize, 0.0f);

    // Gli epoch distano sempre >= minPeriod campioni e vivono in una
    // finestra di bufSize/2 (vedi lo scarto degli epoch vecchi in
    // detectEpochs()): bufSize/2/minPeriod e' quindi un tetto naturale sul
    // numero di epoch mai vivi contemporaneamente. +4 di margine.
    epochCapacity = (size_t) (bufSize / 2 / minPeriod) + 4;
    epochRing.assign (epochCapacity, 0);
    epochClear();

    reset();
}

void PsolaShifter::reset()
{
    std::fill (inBuf .begin(), inBuf .end(), 0.0f);
    std::fill (outBuf.begin(), outBuf.end(), 0.0f);
    std::fill (envBuf.begin(), envBuf.end(), 0.0f);
    epochClear();

    // Si finge che i primi `latency` campioni (silenzio) siano gia' entrati:
    // da qui in poi vale sempre absRead == absWrite - latency.
    absWrite  = latency;
    absRead   = 0;
    lastEpoch = -1;
    synthPos  = -1.0;
}

void PsolaShifter::setPitchShiftSemitones (float semitones)
{
    // Unico punto di conversione semitoni -> alpha (rapporto moltiplicativo)
    // richiesto dal motore: il resto del progetto continua a ragionare in
    // semitoni (Voice, HarmonyEngine, APVTS).
    alpha = clampd (std::exp2 ((double) semitones / 12.0), 0.125, 8.0);
}

void PsolaShifter::setFormantRatio (double v)
{
    beta = clampd (v, 0.25, 4.0);
}

int PsolaShifter::currentPeriod() const
{
    if (currentF0Hz <= 0.0) return 0;
    int p = (int) std::lround (sr / currentF0Hz);
    return std::clamp (p, minPeriod, maxPeriod);
}

float PsolaShifter::readInterp (double pos) const
{
    // Interpolazione cubica di Hermite a 4 punti, NON lineare: quando il
    // rapporto di trasposizione non e' una potenza di 2 il posizionamento
    // dei grani cade su frazioni di campione che cambiano a ogni grano.
    // L'errore dell'interpolazione lineare dipende dalla frazione e si
    // traduce in una sub-armonica percepibile sotto la nota voluta; quella
    // cubica la elimina (misurato sulla suite di test sorgente).
    const long long i1 = (long long) std::floor (pos);
    const double    t  = pos - (double) i1;

    const float xm = inBuf[(size_t) ((i1 - 1) & mask)];
    const float x0 = inBuf[(size_t) ( i1      & mask)];
    const float x1 = inBuf[(size_t) ((i1 + 1) & mask)];
    const float x2 = inBuf[(size_t) ((i1 + 2) & mask)];

    const double c0 = x0;
    const double c1 = 0.5 * (x1 - xm);
    const double c2 = xm - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm) + 1.5 * (x0 - x1);

    return (float) (((c3 * t + c2) * t + c1) * t + c0);
}

void PsolaShifter::epochPushBack (long long value) noexcept
{
    if (epochCount == epochCapacity)
    {
        // Non dovrebbe succedere con il dimensionamento di prepare() (ampio
        // margine sopra il tetto teorico di epoch vivi): se succede si
        // scarta il piu' vecchio invece di allocare o bloccare.
        epochHead = (epochHead + 1) % epochCapacity;
        --epochCount;
    }
    epochRing[(epochHead + epochCount) % epochCapacity] = value;
    ++epochCount;
}

void PsolaShifter::epochPopFront() noexcept
{
    if (epochCount == 0) return;
    epochHead = (epochHead + 1) % epochCapacity;
    --epochCount;
}

void PsolaShifter::detectEpochs()
{
    const int P = currentPeriod();
    if (P <= 0) return;

    if (lastEpoch < 0)
    {
        if (absWrite < 3 * P) return;
        lastEpoch = absWrite - 2 * (long long) P;
    }

    const int w = std::max (2, P / 4);

    // Un grano legge fino a epoch + P, quindi l'epoch e' utilizzabile solo
    // quando quei campioni sono gia' entrati.
    while (lastEpoch + P + w + P < absWrite)
    {
        const long long pred = lastEpoch + P;

        long long bestIdx = pred;
        float     bestMag = -1.0f;

        for (long long k = pred - w; k <= pred + w; ++k)
        {
            const float m = std::fabs (inBuf[(size_t) (k & mask)]);
            if (m > bestMag) { bestMag = m; bestIdx = k; }
        }

        lastEpoch = bestIdx;
        epochPushBack (bestIdx);
    }

    // Si scartano gli epoch usciti dalla finestra utile del buffer.
    const long long oldest = absWrite - bufSize / 2;
    while (! epochEmpty() && epochFront() < oldest)
        epochPopFront();
}

long long PsolaShifter::nearestEpoch (long long t) const
{
    long long best = epochFront();
    long long bestD = std::llabs (best - t);

    for (size_t i = 0; i < epochCount; ++i)
    {
        const long long e = epochRing[(epochHead + i) % epochCapacity];
        const long long d = std::llabs (e - t);
        if (d < bestD) { bestD = d; best = e; }
        else if (e > t) break; // ring ordinato per inserimento: da qui in poi peggiora
    }
    return best;
}

void PsolaShifter::emitGrain (long long analysisEpoch, long long synthEpoch, double frac, int P)
{
    // Semiampiezza della finestra, in campioni sorgente.
    //
    // Il grano ha lunghezza totale Lg = 2W: perche' due grani di sintesi
    // consecutivi (spaziati di synthPeriod = P/alpha) continuino a toccarsi
    // serve 2W >= synthPeriod, cioe' W >= P/(2*alpha). Con W = P fisso, sotto
    // alpha = 0.5 (circa un'ottava sotto) la spaziatura supera la lunghezza
    // del grano e l'inviluppo crolla a vuoti periodici (misurato dal test 6
    // della suite: RMS a breve termine che crolla sotto -14 semitoni).
    //
    // ATTENZIONE: allargare troppo il grano e' un problema diverso, non solo
    // un'ottimizzazione mancata — a beta=1 il grano e' una copia diretta del
    // segnale sorgente, quindi un grano piu' lungo della spaziatura reintro-
    // duce direttamente periodicita' al passo ORIGINALE (non trasposto)
    // dentro ogni singolo grano. Usare il margine largo P/alpha (anziche' il
    // minimo P/(2*alpha) qui sotto) fa infatti fallire il test 1: a -12
    // semitoni la f0 misurata torna quella originale (errore di un'ottava),
    // perche' ogni grano da solo contiene gia' piu' periodi del segnale non
    // shiftato. Il margine minimo che garantisce il solo contatto (nessuna
    // sovrapposizione in eccesso) e' quindi la scelta corretta, non un
    // compromesso.
    // Il margine esatto (2W == synthPeriod) fa toccare i grani solo al
    // limite: la finestra di Hann si azzera ai bordi, quindi al contatto
    // esatto l'inviluppo scende comunque a vuoti stretti (misurato: ratio
    // ~0.23, sotto la soglia 0.25 del test 6). Un margine del 20% e'
    // sufficiente a restare oltre soglia senza riavvicinarsi alla larghezza
    // che reintroduce l'artefatto di ottava del test 1 (vedi sopra).
    constexpr double kOverlapMargin = 1.2;
    const double W = kOverlapMargin * std::max ((double) P, (double) P / (2.0 * alpha));

    const int Lg = (int) std::lround (2.0 * W / beta);
    if (Lg < 4) return;

    const long long halfI = Lg / 2;
    const double    half  = (double) halfI;
    const double twoPiOverLg = 6.283185307179586 / (double) Lg;

    for (int j = 0; j < Lg; ++j)
    {
        const double srcPos = (double) analysisEpoch
                            + ((double) j - half - frac) * beta;
        const float  s      = readInterp (srcPos);

        const float w = 0.5f - 0.5f * (float) std::cos (twoPiOverLg * ((double) j - frac + 0.5));

        const size_t idx = (size_t) ((synthEpoch + j - halfI) & mask);

        outBuf[idx] += s * w;
        envBuf[idx] += w;
    }
}

void PsolaShifter::synthesise()
{
    const int P = currentPeriod();
    if (P <= 0 || epochEmpty()) return;

    if (synthPos < 0.0)
        synthPos = (double) epochFront();

    const double synthPeriod = std::max (2.0, (double) P / alpha);
    const long long limit    = absWrite - (long long) std::max ((double) maxPeriod, synthPeriod);

    int guard = 0; // salvagente contro loop patologici sull'audio thread
    while (synthPos < (double) limit && guard++ < 4096)
    {
        const long long sp   = (long long) std::floor (synthPos);
        const double    frac = synthPos - (double) sp;

        emitGrain (nearestEpoch (sp), sp, frac, P);
        synthPos += synthPeriod;
    }
}

void PsolaShifter::process (const float* in, float* out, int numSamples)
{
    // Elabora a fette di al piu' maxBlock (kInternalChunk) campioni: e' la
    // dimensione su cui e' valida la garanzia di correttezza 2W>=synthPeriod
    // e la formula di latenza dichiarata (vedi prepare()), indipendentemente
    // da quanto e' grande il blocco che l'host passa a questo metodo.
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - done);
        processChunk (in + done, out + done, n);
        done += n;
    }
}

void PsolaShifter::processChunk (const float* in, float* out, int numSamples)
{
    // --- 1. scrittura in ingresso -----------------------------------
    for (int i = 0; i < numSamples; ++i)
        inBuf[(size_t) ((absWrite + i) & mask)] = in[i];

    absWrite += numSamples;

    // --- 2. analisi: individuazione degli epoch ---------------------
    detectEpochs();

    // --- 3. sintesi: overlap-add dei grani --------------------------
    synthesise();

    // --- 4. lettura in uscita con normalizzazione d'inviluppo -------
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t idx = (size_t) ((absRead + i) & mask);
        const float  e   = envBuf[idx];

        // Si divide solo quando la sovrapposizione supera l'unita'
        // (trasposizioni verso l'alto). Verso il basso i grani si
        // distanziano e fra un impulso glottale e il successivo il segnale
        // decade davvero: amplificare quel tratto non lo ricostruisce, alza
        // solo il rumore.
        out[i] = outBuf[idx] / std::max (e, 1.0f);

        outBuf[idx] = 0.0f; // la cella e' consumata: si azzera subito
        envBuf[idx] = 0.0f;
    }

    absRead += numSamples;
}
