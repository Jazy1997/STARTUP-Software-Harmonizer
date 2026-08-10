// Strumento diagnostico OFFLINE minimale — sessione 28 (piano "ribattuto"):
// dump dell'inviluppo RMS FINE (hop in ms, non i 10ms/30ms del confronto
// pitch-per-frame di real_export_probe.cpp) di UN SOLO file WAV su una
// finestra di tempo, per misurare con precisione buchi/gap gia' individuati
// a occhio in un DAW (screenshot) invece di stimarli visivamente. Nessuna
// dipendenza JUCE.
//
// NON in ctest: dipende da file WAV esterni forniti dall'utente.
//
// Uso: envelope_probe <file.wav> <startSec> <endSec> [hopMs=1.0]
//
// Compilazione (vedi anche il target CMake `envelope_probe`):
//   g++ -O2 -std=c++20 -Isrc tests/envelope_probe.cpp -o envelope_probe

#include "SampleAnalysis.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf ("Uso: envelope_probe <file.wav> <startSec> <endSec> [hopMs=1.0]\n");
        return 1;
    }

    const std::string path = argv[1];
    const double startSec = std::stod (argv[2]);
    const double endSec = std::stod (argv[3]);
    const double hopMs = argc > 4 ? std::stod (argv[4]) : 1.0;

    WavFile wav;
    std::string error;
    if (! readWav (path, wav, error))
    {
        std::printf ("Errore leggendo '%s': %s\n", path.c_str(), error.c_str());
        return 1;
    }

    const auto mono = downmix (wav);
    const double sr = wav.sampleRate;
    const int hop = std::max (1, (int) std::lround (hopMs * sr / 1000.0));

    std::printf ("File: %s (%.0fHz, %.3fs)\n", path.c_str(), sr, (double) mono.size() / sr);
    std::printf ("Inviluppo RMS, hop=%dcampioni (%.2fms), %.3f-%.3fs:\n\n", hop, 1000.0 * hop / sr, startSec, endSec);
    std::printf ("%10s  %10s  %8s\n", "t", "rms", "dBFS");

    const int fromSample = (int) (startSec * sr);
    const int toSample = std::min ((int) mono.size(), (int) (endSec * sr));

    for (int s = fromSample; s + hop <= toSample; s += hop)
    {
        const double r = rms (mono, s, hop);
        const double db = r > 1.0e-9 ? 20.0 * std::log10 (r) : -999.0;
        std::printf ("%10.4f  %10.6f  %8.1f\n", (double) s / sr, r, db);
    }

    return 0;
}
