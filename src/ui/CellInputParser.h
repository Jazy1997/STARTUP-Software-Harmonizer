#pragma once

#include <optional>
#include <string>
#include <cctype>

// Parser validato per il testo digitato in una cella dell'editor tabella
// preset (sessione M5, §8.2 del PRD). Logica pura, nessuna dipendenza JUCE
// ne' da harmony/ (stesso principio di Glide.h/PitchLatch.h): testabile in
// isolamento, vedi tests/cell_input_parser_test.cpp.
//
// Perche' esiste: harmony::CsvIo::parseCsv usa juce::String::getIntValue(),
// che ritorna silenziosamente 0 per qualunque testo non numerico ("abc",
// "12x", "--3" diventerebbero tutti 0 = unisono) — solo la stringa vuota
// diventa cella muta. Confondere un refuso di battitura con un unisono vero
// e' esattamente la confusione che CLAUDE.md regola 3 vieta ("0 e cella
// vuota sono cose diverse... confonderli rompe la semantica delle voci
// mute"). Un editor con digitazione diretta ha bisogno di RIFIUTARE
// l'input invalido, non di coercerlo — questa funzione fa quello, CsvIo
// resta invariata (il suo contratto di file esterno e' un problema diverso).
namespace ui
{
    // Esito a due livelli, entrambi std::optional:
    //  - il livello ESTERNO e' nullopt quando il testo va RIFIUTATO (l'editor
    //    non deve scrivere nulla e deve ripristinare il valore precedente);
    //  - se il livello esterno ha un valore, quel valore e' la Cell
    //    risultante: nullopt (interno) = voce muta (input vuoto/solo spazi),
    //    altrimenti l'offset in semitoni (harmony::Cell ha la stessa identica
    //    forma std::optional<int> — nessuna dipendenza diretta da harmony/
    //    qui, il chiamante converte).
    inline std::optional<std::optional<int>> parseCellInput (const std::string& rawText)
    {
        size_t start = 0, end = rawText.size();
        while (start < end && std::isspace ((unsigned char) rawText[start])) ++start;
        while (end > start && std::isspace ((unsigned char) rawText[end - 1])) --end;
        const std::string text = rawText.substr (start, end - start);

        if (text.empty())
            return std::optional<int> {}; // valido: cella vuota, voce muta

        size_t i = 0;
        bool negative = false;
        if (text[i] == '+' || text[i] == '-')
        {
            negative = (text[i] == '-');
            ++i;
        }

        if (i >= text.size()) // solo un segno, nessuna cifra dopo
            return std::nullopt;

        long long value = 0;
        for (; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c < '0' || c > '9')
                return std::nullopt; // carattere non numerico: rifiuta, MAI troncare/coercere

            value = value * 10 + (c - '0');
            if (value > 1000000) // guardia anti-overflow, ben oltre il range valido sotto
                return std::nullopt;
        }

        if (negative)
            value = -value;

        // Range sano (4 ottave): non un vincolo del motore (harmony::Cell
        // accetta qualunque int), solo una protezione contro refusi
        // grossolani (es. "1200" invece di "12") nell'editor.
        constexpr long long kMaxSemitones = 48;
        if (value < -kMaxSemitones || value > kMaxSemitones)
            return std::nullopt;

        return std::optional<int> { (int) value };
    }
}
