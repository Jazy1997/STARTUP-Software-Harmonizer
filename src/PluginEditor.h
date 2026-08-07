#pragma once

#include "PluginProcessor.h"
#include "ui/PresetTableEditor.h"
#include "ui/PresetListEditor.h"
#include "ui/RootNoteGrid.h"

// Sessione 25 — PRD-UI.md (Main/Edit/Impostazioni, FR-73..83, "Passo 1":
// riorganizzazione strutturale a suono invariato, nessun cambiamento a
// processBlock). Tre schermate con una barra di navigazione a 3 pulsanti
// SEMPRE VISIBILE (FR-73) al posto del vecchio pannello piatto (era un
// placeholder esplicito fin da M0, alto 1428px al minimo con tutti i
// controlli impilati in un'unica colonna).
//
// Storia precedente, invariata in questa sessione: sessione 21 (lista
// preset drag&drop, ui::PresetListEditor), sessione 22 (griglia
// fondamentale, ui::RootNoteGrid — ora 2x6, FR-75), sessione 23 (gain/pan
// per voce). Tutti gli attachment/lambda/logica di sincronizzazione restano
// gli stessi: questa sessione cambia solo QUALE CONTENITORE possiede ogni
// controllo e ALCUNI widget (Stability e i CC di sessione 24, vedi sotto).
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
    void syncPresetSelectionFromParameter();
    void syncRootNoteFromParameter();
    void syncCcControlsFromRouter();
    void commitRename();
    void selectPresetIndex (int index);
    void selectRootNote (int pitchClass);

    // FR-73: navigazione fra le 3 schermate, a un click da qualunque altra.
    enum class Page { main, edit, settings };
    void showPage (Page page);
    Page currentPage = Page::main;

    juce::TextButton navMainButton      { "Main" };
    juce::TextButton navEditButton      { "Edit" };
    juce::TextButton navSettingsButton  { "Impostazioni" };

    // I tre contenitori delle schermate. Membri (non puntatori), stesso
    // pattern gia' in uso per presetListEditor/rootNoteGrid. Ogni controllo
    // esistente diventa figlio di UNO di questi tre invece che di *this
    // direttamente; showPage() ne alterna la visibilita'. I tre condividono
    // sempre gli stessi bounds (vedi resized()): cambiare schermata non
    // ridimensiona la finestra ne' ricalcola il layout.
    juce::Component mainPage, editPage, settingsPage;

    void layoutMain (juce::Rectangle<int> area);
    void layoutEdit (juce::Rectangle<int> area);
    void layoutSettings (juce::Rectangle<int> area);

    HarmonizerAudioProcessor& processorRef;

    // --- Main (FR-74) --------------------------------------------------
    // Sessione 22: griglia cromatica di 12 pulsanti (drag&drop non serve
    // qui, solo click) al posto della ComboBox — stesso principio "lettura
    // dal vivo" di ui::PresetListEditor, nessuna attachment diretta (vedi
    // selectRootNote()/syncRootNoteFromParameter()). Sessione 25: reshape
    // 2x6 (FR-75), API pubblica invariata.
    ui::RootNoteGrid rootNoteGrid;
    // Sessione 21: la lista preset e' ui::PresetListEditor (drag&drop,
    // FR-06/07) dentro un Viewport per lo scroll verticale (fino a
    // PresetLibrary::maxPresets = 128 righe). Sessione 25: 5 righe visibili
    // invece di 7 (FR-76), vive SOLO su Main.
    juce::Viewport presetListViewport;
    ui::PresetListEditor presetListEditor;
    // FR-78: manopola rotativa, non piu' ComboBox — il parametro APVTS
    // resta un AudioParameterChoice (vedi PluginProcessor), cambia solo il
    // widget (SliderAttachment su un AudioParameterChoice funziona come su
    // un AudioParameterInt, vedi PRD-UI.md §6.2).
    juce::Slider stabilityKnob;
    juce::Slider numVoicesSlider;      // FR-78: manopola
    juce::Slider glideTimeSlider;      // FR-78: manopola
    juce::Slider formantSpreadSlider;  // FR-78/FR-40: manopola
    // FR-77: un solo knob "Dry/Wet" su Main. Il parametro dryWetMix vero e
    // proprio e' Passo 2 di questa sessione (cambia processBlock, va
    // confermato all'ascolto — CLAUDE.md regola 12): qui il knob resta
    // agganciato a "wetLevel" come segnaposto funzionante, cosi' il Passo 1
    // non cambia il suono. "dryLevel" resta un parametro APVTS dichiarato e
    // letto in processBlock come oggi (default 1.0, invariato), solo senza
    // un controllo UI dedicato per questo passo — coerente con PRD-UI §3
    // (niente due slider Dry/Wet separati su Main).
    juce::Slider dryWetKnob;
    juce::Label activeVoicesValueLabel; // FR-53: aggiornata dal timer, non un parametro
    // FR-83: versione SINTETICA sintetica su Main (solo nome nota o "--").
    // La versione diagnostica completa vive su Impostazioni
    // (detectedValueLabel sotto).
    juce::Label detectedShortValueLabel;
    // FR-24/28: interruttore Harmonizer/Play.
    juce::ToggleButton playModeToggle;
    // FR-30/34: bypass E' un parametro APVTS (automatizzabile), il CC lo
    // mette in override come gli altri — questo toggle serve per testare
    // senza hardware MIDI.
    juce::ToggleButton bypassToggle;

    // --- Edit (FR-81) ---------------------------------------------------
    juce::TextEditor presetNameEditor;
    // Sessione M5 (editor tabella preset, §8.2): griglia 12x8 editabile per
    // il preset correntemente selezionato. Si ricarica da showPreset(),
    // chiamata da syncPresetSelectionFromParameter() a ogni cambio di
    // selezione (che copre gia' add/duplicate/delete/import/load-global,
    // perche' tutti quei percorsi chiamano selectPresetIndex() a valle).
    ui::PresetTableEditor presetTableEditor;

    juce::TextButton addButton          { "Add" };
    juce::TextButton duplicateButton    { "Duplicate" };
    juce::TextButton deleteButton       { "Delete" };
    juce::TextButton importCsvButton    { "Import CSV" };
    juce::TextButton exportCsvButton    { "Export CSV" };
    juce::TextButton loadGlobalButton   { "Load Global" };
    juce::TextButton saveGlobalButton   { "Save As Global" };

    // Sessione 23 (FR-11/§8.1): striscia unica "voci" — una colonna per
    // voce (V1..V8), con Fix/Fmt/Pan/Gain impilati verticalmente. I quattro
    // array restano paralleli fra loro (stesso indice = stessa voce);
    // voiceColumnHeaders e' solo testo ("1".."8"), nessuna interazione.
    // Sessione 25: il blocco intero si sposta su Edit (PRD-UI §4), senza
    // split fra schermate.
    std::array<juce::Label, harmony::numVoices> voiceColumnHeaders;
    // FR-23: Fix/Move per singola voce — un bottone a due stati per voce.
    std::array<juce::ToggleButton, harmony::numVoices> voiceFixButtons;
    // FR-41: offset formantico manuale per voce, in semitoni-equivalenti.
    std::array<juce::Slider, harmony::numVoices> voiceFormantSliders;
    // FR-11/§8.1: pan per voce, -1..+1 (vedi Voice::processAdd per la legge
    // di pan a potenza costante).
    std::array<juce::Slider, harmony::numVoices> voicePanSliders;
    // FR-11/§8.1: gain per voce, in dB (-60..+6, vedi ParamIDs::voiceGain).
    std::array<juce::Slider, harmony::numVoices> voiceGainSliders;

    // Vedi Phrase.h/PhraseScheduler::setKeepTails — sessione 10.
    juce::ToggleButton keepTailsToggle;

    // --- Impostazioni (FR-82) -------------------------------------------
    // FR-79: numero del CC + casella di testo numerica (non piu' slider
    // trascinabile) + pulsante Learn (invariato). Non sono parametri APVTS
    // (vedi PluginProcessor::getStateInformation): si collegano direttamente
    // a CcRouter e si risincronizzano dal timer esistente
    // (syncCcControlsFromRouter).
    juce::TextEditor rootCcEditor, presetCcEditor, bypassCcEditor;
    juce::TextButton learnRootButton   { "Learn" };
    juce::TextButton learnPresetButton { "Learn" };
    juce::TextButton learnBypassButton { "Learn" };
    // FR-32: canale MIDI (Omni/1-16), ComboBox, invariato.
    juce::ComboBox midiChannelBox;
    // FR-51: tetto voci simultanee tra tutte le frasi — non fa parte del
    // gruppo di manopole FR-78, resta uno slider lineare.
    juce::Slider maxVoicesSlider;
    // FR-83: diagnostica completa (confidenza, stabile/instabile, gate,
    // late-bindings, block size) — esattamente il testo di prima, spostato
    // qui da Main.
    juce::Label detectedValueLabel;

    // --- Label -----------------------------------------------------------
    juce::Label rootNoteLabel   { {}, "Root" };
    juce::Label presetLabel     { {}, "Chord" };
    juce::Label numVoicesLabel  { {}, "Voices" };
    juce::Label dryWetLabel     { {}, "Dry/Wet" };
    juce::Label nameLabel       { {}, "Name" };
    juce::Label stabilityLabel  { {}, "Stability" };
    juce::Label glideLabel      { {}, "Glide ms" };
    juce::Label fixMoveLabel    { {}, "Fix/Move" };
    juce::Label maxVoicesLabel  { {}, "Voice Cap" };
    juce::Label activeVoicesLabel { {}, "Active" };
    juce::Label formantSpreadLabel { {}, "Fmt Spread" };
    juce::Label voiceFormantLabel  { {}, "Fmt/Voice" };
    juce::Label voicePanLabel      { {}, "Pan/Voice" };
    juce::Label voiceGainLabel     { {}, "Gain/Voice" };
    juce::Label detectedLabel { {}, "Detected" };
    juce::Label detectedShortLabel { {}, "Nota" };
    juce::Label midiChannelLabel { {}, "MIDI Ch" };
    juce::Label rootCcLabel      { {}, "CC Root" };
    juce::Label presetCcLabel    { {}, "CC Chord" };
    juce::Label bypassCcLabel    { {}, "CC Bypass" };
    juce::Label bypassLabel      { {}, "Bypass" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // FR-78: stabilityKnob e' uno Slider attaccato a un AudioParameterChoice
    // (vedi PRD-UI.md §6.2) — stesso meccanismo di attach di numVoices/Voice
    // Cap, nessuna ComboBoxAttachment piu' in uso in questo editor.
    std::unique_ptr<SliderAttachment> stabilityAttachment;
    std::unique_ptr<SliderAttachment> numVoicesAttachment;
    std::unique_ptr<SliderAttachment> dryWetAttachment;
    std::unique_ptr<SliderAttachment> glideTimeAttachment;
    std::unique_ptr<SliderAttachment> maxVoicesAttachment;
    std::unique_ptr<SliderAttachment> formantSpreadAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, harmony::numVoices> voiceFixAttachments;
    std::array<std::unique_ptr<SliderAttachment>, harmony::numVoices> voiceFormantAttachments;
    std::array<std::unique_ptr<SliderAttachment>, harmony::numVoices> voicePanAttachments;
    std::array<std::unique_ptr<SliderAttachment>, harmony::numVoices> voiceGainAttachments;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> playModeAttachment;
    std::unique_ptr<ButtonAttachment> keepTailsAttachment;

    // presetListEditor non ha un ComboBoxAttachment/parametro diretto: la
    // libreria puo' cambiare dimensione a runtime, cosa che le "choices"
    // fisse di un parametro APVTS non supportano (vedi createParameterLayout
    // — presetIndex e' un Int, non una Choice). Sincronizzazione manuale via
    // polling (Timer): presetListEditor legge sempre il modello dal vivo in
    // paint(), quindi qui serve tracciare solo l'ultima selezione applicata.
    int lastSyncedSelectedIndex = -1;

    // std::unique_ptr cosi' il FileChooser resta vivo per la durata della
    // callback asincrona (obbligatorio con l'API async di JUCE).
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessorEditor)
};
