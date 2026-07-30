#include "CsvIo.h"

namespace harmony::CsvIo
{
    juce::String tableToCsv (const Table& table)
    {
        juce::StringArray lines;

        juce::StringArray header;
        for (int d = 0; d < numDegrees; ++d)
            header.add (juce::String (d));
        lines.add (header.joinIntoString (","));

        for (int v = 0; v < numVoices; ++v)
        {
            juce::StringArray row;
            for (int d = 0; d < numDegrees; ++d)
            {
                const auto& cell = table[(size_t) d][(size_t) v];
                row.add (cell.has_value() ? juce::String (*cell) : juce::String());
            }
            lines.add (row.joinIntoString (","));
        }

        return lines.joinIntoString ("\n");
    }

    std::optional<Table> parseCsv (const juce::String& csvText)
    {
        auto lines = juce::StringArray::fromLines (csvText);
        lines.removeEmptyStrings();

        if (lines.size() < 1 + numVoices)
            return std::nullopt;

        Table table {};

        // lines[0] e' l'intestazione (gradi 0..11), ignorata in lettura.
        for (int v = 0; v < numVoices; ++v)
        {
            const auto row = juce::StringArray::fromTokens (lines[1 + v], ",", "");
            if (row.size() < numDegrees)
                return std::nullopt;

            for (int d = 0; d < numDegrees; ++d)
            {
                const auto token = row[d].trim();
                table[(size_t) d][(size_t) v] = token.isEmpty()
                    ? Cell (std::nullopt)
                    : Cell (token.getIntValue());
            }
        }

        return table;
    }
}
