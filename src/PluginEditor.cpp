#include "PluginEditor.h"
#include "harmony/CsvIo.h"

namespace
{
    void setupSlider (juce::Slider& slider, juce::Label& label, juce::Component& parent)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
        parent.addAndMakeVisible (slider);

        label.attachToComponent (&slider, true);
        parent.addAndMakeVisible (label);
    }

    void layoutRowOfButtons (juce::Rectangle<int> row, std::initializer_list<juce::Component*> buttons)
    {
        const int n = (int) buttons.size();
        if (n == 0)
            return;

        const int w = row.getWidth() / n;
        int x = row.getX();
        for (auto* b : buttons)
        {
            b->setBounds (x, row.getY(), w - 4, row.getHeight());
            x += w;
        }
    }
}

HarmonizerAudioProcessorEditor::HarmonizerAudioProcessorEditor (HarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    auto& apvtsRef = processorRef.apvts;

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvtsRef.getParameter ("rootNote")))
        for (auto& choice : choiceParam->choices)
            rootNoteBox.addItem (choice, rootNoteBox.getNumItems() + 1);

    addAndMakeVisible (rootNoteBox);
    rootNoteLabel.attachToComponent (&rootNoteBox, true);
    addAndMakeVisible (rootNoteLabel);

    addAndMakeVisible (presetBox);
    presetLabel.attachToComponent (&presetBox, true);
    addAndMakeVisible (presetLabel);
    presetBox.onChange = [this]
    {
        if (! ignoreComboCallback)
            selectPresetIndex (presetBox.getSelectedItemIndex());
    };

    addAndMakeVisible (presetNameEditor);
    nameLabel.attachToComponent (&presetNameEditor, true);
    addAndMakeVisible (nameLabel);
    presetNameEditor.onReturnKey = [this] { commitRename(); };
    presetNameEditor.onFocusLost = [this] { commitRename(); };

    setupSlider (numVoicesSlider, numVoicesLabel, *this);
    setupSlider (dryLevelSlider, dryLevelLabel, *this);
    setupSlider (wetLevelSlider, wetLevelLabel, *this);

    rootNoteAttachment  = std::make_unique<ComboAttachment>  (apvtsRef, "rootNote",  rootNoteBox);
    numVoicesAttachment = std::make_unique<SliderAttachment> (apvtsRef, "numVoices", numVoicesSlider);
    dryLevelAttachment  = std::make_unique<SliderAttachment> (apvtsRef, "dryLevel",  dryLevelSlider);
    wetLevelAttachment  = std::make_unique<SliderAttachment> (apvtsRef, "wetLevel",  wetLevelSlider);

    for (auto* b : { &addButton, &duplicateButton, &deleteButton, &moveUpButton, &moveDownButton,
                     &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton })
        addAndMakeVisible (b);

    addButton.onClick = [this]
    {
        processorRef.editPresetLibrary ([] (harmony::PresetLibrary& lib)
        {
            lib.addPreset ("New Preset", harmony::Table {});
        });
        refreshPresetBoxFromLibrary();
        selectPresetIndex (processorRef.getPresetLibrary()->getNumPresets() - 1);
    };

    duplicateButton.onClick = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        int newIndex = -1;
        processorRef.editPresetLibrary ([&] (harmony::PresetLibrary& lib) { newIndex = lib.duplicatePreset (idx); });
        refreshPresetBoxFromLibrary();
        if (newIndex >= 0)
            selectPresetIndex (newIndex);
    };

    deleteButton.onClick = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx < 0 || processorRef.getPresetLibrary()->getNumPresets() <= 1)
            return; // non si puo' restare senza preset

        processorRef.editPresetLibrary ([idx] (harmony::PresetLibrary& lib) { lib.removePreset (idx); });
        refreshPresetBoxFromLibrary();
        selectPresetIndex (juce::jmin (idx, processorRef.getPresetLibrary()->getNumPresets() - 1));
    };

    moveUpButton.onClick = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx <= 0)
            return;
        processorRef.editPresetLibrary ([idx] (harmony::PresetLibrary& lib) { lib.movePreset (idx, idx - 1); });
        refreshPresetBoxFromLibrary();
        selectPresetIndex (idx - 1);
    };

    moveDownButton.onClick = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        const auto lib = processorRef.getPresetLibrary();
        if (idx < 0 || idx >= lib->getNumPresets() - 1)
            return;
        processorRef.editPresetLibrary ([idx] (harmony::PresetLibrary& l) { l.movePreset (idx, idx + 1); });
        refreshPresetBoxFromLibrary();
        selectPresetIndex (idx + 1);
    };

    importCsvButton.onClick = [this]
    {
        activeFileChooser = std::make_unique<juce::FileChooser> ("Importa preset CSV", juce::File(), "*.csv");
        activeFileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return;

                if (auto parsed = harmony::CsvIo::parseCsv (file.loadFileAsString()))
                {
                    const auto name = file.getFileNameWithoutExtension();
                    int newIndex = -1;
                    processorRef.editPresetLibrary ([&] (harmony::PresetLibrary& lib)
                    {
                        newIndex = lib.addPreset (name, *parsed);
                    });
                    refreshPresetBoxFromLibrary();
                    if (newIndex >= 0)
                        selectPresetIndex (newIndex);
                }
                else
                {
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        "Import CSV",
                        "Il file non ha il formato atteso (intestazione + 8 righe x 12 colonne).");
                }
            });
    };

    exportCsvButton.onClick = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx < 0)
            return;

        const auto lib = processorRef.getPresetLibrary();
        const auto& preset = lib->getPreset (idx);

        activeFileChooser = std::make_unique<juce::FileChooser> ("Esporta preset CSV",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (preset.name + ".csv"),
            "*.csv");
        activeFileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [csv = harmony::CsvIo::tableToCsv (preset.table)] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file != juce::File())
                    file.replaceWithText (csv);
            });
    };

    loadGlobalButton.onClick = [this]
    {
        bool ok = false;
        processorRef.editPresetLibrary ([&] (harmony::PresetLibrary& lib) { ok = lib.loadGlobal(); });
        refreshPresetBoxFromLibrary();
        selectPresetIndex (0);

        if (! ok)
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "Libreria globale", "Nessuna libreria globale trovata su disco.");
    };

    saveGlobalButton.onClick = [this]
    {
        const bool ok = processorRef.getPresetLibrary()->saveAsGlobal();
        juce::NativeMessageBox::showMessageBoxAsync (
            ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
            "Libreria globale",
            ok ? "Libreria salvata come globale." : "Salvataggio fallito.");
    };

    refreshPresetBoxFromLibrary();
    syncPresetSelectionFromParameter();

    setResizable (true, true);
    setResizeLimits (420, 420, 1200, 900);
    setSize (460, 460);

    startTimerHz (15);
}

HarmonizerAudioProcessorEditor::~HarmonizerAudioProcessorEditor()
{
    stopTimer();
}

void HarmonizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText ("HARMONIZER - M0/M1/M2 placeholder (motore Signalsmith interinale)",
                       getLocalBounds().removeFromTop (24), juce::Justification::centred, 1);
}

void HarmonizerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (28); // spazio per il titolo disegnato in paint()

    const int labelWidth = 60;
    const int rowHeight = 26;
    const int gap = 6;

    auto layoutRow = [&] (juce::Component& c)
    {
        auto row = area.removeFromTop (rowHeight);
        row.removeFromLeft (labelWidth);
        c.setBounds (row);
        area.removeFromTop (gap);
    };

    layoutRow (rootNoteBox);
    layoutRow (presetBox);
    layoutRow (presetNameEditor);
    layoutRow (numVoicesSlider);
    layoutRow (dryLevelSlider);
    layoutRow (wetLevelSlider);

    area.removeFromTop (gap);

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &addButton, &duplicateButton, &deleteButton, &moveUpButton, &moveDownButton });
    area.removeFromTop (gap);

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton });
}

void HarmonizerAudioProcessorEditor::timerCallback()
{
    refreshPresetBoxFromLibrary();
    syncPresetSelectionFromParameter();
}

void HarmonizerAudioProcessorEditor::refreshPresetBoxFromLibrary()
{
    const auto lib = processorRef.getPresetLibrary();
    const int n = lib->getNumPresets();

    bool changed = (n != lastKnownPresetCount);
    if (! changed)
        for (int i = 0; i < n; ++i)
            if (presetBox.getItemText (i) != lib->getPreset (i).name) { changed = true; break; }

    if (! changed)
        return;

    const int previousSelection = presetBox.getSelectedItemIndex();

    ignoreComboCallback = true;
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < n; ++i)
        presetBox.addItem (lib->getPreset (i).name, i + 1);

    if (previousSelection >= 0 && previousSelection < n)
        presetBox.setSelectedItemIndex (previousSelection, juce::dontSendNotification);
    ignoreComboCallback = false;

    lastKnownPresetCount = n;
}

void HarmonizerAudioProcessorEditor::syncPresetSelectionFromParameter()
{
    auto* intParam = dynamic_cast<juce::AudioParameterInt*> (processorRef.apvts.getParameter ("presetIndex"));
    if (intParam == nullptr)
        return;

    const auto lib = processorRef.getPresetLibrary();
    if (lib->getNumPresets() == 0)
        return;

    const int desiredIndex = juce::jlimit (0, lib->getNumPresets() - 1, intParam->get() - 1);
    if (desiredIndex == lastSyncedSelectedIndex)
        return;

    ignoreComboCallback = true;
    presetBox.setSelectedItemIndex (desiredIndex, juce::dontSendNotification);
    ignoreComboCallback = false;

    if (! presetNameEditor.hasKeyboardFocus (false))
        presetNameEditor.setText (lib->getPreset (desiredIndex).name, juce::dontSendNotification);

    lastSyncedSelectedIndex = desiredIndex;
}

void HarmonizerAudioProcessorEditor::commitRename()
{
    const int idx = presetBox.getSelectedItemIndex();
    if (idx < 0)
        return;

    const auto newName = presetNameEditor.getText();
    processorRef.editPresetLibrary ([idx, newName] (harmony::PresetLibrary& lib) { lib.renamePreset (idx, newName); });

    lastKnownPresetCount = -1; // forza il refresh della combo: il nome e' cambiato
    refreshPresetBoxFromLibrary();
}

void HarmonizerAudioProcessorEditor::selectPresetIndex (int index)
{
    if (index < 0)
        return;

    auto* intParam = dynamic_cast<juce::AudioParameterInt*> (processorRef.apvts.getParameter ("presetIndex"));
    if (intParam == nullptr)
        return;

    *intParam = index + 1; // 1-based, coerente col futuro CC posizionale (FR-05)

    lastSyncedSelectedIndex = -1; // forza il riallineamento immediato sotto
    syncPresetSelectionFromParameter();
}
