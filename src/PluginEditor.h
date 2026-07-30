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
    void commitRename();
    void selectPresetIndex (int index);

    HarmonizerAudioProcessor& processorRef;

    juce::ComboBox rootNoteBox;
    juce::ComboBox presetBox;
    juce::Slider numVoicesSlider;
    juce::Slider dryLevelSlider;
    juce::Slider wetLevelSlider;
    juce::TextEditor presetNameEditor;

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

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<ComboAttachment> rootNoteAttachment;
    std::unique_ptr<SliderAttachment> numVoicesAttachment;
    std::unique_ptr<SliderAttachment> dryLevelAttachment;
    std::unique_ptr<SliderAttachment> wetLevelAttachment;

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
