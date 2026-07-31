#pragma once

#include "PluginProcessor.h"

// M0/M1/M2 — editor minimo con controlli diretti (ComboBox/Slider/bottoni),
// sufficiente per gestire la libreria preset e testare l'armonizzazione in un
// host senza hardware MIDI CC (M4) ne' le tre schermate definitive (§8 del
// PRD, lavoro di M5). Il riordino qui e' con bottoni Su/Giu', non drag&drop
// (FR-06 vero e proprio e' UI di M5).
class HarmonizerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit HarmonizerAudioProcessorEditor (HarmonizerAudioProcessor&);
    ~HarmonizerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBoxFromLibrary();
    void syncPresetSelectionFromParameter();
    void syncCcControlsFromRouter();
    void commitRename();
    void selectPresetIndex (int index);

    HarmonizerAudioProcessor& processorRef;

    juce::ComboBox rootNoteBox;
    juce::ComboBox presetBox;
    juce::ComboBox stabilityBox;
    juce::Slider numVoicesSlider;
    juce::Slider dryLevelSlider;
    juce::Slider wetLevelSlider;
    juce::Slider glideTimeSlider;
    juce::Slider maxVoicesSlider; // FR-51: tetto voci simultanee tra tutte le frasi
    juce::Slider formantSpreadSlider; // FR-40
    juce::TextEditor presetNameEditor;
    juce::Label activeVoicesValueLabel; // FR-53: aggiornata dal timer, non un parametro

    // FR-23: Fix/Move per singola voce — un bottone a due stati per voce.
    std::array<juce::ToggleButton, harmony::numVoices> voiceFixButtons;
    // FR-41: offset formantico manuale per voce, in semitoni-equivalenti.
    std::array<juce::Slider, harmony::numVoices> voiceFormantSliders;

    // FR-31/32/33: numeri CC (non parametri APVTS, vedi PluginProcessor —
    // sono configurazione di routing, non valori automatizzabili) + MIDI
    // Learn. Sincronizzati dal timer esistente (15Hz), non un'attachment.
    juce::ComboBox midiChannelBox;
    juce::Slider rootCcSlider, presetCcSlider, bypassCcSlider;
    juce::TextButton learnRootButton   { "Learn" };
    juce::TextButton learnPresetButton { "Learn" };
    juce::TextButton learnBypassButton { "Learn" };
    // FR-30/34: bypass E' un parametro APVTS (automatizzabile), il CC lo
    // mette in override come gli altri — questo toggle serve per testare
    // senza hardware MIDI.
    juce::ToggleButton bypassToggle;
    // FR-24/28: interruttore Harmonizer/Play.
    juce::ToggleButton playModeToggle;

    juce::TextButton addButton          { "Add" };
    juce::TextButton duplicateButton    { "Duplicate" };
    juce::TextButton deleteButton       { "Delete" };
    juce::TextButton moveUpButton       { "Up" };
    juce::TextButton moveDownButton     { "Down" };
    juce::TextButton importCsvButton    { "Import CSV" };
    juce::TextButton exportCsvButton    { "Export CSV" };
    juce::TextButton loadGlobalButton   { "Load Global" };
    juce::TextButton saveGlobalButton   { "Save As Global" };

    juce::Label rootNoteLabel   { {}, "Root" };
    juce::Label presetLabel     { {}, "Chord" };
    juce::Label numVoicesLabel  { {}, "Voices" };
    juce::Label dryLevelLabel   { {}, "Dry" };
    juce::Label wetLevelLabel   { {}, "Wet" };
    juce::Label nameLabel       { {}, "Name" };
    juce::Label stabilityLabel  { {}, "Stability" };
    juce::Label glideLabel      { {}, "Glide ms" };
    juce::Label fixMoveLabel    { {}, "Fix/Move" };
    juce::Label maxVoicesLabel  { {}, "Voice Cap" };
    juce::Label activeVoicesLabel { {}, "Active" };
    juce::Label formantSpreadLabel { {}, "Fmt Spread" };
    juce::Label voiceFormantLabel  { {}, "Fmt/Voice" };
    juce::Label midiChannelLabel { {}, "MIDI Ch" };
    juce::Label rootCcLabel      { {}, "CC Root" };
    juce::Label presetCcLabel    { {}, "CC Chord" };
    juce::Label bypassCcLabel    { {}, "CC Bypass" };
    juce::Label bypassLabel      { {}, "Bypass" };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment> rootNoteAttachment;
    std::unique_ptr<ComboAttachment> stabilityAttachment;
    std::unique_ptr<SliderAttachment> numVoicesAttachment;
    std::unique_ptr<SliderAttachment> dryLevelAttachment;
    std::unique_ptr<SliderAttachment> wetLevelAttachment;
    std::unique_ptr<SliderAttachment> glideTimeAttachment;
    std::unique_ptr<SliderAttachment> maxVoicesAttachment;
    std::unique_ptr<SliderAttachment> formantSpreadAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, harmony::numVoices> voiceFixAttachments;
    std::array<std::unique_ptr<SliderAttachment>, harmony::numVoices> voiceFormantAttachments;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> playModeAttachment;

    // presetBox non ha un ComboBoxAttachment: la libreria puo' cambiare
    // dimensione a runtime, cosa che le "choices" fisse di un parametro APVTS
    // non supportano (vedi createParameterLayout — presetIndex e' un Int, non
    // una Choice). Sincronizzazione manuale via polling (Timer).
    int lastKnownPresetCount = -1;
    int lastSyncedSelectedIndex = -1;
    bool ignoreComboCallback = false;

    // std::unique_ptr cosi' il FileChooser resta vivo per la durata della
    // callback asincrona (obbligatorio con l'API async di JUCE).
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessorEditor)
};
