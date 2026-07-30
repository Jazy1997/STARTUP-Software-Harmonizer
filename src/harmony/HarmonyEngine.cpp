#include "HarmonyEngine.h"

namespace harmony::HarmonyEngine
{
    int degreeOf (int playedMidiNote, int rootPitchClass) noexcept
    {
        const int rel = (playedMidiNote - rootPitchClass) % numDegrees;
        return rel < 0 ? rel + numDegrees : rel;
    }

    const std::array<Cell, numVoices>& getOffsets (const Preset& preset,
                                                     int playedMidiNote,
                                                     int rootPitchClass) noexcept
    {
        const int d = degreeOf (playedMidiNote, rootPitchClass);
        return preset.table[(size_t) d];
    }
}
