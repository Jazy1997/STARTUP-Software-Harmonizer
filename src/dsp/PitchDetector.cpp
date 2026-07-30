#include "PitchDetector.h"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#include <q/support/pitch.hpp>

using namespace cycfi::q::literals;

struct PitchDetector::Impl
{
    Impl (double sampleRate)
        : detector (60_Hz, 1500_Hz, (float) sampleRate, -35_dB)
    {
    }

    cycfi::q::pitch_detector detector;
};

// Soglia di confidenza minima sotto la quale il segnale e' considerato non
// intonato (silenzio, rumore, transiente). Placeholder: FR-20 (fade invece di
// artefatti) e la calibrazione fine restano da fare in M1.
static constexpr float confidenceThreshold = cycfi::q::pitch_detector::min_periodicity;

PitchDetector::PitchDetector() = default;
PitchDetector::~PitchDetector() = default;

void PitchDetector::prepare (double sampleRate)
{
    impl = std::make_unique<Impl> (sampleRate);
    currentMidiNote = -1.0f;
    currentConfidence = 0.0f;
}

void PitchDetector::reset()
{
    if (impl)
        impl->detector.reset();
    currentMidiNote = -1.0f;
    currentConfidence = 0.0f;
}

bool PitchDetector::pushSample (float sample) noexcept
{
    if (! impl)
        return false;

    const bool updated = impl->detector (sample);
    if (updated)
    {
        currentConfidence = impl->detector.periodicity();
        const float freqHz = impl->detector.get_frequency();
        if (freqHz > 0.0f)
        {
            const cycfi::q::pitch p { cycfi::q::frequency { freqHz } };
            currentMidiNote = cycfi::q::as_float (p);
        }
    }
    return updated;
}

bool PitchDetector::hasStableSignal() const noexcept
{
    return currentMidiNote >= 0.0f && currentConfidence >= confidenceThreshold;
}
