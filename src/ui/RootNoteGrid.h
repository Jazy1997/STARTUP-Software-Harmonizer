#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Griglia cromatica dei 12 pulsanti per la selezione della fondamentale
// (PRD §8.1/FR-15, terzo slice di M5 dopo la tabella 12x8 di sessione 15 e
// la lista preset drag&drop di sessione 21). Sostituisce la ComboBox
// dell'editor minimo (M0/M1/M2).
//
// Sessione 25 (PRD-UI.md §3/FR-75): reshape da 1x12 a 2 colonne x 6 righe,
// indice = riga*2 + colonna (riga 0 = C/C#, riga 1 = D/D#, ... riga 5 =
// A#/B) — API pubblica invariata, cambia solo come cellAt()/paint()
// calcolano la geometria.
//
// Stesso principio "nessuna copia cache" gia' usato da PresetListEditor/
// PresetTableEditor: i 12 nomi nota si leggono dal vivo da
// AudioParameterChoice::choices in paint(), non c'e' testo che possa
// restare "stantio" rispetto al parametro APVTS.
namespace ui
{
    class RootNoteGrid : public juce::Component
    {
    public:
        explicit RootNoteGrid (HarmonizerAudioProcessor& processor);

        // Aggiorna SOLO l'evidenziazione visiva (indice 0-11) — non scrive
        // mai il parametro APVTS: stesso principio di
        // PresetListEditor::setSelectedIndex, PluginEditor resta l'unica
        // fonte di verita' sulla selezione (vedi selectRootNote()).
        void setSelectedIndex (int pitchClass);
        int getSelectedIndex() const noexcept { return selectedIndex; }

        // L'utente ha cliccato una cella.
        std::function<void (int pitchClass)> onNoteClicked;

        void paint (juce::Graphics&) override;

    private:
        void mouseUp (const juce::MouseEvent&) override;

        // Indice di cella sotto la coordinata (x,y) locale, o -1 se fuori.
        // Griglia 2 colonne x 6 righe (FR-75): indice = riga*2 + colonna.
        int cellAt (int x, int y) const noexcept;

        HarmonizerAudioProcessor& processorRef;
        int selectedIndex = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RootNoteGrid)
    };
}
