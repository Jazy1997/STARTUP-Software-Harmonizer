// Verifica numerica del parser di input per l'editor tabella preset
// (sessione M5, §8.2 del PRD). Nessuna dipendenza JUCE: si compila ed
// esegue in meno di un secondo, stesso principio di pitch_latch_test.cpp
// e glide_test.cpp.
//
// Compilazione: g++ -O2 -std=c++20 -Isrc tests/cell_input_parser_test.cpp -o cell_input_parser_test

#include "ui/CellInputParser.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("  %-70s %s\n", description, condition ? "OK" : "FALLITO");
        if (! condition)
            ++failures;
    }

    // Vero solo se l'input e' stato ACCETTATO come cella vuota (voce muta).
    bool isAcceptedEmpty (const std::optional<std::optional<int>>& r)
    {
        return r.has_value() && ! r->has_value();
    }

    // Vero solo se l'input e' stato ACCETTATO con esattamente questo valore.
    bool isAcceptedValue (const std::optional<std::optional<int>>& r, int expected)
    {
        return r.has_value() && r->has_value() && **r == expected;
    }

    bool isRejected (const std::optional<std::optional<int>>& r)
    {
        return ! r.has_value();
    }
}

int main()
{
    std::printf ("TEST 1 - stringa vuota o solo spazi -> cella vuota (voce muta), MAI 0\n");
    {
        check (isAcceptedEmpty (ui::parseCellInput ("")), "\"\" -> vuota");
        check (isAcceptedEmpty (ui::parseCellInput ("   ")), "\"   \" -> vuota (solo spazi)");
        check (isAcceptedEmpty (ui::parseCellInput ("\t \t")), "tab/spazi misti -> vuota");
    }

    std::printf ("\nTEST 2 - \"0\" e' un valore ESPLICITO (unisono), distinto dalla cella vuota\n"
                 "         (verifica diretta della distinzione di CLAUDE.md regola 3)\n");
    {
        const auto r = ui::parseCellInput ("0");
        check (isAcceptedValue (r, 0), "\"0\" -> Cell{0}, non vuota");
        check (! isAcceptedEmpty (r), "\"0\" NON e' trattato come vuota");
    }

    std::printf ("\nTEST 3 - interi positivi e negativi validi entro il range\n");
    {
        check (isAcceptedValue (ui::parseCellInput ("7"), 7), "\"7\" -> Cell{7}");
        check (isAcceptedValue (ui::parseCellInput ("-5"), -5), "\"-5\" -> Cell{-5}");
        check (isAcceptedValue (ui::parseCellInput ("+3"), 3), "\"+3\" -> Cell{3} (segno esplicito ammesso)");
        check (isAcceptedValue (ui::parseCellInput ("  12  "), 12), "\"  12  \" -> Cell{12} (spazi ai bordi ignorati)");
        check (isAcceptedValue (ui::parseCellInput ("48"), 48), "\"48\" -> Cell{48} (limite superiore incluso)");
        check (isAcceptedValue (ui::parseCellInput ("-48"), -48), "\"-48\" -> Cell{-48} (limite inferiore incluso)");
    }

    std::printf ("\nTEST 4 - fuori range (oltre 4 ottave): rifiutato, non troncato/clampato\n");
    {
        check (isRejected (ui::parseCellInput ("49")), "\"49\" -> rifiutato");
        check (isRejected (ui::parseCellInput ("-49")), "\"-49\" -> rifiutato");
        check (isRejected (ui::parseCellInput ("1200")), "\"1200\" -> rifiutato (refuso plausibile: 12 con uno zero di troppo)");
    }

    std::printf ("\nTEST 5 - testo non numerico: RIFIUTATO esplicitamente, MAI coerto a 0\n"
                 "         (il contrario esatto della trappola in CsvIo::getIntValue())\n");
    {
        check (isRejected (ui::parseCellInput ("abc")), "\"abc\" -> rifiutato");
        check (isRejected (ui::parseCellInput ("12x")), "\"12x\" -> rifiutato (non troncato a 12)");
        check (isRejected (ui::parseCellInput ("x12")), "\"x12\" -> rifiutato");
        check (isRejected (ui::parseCellInput ("--3")), "\"--3\" -> rifiutato (doppio segno)");
        check (isRejected (ui::parseCellInput ("3-3")), "\"3-3\" -> rifiutato");
        check (isRejected (ui::parseCellInput ("-")), "\"-\" -> rifiutato (solo segno, nessuna cifra)");
        check (isRejected (ui::parseCellInput ("1.5")), "\"1.5\" -> rifiutato (non intero)");
    }

    std::printf ("\n===================================\n");
    std::printf ("%s  (%d verifiche fallite)\n",
                 failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI", failures);

    return failures == 0 ? 0 : 1;
}
