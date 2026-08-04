#include "PresetTableEditor.h"
#include "CellInputParser.h"
#include "DegreeNames.h"

namespace ui
{
    namespace
    {
        juce::String cellText (const harmony::Cell& c)
        {
            return c.has_value() ? juce::String (*c) : juce::String();
        }
    }

    PresetTableEditor::PresetTableEditor (HarmonizerAudioProcessor& processor)
        : processorRef (processor)
    {
        for (int d = 0; d < harmony::numDegrees; ++d)
        {
            auto& h = columnHeaders[(size_t) d];
            h.setText (degreeName (d), juce::dontSendNotification);
            h.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (h);
        }

        for (int v = 0; v < harmony::numVoices; ++v)
        {
            auto& h = rowHeaders[(size_t) v];
            h.setText ("V" + juce::String (v + 1), juce::dontSendNotification);
            h.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (h);
        }

        for (int d = 0; d < harmony::numDegrees; ++d)
        {
            for (int v = 0; v < harmony::numVoices; ++v)
            {
                auto& cell = cells[(size_t) d][(size_t) v];
                cell.setEditable (true, true, false);
                cell.setJustificationType (juce::Justification::centred);
                cell.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.15f));

                // onTextChange scatta quando l'utente conferma l'editor
                // (Invio o click fuori) — la libreria JUCE ha gia' scritto il
                // nuovo testo nella label prima di chiamare questa callback,
                // quindi label.getText() dentro commitCell e' il testo
                // digitato. Se commitCell rifiuta l'input e richiama
                // setText(..., dontSendNotification) per ripristinarlo, NON
                // si rientra qui (dontSendNotification lo impedisce).
                cell.onTextChange = [this, d, v] { commitCell (d, v); };

                addAndMakeVisible (cell);
            }
        }
    }

    void PresetTableEditor::showPreset (int presetIndex)
    {
        const auto lib = processorRef.getPresetLibrary();
        if (presetIndex < 0 || presetIndex >= lib->getNumPresets())
        {
            currentPresetIndex = -1;
            return;
        }

        currentPresetIndex = presetIndex;
        const auto& table = lib->getPreset (presetIndex).table;

        for (int d = 0; d < harmony::numDegrees; ++d)
            for (int v = 0; v < harmony::numVoices; ++v)
                cells[(size_t) d][(size_t) v].setText (cellText (table[(size_t) d][(size_t) v]),
                                                        juce::dontSendNotification);
    }

    void PresetTableEditor::commitCell (int degree, int voice)
    {
        auto& label = cells[(size_t) degree][(size_t) voice];

        if (currentPresetIndex < 0)
            return; // nessun preset visualizzato: non dovrebbe succedere, ma non scrivere nulla

        const auto parsed = parseCellInput (label.getText().toStdString());

        const auto lib = processorRef.getPresetLibrary();
        if (currentPresetIndex >= lib->getNumPresets())
            return; // il preset e' sparito sotto i piedi (delete concorrente): non scrivere

        if (! parsed.has_value())
        {
            // Rifiutato: ripristina il valore CANONICO dal modello, non
            // quello digitato — CLAUDE.md regola 3, un refuso non deve
            // restare visualizzato come se fosse stato accettato.
            const auto& table = lib->getPreset (currentPresetIndex).table;
            label.setText (cellText (table[(size_t) degree][(size_t) voice]), juce::dontSendNotification);
            return;
        }

        const harmony::Cell newValue = *parsed; // std::optional<int> -> harmony::Cell, stessa forma
        const int presetIndex = currentPresetIndex;
        processorRef.editPresetLibrary ([presetIndex, degree, voice, newValue] (harmony::PresetLibrary& l)
        {
            l.setCell (presetIndex, degree, voice, newValue);
        });

        // Ricanonizza il testo visualizzato (es. "007" -> "7", "+3" -> "3").
        label.setText (cellText (newValue), juce::dontSendNotification);
    }

    void PresetTableEditor::resized()
    {
        auto area = getLocalBounds();

        constexpr int rowHeaderWidth = 32;
        constexpr int headerRowHeight = 22;
        constexpr int cellRowHeight = 24;

        auto headerRow = area.removeFromTop (headerRowHeight);
        headerRow.removeFromLeft (rowHeaderWidth); // angolo vuoto sopra le intestazioni di riga

        const int colWidth = headerRow.getWidth() / harmony::numDegrees;
        for (int d = 0; d < harmony::numDegrees; ++d)
            columnHeaders[(size_t) d].setBounds (headerRow.removeFromLeft (colWidth));

        for (int v = 0; v < harmony::numVoices; ++v)
        {
            auto row = area.removeFromTop (cellRowHeight);
            rowHeaders[(size_t) v].setBounds (row.removeFromLeft (rowHeaderWidth));

            const int cw = row.getWidth() / harmony::numDegrees;
            for (int d = 0; d < harmony::numDegrees; ++d)
                cells[(size_t) d][(size_t) v].setBounds (row.removeFromLeft (cw));
        }
    }
}
