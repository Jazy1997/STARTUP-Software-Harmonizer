#include "SpectralShifter.h"

void SpectralShifter::prepare (double sampleRate, int /*maxBlockSize*/)
{
    stretch.presetDefault (1, (float) sampleRate);
    stretch.setTransposeSemitones (currentSemitones);
}

void SpectralShifter::reset()
{
    stretch.reset();
}

void SpectralShifter::setPitchShiftSemitones (float semitones)
{
    if (semitones == currentSemitones)
        return;

    currentSemitones = semitones;
    stretch.setTransposeSemitones (semitones);
}

int SpectralShifter::getLatencySamples() const
{
    return stretch.inputLatency() + stretch.outputLatency();
}

void SpectralShifter::process (const float* in, float* out, int numSamples)
{
    const float* const inputs[1] = { in };
    float* const outputs[1] = { out };
    stretch.process (inputs, numSamples, outputs, numSamples);
}

std::unique_ptr<PitchShifter> createDefaultPitchShifter()
{
    return std::make_unique<SpectralShifter>();
}
