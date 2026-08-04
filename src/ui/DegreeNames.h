#pragma once

#include "../harmony/HarmonyPreset.h"

// Nomi di grado leggibili per le intestazioni di colonna dell'editor tabella
// preset (PRD §8.2: "intestazioni di colonna che mostrano il grado in forma
// leggibile"). Solo presentazione — HarmonyEngine e PresetLibrary continuano
// a ragionare esclusivamente in interi 0-11 (d = (notaMIDI-fondamentale)
// mod 12), questa tabella non e' letta da nessun codice DSP.
//
// Convenzione standard di teoria (intervalli rispetto alla fondamentale):
// R, b2, 2, b3, 3, 4, b5, 5, b6, 6, b7, 7.
namespace ui
{
    inline const char* degreeName (int degree) noexcept
    {
        static constexpr const char* names[harmony::numDegrees] =
        {
            "R", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"
        };

        return (degree >= 0 && degree < harmony::numDegrees) ? names[degree] : "?";
    }
}
