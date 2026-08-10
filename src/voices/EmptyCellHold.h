#pragma once

// FR-17 (sessione 28 — "ribattuto"): decide se una colonna GIA' legata a uno
// slot fisico su una frase viva deve ammutolirsi SUBITO quando la sua cella
// diventa vuota, o se puo' restare com'era (nessun mute, ultimo target
// valido) per un breve tempo di attesa prima di farlo.
//
// Perche' serve: Fase 0 (piano "ciao-claude-stiamo-facendo-crispy-hellman",
// vedi LOG/archivio-s01-s28.md sessione 28) ha misurato col codice VERO
// (OnsetDetector reale + un modello fedele a PhraseScheduler) che sul
// materiale di riferimento dell'utente il gate non si chiude MAI (una sola
// transizione in 8s) e la colonna tracciata lega un fisico slot UNA SOLA
// VOLTA per l'intero file — nessun ri-attacco, nessun furto di slot, nessuno
// slot freddo. Il "ribattuto" riportato dall'utente e' quindi prodotto da
// Voice::setMuted() che si accende e spegne SULLA STESSA frase viva ogni
// volta che la melodia attraversa un grado non compilato del preset a gradi
// parziali, anche solo di passaggio verso un grado compilato successivo — un
// fade-out (ampGlide) seguito da un fade-in e' udibile come "la stessa nota
// suona due volte", esattamente la descrizione dell'utente ("dovrebbe
// cominciare subito con la nuova nota target, senza sporcarla con altre
// intermedie").
//
// La soluzione NON cambia la semantica di FR-17/CLAUDE.md regola 3 (cella
// vuota = voce muta): un vuoto che persiste oltre la soglia continua a
// mutare esattamente come prima. Cambia solo QUANDO scatta il mute: non piu'
// al primo blocco in cui la cella e' vuota, ma solo se resta vuota per
// almeno holdThresholdSamples campioni consecutivi. Sotto soglia, il
// chiamante non deve ne' mutare ne' ri-targettare: la voce continua a
// suonare l'ultimo target valido, cosi' un grado di passaggio non produce
// alcuna interruzione udibile, e quando la melodia arriva al grado
// compilato successivo il target scatta direttamente su quello (mai
// "sporcato" da un target intermedio).
//
// Funzione pura, zero dipendenze (nessun JUCE, nessuna allocazione): puo'
// girare in processBlock (CLAUDE.md regola 1) e si puo' testare in
// isolamento senza linkare PhraseScheduler (che dipende da juce_core via
// VoicePool/HarmonyPreset.h — vedi tests/empty_cell_hold_test.cpp).

struct EmptyCellHoldResult
{
    int emptySamplesAfter;
    bool shouldMuteNow;
};

// emptySamplesBefore: quanti campioni consecutivi questa colonna e' gia'
//   stata vuota su questa frase (0 se l'ultimo blocco aveva un valore).
// numSamplesThisBlock: dimensione del blocco corrente.
// cellHasValueThisBlock: vero se il grado corrente ha un valore compilato in
//   questo blocco (se vero, il conteggio si azzera SEMPRE, a prescindere
//   dalla soglia — un ritorno a un grado compilato annulla l'attesa).
// holdThresholdSamples: quanti campioni consecutivi di vuoto tollerare prima
//   di mutare per davvero (0 = comportamento precedente a questa sessione,
//   muta al primo blocco vuoto).
inline EmptyCellHoldResult stepEmptyCellHold (int emptySamplesBefore, int numSamplesThisBlock,
                                              bool cellHasValueThisBlock, int holdThresholdSamples) noexcept
{
    if (cellHasValueThisBlock)
        return { 0, false };

    const int emptySamplesAfter = emptySamplesBefore + numSamplesThisBlock;
    return { emptySamplesAfter, emptySamplesAfter >= holdThresholdSamples };
}
