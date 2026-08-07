#include "RootNoteGrid.h"

namespace ui
{
    namespace
    {
        constexpr int numPitchClasses = 12;
        // FR-75: 2 colonne x 6 righe, non piu' 1x12 — indice = riga*2 + colonna.
        constexpr int numCols = 2;
        constexpr int numRows = 6;
    }

    RootNoteGrid::RootNoteGrid (HarmonizerAudioProcessor& processor)
        : processorRef (processor)
    {
    }

    void RootNoteGrid::setSelectedIndex (int pitchClass)
    {
        if (selectedIndex == pitchClass)
            return;
        selectedIndex = pitchClass;
        repaint();
    }

    int RootNoteGrid::cellAt (int x, int y) const noexcept
    {
        const int cellWidth  = getWidth() / numCols;
        const int cellHeight = getHeight() / numRows;
        if (cellWidth <= 0 || cellHeight <= 0 || x < 0 || y < 0)
            return -1;

        const int col = x / cellWidth;
        const int row = y / cellHeight;
        if (col < 0 || col >= numCols || row < 0 || row >= numRows)
            return -1;

        const int cell = row * numCols + col;
        return (cell >= 0 && cell < numPitchClasses) ? cell : -1;
    }

    void RootNoteGrid::paint (juce::Graphics& g)
    {
        auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter ("rootNote"));
        if (choiceParam == nullptr)
            return;

        const auto& choices = choiceParam->choices;
        const int n = juce::jmin (numPitchClasses, choices.size());
        const int cellWidth  = getWidth() / numCols;
        const int cellHeight = getHeight() / numRows;

        for (int i = 0; i < n; ++i)
        {
            const int col = i % numCols;
            const int row = i / numCols;
            const juce::Rectangle<int> cell (col * cellWidth, row * cellHeight, cellWidth, cellHeight);

            g.setColour (i == selectedIndex ? juce::Colours::white.withAlpha (0.25f)
                                             : juce::Colours::white.withAlpha (0.06f));
            g.fillRect (cell.reduced (1));

            if (i == selectedIndex)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawRect (cell.reduced (1), 1);
            }

            // Le note "diesis" (contengono '#') prendono un testo
            // leggermente attenuato, puro aiuto di lettura cromatica per
            // ricalcare a colpo d'occhio il pattern tasti bianchi/neri di
            // una tastiera — nessun trattamento diverso altrove nel codice,
            // tutti e 12 i valori sono equivalenti per il modello.
            const bool isSharp = choices[i].containsChar ('#');
            g.setColour (isSharp ? juce::Colours::white.withAlpha (0.75f) : juce::Colours::white);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (choices[i], cell, juce::Justification::centred);
        }
    }

    void RootNoteGrid::mouseUp (const juce::MouseEvent& e)
    {
        const int cell = cellAt (e.x, e.y);
        if (cell >= 0 && onNoteClicked)
            onNoteClicked (cell);
    }
}
