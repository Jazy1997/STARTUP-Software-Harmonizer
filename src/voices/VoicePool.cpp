#include "VoicePool.h"

void VoicePool::prepare (int numSlots, double sampleRate, int maxBlockSize, int stabilityLevel)
{
    storedSampleRate = sampleRate;
    storedMaxBlockSize = maxBlockSize;

    slots = std::vector<Voice> ((size_t) numSlots);
    for (auto& slot : slots)
        slot.prepare (sampleRate, maxBlockSize, stabilityLevel);
}

void VoicePool::reset()
{
    for (auto& slot : slots)
        slot.reset();
}

void VoicePool::requestStabilityChange (int newStabilityLevel)
{
    std::vector<std::unique_ptr<PitchShifter>> fresh;
    fresh.reserve (slots.size());
    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto s = createDefaultPitchShifter();
        s->prepare (storedSampleRate, storedMaxBlockSize, newStabilityLevel);
        fresh.push_back (std::move (s));
    }

    const juce::SpinLock::ScopedLockType sl (pendingLock);
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

bool VoicePool::applyPendingStabilityChangeIfSafe (bool applyNow)
{
    if (! applyNow)
        return false;

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

    if (toApply.size() != slots.size())
        return false;

    for (size_t i = 0; i < slots.size(); ++i)
        slots[i].swapShifterNoAlloc (toApply[i]);

    const juce::SpinLock::ScopedLockType sl (retiredLock);
    for (auto& old : toApply)
        retiredShifters.push_back (std::move (old));

    return true;
}
