#include "Voice.h"

void Voice::prepare (double sampleRate, int maxBlockSize)
{
    shifter = createDefaultPitchShifter();
    shifter->prepare (sampleRate, maxBlockSize);
    scratch.assign ((size_t) maxBlockSize, 0.0f);
}

void Voice::reset()
{
    if (shifter)
        shifter->reset();
}

int Voice::getLatencySamples() const
{
    return shifter ? shifter->getLatencySamples() : 0;
}

void Voice::processAdd (const float* monoIn, float* mixOutput, int numSamples)
{
    if (muted || shifter == nullptr)
        return;

    shifter->setPitchShiftSemitones (targetSemitones);
    shifter->process (monoIn, scratch.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
        mixOutput[i] += scratch[(size_t) i];
}
