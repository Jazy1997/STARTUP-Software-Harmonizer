#include "VoicePool.h"
#include <algorithm>

void VoicePool::prepare (double sampleRate, int maxBlockSize, int stabilityLevel)
{
    storedSampleRate = sampleRate;
    storedMaxBlockSize = maxBlockSize;

    for (auto& voice : voices)
        voice.prepare (sampleRate, maxBlockSize, stabilityLevel);
}

void VoicePool::reset()
{
    for (auto& voice : voices)
        voice.reset();
}

void VoicePool::setVoiceMode (int voiceIndex, ShiftMode mode)
{
    if (voiceIndex >= 0 && voiceIndex < maxVoices)
        voices[(size_t) voiceIndex].setMode (mode);
}

void VoicePool::setGlideTimeMs (float ms)
{
    for (auto& voice : voices)
        voice.setGlideTimeMs (ms);
}

void VoicePool::requestStabilityChange (int newStabilityLevel)
{
    // Costruzione (con allocazione) delle nuove istanze: SOLO message thread.
    std::vector<std::unique_ptr<PitchShifter>> fresh;
    fresh.reserve ((size_t) maxVoices);
    for (int i = 0; i < maxVoices; ++i)
    {
        auto s = createDefaultPitchShifter();
        s->prepare (storedSampleRate, storedMaxBlockSize, newStabilityLevel);
        fresh.push_back (std::move (s));
    }

    const juce::SpinLock::ScopedLockType sl (pendingLock);
    // Se esisteva gia' una richiesta non ancora applicata dall'audio thread,
    // i suoi shifter non sono mai stati usati: si distruggono qui (message
    // thread, va bene) semplicemente sovrascrivendo il vector.
    pendingShifters = std::move (fresh);
    hasPendingChange = true;
}

void VoicePool::collectGarbage()
{
    std::vector<std::unique_ptr<PitchShifter>> toDelete;
    {
        const juce::SpinLock::ScopedLockType sl (retiredLock);
        toDelete = std::move (retiredShifters);
        retiredShifters.clear();
    }
    // toDelete esce dallo scope qui sotto: distruzione sul message thread.
}

bool VoicePool::process (const float* monoIn,
                          float* mixOutput,
                          int numSamples,
                          const std::array<harmony::Cell, harmony::numVoices>& offsets,
                          int numActiveVoices,
                          int quantizedPlayedNote,
                          float continuousInputMidiNote,
                          bool applyStabilityChangesNow)
{
    bool appliedChange = false;

    if (applyStabilityChangesNow)
    {
        std::vector<std::unique_ptr<PitchShifter>> toApply;
        {
            const juce::SpinLock::ScopedLockType sl (pendingLock);
            if (hasPendingChange)
            {
                toApply = std::move (pendingShifters);
                pendingShifters.clear();
                hasPendingChange = false;
            }
        }

        if (toApply.size() == (size_t) maxVoices)
        {
            // Scambio senza allocazione/deallocazione (vedi Voice::swapShifterNoAlloc):
            // dopo questo ciclo, toApply contiene i VECCHI shifter.
            for (int v = 0; v < maxVoices; ++v)
                voices[(size_t) v].swapShifterNoAlloc (toApply[(size_t) v]);

            const juce::SpinLock::ScopedLockType sl (retiredLock);
            for (auto& old : toApply)
                retiredShifters.push_back (std::move (old));

            appliedChange = true;
        }
    }

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
        voices[(size_t) v].setTargetOffsetSemitones ((float) *cell);
        voices[(size_t) v].processAdd (monoIn, mixOutput, numSamples, quantizedPlayedNote, continuousInputMidiNote);
    }

    return appliedChange;
}
