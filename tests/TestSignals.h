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
// Posizione del picco formantico: proxy numerico delle formanti.
// ---------------------------------------------------------------------------
static double formantPeak (const std::vector<float>& x, int from, int len, double sr,
                           double loHz = 300.0, double hiHz = 3500.0)
{
    const double stepHz = 20.0;
    const int    nbins  = (int) ((hiHz - loHz) / stepHz) + 1;

    std::vector<double> mag ((size_t) nbins, 0.0);

    for (int k = 0; k < nbins; ++k)
    {
        const double freq = loHz + k * stepHz;
        const double w    = 2.0 * kPi * freq / sr;
        double re = 0.0, im = 0.0;

        for (int i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * i / len);
            const double s   = (double) x[(size_t) (from + i)] * win;
            re += s * std::cos (w * i);
            im -= s * std::sin (w * i);
        }
        mag[(size_t) k] = std::sqrt (re * re + im * im);
    }

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
