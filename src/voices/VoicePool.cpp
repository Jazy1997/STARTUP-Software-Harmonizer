#include "VoicePool.h"
#include <algorithm>

void VoicePool::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& voice : voices)
        voice.prepare (sampleRate, maxBlockSize);
}

void VoicePool::reset()
{
    for (auto& voice : voices)
        voice.reset();
}

void VoicePool::process (const float* monoIn,
                          float* mixOutput,
                          int numSamples,
                          const std::array<harmony::Cell, harmony::numVoices>& offsets,
                          int numActiveVoices)
{
    std::fill (mixOutput, mixOutput + numSamples, 0.0f);

    for (int v = 0; v < maxVoices; ++v)
    {
        const bool withinActiveCount = v < numActiveVoices;
        const auto& cell = offsets[(size_t) v];

        if (! withinActiveCount || ! cell.has_value())
        {
            voices[(size_t) v].setMuted (true);
            continue;
        }

        voices[(size_t) v].setMuted (false);
        voices[(size_t) v].setTargetSemitones ((float) *cell);
        voices[(size_t) v].processAdd (monoIn, mixOutput, numSamples);
    }
}
