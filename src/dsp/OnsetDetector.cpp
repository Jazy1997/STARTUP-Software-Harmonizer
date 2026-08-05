#include "OnsetDetector.h"

#include <q/fx/envelope.hpp>
#include <q/fx/onset_gate.hpp>
#include <q/support/literals.hpp>
#include <cmath>

using namespace cycfi::q::literals;

struct OnsetDetector::Impl
{
    explicit Impl (double sampleRate)
        : envelopeFollower (50_ms, (float) sampleRate)
        // Sessione 18: release_threshold (il terzo parametro, -36dB
        // originale) e' GIA' un parametro separato da onset_threshold/
        // slope_threshold in cycfi::q::onset_gate (vedi onset_gate.hpp) —
        // "attacco" e "rilascio" non erano confusi, ma -36dB si e' rivelato
        // troppo aggressivo su materiale reale: misurato con
        // tests/real_export_probe.cpp su "SAMPLE TEST/Export V1.wav" che il
        // decadimento naturale di una nota (E4, Silk Horns) attraversa
        // -36dB di picco mentre il segnale e' ancora chiaramente udibile e
        // perfettamente periodico (periodicita' 1.000) nel dry — il gate si
        // chiude, la voce armonizzata si ammutolisce (dissolvenza 8ms) e
        // tace per il resto della coda della nota, un buco reale (t≈5.95-
        // 6.0s nel file di test, periodicita' del wet crollata a 0.000
        // mentre il dry e' ancora a 1.000). Abbassato a -45dB (9dB piu'
        // permissivo, solo sul rilascio): lascia la voce seguire piu' a
        // lungo nel decadimento naturale, SENZA toccare onset_threshold
        // (-24dB) ne' slope_threshold (-30dB), che restano quelli che
        // decidono se un nuovo attacco apre il gate — non tarati qui.
        // Compromesso da conoscere: piu' il rilascio e' permissivo, piu'
        // tardi si richiude il gate fra due note molto legate — se troppo
        // basso, una nota successiva molto ravvicinata rischierebbe di non
        // essere piu' riconosciuta come un nuovo attacco (stessa classe di
        // bug delle sessioni 10-12, corsa onset/pitch). -45dB e' una scelta
        // moderata (non il minimo tecnico), da confermare all'ascolto.
        , gate (-24_dB, -30_dB, -45_dB, 10_ms, (float) sampleRate)
    {
    }

    // Uso documentato in Cycfi Q (noise_gate.hpp): l'inviluppo di picco
    // alimenta il gate, che espone lo stato aperto/chiuso.
    cycfi::q::peak_envelope_follower envelopeFollower;
    cycfi::q::onset_gate gate;
};

OnsetDetector::OnsetDetector() = default;
OnsetDetector::~OnsetDetector() = default;

void OnsetDetector::prepare (double sampleRate)
{
    impl = std::make_unique<Impl> (sampleRate);
    gateOpen = false;
}

void OnsetDetector::reset()
{
    gateOpen = false;
}

bool OnsetDetector::pushSample (float sample) noexcept
{
    if (! impl)
        return false;

    const float env = impl->envelopeFollower (std::abs (sample));
    const bool newState = impl->gate (env);

    const bool onsetEvent = newState && ! gateOpen;
    gateOpen = newState;
    return onsetEvent;
}
