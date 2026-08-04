#pragma once

#include "../PluginProcessor.h"
#include "../harmony/HarmonyPreset.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Editor tabella preset armonici (sessione M5, PRD §8.2): griglia 12 colonne
// (gradi) x 8 righe (voci) interamente editabile a mano. Prima di questa
// sessione l'unico modo di popolare un preset era importare un CSV esterno
// (harmony::CsvIo) — il bottone "Add" nell'editor principale crea un preset
// con harmony::Table{} (96 celle mute) senza alcun modo di modificarlo
// dall'interfaccia.
namespace ui
{
    // Indicizzazione: stessa di harmony::Table, table[grado][voce] (12
    // esterno, 8 interno) — la griglia visiva (colonne=gradi, righe=voci)
    // rispecchia direttamente l'indicizzazione in memoria, nessuna
    // traduzione di indici da tenere a mente qui o altrove.
    class PresetTableEditor : public juce::Component
    {
    public:
        explicit PresetTableEditor (HarmonizerAudioProcessor& processor);

        // Da chiamare ogni volta che il preset da mostrare cambia (nuova
        // selezione, import CSV, load global, add/duplicate/delete...) — vedi
        // PluginEditor.cpp per tutti i punti di chiamata. Rilegge l'intera
        // tabella dal modello e ripopola le 96 celle senza notificare
        // (nessun trigger di onTextChange durante il refresh).
        void showPreset (int presetIndex);

        void resized() override;

    private:
        // Valida il testo digitato in una cella (CellInputParser.h) e, se
        // accettato, scrive nel modello via PresetLibrary::setCell; se
        // rifiutato ripristina il testo canonico precedente (mai lasciare
        // visualizzato un refuso come se fosse stato accettato — CLAUDE.md
        // regola 3).
        void commitCell (int degree, int voice);

        HarmonizerAudioProcessor& processorRef;
        int currentPresetIndex = -1;

        std::array<juce::Label, harmony::numDegrees> columnHeaders;
        std::array<juce::Label, harmony::numVoices> rowHeaders;
        std::array<std::array<juce::Label, harmony::numVoices>, harmony::numDegrees> cells;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetTableEditor)
    };
}
