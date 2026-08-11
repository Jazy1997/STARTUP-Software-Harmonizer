#pragma once

#include <array>

// B-14/D-18 (sessione 32) — gli strumenti proposti all'utente e la nota piu'
// grave che ciascuno puo' produrre.
//
// A cosa serve davvero: quella nota decide la finestra d'analisi di Cycfi Q
// (vedi PitchDetector.h), e quindi quanto tardi l'offset giusto raggiunge il
// motore quando la melodia cambia nota. Misurato in s.32 su materiale reale, a
// 1024 campioni di buffer: 79 ms con la vecchia soglia fissa di 60 Hz contro
// 44 ms a E2. Non e' una preferenza, e' il compromesso fra gamma e prontezza,
// ed e' per questo che lo sceglie l'utente.
//
// IL COMPROMESSO HA DUE FACCE, non una. Una finestra corta e' piu' pronta ma
// anche piu' RUMOROSA: il rilevatore sbanda piu' spesso, e su un preset con
// tutti i gradi compilati ogni sbandata diventa un offset sbagliato udibile
// (la famiglia di B-13). Misurato in s.32 sul materiale di prova, alle voci
// sopra Eb Alto Sax compaiono 1-3 offset di passaggio, e NESSUNA taratura
// dell'attesa di PitchLatch li elimina — provato fino a 25 ms di attesa
// assoluta. Da qui il default a Voice Male (E2), l'impostazione piu' pronta
// che resta pulita su tutte e quattro le tabelle di prova.
//
// ATTENZIONE PRIMA DI CONCLUDERNE TROPPO: quella misura viene da un solo file,
// che suona C4-D4-E4 — il registro GRAVE per un flauto o un soprano sax.
// Quando uno di quegli strumenti suona nel proprio registro la finestra corta
// potrebbe comportarsi benissimo: non abbiamo materiale per dirlo. Le voci
// acute restano quindi in lista, con il limite annotato in BUGS.md § B-14 e
// un export dedicato da chiedere all'utente per verificarle davvero.
//
// L'ORDINE E' PARTE DELL'INFORMAZIONE: dal piu' acuto al piu' grave, cosi'
// scendendo nella lista si vede crescere il ritardo. Aggiungere uno strumento
// significa INSERIRLO al posto giusto, non appenderlo in fondo — ed e' per
// questo che il parametro serializza la nota MIDI e non la posizione in lista
// (D-18): riordinare qui non tocca nessuna sessione gia' salvata.
//
// Le note vengono dall'utente (s.32), non da una tabella generica: sono le
// note piu' gravi REALMENTE suonabili sugli strumenti che ha in mente.
// "Voice Female" e "Bb Trumpet" condividono E3: due etichette, stesso valore
// serializzato — la UI evidenzia la prima che corrisponde.
namespace ui
{
    struct InstrumentRange
    {
        const char* name;
        const char* noteName;
        int lowestNoteMidi;
    };

    inline constexpr std::array<InstrumentRange, 10> kInstrumentRanges { {
        { "Flute",           "C4",  60 }, // 261.6 Hz
        { "Bb Soprano Sax",  "Ab3", 56 }, // 207.7 Hz
        { "Voice Female",    "E3",  52 }, // 164.8 Hz
        { "Bb Trumpet",      "E3",  52 }, // 164.8 Hz
        { "Eb Alto Sax",     "Db3", 49 }, // 138.6 Hz
        { "Bb Tenor Sax",    "Ab2", 44 }, // 103.8 Hz
        { "Voice Male",      "E2",  40 }, //  82.4 Hz — default (vedi sopra)
        { "Bass Clarinet",   "Db2", 37 }, //  69.3 Hz
        { "Eb Baritone Sax", "C2",  36 }, //  65.4 Hz
        { "Trombone",        "Ab1", 32 }, //  51.9 Hz
    } };
}
