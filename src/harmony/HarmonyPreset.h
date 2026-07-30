#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <optional>

namespace harmony
{
    constexpr int numDegrees = 12;
    constexpr int numVoices = 8;

    // 0 e cella vuota sono stati distinti (CLAUDE.md regola 3 / FR-01).
    // std::nullopt == voce muta su quel grado; un intero (anche 0) == offset in semitoni.
    using Cell = std::optional<int>;
    using Table = std::array<std::array<Cell, numVoices>, numDegrees>;

    struct Preset
    {
        // ID interno stabile (FR-10): non cambia mai, indipendente dalla
        // posizione in lista. Il CC resta posizionale (FR-05) — vedi PresetLibrary.
        juce::Uuid id;
        juce::String name;
        Table table {};
    };
}
