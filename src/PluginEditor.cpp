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

    // FR-78: Stability, Num Voices, Fmt Spread, Glide (e Dry/Wet, FR-77)
    // diventano manopole rotative su Main, per coerenza visiva fra loro e
    // con lo stile "performance" della schermata (PRD-UI §3/§6.2). A
    // differenza di setupSlider la label non e' attaccata al componente (va
    // in una riga di intestazione separata sopra la riga di manopole, vedi
    // layoutMain) — un'unica riga di didascalie sopra N manopole allineate.
    void setupKnob (juce::Slider& slider, juce::Label& label, juce::Component& parent)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        parent.addAndMakeVisible (slider);

        label.setJustificationType (juce::Justification::centred);
        parent.addAndMakeVisible (label);
    }

    // FR-79: casella di testo numerica (0-127) + pulsante Learn, non piu'
    // uno slider trascinabile.
    void setupCcRow (juce::TextEditor& editor, juce::Label& label, juce::TextButton& learnButton, juce::Component& parent)
    {
        editor.setInputRestrictions (3, "0123456789");
        editor.setJustification (juce::Justification::centredRight);
        parent.addAndMakeVisible (editor);

        label.attachToComponent (&editor, true);
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
    : AudioProcessorEditor (&p), processorRef (p), rootNoteGrid (p), presetListEditor (p), presetTableEditor (p)
{
    auto& apvtsRef = processorRef.apvts;

    // FR-73: barra di navigazione a 3 pulsanti — figli di *this, non di una
    // pagina, cosi' restano visibili su tutte e tre le schermate (a
    // differenza della lettera originale di §8.1, che la rendeva
    // raggiungibile solo da Main).
    addAndMakeVisible (navMainButton);
    addAndMakeVisible (navEditButton);
    addAndMakeVisible (navSettingsButton);
    navMainButton.onClick     = [this] { showPage (Page::main); };
    navEditButton.onClick     = [this] { showPage (Page::edit); };
    navSettingsButton.onClick = [this] { showPage (Page::settings); };

    addAndMakeVisible (mainPage);
    addAndMakeVisible (editPage);
    addAndMakeVisible (settingsPage);

    // =================== Main (FR-74) ===================
    mainPage.addAndMakeVisible (rootNoteGrid);
    mainPage.addAndMakeVisible (rootNoteLabel);
    rootNoteGrid.onNoteClicked = [this] (int pitchClass) { selectRootNote (pitchClass); };

    mainPage.addAndMakeVisible (presetListViewport);
    presetListViewport.setViewedComponent (&presetListEditor, false);
    presetListViewport.setScrollBarsShown (true, false); // solo verticale
    mainPage.addAndMakeVisible (presetLabel);
    presetListEditor.onRowClicked = [this] (int index) { selectPresetIndex (index); };
    // Un passo di drag ha gia' applicato movePreset: si tiene la selezione
    // agganciata al preset trascinato, stessa chiamata gia' usata prima di
    // questa sessione da moveUpButton/moveDownButton.
    presetListEditor.onReordered = [this] (int newIndex) { selectPresetIndex (newIndex); };

    // FR-78/§6.2: stabilityKnob e' uno Slider su un AudioParameterChoice —
    // il range del widget copre gli indici delle scelte (0..N-1), non i
    // valori grezzi; SliderParameterAttachment usa
    // RangedAudioParameter::getText per popolare il testo del knob (mostra
    // "Fast"/"Balanced"/... non un numero, vedi PRD-UI.md §6.2).
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvtsRef.getParameter ("stabilityLevel")))
        stabilityKnob.setRange (0.0, (double) (choiceParam->choices.size() - 1), 1.0);

    setupKnob (dryWetKnob,        dryWetLabel,        mainPage);
    setupKnob (stabilityKnob,     stabilityLabel,     mainPage);
    setupKnob (formantSpreadSlider, formantSpreadLabel, mainPage);
    setupKnob (glideTimeSlider,   glideLabel,          mainPage);
    setupKnob (numVoicesSlider,   numVoicesLabel,      mainPage);

    // FR-77: dryWetKnob resta agganciato a "wetLevel" in questo passo (vedi
    // PluginEditor.h) — non e' ancora il crossfade dryWetMix (Passo 2,
    // cambia processBlock, va confermato all'ascolto).
    dryWetAttachment        = std::make_unique<SliderAttachment> (apvtsRef, "wetLevel",       dryWetKnob);
    stabilityAttachment     = std::make_unique<SliderAttachment> (apvtsRef, "stabilityLevel", stabilityKnob);
    formantSpreadAttachment = std::make_unique<SliderAttachment> (apvtsRef, "formantSpread",  formantSpreadSlider);
    glideTimeAttachment     = std::make_unique<SliderAttachment> (apvtsRef, "glideTimeMs",    glideTimeSlider);
    numVoicesAttachment     = std::make_unique<SliderAttachment> (apvtsRef, "numVoices",      numVoicesSlider);

    // FR-30/34/36: bypass e' un parametro APVTS come gli altri (automatizzabile
    // dall'host); il CC lo mette in override esattamente come root/preset
    // (vedi PluginProcessor::processBlock, OverrideManager). Il bottone qui
    // serve a testare senza un controller MIDI a disposizione.
    bypassToggle.setButtonText ("Bypass");
    mainPage.addAndMakeVisible (bypassToggle);
    bypassAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "bypass", bypassToggle);

    // FR-24/28: interruttore Harmonizer/Play.
    playModeToggle.setButtonText ("Play Mode");
    mainPage.addAndMakeVisible (playModeToggle);
    playModeAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "playModeEnabled", playModeToggle);

    // FR-83: versione sintetica della nota rilevata (solo nome nota o "--").
    detectedShortLabel.attachToComponent (&detectedShortValueLabel, true);
    mainPage.addAndMakeVisible (detectedShortLabel);
    mainPage.addAndMakeVisible (detectedShortValueLabel);

    activeVoicesLabel.attachToComponent (&activeVoicesValueLabel, true);
    mainPage.addAndMakeVisible (activeVoicesLabel);
    mainPage.addAndMakeVisible (activeVoicesValueLabel);

    // =================== Edit (FR-81) ===================
    editPage.addAndMakeVisible (presetNameEditor);
    nameLabel.attachToComponent (&presetNameEditor, true);
    editPage.addAndMakeVisible (nameLabel);
    presetNameEditor.onReturnKey = [this] { commitRename(); };
    presetNameEditor.onFocusLost = [this] { commitRename(); };

    editPage.addAndMakeVisible (presetTableEditor);

    for (auto* b : { &addButton, &duplicateButton, &deleteButton,
                     &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton })
        editPage.addAndMakeVisible (b);

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

    // Sessione 23 (FR-11/§8.1): striscia unica "voci" — colonna V1..V8,
    // Fix/Fmt/Pan/Gain impilati. voiceColumnHeaders e' solo testo, nessuna
    // interazione. Sessione 25: il blocco si sposta INTERO su Edit
    // (PRD-UI §4), stesso ordine verticale, nessuno split.
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& header = voiceColumnHeaders[(size_t) v];
        header.setText (juce::String (v + 1), juce::dontSendNotification);
        header.setJustificationType (juce::Justification::centred);
        editPage.addAndMakeVisible (header);
    }

    editPage.addAndMakeVisible (fixMoveLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& button = voiceFixButtons[(size_t) v];
        button.setButtonText (juce::String (v + 1));
        button.setClickingTogglesState (true);
        editPage.addAndMakeVisible (button);
        voiceFixAttachments[(size_t) v] = std::make_unique<ButtonAttachment> (apvtsRef, "voiceFix" + juce::String (v + 1), button);
    }

    editPage.addAndMakeVisible (voiceFormantLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& slider = voiceFormantSliders[(size_t) v];
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        editPage.addAndMakeVisible (slider);
        voiceFormantAttachments[(size_t) v] = std::make_unique<SliderAttachment> (apvtsRef, "voiceFormantOffset" + juce::String (v + 1), slider);
    }

    editPage.addAndMakeVisible (voicePanLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& slider = voicePanSliders[(size_t) v];
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        editPage.addAndMakeVisible (slider);
        voicePanAttachments[(size_t) v] = std::make_unique<SliderAttachment> (apvtsRef, "voicePan" + juce::String (v + 1), slider);
    }

    editPage.addAndMakeVisible (voiceGainLabel);
    for (int v = 0; v < harmony::numVoices; ++v)
    {
        auto& slider = voiceGainSliders[(size_t) v];
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        editPage.addAndMakeVisible (slider);
        voiceGainAttachments[(size_t) v] = std::make_unique<SliderAttachment> (apvtsRef, "voiceGain" + juce::String (v + 1), slider);
    }

    // Sessione 10: vedi Phrase.h per la semantica completa.
    keepTailsToggle.setButtonText ("Keep Tails");
    editPage.addAndMakeVisible (keepTailsToggle);
    keepTailsAttachment = std::make_unique<ButtonAttachment> (apvtsRef, "keepPhraseTails", keepTailsToggle);

    // =================== Impostazioni (FR-82) ===================
    // FR-32: canale MIDI, omni come default (itemId 1). itemId N+1 = canale N.
    midiChannelBox.addItem ("Omni", 1);
    for (int ch = 1; ch <= 16; ++ch)
        midiChannelBox.addItem (juce::String (ch), ch + 1);
    settingsPage.addAndMakeVisible (midiChannelBox);
    midiChannelLabel.attachToComponent (&midiChannelBox, true);
    settingsPage.addAndMakeVisible (midiChannelLabel);
    midiChannelBox.onChange = [this]
    {
        const int itemId = midiChannelBox.getSelectedId();
        processorRef.getCcRouter().setMidiChannel (itemId <= 1 ? 0 : itemId - 1);
    };

    // FR-30/31/33/79: numero CC per funzione (casella di testo numerica, non
    // piu' slider trascinabile) + MIDI Learn. Non sono
    // AudioProcessorValueTreeState::SliderAttachment: i numeri CC non sono
    // parametri APVTS (vedi PluginProcessor::getStateInformation), quindi le
    // caselle si collegano direttamente a CcRouter e si risincronizzano dal
    // timer esistente (syncCcControlsFromRouter).
    setupCcRow (rootCcEditor, rootCcLabel, learnRootButton, settingsPage);
    setupCcRow (presetCcEditor, presetCcLabel, learnPresetButton, settingsPage);
    setupCcRow (bypassCcEditor, bypassCcLabel, learnBypassButton, settingsPage);

    auto commitRootCc = [this]
    {
        const int v = juce::jlimit (0, 127, rootCcEditor.getText().getIntValue());
        rootCcEditor.setText (juce::String (v), juce::dontSendNotification);
        processorRef.getCcRouter().setRootCc (v);
    };
    rootCcEditor.onReturnKey = commitRootCc;
    rootCcEditor.onFocusLost = commitRootCc;

    auto commitPresetCc = [this]
    {
        const int v = juce::jlimit (0, 127, presetCcEditor.getText().getIntValue());
        presetCcEditor.setText (juce::String (v), juce::dontSendNotification);
        processorRef.getCcRouter().setPresetCc (v);
    };
    presetCcEditor.onReturnKey = commitPresetCc;
    presetCcEditor.onFocusLost = commitPresetCc;

    auto commitBypassCc = [this]
    {
        const int v = juce::jlimit (0, 127, bypassCcEditor.getText().getIntValue());
        bypassCcEditor.setText (juce::String (v), juce::dontSendNotification);
        processorRef.getCcRouter().setBypassCc (v);
    };
    bypassCcEditor.onReturnKey = commitBypassCc;
    bypassCcEditor.onFocusLost = commitBypassCc;

    learnRootButton.onClick   = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::root); };
    learnPresetButton.onClick = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::preset); };
    learnBypassButton.onClick = [this] { processorRef.getCcRouter().startLearning (CcRouter::LearnTarget::bypass); };

    // FR-51: tetto voci simultanee — resta uno slider lineare, non fa parte
    // del gruppo FR-78.
    setupSlider (maxVoicesSlider, maxVoicesLabel, settingsPage);
    maxVoicesAttachment = std::make_unique<SliderAttachment> (apvtsRef, "maxSimultaneousVoices", maxVoicesSlider);

    // FR-83: diagnostica completa (confidenza, stabile/instabile, gate,
    // late-bindings, block size) — spostata qui da Main, testo invariato.
    detectedLabel.attachToComponent (&detectedValueLabel, true);
    settingsPage.addAndMakeVisible (detectedLabel);
    settingsPage.addAndMakeVisible (detectedValueLabel);

    syncCcControlsFromRouter();

    presetListEditor.refresh();
    syncPresetSelectionFromParameter();

    showPage (Page::main);

    setResizable (true, true);
    // Sessione 25 (PRD-UI.md, "Passo 1"): il pannello piatto da 1428px al
    // minimo (tutti i controlli impilati in un'unica colonna) diventa tre
    // schermate, dimensionate sulla piu' alta delle tre (verosimilmente
    // Edit: tabella 12x8 + due righe di bottoni + striscia voci a 4 righe).
    // Beneficio collaterale atteso in PRD-UI §8 (stima 550-650px) — qui
    // confermato per calcolo sulla somma delle righe di layoutEdit().
    setResizeLimits (520, 620, 1200, 900);
    setSize (900, 660);

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

    // FR-73: barra di navigazione a 3 pulsanti, sempre visibile.
    auto navBar = area.removeFromTop (28);
    layoutRowOfButtons (navBar, { &navMainButton, &navEditButton, &navSettingsButton });
    area.removeFromTop (10);

    // Le tre pagine condividono sempre gli stessi bounds (PRD-UI §2:
    // cambiare schermata non ridimensiona la finestra ne' fa scattare il
    // layout) — showPage() alterna solo la visibilita'.
    mainPage.setBounds (area);
    editPage.setBounds (area);
    settingsPage.setBounds (area);

    layoutMain (mainPage.getLocalBounds());
    layoutEdit (editPage.getLocalBounds());
    layoutSettings (settingsPage.getLocalBounds());
}

void HarmonizerAudioProcessorEditor::layoutMain (juce::Rectangle<int> area)
{
    const int labelWidth = 60;
    const int rowHeight = 26;
    const int gap = 6;

    // FR-74/75/76: griglia fondamentale (2x6) a sinistra, lista preset (5
    // righe) a destra, stessa riga — vedi il mockup ASCII di PRD-UI §3.
    {
        constexpr int visiblePresetRows = 5; // FR-76
        constexpr int gridRows = 6;          // FR-75
        constexpr int cellHeight = 26;
        const int topRowHeight = juce::jmax (visiblePresetRows * ui::PresetListEditor::rowHeight,
                                              gridRows * cellHeight);
        auto row = area.removeFromTop (topRowHeight);

        auto leftHalf = row.removeFromLeft (row.getWidth() / 2);
        rootNoteLabel.setBounds (leftHalf.removeFromLeft (labelWidth));
        rootNoteGrid.setBounds (leftHalf);

        presetLabel.setBounds (row.removeFromLeft (labelWidth));
        presetListViewport.setBounds (row);
        presetListEditor.setContentWidth (presetListViewport.getWidth()
                                          - presetListViewport.getScrollBarThickness());
        area.removeFromTop (gap);
    }

    // FR-78: riga di manopole — (Dry/Wet) (Stability) (Fmt Spread) (Glide)
    // (Voices), stesso ordine del mockup ASCII di PRD-UI §3. Una riga di
    // didascalie sopra la riga di manopole vere e proprie (le manopole non
    // usano attachToComponent, vedi setupKnob).
    {
        auto labelRow = area.removeFromTop (14);
        layoutRowOfButtons (labelRow, { &dryWetLabel, &stabilityLabel, &formantSpreadLabel, &glideLabel, &numVoicesLabel });
        area.removeFromTop (2);
        auto knobRow = area.removeFromTop (70);
        layoutRowOfButtons (knobRow, { &dryWetKnob, &stabilityKnob, &formantSpreadSlider, &glideTimeSlider, &numVoicesSlider });
        area.removeFromTop (gap);
    }

    layoutRowOfButtons (area.removeFromTop (rowHeight), { &bypassToggle, &playModeToggle });
    area.removeFromTop (gap);

    // Nota rilevata (sintetica) + voci attive, affiancate.
    {
        auto row = area.removeFromTop (rowHeight);
        const int half = row.getWidth() / 2;
        detectedShortLabel.setBounds (row.removeFromLeft (labelWidth));
        detectedShortValueLabel.setBounds (row.removeFromLeft (juce::jmax (0, half - labelWidth)));
        activeVoicesLabel.setBounds (row.removeFromLeft (labelWidth));
        activeVoicesValueLabel.setBounds (row);
    }
}

void HarmonizerAudioProcessorEditor::layoutEdit (juce::Rectangle<int> area)
{
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

    layoutRow (presetNameEditor);

    {
        // Sessione M5 (editor tabella preset, §8.2): altezza fissa =
        // intestazione di grado + 8 righe di voce — deve restare in sync con
        // headerRowHeight/cellRowHeight in PresetTableEditor::resized().
        constexpr int presetTableHeight = 22 + 8 * 24;
        presetTableEditor.setBounds (area.removeFromTop (presetTableHeight));
        area.removeFromTop (gap);
    }

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &addButton, &duplicateButton, &deleteButton });
    area.removeFromTop (gap);

    layoutRowOfButtons (area.removeFromTop (rowHeight),
                        { &importCsvButton, &exportCsvButton, &loadGlobalButton, &saveGlobalButton });
    area.removeFromTop (gap);

    // Sessione 23 (FR-11/§8.1): striscia unica "voci" — una colonna per
    // voce (V1..V8), Fix/Fmt/Pan/Gain impilati verticalmente. Sessione 25:
    // il blocco intero vive su Edit (PRD-UI §4), nessuno split fra schermate.
    {
        const int headerRowHeight = 16;
        auto row = area.removeFromTop (headerRowHeight);
        row.removeFromLeft (labelWidth); // nessuna label a sinistra dell'intestazione
        std::vector<juce::Component*> headers;
        for (auto& h : voiceColumnHeaders)
            headers.push_back (&h);
        layoutRowOfButtons (row, headers);
    }
    area.removeFromTop (gap);

    {
        auto row = area.removeFromTop (rowHeight);
        fixMoveLabel.setBounds (row.removeFromLeft (labelWidth));
        std::vector<juce::Component*> buttons;
        for (auto& b : voiceFixButtons)
            buttons.push_back (&b);
        layoutRowOfButtons (row, buttons);
    }
    area.removeFromTop (gap);

    // Riga piu' alta delle altre: una manopola rotativa ha bisogno di piu'
    // di 26px per essere leggibile. Stessa altezza per Fmt/Pan/Gain.
    const int knobRowHeight = 36;

    {
        auto row = area.removeFromTop (knobRowHeight);
        voiceFormantLabel.setBounds (row.removeFromLeft (labelWidth));
        std::vector<juce::Component*> knobs;
        for (auto& s : voiceFormantSliders)
            knobs.push_back (&s);
        layoutRowOfButtons (row, knobs);
    }
    area.removeFromTop (gap);

    {
        auto row = area.removeFromTop (knobRowHeight);
        voicePanLabel.setBounds (row.removeFromLeft (labelWidth));
        std::vector<juce::Component*> knobs;
        for (auto& s : voicePanSliders)
            knobs.push_back (&s);
        layoutRowOfButtons (row, knobs);
    }
    area.removeFromTop (gap);

    {
        auto row = area.removeFromTop (knobRowHeight);
        voiceGainLabel.setBounds (row.removeFromLeft (labelWidth));
        std::vector<juce::Component*> knobs;
        for (auto& s : voiceGainSliders)
            knobs.push_back (&s);
        layoutRowOfButtons (row, knobs);
    }
    area.removeFromTop (gap);

    layoutRow (keepTailsToggle);
}

void HarmonizerAudioProcessorEditor::layoutSettings (juce::Rectangle<int> area)
{
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

    auto layoutCcRow = [&] (juce::TextEditor& editor, juce::TextButton& button)
    {
        auto row = area.removeFromTop (rowHeight);
        row.removeFromLeft (labelWidth);
        auto buttonArea = row.removeFromRight (70);
        button.setBounds (buttonArea);
        row.removeFromRight (4);
        editor.setBounds (row);
        area.removeFromTop (gap);
    };

    layoutCcRow (rootCcEditor, learnRootButton);
    layoutCcRow (presetCcEditor, learnPresetButton);
    layoutCcRow (bypassCcEditor, learnBypassButton);

    layoutRow (midiChannelBox);
    layoutRow (maxVoicesSlider);

    layoutRow (detectedValueLabel);
}

void HarmonizerAudioProcessorEditor::showPage (Page page)
{
    currentPage = page;
    mainPage.setVisible (page == Page::main);
    editPage.setVisible (page == Page::edit);
    settingsPage.setVisible (page == Page::settings);

    navMainButton.setToggleState (page == Page::main, juce::dontSendNotification);
    navEditButton.setToggleState (page == Page::edit, juce::dontSendNotification);
    navSettingsButton.setToggleState (page == Page::settings, juce::dontSendNotification);
}

void HarmonizerAudioProcessorEditor::timerCallback()
{
    // presetListEditor legge sempre il modello dal vivo in paint(): a
    // differenza della vecchia ComboBox non serve un diff sui nomi per
    // decidere se aggiornare, un repaint periodico basta ed e' innocuo
    // (nessun componente figlio da ricreare).
    presetListEditor.refresh();
    syncPresetSelectionFromParameter();
    syncRootNoteFromParameter();
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

    // FR-83: versione sintetica su Main — solo il nome della nota, o "--".
    detectedShortValueLabel.setText (
        midiNote >= 0.0f ? juce::MidiMessage::getMidiNoteName (juce::roundToInt (midiNote), true, true, 3)
                          : juce::String ("--"),
        juce::dontSendNotification);

    // FR-83: versione diagnostica completa su Impostazioni — testo invariato.
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

    // Non toccare una casella mentre l'utente ci sta scrivendo dentro:
    // eviterebbe che il numero "scatti" sotto le dita durante il polling a
    // 15Hz — stesso principio gia' in uso per presetNameEditor.
    if (! rootCcEditor.hasKeyboardFocus (false))
        rootCcEditor.setText (juce::String (router.getRootCc()), juce::dontSendNotification);
    if (! presetCcEditor.hasKeyboardFocus (false))
        presetCcEditor.setText (juce::String (router.getPresetCc()), juce::dontSendNotification);
    if (! bypassCcEditor.hasKeyboardFocus (false))
        bypassCcEditor.setText (juce::String (router.getBypassCc()), juce::dontSendNotification);

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

void HarmonizerAudioProcessorEditor::syncRootNoteFromParameter()
{
    // Nota: la CC override della fondamentale (FR-36/37) non scrive nel
    // parametro APVTS "rootNote" (risolta a valle in OverrideManager, vedi
    // PluginProcessor::processBlock) — la griglia non riflette un override
    // CC attivo, stessa limitazione gia' nota per la vecchia ComboBox
    // (handsoff.md §6). Qui si riflette solo il parametro (automazione host,
    // caricamento stato, click).
    auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter ("rootNote"));
    if (choiceParam == nullptr)
        return;

    rootNoteGrid.setSelectedIndex (choiceParam->getIndex());
}

void HarmonizerAudioProcessorEditor::selectRootNote (int pitchClass)
{
    auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter ("rootNote"));
    if (choiceParam == nullptr)
        return;

    *choiceParam = pitchClass;
    rootNoteGrid.setSelectedIndex (pitchClass); // riscontro immediato al click, non aspetta il prossimo tick del timer
}
