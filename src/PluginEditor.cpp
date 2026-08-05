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

    void setupCcRow (juce::Slider& slider, juce::Label& label, juce::TextButton& learnButton, juce::Component& parent)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setRange (0.0, 127.0, 1.0);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        parent.addAndMakeVisible (slider);

        label.attachToComponent (&slider, true);
        parent.addAndMakeVisible (label);

        learnButton.setClickingTogglesState (false);
        parent.addAndMakeVisible (learnButton);
    }

    void layoutRowOfButtons (juce::Rectangle<int> row, const std::vector<juce::Component*>& buttons)
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
    : AudioProcessorEditor (&p), processorRef (p), presetListEditor (p), presetTableEditor (p)
{
    auto& apvtsRef = processorRef.apvts;

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvtsRef.getParameter ("rootNote")))
        for (auto& choice : choiceParam->choices)
            rootNoteBox.addItem (choice, rootNoteBox.getNumItems() + 1);

    addAndMakeVisible (rootNoteBox);
    rootNoteLabel.attachToComponent (&rootNoteBox, true);
    addAndMakeVisible (rootNoteLabel);

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvtsRef.getParameter ("stabilityLevel")))
        for (auto& choice : choiceParam->choices)
            stabilityBox.addItem (choice, stabilityBox.getNumItems() + 1);

    addAndMakeVisible (stabilityBox);
    stabilityLabel.attachToComponent (&stabilityBox, true);
    addAndMakeVisible (stabilityLabel);

    addAndMakeVisible (presetListViewport);
    presetListViewport.setViewedComponent (&presetListEditor, false);
    presetListViewport.setScrollBarsShown (true, false); // solo verticale
    presetLabel.attachToComponent (&presetListViewport, true);
    addAndMakeVisible (presetLabel);
    presetListEditor.onRowClicked = [this] (int index) { selectPresetIndex (index); };
    // Un passo di drag ha gia' applicato movePreset: si tiene la selezione
    // agganciata al preset trascinato, stessa chiamata gia' usata prima di
    // questa sessione da moveUpButton/moveDownButton.
    presetListEditor.onReordered = [this] (int newIndex) { selectPresetIndex (newIndex); };

    addAndMakeVisible (presetNameEditor);
    nameLabel.attachToComponent (&presetNameEditor, true);
    addAndMakeVisible (nameLabel);
    presetNameEditor.onReturnKey = [this] { commitRename(); };
    presetNameEditor.onFocusLost = [this] { commitRename(); };

    addAndMakeVisible (presetTableEditor);

    setupSlider (numVoicesSlider, numVoicesLabel, *this);
    setupSlider (dryLevelSlider, dryLevelLabel, *this);
    setupSlider (wetLevelSlider, wetLevelLabel, *this);
    setupSlider (glideTimeSlider, glideLabel, *this);
    setupSlider (maxVoicesSlider, maxVoicesLabel, *this);
    setupSlider (formantSpreadSlider, formantSpreadLabel, *this);

    activeVoicesLabel.attachToComponent (&activeVoicesValueLabel, true);
    addAndMakeVisible (activeVoicesLabel);
    addAndMakeVisible (activeVoicesValueLabel);

    rootNoteAttachment   = std::make_unique<ComboAttachment>  (apvtsRef, "rootNote",      rootNoteBox);
    stabilityAttachment  = std::make_unique<ComboAttachment>  (apvtsRef, "stabilityLevel", stabilityBox);
    numVoicesAttachment  = std::make_unique<SliderAttachment> (apvtsRef, "numVoices",     numVoicesSlider);
    dryLevelAttachment   = std::make_unique<SliderAttachment> (apvtsRef, "dryLevel",      dryLevelSlider);
    wetLevelAttachment   = std::make_unique<SliderAttachment> (apvtsRef, "wetLevel",      wetLevelSlider);
    glideTimeAttachment  = std::make_unique<SliderAttachment> (apvtsRef, "glideTimeMs",   glideTimeSlider);
    maxVoicesAttachment  = std::make_unique<SliderAttachment> (apvtsRef, "maxSimultaneousVoices", maxVoicesSlider);
    formantSpreadAttachment = std::make_unique<SliderAttachment> (apvtsRef, "formantSpread", formantSpreadSlider);

    addAndMakeVisible (fixMoveLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& button = voiceFixButtons[(size_t) v];
        button.setButtonText (juce::String (v + 1));
        button.setClickingTogglesState (true);
        addAndMakeVisible (button);
        voiceFixAttachments[(size_t) v] = std::make_unique<ButtonAttachment> (apvtsRef, "voiceFix" + juce::String (v + 1), button);
    }

    addAndMakeVisible (voiceFormantLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& slider = voiceFormantSliders[(size_t) v];
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (slider);
        voiceFormantAttachments[(size_t) v] = std::make_unique<SliderAttachment> (apvtsRef, "voiceFormantOffset" + juce::String (v + 1), slider);
    }

    for (auto* b : { &addButton, &duplicateButton, &deleteButton,
                     &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton })
        addAndMakeVisible (b);

    addButton.onClick = [this]
    {
        processorRef.editPresetLibrary ([] (harmony::PresetLibrary& lib)
        {
            lib.addPreset ("New Preset", harmony::Table {});
        });
        presetListEditor.refresh();
        selectPresetIndex (processorRef.getPresetLibrary()->getNumPresets() - 1);
    };

    duplicateButton.onClick = [this]
    {
        const int idx = presetListEditor.getSelectedIndex();
        int newIndex = -1;
        processorRef.editPresetLibrary ([&] (harmony::PresetLibrary& lib) { newIndex = lib.duplicatePreset (idx); });
        presetListEditor.refresh();
        if (newIndex >= 0)
            selectPresetIndex (newIndex);
    };

    deleteButton.onClick = [this]
    {
        const int idx = presetListEditor.getSelectedIndex();
        if (idx < 0 || processorRef.getPresetLibrary()->getNumPresets() <= 1)
            return; // non si puo' restare senza preset

        processorRef.editPresetLibrary ([idx] (harmony::PresetLibrary& lib) { lib.removePreset (idx); });
        presetListEditor.refresh();
        selectPresetIndex (juce::jmin (idx, processorRef.getPresetLibrary()->getNumPresets() - 1));
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
                    presetListEditor.refresh();
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
        const int idx = presetListEditor.getSelectedIndex();
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
        presetListEditor.refresh();
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

    // FR-32: canale MIDI, omni come default (itemId 1). itemId N+1 = canale N.
    midiChannelBox.addItem ("Omni", 1);
    for (int ch = 1; ch <= 16; ++ch)
        midiChannelBox.addItem (juce::String (ch), ch + 1);
    addAndMakeVisible (midiChannelBox);
    midiChannelLabel.attachToComponent (&midiChannelBox, true);
    addAndMakeVisible (midiChannelLabel);
    midiChannelBox.onChange = [this]
    {
        const int itemId = midiChannelBox.getSelectedId();
        processorRef.getCcRouter().setMidiChannel (itemId <= 1 ? 0 : itemId - 1);
    };

    // FR-30/31/33: numero CC per funzione + MIDI Learn. Non sono
    // AudioProcessorValueTreeState::SliderAttachment: i numeri CC non sono
    // parametri APVTS (vedi PluginProcessor::getStateInformation), quindi
    // slider e bottoni si collegano direttamente a CcRouter e si
    // risincronizzano dal timer esistente (syncCcControlsFromRouter).
    setupCcRow (rootCcSlider, rootCcLabel, learnRootButton, *this);
    setupCcRow (presetCcSlider, presetCcLabel, learnPresetButton, *this);
    setupCcRow (bypassCcSlider, bypassCcLabel, learnBypassButton, *this);

    rootCcSlider.onValueChange   = [this] { processorRef.getCcRouter().setRootCc   ((int) rootCcSlider.getValue()); };
    presetCcSlider.onValueChange = [this] { processorRef.getCcRouter().setPresetCc ((int) presetCcSlider.getValue()); };
    bypassCcSlider.onValueChange = [this] { processorRef.getCcRouter().setBypassCc ((int) bypassCcSlider.getValue()); };

    learnRootButton.onClick   = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::root); };
    learnPresetButton.onClick = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::preset); };
    learnBypassButton.onClick = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::bypass); };

    // FR-30/34/36: bypass e' un parametro APVTS come gli altri (automatizzabile
    // dall'host); il CC lo mette in override esattamente come root/preset
    // (vedi PluginProcessor::processBlock, OverrideManager). Il bottone qui
    // serve a testare senza un controller MIDI a disposizione.
    bypassToggle.setButtonText ("Bypass");
    addAndMakeVisible (bypassToggle);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "bypass", bypassToggle);

    // FR-24/28: interruttore Harmonizer/Play.
    playModeToggle.setButtonText ("Play Mode");
    addAndMakeVisible (playModeToggle);
    playModeAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "playModeEnabled", playModeToggle);

    // Sessione 10: vedi Phrase.h per la semantica completa.
    keepTailsToggle.setButtonText ("Keep Tails");
    addAndMakeVisible (keepTailsToggle);
    keepTailsAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "keepPhraseTails", keepTailsToggle);

    // Diagnostica PRD §8.1 "Display della nota rilevata" — sessione 10.
    // Non e' un parametro: sincronizzata dal timer esistente come
    // activeVoicesValueLabel.
    detectedLabel.attachToComponent (&detectedValueLabel, true);
    addAndMakeVisible (detectedLabel);
    addAndMakeVisible (detectedValueLabel);

    syncCcControlsFromRouter();

    presetListEditor.refresh();
    syncPresetSelectionFromParameter();

    setResizable (true, true);
    // Sessione M5: la griglia preset (12x8 + intestazione, ~214px) aggiunge
    // circa 220px di altezza minima necessaria e richiede un minimo di
    // larghezza maggiore (12 colonne + colonna di intestazione riga devono
    // restare leggibili). Limiti/dimensione precedenti erano (460,950)-(1200,1230)/500x990.
    // Sessione 21: la ComboBox a riga singola (26px+6px gap) diventa la lista
    // preset drag&drop (7 righe visibili di default, vedi resized()) —
    // netto +122px di altezza minima necessaria.
    setResizeLimits (500, 1292, 1200, 1572);
    setSize (520, 1322);

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
    // Editor placeholder (le tre schermate definitive sono lavoro di M5):
    // il testo qui sotto descrive solo il motore attivo, non l'elenco delle
    // milestone completate — evita di doverlo tenere sincronizzato a mano
    // (era rimasto "motore Signalsmith interinale" ben oltre il cambio a
    // PSOLA in sessione 9, scoperto solo al primo test reale in sessione 10).
    g.drawFittedText ("HARMONIZER - motore PSOLA",
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

    {
        // Sessione 21 (drag&drop preset, FR-06/07/§8.2): 7 righe visibili di
        // default (il numero di preset di fabbrica, cosi' non scrolla nulla
        // finche' l'utente non ne aggiunge altri) — deve restare in sync con
        // ui::PresetListEditor::rowHeight.
        constexpr int visiblePresetRows = 7;
        auto row = area.removeFromTop (visiblePresetRows * ui::PresetListEditor::rowHeight);
        row.removeFromLeft (labelWidth);
        presetListViewport.setBounds (row);
        presetListEditor.setContentWidth (presetListViewport.getWidth()
                                          - presetListViewport.getScrollBarThickness());
        area.removeFromTop (gap);
    }

    layoutRow (presetNameEditor);

    {
        // Sessione M5 (editor tabella preset, §8.2): altezza fissa =
        // intestazione di grado + 8 righe di voce — deve restare in sync con
        // headerRowHeight/cellRowHeight in PresetTableEditor::resized().
        constexpr int presetTableHeight = 22 + 8 * 24;
        presetTableEditor.setBounds (area.removeFromTop (presetTableHeight));
        area.removeFromTop (gap);
    }

    layoutRow (numVoicesSlider);
    layoutRow (dryLevelSlider);
    layoutRow (wetLevelSlider);
    layoutRow (stabilityBox);
    layoutRow (glideTimeSlider);
    layoutRow (maxVoicesSlider);
    layoutRow (formantSpreadSlider);
    layoutRow (activeVoicesValueLabel);

    area.removeFromTop (gap);

    {
        auto row = area.removeFromTop (rowHeight);
        // Bug di sessione 6 corretto in sessione 10: il rettangolo tolto a
        // sinistra andava assegnato alla label, non solo scartato — la
        // label restava con bounds vuoti (invisibile), pur essendo
        // aggiunta come figlia visibile.
        fixMoveLabel.setBounds (row.removeFromLeft (labelWidth));
        std::vector<juce::Component*> buttons;
        for (auto& b : voiceFixButtons)
            buttons.push_back (&b);
        layoutRowOfButtons (row, buttons);
    }
    area.removeFromTop (gap);

    {
        // Riga piu' alta delle altre: una manopola rotativa ha bisogno di
        // piu' di 26px per essere leggibile (erano i "puntini" nello
        // screenshot del test di sessione 10 — bounds validi ma minuscoli).
        const int knobRowHeight = 36;
        auto row = area.removeFromTop (knobRowHeight);
        voiceFormantLabel.setBounds (row.removeFromLeft (labelWidth)); // stesso bug del blocco sopra
        std::vector<juce::Component*> knobs;
        for (auto& s : voiceFormantSliders)
            knobs.push_back (&s);
        layoutRowOfButtons (row, knobs);
    }
    area.removeFromTop (gap);

    layoutRow (midiChannelBox);

    auto layoutCcRow = [&] (juce::Slider& slider, juce::TextButton& button)
    {
        auto row = area.removeFromTop (rowHeight);
        row.removeFromLeft (labelWidth);
        auto buttonArea = row.removeFromRight (70);
        button.setBounds (buttonArea);
        row.removeFromRight (4);
        slider.setBounds (row);
        area.removeFromTop (gap);
    };

    layoutCcRow (rootCcSlider, learnRootButton);
    layoutCcRow (presetCcSlider, learnPresetButton);
    layoutCcRow (bypassCcSlider, learnBypassButton);

    layoutRow (bypassToggle);
    layoutRow (playModeToggle);
    layoutRow (keepTailsToggle);
    layoutRow (detectedValueLabel);

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &addButton, &duplicateButton, &deleteButton });
    area.removeFromTop (gap);

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton });
}

void HarmonizerAudioProcessorEditor::timerCallback()
{
    // presetListEditor legge sempre il modello dal vivo in paint(): a
    // differenza della vecchia ComboBox non serve un diff sui nomi per
    // decidere se aggiornare, un repaint periodico basta ed e' innocuo
    // (nessun componente figlio da ricreare).
    presetListEditor.refresh();
    syncPresetSelectionFromParameter();
    syncCcControlsFromRouter();

    activeVoicesValueLabel.setText (juce::String (processorRef.getNumActiveVoices()), juce::dontSendNotification);

    // Diagnostica PRD §8.1 — sessione 10, vedi PluginProcessor::processBlock
    // per dove viene scritta. Sessione 12: aggiunti stato del gate (distinto
    // da "stable", vedi FR-43/45/46) e il contatore di allocazioni differite
    // — servono a confermare all'ascolto che il fix delle note saltate sta
    // intervenendo davvero, non solo a diagnosticare come in sessione 10.
    const float midiNote = processorRef.getLastDetectedMidiNote();
    const bool stable = processorRef.getLastInputStable();
    const bool gateOpen = processorRef.getLastGateOpen();
    juce::String detectedText;
    if (midiNote >= 0.0f)
    {
        const auto noteName = juce::MidiMessage::getMidiNoteName (juce::roundToInt (midiNote), true, true, 3);
        detectedText = noteName + juce::String::formatted (
            "  (%.2f, conf %.2f)  %s  gate %s", midiNote,
            (double) processorRef.getLastDetectedConfidence(),
            stable ? "stable" : "unstable",
            gateOpen ? "open" : "closed");
    }
    else
    {
        detectedText = juce::String ("-- (nessun segnale)  gate ") + (gateOpen ? "open" : "closed");
    }
    detectedText += "  late-bindings " + juce::String (processorRef.getNumLateBindings());
    // Sessione 13 (indagine click/wobbling): la dimensione del blocco host
    // determina la granularita' di ogni controllo aggiornato "una volta per
    // blocco" (Glide, parametri del PitchShifter) — un buffer grande (host
    // senza ASIO) puo' essere causa diretta di artefatti, vedi handsoff.md.
    detectedText += "  blk " + juce::String (processorRef.getLastBlockSize());
    detectedValueLabel.setText (detectedText, juce::dontSendNotification);
}

void HarmonizerAudioProcessorEditor::syncCcControlsFromRouter()
{
    auto& router = processorRef.getCcRouter();

    // Non toccare uno slider mentre l'utente lo sta trascinando: eviterebbe
    // che il valore "scatti" sotto il mouse durante il polling a 15Hz.
    if (! rootCcSlider.isMouseButtonDown())
        rootCcSlider.setValue (router.getRootCc(), juce::dontSendNotification);
    if (! presetCcSlider.isMouseButtonDown())
        presetCcSlider.setValue (router.getPresetCc(), juce::dontSendNotification);
    if (! bypassCcSlider.isMouseButtonDown())
        bypassCcSlider.setValue (router.getBypassCc(), juce::dontSendNotification);

    const int channel = router.getMidiChannel();
    const int desiredItemId = channel == 0 ? 1 : channel + 1;
    if (midiChannelBox.getSelectedId() != desiredItemId)
        midiChannelBox.setSelectedId (desiredItemId, juce::dontSendNotification);

    const auto learning = router.getLearnTarget();
    learnRootButton.setButtonText   (learning == CcRouter::LearnTarget::root   ? "Learning..." : "Learn");
    learnPresetButton.setButtonText (learning == CcRouter::LearnTarget::preset ? "Learning..." : "Learn");
    learnBypassButton.setButtonText (learning == CcRouter::LearnTarget::bypass ? "Learning..." : "Learn");
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

    presetListEditor.setSelectedIndex (desiredIndex);

    // Scroll-into-view: Add/Duplicate/CC/automazione possono selezionare un
    // preset fuori dall'area visibile del Viewport — senza questo l'utente
    // non avrebbe alcun segnale che la selezione e' cambiata.
    {
        const int rowTop    = desiredIndex * ui::PresetListEditor::rowHeight;
        const int rowBottom = rowTop + ui::PresetListEditor::rowHeight;
        const auto visible  = presetListViewport.getViewArea();
        if (rowTop < visible.getY())
            presetListViewport.setViewPosition (0, rowTop);
        else if (rowBottom > visible.getBottom())
            presetListViewport.setViewPosition (0, rowBottom - visible.getHeight());
    }

    if (! presetNameEditor.hasKeyboardFocus (false))
        presetNameEditor.setText (lib->getPreset (desiredIndex).name, juce::dontSendNotification);

    // Sessione M5: ogni percorso che cambia selezione o contenuto della
    // libreria (add/duplicate/delete/move/import CSV/load global) chiama
    // gia' selectPresetIndex() a valle, che a sua volta forza il passaggio
    // qui sotto (lastSyncedSelectedIndex = -1) — un solo punto di aggancio
    // basta, non serve ripeterlo in ogni onClick.
    presetTableEditor.showPreset (desiredIndex);

    lastSyncedSelectedIndex = desiredIndex;
}

void HarmonizerAudioProcessorEditor::commitRename()
{
    const int idx = presetListEditor.getSelectedIndex();
    if (idx < 0)
        return;

    const auto newName = presetNameEditor.getText();
    processorRef.editPresetLibrary ([idx, newName] (harmony::PresetLibrary& lib) { lib.renamePreset (idx, newName); });

    presetListEditor.refresh(); // il nome e' cambiato, il repaint lo legge dal modello
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
