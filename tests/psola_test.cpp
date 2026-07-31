// Suite di verifica numerica di PsolaShifter.
//
// Ragione d'essere di questo file (CLAUDE.md, "non puoi ascoltare"): non
// esiste modo di sapere se il pitch shifter "suona bene" senza ascoltarlo.
// Qui il criterio di completamento e' numerico. Deliberatamente SENZA
// dipendenze JUCE: si compila ed esegue in un secondo, senza aprire una DAW
// (CLAUDE.md regola 11 — lo stesso principio del target standalone).
//
// Portato da un'implementazione esterna verificata (vedi handsoff.md,
// sessioni 8/9) e adattato alla nostra interfaccia PitchShifter (namespace
// globale, setPitchShiftSemitones in semitoni invece di setPitchRatio in
// alpha, setInputF0Hz invece di setF0, prepare con stabilityLevel invece di
// minF0Hz diretto).
//
// Compilazione (vedi anche il target CMake `psola_test`):
//   g++ -O2 -std=c++20 -Isrc tests/psola_test.cpp src/dsp/PsolaShifter.cpp -o psola_test

#include "dsp/PsolaShifter.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

static constexpr double SR    = 48000.0;
static constexpr int    BLOCK = 256;
static constexpr double kPi   = 3.14159265358979323846;

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
static std::vector<float> makeVowel (double f0, double seconds,
                                     double formant1 = 1100.0)
{
    const int n = (int) (seconds * SR);
    std::vector<float> v ((size_t) n, 0.0f);

    Resonator r1;
    r1.set (formant1, 10.0, SR);

    double phase = 0.0;
    const double inc = f0 / SR;

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

// Come makeVowel, ma f0 varia linearmente nel tempo da f0Start a f0End:
// esercita il ricambio continuo di epoch (test 7), mai messo alla prova
// dalla suite originale, che passa sempre una f0 costante.
static std::vector<float> makeSweptVowel (double f0Start, double f0End, double seconds,
                                          double formant1 = 1100.0)
{
    const int n = (int) (seconds * SR);
    std::vector<float> v ((size_t) n, 0.0f);

    Resonator r1;
    r1.set (formant1, 10.0, SR);

    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t  = (double) i / (double) n;
        const double f0 = f0Start + (f0End - f0Start) * t;
        phase += f0 / SR;
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
static double measureF0 (const std::vector<float>& x, int from, int len)
{
    const int minLag = (int) (SR / 800.0);
    const int maxLag = (int) (SR / 50.0);

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

    return SR / ((double) best + delta);
}

// ---------------------------------------------------------------------------
// Posizione del picco formantico: proxy numerico delle formanti.
// ---------------------------------------------------------------------------
static double formantPeak (const std::vector<float>& x, int from, int len,
                           double loHz = 300.0, double hiHz = 3500.0)
{
    const double stepHz = 20.0;
    const int    nbins  = (int) ((hiHz - loHz) / stepHz) + 1;

    std::vector<double> mag ((size_t) nbins, 0.0);

    for (int k = 0; k < nbins; ++k)
    {
        const double freq = loHz + k * stepHz;
        const double w    = 2.0 * kPi * freq / SR;
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
// che smette di sovrapporre i grani (problema individuato nel port, vedi
// handsoff.md) produce vuoti periodici: il minimo crolla molto sotto l'RMS
// medio anche se il livello medio sembra a posto.
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

// ---------------------------------------------------------------------------
static std::vector<float> runShifter (const std::vector<float>& in,
                                      double f0, double semitones, double beta,
                                      int stabilityLevel = Stability::numLevels - 1)
{
    PsolaShifter ps;
    ps.prepare (SR, BLOCK, stabilityLevel);
    ps.setInputF0Hz (f0);
    ps.setPitchShiftSemitones ((float) semitones);
    ps.setFormantRatio (beta);

    std::vector<float> out (in.size(), 0.0f);

    for (size_t i = 0; i + BLOCK <= in.size(); i += BLOCK)
        ps.process (&in[i], &out[i], BLOCK);

    return out;
}

static double centsError (double measured, double expected)
{
    if (measured <= 0.0 || expected <= 0.0) return 1.0e9;
    return 1200.0 * std::log2 (measured / expected);
}

// ---------------------------------------------------------------------------
int main()
{
    int failures = 0;
    const double f0in = 200.0;
    const auto input = makeVowel (f0in, 1.5);

    const int anaFrom = (int) (0.8 * SR);
    const int anaLen  = 8192;

    const double inPeak = formantPeak (input, anaFrom, anaLen);
    const double inRms  = rms (input, anaFrom, anaLen);

    std::printf ("Segnale di test: vocale sintetica, f0 = %.1f Hz, "
                 "formante a 1100 Hz\n", f0in);
    std::printf ("Picco formantico in ingresso: %.0f Hz   RMS: %.4f\n\n", inPeak, inRms);

    // -----------------------------------------------------------------------
    std::printf ("TEST 1 - accuratezza di trasposizione (beta = 1)\n");
    std::printf ("%10s %12s %12s %10s %10s\n",
                 "semitoni", "atteso Hz", "misurato Hz", "errore c", "esito");

    const int semis[] = { -12, -7, -5, -3, 0, 4, 7, 12 };

    for (int st : semis)
    {
        const double alpha = std::pow (2.0, st / 12.0);
        const auto   out   = runShifter (input, f0in, st, 1.0);

        const double expected = f0in * alpha;
        const double measured = measureF0 (out, anaFrom, anaLen);
        const double err      = centsError (measured, expected);
        const bool   ok       = std::fabs (err) < 10.0;

        if (! ok) ++failures;
        std::printf ("%10d %12.2f %12.2f %10.2f %10s\n",
                     st, expected, measured, err, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 2 - ortogonalita': con beta = 1 le formanti non "
                 "devono seguire il pitch\n");
    std::printf ("%10s %14s %14s %10s\n",
                 "semitoni", "picco Hz", "scostamento", "esito");

    for (int st : semis)
    {
        const auto out = runShifter (input, f0in, st, 1.0);

        const double c    = formantPeak (out, anaFrom, anaLen);
        const double dev  = 100.0 * (c - inPeak) / inPeak;

        const bool   ok   = std::fabs (dev) < 15.0;

        if (! ok) ++failures;
        std::printf ("%10d %14.1f %13.1f%% %10s\n",
                     st, c, dev, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 3 - beta sposta le formanti senza toccare il pitch "
                 "(semitoni = 0)\n");
    std::printf ("%10s %14s %14s %12s %10s\n",
                 "beta", "picco Hz", "atteso Hz", "f0 misurata", "esito");

    for (double beta : { 0.7, 0.85, 1.0, 1.2, 1.5 })
    {
        const auto out = runShifter (input, f0in, 0.0, beta);

        const double c        = formantPeak (out, anaFrom, anaLen);
        const double expected = inPeak * beta;
        const double devC     = 100.0 * (c - expected) / expected;
        const double measured = measureF0 (out, anaFrom, anaLen);
        const double errF0    = centsError (measured, f0in);

        const bool ok = std::fabs (devC) < 12.0 && std::fabs (errF0) < 10.0;
        if (! ok) ++failures;

        std::printf ("%10.2f %14.1f %14.1f %12.2f %10s\n",
                     beta, c, expected, measured, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 4 - assenza di discontinuita' e stabilita' di livello\n");
    const double inJump = maxJump (input, anaFrom, anaLen);

    for (int st : { -12, -5, 7 })
    {
        const double alpha = std::pow (2.0, st / 12.0);
        const auto   out   = runShifter (input, f0in, st, 1.0);

        const double j  = maxJump (out, anaFrom, anaLen);
        const double r  = rms (out, anaFrom, anaLen);
        const double dB = 20.0 * std::log10 (r / inRms);

        const double allowed = inJump * std::max (1.0, alpha) * 3.0;
        const bool   ok      = (j < allowed) && std::fabs (dB) < 6.0;

        if (! ok) ++failures;
        std::printf ("  %+3d st: salto max %.4f (limite %.4f), livello %+.2f dB  %s\n",
                     st, j, allowed, dB, ok ? "OK" : "FALLITO");
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 5 - la latenza dichiarata cala con Stability piu' "
                 "reattivo (minF0 piu' alto)\n");
    {
        int prevLatency = -1;
        bool monotonic = true;
        for (int level = 0; level < Stability::numLevels; ++level)
        {
            PsolaShifter ps;
            ps.prepare (SR, BLOCK, level);
            const int lat = ps.getLatencySamples();
            std::printf ("  Stability[%d] (%s) -> %d campioni = %.1f ms\n",
                         level, Stability::names[level], lat, 1000.0 * lat / SR);
            if (prevLatency >= 0 && lat < prevLatency) monotonic = false;
            prevLatency = lat;
        }
        if (! monotonic) { ++failures; std::printf ("  FALLITO: la latenza deve crescere da Fast ad Accurate\n"); }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 6 - inviluppo minimo di sovrapposizione (rischio: "
                 "vuoti periodici sotto -12 semitoni)\n");
    {
        const int win = (int) (0.005 * SR); // ~5 ms
        for (double st : { -12.0, -14.5, -17.0 }) // alpha ~ 0.5, 0.43, 0.35
        {
            const auto out = runShifter (input, f0in, st, 1.0);

            const double minR = minShortTimeRms (out, anaFrom, anaLen, win);
            const double avgR = rms (out, anaFrom, anaLen);
            const double ratio = (avgR > 0.0) ? minR / avgR : 0.0;

            // Un motore che smette di sovrapporre i grani produce vuoti
            // profondi: senza, la variazione naturale del treno di impulsi
            // di test non fa scendere il minimo sotto ~1/4 della media.
            const bool ok = ratio > 0.25;
            if (! ok) ++failures;
            std::printf ("  %+.1f st: RMS minimo/medio = %.3f  %s\n",
                         st, ratio, ok ? "OK" : "FALLITO");
        }
    }

    // -----------------------------------------------------------------------
    std::printf ("\nTEST 7 - tenuta con f0 variabile nel tempo (ricambio "
                 "continuo di epoch)\n");
    {
        const auto swept = makeSweptVowel (150.0, 260.0, 1.5);
        PsolaShifter ps;
        ps.prepare (SR, BLOCK, Stability::numLevels - 1);
        ps.setPitchShiftSemitones (0.0f);

        std::vector<float> out (swept.size(), 0.0f);
        bool finiteThroughout = true;

        for (size_t i = 0; i + BLOCK <= swept.size(); i += BLOCK)
        {
            const double t  = (double) i / (double) swept.size();
            const double f0 = 150.0 + (260.0 - 150.0) * t;
            ps.setInputF0Hz (f0);
            ps.process (&swept[i], &out[i], BLOCK);

            for (int s = 0; s < BLOCK; ++s)
                if (! std::isfinite (out[i + (size_t) s])) finiteThroughout = false;
        }

        const bool ok = finiteThroughout && allFinite (out);
        if (! ok) ++failures;
        std::printf ("  uscita finita per tutta la durata (f0 150->260 Hz): %s\n",
                     ok ? "OK" : "FALLITO");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
