#pragma once

#include "HarmonyPreset.h"

namespace harmony
{
    // Puro calcolo, nessuno stato: la lista dei preset e la loro gestione
    // vivono in PresetLibrary. HarmonyEngine sa solo leggere una tabella.
    namespace HarmonyEngine
    {
        // d = (notaMIDI - fondamentale) mod 12 — invariante rispetto all'ottava (FR-16).
        int degreeOf (int playedMidiNote, int rootPitchClass) noexcept;

        const std::array<Cell, numVoices>& getOffsets (const Preset& preset,
                                                         int playedMidiNote,
                                                         int rootPitchClass) noexcept;
    }
}
