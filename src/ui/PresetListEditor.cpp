#include "PresetListEditor.h"
#include <cstdlib>

namespace ui
{
    PresetListEditor::PresetListEditor (HarmonizerAudioProcessor& processor)
        : processorRef (processor)
    {
        setSize (contentWidth, rowHeight);
    }

    void PresetListEditor::refresh()
    {
        numRows = processorRef.getPresetLibrary()->getNumPresets();
        setSize (contentWidth, juce::jmax (rowHeight, numRows * rowHeight));
        repaint();
    }

    void PresetListEditor::setContentWidth (int newWidth)
    {
        contentWidth = juce::jmax (40, newWidth);
        refresh();
    }

    void PresetListEditor::setSelectedIndex (int index)
    {
        if (selectedIndex == index)
            return;
        selectedIndex = index;
        repaint();
    }

    int PresetListEditor::rowAt (int y) const noexcept
    {
        if (rowHeight <= 0 || y < 0)
            return -1;
        const int row = y / rowHeight;
        return (row >= 0 && row < numRows) ? row : -1;
    }

    void PresetListEditor::paint (juce::Graphics& g)
    {
        const auto lib = processorRef.getPresetLibrary();
        const int n = juce::jmin (numRows, lib->getNumPresets()); // difesa: shrink concorrente

        // I primi 5 preset sono quelli raggiungibili dalle 5 direzioni del
        // navigation button del controller hardware dedicato (PRD §8.2,
        // FR-07): l'utente riordina la lista proprio per decidere cosa
        // avere "sotto le dita" li'. Un badge numerato (1-5) rende visibile
        // quella corrispondenza senza dover consultare il manuale.
        constexpr int numNavSlots = 5;
        const auto navAccentColour = juce::Colour (0xffe0a72e);

        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<int> row (0, i * rowHeight, getWidth(), rowHeight);

            // Sfondo: selezionata sempre evidenziata; la riga sotto il dito
            // durante un trascinamento attivo prende un rilievo piu' forte,
            // per dare il feedback "sto sollevando questa icona" richiesto
            // da FR-06 ("interazione tipica del riordino di icone su
            // smartphone").
            if (isDragging && i == dragRow)
                g.setColour (juce::Colours::white.withAlpha (0.28f));
            else if (i == selectedIndex)
                g.setColour (juce::Colours::white.withAlpha (0.15f));
            else
                g.setColour (juce::Colours::transparentBlack);
            g.fillRect (row);

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawLine ((float) row.getX(), (float) row.getBottom(),
                       (float) row.getRight(), (float) row.getBottom());

            const auto& preset = lib->getPreset (i);

            auto textArea = row.reduced (4, 0);
            auto navArea = textArea.removeFromLeft (18);
            if (i < numNavSlots)
            {
                auto badge = navArea.withSizeKeepingCentre (14, 14);
                g.setColour (navAccentColour.withAlpha (0.25f));
                g.fillRoundedRectangle (badge.toFloat(), 3.0f);
                g.setColour (navAccentColour);
                g.drawRoundedRectangle (badge.toFloat(), 3.0f, 1.0f);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
            }

            auto ccArea = textArea.removeFromLeft (36);

            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("CC " + juce::String (lib->getCcValue (i)), ccArea,
                       juce::Justification::centredLeft);

            g.setColour (juce::Colours::white);
            g.drawText (preset.name, textArea, juce::Justification::centredLeft);
        }
    }

    void PresetListEditor::mouseDown (const juce::MouseEvent& e)
    {
        pressedRow = rowAt (e.y);
        dragRow    = -1;
        isDragging = false;
        dragStartY = e.y;
    }

    void PresetListEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (pressedRow < 0)
            return;

        if (! isDragging)
        {
            // Soglia minima prima di considerarlo un trascinamento e non un
            // click: un piccolo tremore della mano durante un click non deve
            // scatenare un riordino.
            if (std::abs (e.y - dragStartY) < rowHeight / 3)
                return;
            isDragging = true;
            dragRow = pressedRow;
        }

        const int target = juce::jlimit (0, juce::jmax (0, numRows - 1), e.y / rowHeight);
        if (target != dragRow)
        {
            const int from = dragRow;
            processorRef.editPresetLibrary ([from, target] (harmony::PresetLibrary& lib)
            {
                lib.movePreset (from, target);
            });
            dragRow = target;

            if (onReordered)
                onReordered (dragRow);
        }

        repaint();
    }

    void PresetListEditor::mouseUp (const juce::MouseEvent&)
    {
        if (! isDragging && pressedRow >= 0 && onRowClicked)
            onRowClicked (pressedRow);

        pressedRow = -1;
        dragRow    = -1;
        isDragging = false;
        repaint();
    }
}
