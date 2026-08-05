#include "RootNoteGrid.h"

namespace ui
{
    namespace
    {
        constexpr int numPitchClasses = 12;
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

    int RootNoteGrid::cellAt (int x) const noexcept
    {
        const int cellWidth = getWidth() / numPitchClasses;
        if (cellWidth <= 0 || x < 0)
            return -1;
        const int cell = x / cellWidth;
        return (cell >= 0 && cell < numPitchClasses) ? cell : -1;
    }

    void RootNoteGrid::paint (juce::Graphics& g)
    {
        auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter ("rootNote"));
        if (choiceParam == nullptr)
            return;

        const auto& choices = choiceParam->choices;
        const int n = juce::jmin (numPitchClasses, choices.size());
        const int cellWidth = getWidth() / numPitchClasses;

        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<int> cell (i * cellWidth, 0, cellWidth, getHeight());

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
        const int cell = cellAt (e.x);
        if (cell >= 0 && onNoteClicked)
            onNoteClicked (cell);
    }
}
