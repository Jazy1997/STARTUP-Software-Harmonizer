#pragma once

#include "../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Lista dei preset armonici con riordino drag&drop (sessione M5, PRD §8.2/FR-06/FR-07).
// Sostituisce la ComboBox + bottoni Su/Giu' dell'editor minimo (M0/M1/M2): quello era
// esplicitamente un placeholder in attesa di questo componente, vedi PluginEditor.h.
//
// Niente Label figlie per riga (a differenza di PresetTableEditor, che ha una griglia a
// dimensione FISSA 12x8): qui il numero di righe varia da 1 a
// harmony::PresetLibrary::maxPresets (128) a ogni add/duplicate/delete, quindi si disegna
// tutto a mano in paint() leggendo il modello dal vivo — nessun testo puo' restare "stantio"
// perche' non esiste alcuna copia cache del nome/CC da tenere sincronizzata.
namespace ui
{
    class PresetListEditor : public juce::Component
    {
    public:
        explicit PresetListEditor (HarmonizerAudioProcessor& processor);

        // Pubblica: PluginEditor la usa sia per dimensionare l'area visibile
        // del Viewport in resized() sia per lo scroll-into-view del preset
        // selezionato — un solo posto dove vive il numero, nessun commento
        // "deve restare in sync" da tenere a mano.
        static constexpr int rowHeight = 22;

        // Rilegge il numero di preset dal modello, ridimensiona il componente
        // (contentWidth x numPreset*rowHeight, minimo una riga) e forza un
        // repaint. Da chiamare dopo qualunque mutazione della libreria che
        // possa cambiare il conteggio o i nomi (add/duplicate/delete/import/
        // load/rename) — un riordino puro (drag) NON ne ha bisogno, si
        // ripaint da solo.
        void refresh();

        // Il genitore la chiama da resized() quando la larghezza disponibile
        // cambia (il Viewport puo' restringere la larghezza utile per fare
        // posto alla scrollbar verticale).
        void setContentWidth (int newWidth);

        int getNumRows() const noexcept { return numRows; }
        int getSelectedIndex() const noexcept { return selectedIndex; }

        // Aggiorna SOLO l'evidenziazione visiva — non scrive mai il
        // parametro APVTS ne' il modello: stesso principio di
        // PresetTableEditor::showPreset, PluginEditor resta l'unica fonte di
        // verita' sulla selezione (vedi selectPresetIndex()).
        void setSelectedIndex (int index);

        // L'utente ha cliccato (senza trascinare) una riga.
        std::function<void (int index)> onRowClicked;
        // Un passo di drag ha gia' applicato PresetLibrary::movePreset: il
        // preset trascinato e' ora in newIndex. Il chiamante deve tenere la
        // selezione (parametro presetIndex, tabella 12x8) agganciata al
        // preset che si sta trascinando — stessa identica chiamata gia'
        // usata da moveUpButton/moveDownButton prima di questa sessione.
        std::function<void (int newIndex)> onReordered;

        void paint (juce::Graphics&) override;

    private:
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        // Indice di riga sotto la coordinata y locale, o -1 se fuori dalle
        // righe vive (numRows).
        int rowAt (int y) const noexcept;

        HarmonizerAudioProcessor& processorRef;

        int contentWidth = 200;
        int numRows      = 0; // cache locale aggiornata da refresh(), usata da rowAt()/paint()

        int selectedIndex = -1;

        // Stato del gesto di trascinamento in corso, azzerato a ogni mouseUp.
        int  pressedRow  = -1; // riga sotto il mouse al mouseDown
        int  dragRow     = -1; // indice CORRENTE del preset trascinato (-1 = nessun drag attivo)
        int  dragStartY  = 0;
        bool isDragging  = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetListEditor)
    };
}
