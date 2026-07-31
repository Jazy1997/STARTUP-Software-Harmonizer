#pragma once

// Regola di precedenza CC vs automazione host (FR-36/37/38). Logica pura,
// nessuna dipendenza JUCE: e' cio' che la rende testabile in isolamento
// (vedi tests/override_manager_test.cpp), come psola_test per il DSP.
//
// Un CC in ingresso mette l'automazione della DAW in stato di "override" per
// quel parametro: finche' l'override e' attivo, il valore effettivo usato
// dal plugin e' quello derivato dall'ultimo CC ricevuto, non quello scritto
// dall'automazione host, anche se l'host continua a inviare nuovi valori
// automatizzati nel frattempo. L'override si revoca SOLO allo stop del
// transport (FR-36) — mai in modo continuo, e mai in standalone (FR-37, non
// esiste un transport: il CC comanda sempre, senza revoca).
//
// FR-38 (l'ultimo CC ricevuto vince, in caso di piu' sorgenti in conflitto)
// e' soddisfatto per costruzione: ogni CC valido sovrascrive semplicemente
// il valore precedente, senza bisogno di logica dedicata.
class OverrideManager
{
public:
    struct CcEvent
    {
        bool present = false;
        int value = 0; // semantica dipendente dalla funzione (vedi CcRouter)
    };

    struct CcEvents
    {
        CcEvent root;
        CcEvent preset;
        CcEvent bypass;
    };

    struct Effective
    {
        int rootPitchClass;  // 0..11
        int presetOneBased;  // 1-based, stessa convenzione del parametro presetIndex
        bool bypassed;
    };

    // Da chiamare una volta per blocco, PRIMA di resolve(): applica i nuovi
    // eventi CC di questo blocco allo stato di override (se presenti) e
    // ritorna i valori effettivi da usare a valle. hostRoot/hostPreset/
    // hostBypass sono i valori correnti dei parametri APVTS (cio' che userebbe
    // il plugin se non ci fosse nessun override attivo).
    Effective resolve (const CcEvents& events,
                        int hostRootPitchClass,
                        int hostPresetOneBased,
                        bool hostBypass) noexcept;

    // FR-36: da chiamare sul fronte di discesa del transport (era in
    // riproduzione, ora e' fermo). Non chiamare mai in standalone (FR-37).
    void clearOverrides() noexcept;

private:
    struct State
    {
        bool active = false;
        int value = 0;
    };

    State rootState;
    State presetState;
    State bypassState;
};
