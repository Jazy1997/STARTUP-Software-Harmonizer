#pragma once

// Generatori e misure numeriche condivisi fra le suite di test DSP
// (CLAUDE.md, "non puoi ascoltare" — criteri di completamento numerici).
// Estratto da tests/psola_test.cpp (sessione 16) perche' tests/voice_test.cpp
// ha bisogno esattamente dello stesso segnale di prova e delle stesse misure
// (RMS, salto massimo, F0 per autocorrelazione) — CLAUDE.md non vieta la
// duplicazione esplicitamente, ma duplicare un generatore di segnale gia'
// verificato invece di condividerlo e' esattamente il tipo di divergenza che
// puo' rendere due suite silenziosamente incoerenti fra loro. Nessuna
// dipendenza JUCE, header-only, stesso principio di Glide.h/PitchLatch.h.
//
// Le funzioni sono IDENTICHE (stessa matematica, stesso comportamento) a
// quelle che erano definite localmente in psola_test.cpp prima di questo
// estratto — l'unica differenza e' che il sample rate diventa un parametro
// esplicito invece di una costante globale, cosi' ogni suite puo' passare il
// proprio SR senza dover condividerlo. psola_test.cpp e' stato aggiornato per
// includere questo header al posto delle definizioni locali (vedi handsoff.md
// sessione 16 per la verifica che l'uscita resti bit-per-bit identica).

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

static constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Generatore: treno di impulsi a f0 filtrato da una risonanza fissa.
// Modello sorgente-filtro elementare: le formanti stanno nel filtro e NON
// dipendono da f0. E' il segnale giusto per verificare che pitch e formanti
// siano davvero indipendenti.
// ---------------------------------------------------------------------------
struct Resonator
{
    double b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (double freq, double q, double sr)
    {
        const double w = 2.0 * kPi * freq / sr;
        const double r = std::exp (-w / (2.0 * q));
        a1 = -2.0 * r * std::cos (w);
        a2 = r * r;
        b0 = (1.0 - r);
    }

    double process (double x)
    {
        const double y = b0 * x - a1 * z1 - a2 * z2;
        z2 = z1; z1 = y;
        return y;
    }
};

// Una sola risonanza, non due: con due formanti "il picco dello spettro" e'
// ambiguo e la misura salta dall'una all'altra rendendo il test inaffidabile.
static std::vector<float> makeVowel (double f0, double seconds, double sr,
                                     double formant1 = 1100.0)
{
    const int n = (int) (seconds * sr);
    std::vector<float> v ((size_t) n, 0.0f);

    Resonator r1;
    r1.set (formant1, 10.0, sr);

    double phase = 0.0;
    const double inc = f0 / sr;

    for (int i = 0; i < n; ++i)
    {
        phase += inc;
        double pulse = 0.0;
        if (phase >= 1.0) { phase -= 1.0; pulse = 1.0; }

        const double y = r1.process (pulse);
        v[(size_t) i] = (float) (4.0 * y);
    }
    return v;
}

// Come makeVowel, ma f0 varia linearmente nel tempo da f0Start a f0End.
static std::vector<float> makeSweptVowel (double f0Start, double f0End, double seconds, double sr,
                                          double formant1 = 1100.0)
{
    const int n = (int) (seconds * sr);
    std::vector<float> v ((size_t) n, 0.0f);

    Resonator r1;
    r1.set (formant1, 10.0, sr);

    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t  = (double) i / (double) n;
        const double f0 = f0Start + (f0End - f0Start) * t;
        phase += f0 / sr;
        double pulse = 0.0;
        if (phase >= 1.0) { phase -= 1.0; pulse = 1.0; }

        const double y = r1.process (pulse);
        v[(size_t) i] = (float) (4.0 * y);
    }
    return v;
}

// Come makeVowel, ma f0 oscilla sinusoidalmente intorno a f0Center (vibrato)
// invece di restare fissa — sessione 20, indagine wobbling: un tono
// perfettamente stazionario non basta a riprodurre la deriva synthPos/epoch
// di PsolaShifter (misurato: Test 10 non la riproduce), perche' una nota
// reale non e' MAI perfettamente stazionaria (vibrato/intonazione naturale
// del musicista). depthCents/rateHz scelti bassi e lenti apposta: devono
// restare dentro cio' che un ascoltatore descriverebbe come "nota tenuta",
// non un trillo.
static std::vector<float> makeVibratoVowel (double f0Center, double depthCents, double rateHz,
                                            double seconds, double sr, double formant1 = 1100.0)
{
    const int n = (int) (seconds * sr);
    std::vector<float> v ((size_t) n, 0.0f);

    Resonator r1;
    r1.set (formant1, 10.0, sr);

    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t  = (double) i / sr;
        const double f0 = f0Center * std::pow (2.0, (depthCents * std::sin (2.0 * kPi * rateHz * t)) / 1200.0);
        phase += f0 / sr;
        double pulse = 0.0;
        if (phase >= 1.0) { phase -= 1.0; pulse = 1.0; }

        const double y = r1.process (pulse);
        v[(size_t) i] = (float) (4.0 * y);
    }
    return v;
}

// NOTA (sessione 25/26 — timbro "granuloso"/"che respira" a shift profondo,
// V3/V4 del preset Maj): un generatore makeJitteredVowel (impulsi con
// durata di periodo perturbata da un pattern deterministico a passo aureo,
// invece che esattamente equispaziati come qui sopra) e' stato scritto per
// riprodurre in laboratorio il jitter ciclo-per-ciclo che uno strumento
// reale ha sempre. Un fix su PsolaShifter::synthesise() (media della
// spaziatura locale fra epoch su una finestra di piu' intervalli invece di
// uno solo, per ridurre l'amplificazione di 1/alpha misurata per calcolo a
// shift profondo) migliorava la misura su QUESTO segnale sintetico — ma
// PEGGIORAVA la stessa misura sul file reale dell'utente (sample_click_
// finder --fixedF0, sweep -1..-10 semitoni: da 0% a 3-10% di finestre
// instabili anche nel range -1..-4, prima perfetto). Il generatore e il
// fix sono stati RITIRATI insieme (CLAUDE.md regola 13, stesso principio
// gia' applicato in sessione 19 a makeCompetingPulsesVowel): il jitter
// deterministico a passo aureo non riproduce fedelmente la statistica del
// jitter reale, quindi un fix validato solo su questo generatore non e'
// una prova sufficiente. Vedi handsoff.md sessione 25/26 per i numeri
// completi — un tentativo futuro deve validarsi PRIMA sul file reale, non
// solo su un segnale sintetico.
//
// Sessione 26 (ripresa): la causa vera e' stata trovata per misura diretta
// (strumentazione temporanea di detectEpochs(), non il periodo di sintesi
// di synthesise() come ipotizzato sopra) e corretta — vedi PsolaShifter.cpp
// e makeRichNoisyVowel sotto, il generatore sintetico che sostituisce
// makeJitteredVowel per la regressione permanente (Test 12,
// tests/psola_test.cpp).

// ---------------------------------------------------------------------------
// Generatore: periodo ESATTAMENTE intero, costruito come somma di armoniche
// con fasi pseudo-casuali deterministiche, piu' rumore bianco deterministico
// a bassa ampiezza — sessione 26, sostituisce makeJitteredVowel (ritirato,
// vedi nota sopra) come base per Test 12 (tests/psola_test.cpp).
//
// A differenza di un treno di impulsi puro (makeVowel: un solo picco netto
// per periodo), fasi armoniche sparse producono tipicamente piu' massimi
// locali entro il periodo, un segnale piu' vicino a materiale reale
// (corno/voce) di un treno di impulsi puro.
//
// ATTENZIONE (onestamente riportato, non nascosto): un tentativo di rendere
// questo generatore DISCRIMINANTE (fallisce sul codice pre-fix, passa dopo —
// il criterio dichiarato per un test nuovo, CLAUDE.md regola 13) non e'
// riuscito. Ne' aumentare il rumore (provato -34/-20/-19/-17/-14 dB: o il
// vecchio algoritmo restava comunque sopra soglia, o l'ingresso stesso
// diventava troppo instabile per fidarsi della misura) ne' un secondo
// disegno esplicito a DUE impulsi per periodo di ampiezza vicina (nello
// spirito di makeCompetingPulsesVowel, sessione 19, ma con ampiezze FISSE
// invece che dinamiche per evitare il vizio che aveva fatto ritirare
// quello) hanno prodotto una separazione pulita: quel secondo disegno ha
// mandato in confusione la stima di periodicita' stessa (measureFrame ha
// agganciato un lag sbagliato, dato lo spettro con due impulsi ravvicinati).
// Stessa difficolta' gia' incontrata in sessione 19/20 nel riprodurre in
// laboratorio questo specifico meccanismo. Test 12 resta quindi una
// verifica di TRASPARENZA PERMANENTE (come Test 10/11), NON una prova del
// meccanismo — quella e' venuta, per intero, dallo sweep sul file reale
// (sample_click_finder --fixedF0) e dalla misura diretta del jitter degli
// epoch (PSOLA_DEBUG_EPOCHS), entrambe in handsoff.md sessione 26.
static std::vector<float> makeRichNoisyVowel (int periodSamples, double seconds, double sr,
                                              int numHarmonics = 12, double noiseDb = -20.0)
{
    // LCG minimale e deterministico (Numerical Recipes): nessuna dipendenza
    // da <random>, stesso principio "niente dipendenze non necessarie" del
    // resto di questo header. Usato due volte con semi diversi (fasi,
    // rumore) cosi' le due sorgenti di variazione sono indipendenti.
    struct Lcg
    {
        uint32_t state;
        explicit Lcg (uint32_t seed) : state (seed) {}
        double next01() { state = state * 1664525u + 1013904223u; return (double) state / 4294967296.0; }
    };

    // Un solo periodo, costruito una volta e ripetuto identico.
    std::vector<double> onePeriod ((size_t) periodSamples, 0.0);
    Lcg phaseRng (12345);
    for (int h = 1; h <= numHarmonics; ++h)
    {
        const double amp = 1.0 / (double) h;
        const double phase = phaseRng.next01() * 2.0 * kPi;
        for (int i = 0; i < periodSamples; ++i)
            onePeriod[(size_t) i] += amp * std::sin (2.0 * kPi * (double) h * (double) i / (double) periodSamples + phase);
    }
    double peakAbs = 0.0;
    for (double s : onePeriod) peakAbs = std::max (peakAbs, std::fabs (s));
    for (double& s : onePeriod) s /= peakAbs; // normalizzato a picco 1.0

    double sumSq = 0.0;
    for (double s : onePeriod) sumSq += s * s;
    const double periodicRms = std::sqrt (sumSq / (double) periodSamples);
    const double noiseRms = periodicRms * std::pow (10.0, noiseDb / 20.0);

    const int n = (int) (seconds * sr);
    std::vector<float> v ((size_t) n, 0.0f);
    Lcg noiseRng (67890);
    for (int i = 0; i < n; ++i)
    {
        // Rumore uniforme centrato, scalato alla RMS voluta (varianza di
        // uniforme[-a,a] = a^2/3 -> a = noiseRms*sqrt(3)): non serve la
        // forma gaussiana, solo un pavimento di energia deterministico e
        // non periodico.
        const double u = 2.0 * noiseRng.next01() - 1.0;
        const double noise = u * noiseRms * std::sqrt (3.0);
        v[(size_t) i] = (float) (onePeriod[(size_t) (i % periodSamples)] + noise);
    }
    return v;
}

// ---------------------------------------------------------------------------
// Misura di f0 per autocorrelazione, con interpolazione parabolica del picco.
// ---------------------------------------------------------------------------
static double measureF0 (const std::vector<float>& x, int from, int len, double sr)
{
    const int minLag = (int) (sr / 800.0);
    const int maxLag = (int) (sr / 50.0);

    // ATTENZIONE - errore facile e gia' commesso una volta (vedi CLAUDE.md,
    // "un test che fallisce potrebbe essere il test sbagliato"). L'auto-
    // correlazione grezza (somma dei prodotti, senza rimozione della media
    // e senza normalizzazione per l'energia) non e' confrontabile fra lag
    // diversi: sceglie sistematicamente la sub-armonica e fa sembrare rotto
    // un pitch shifter che funziona. Serve la correlazione normalizzata.
    std::vector<double> v ((size_t) (len + maxLag + 2));
    double mean = 0.0;
    for (size_t i = 0; i < v.size(); ++i) { v[i] = x[(size_t) from + i]; mean += v[i]; }
    mean /= (double) v.size();
    for (auto& s : v) s -= mean;

    double e0 = 0.0;
    for (int i = 0; i < len; ++i) e0 += v[(size_t) i] * v[(size_t) i];
    if (e0 <= 0.0) return 0.0;

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

    // Correzione dell'errore d'ottava: si sceglie il PRIMO massimo locale che
    // arrivi entro il 90% del massimo assoluto, non il massimo assoluto
    // (stessa logica della soglia assoluta di YIN).
    double acMax = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) acMax = std::max (acMax, ac[(size_t) lag]);

    int best = -1;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (ac[(size_t) lag] >= 0.90 * acMax
            && ac[(size_t) lag] > ac[(size_t) (lag - 1)]
            && ac[(size_t) lag] >= ac[(size_t) (lag + 1)])
        { best = lag; break; }

    if (best <= minLag || best >= maxLag) return 0.0;

    const double a = ac[(size_t) (best - 1)];
    const double b = ac[(size_t) best];
    const double c = ac[(size_t) (best + 1)];
    const double denom = (a - 2.0 * b + c);
    const double delta = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;

    return sr / ((double) best + delta);
}

// ---------------------------------------------------------------------------
// Goertzel a frequenza singola su una finestra di Hann: la "DFT a un solo
// bin" gia' in uso dentro formantPeak, estratta qui (sessione 25/26) perche'
// serve anche a SampleAnalysis.h (harmonicEnergyRatio) — un solo posto dove
// vive la matematica invece di due copie che potrebbero divergere (CLAUDE.md,
// stesso principio gia' applicato quando questo header e' stato estratto da
// psola_test.cpp, vedi commento di testa del file).
// ---------------------------------------------------------------------------
static double goertzelMag (const std::vector<float>& x, int from, int len, double sr, double freqHz)
{
    const double w = 2.0 * kPi * freqHz / sr;
    double re = 0.0, im = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * i / len);
        const double s   = (double) x[(size_t) (from + i)] * win;
        re += s * std::cos (w * i);
        im -= s * std::sin (w * i);
    }
    return std::sqrt (re * re + im * im);
}

// ---------------------------------------------------------------------------
// Posizione del picco formantico: proxy numerico delle formanti.
// ---------------------------------------------------------------------------
static double formantPeak (const std::vector<float>& x, int from, int len, double sr,
                           double loHz = 300.0, double hiHz = 3500.0)
{
    const double stepHz = 20.0;
    const int    nbins  = (int) ((hiHz - loHz) / stepHz) + 1;

    std::vector<double> mag ((size_t) nbins, 0.0);

    for (int k = 0; k < nbins; ++k)
        mag[(size_t) k] = goertzelMag (x, from, len, sr, loHz + k * stepHz);

    // La lisciatura deve essere piu' larga della spaziatura fra le armoniche
    // (qui 200 Hz, cioe' 10 bin): con una finestra piu' stretta il massimo si
    // aggancia a una singola armonica invece che alla formante.
    std::vector<double> sm ((size_t) nbins, 0.0);
    for (int k = 0; k < nbins; ++k)
    {
        double acc = 0.0; int cnt = 0;
        for (int d = -6; d <= 6; ++d)
        {
            const int kk = k + d;
            if (kk >= 0 && kk < nbins) { acc += mag[(size_t) kk]; ++cnt; }
        }
        sm[(size_t) k] = acc / cnt;
    }

    int best = 0;
    for (int k = 0; k < nbins; ++k) if (sm[(size_t) k] > sm[(size_t) best]) best = k;

    return loHz + best * stepHz;
}

// ---------------------------------------------------------------------------
static double maxJump (const std::vector<float>& x, int from, int len)
{
    double m = 0.0;
    for (int i = from + 1; i < from + len; ++i)
        m = std::max (m, (double) std::fabs (x[(size_t) i] - x[(size_t) (i - 1)]));
    return m;
}

static double rms (const std::vector<float>& x, int from, int len)
{
    double s = 0.0;
    for (int i = 0; i < len; ++i)
        s += (double) x[(size_t) (from + i)] * (double) x[(size_t) (from + i)];
    return std::sqrt (s / len);
}

// Minimo, su una finestra scorrevole, dell'RMS a breve termine. Un motore
// che smette di sovrapporre i grani produce vuoti periodici: il minimo crolla
// molto sotto l'RMS medio anche se il livello medio sembra a posto.
static double minShortTimeRms (const std::vector<float>& x, int from, int len, int win)
{
    double m = 1.0e18;
    const int hop = std::max (1, win / 2);
    for (int i = 0; i + win <= len; i += hop)
        m = std::min (m, rms (x, from + i, win));
    return m;
}

static bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}

static double centsError (double measured, double expected)
{
    if (measured <= 0.0 || expected <= 0.0) return 1.0e9;
    return 1200.0 * std::log2 (measured / expected);
}
