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
        , gate (-24_dB, -30_dB, -36_dB, 10_ms, (float) sampleRate)
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
