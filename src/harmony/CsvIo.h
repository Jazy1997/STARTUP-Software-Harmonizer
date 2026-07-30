#pragma once

#include "HarmonyPreset.h"
#include <optional>

namespace harmony::CsvIo
{
    // Formato (FR-03): riga di intestazione con i 12 gradi (0..11, ignorata in
    // lettura), poi 8 righe (una per voce) x 12 colonne (una per grado).
    // Cella vuota = stringa vuota (voce muta); "0" = unisono. Pensato per
    // essere prodotto e modificato in un foglio di calcolo.
    juce::String tableToCsv (const Table& table);

    // Ritorna nullopt se il testo non ha il numero di righe/colonne attese.
    std::optional<Table> parseCsv (const juce::String& csvText);
}
