#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ParamIDs
{
    static const juce::String rootNote { "rootNote" };
    static const juce::String presetIndex { "presetIndex" };
    static const juce::String numVoices { "numVoices" };
    static const juce::String dryLevel { "dryLevel" };
    static const juce::String wetLevel { "wetLevel" };
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerAudioProcessor::createParameterLayout()
{
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Fondamentale e preset sono parametri discreti a passi (FR-35): niente
    // rampe di automazione che attraverserebbero voicing intermedi indesiderati.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::rootNote, 1 }, "Root Note", noteNames, 0));

    // presetIndex e' un intero 1-based su un range FISSO (1..maxPresets), non
    // un AudioParameterChoice: la libreria di preset puo' cambiare dimensione
    // a runtime (aggiunte/rimozioni/riordino da UI), mentre le choices di un
    // parametro APVTS non possono. Il valore coincide gia' concettualmente
    // col futuro CC posizionale (FR-05): valori oltre la libreria attuale
    // vengono ignorati in processBlock, esattamente come da FR-30 per il CC.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::presetIndex, 1 }, "Chord Preset", 1, harmony::PresetLibrary::maxPresets, 1));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::numVoices, 1 }, "Num Voices", 1, harmony::numVoices, 4));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::dryLevel, 1 }, "Dry Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::wetLevel, 1 }, "Wet Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

    return { params.begin(), params.end() };
}

HarmonizerAudioProcessor::HarmonizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
    , currentPresetLibrary (std::make_shared<const harmony::PresetLibrary>())
{
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor() = default;

void HarmonizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Margine oltre il range dichiarato in NFR-03 (32-4096, anche variabile):
    // dimensioniamo sul caso peggiore per non riallocare mai in processBlock.
    constexpr int absoluteMaxBlockSize = 8192;
    const int scratchSize = juce::jmax (samplesPerBlock, absoluteMaxBlockSize);

    monoInputScratch.setSize (1, scratchSize, false, false, true);
    voicesMixScratch.setSize (1, scratchSize, false, false, true);

    pitchDetector.prepare (sampleRate);
    voicePool.prepare (sampleRate, scratchSize);

    // SpectralShifter (motore interinale) ha una latenza reale non banale
    // (STFT): dichiararla e' necessario perche' l'host possa compensarla.
    setLatencySamples (voicePool.getLatencySamples());
}

void HarmonizerAudioProcessor::releaseResources()
{
}

bool HarmonizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != mono && in != stereo)
        return false;

    if (out != mono && out != stereo)
        return false;

    return true;
}

void HarmonizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    if (numSamples == 0)
        return;

    // 1. Downmix a mono: il prodotto assume una sorgente monofonica in ingresso
    // (PRD §3.1, "Audio in (mono)"). Anche il percorso dry usa questo segnale,
    // cosi' rimane allineato in fase con le voci (FR-58) senza bisogno di una
    // delay line dedicata in questo M0/M1.
    monoInputScratch.setSize (1, numSamples, false, false, true);
    auto* mono = monoInputScratch.getWritePointer (0);

    if (numInputChannels >= 2)
    {
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (left[i] + right[i]);
    }
    else if (numInputChannels == 1)
    {
        const auto* src = buffer.getReadPointer (0);
        std::copy (src, src + numSamples, mono);
    }
    else
    {
        std::fill (mono, mono + numSamples, 0.0f);
    }

    for (int i = 0; i < numSamples; ++i)
        pitchDetector.pushSample (mono[i]);

    const int rootPitchClass = (int) *apvts.getRawParameterValue (ParamIDs::rootNote);
    const int numActiveVoices = (int) *apvts.getRawParameterValue (ParamIDs::numVoices);
    const float dryLevel = *apvts.getRawParameterValue (ParamIDs::dryLevel);
    const float wetLevel = *apvts.getRawParameterValue (ParamIDs::wetLevel);

    // Snapshot immutabile: sicuro da leggere sull'audio thread (vedi header).
    const auto presetLibrary = getPresetLibrary();
    const int presetOneBased = (int) *apvts.getRawParameterValue (ParamIDs::presetIndex);
    // Valori oltre la lunghezza attuale della libreria vengono ignorati
    // (stesso comportamento del futuro CC posizionale, FR-30).
    const int presetIndex = juce::jlimit (0, presetLibrary->getNumPresets() - 1, presetOneBased - 1);

    std::array<harmony::Cell, harmony::numVoices> offsets {};
    if (pitchDetector.hasStableSignal())
    {
        const int playedNote = juce::roundToInt (pitchDetector.getMidiNote());
        offsets = harmony::HarmonyEngine::getOffsets (presetLibrary->getPreset (presetIndex), playedNote, rootPitchClass);
    }
    // Altrimenti offsets resta tutto nullopt: silenzio sulle voci finche' non
    // c'e' un pitch stabile (placeholder per il fade morbido di FR-20).

    voicesMixScratch.setSize (1, numSamples, false, false, true);
    voicePool.process (mono, voicesMixScratch.getWritePointer (0), numSamples, offsets, numActiveVoices);
    const auto* voicesMix = voicesMixScratch.getReadPointer (0);

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* out = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            out[i] = dryLevel * mono[i] + wetLevel * voicesMix[i];
    }
}

juce::AudioProcessorEditor* HarmonizerAudioProcessor::createEditor()
{
    return new HarmonizerAudioProcessorEditor (*this);
}

bool HarmonizerAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String HarmonizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HarmonizerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool HarmonizerAudioProcessor::producesMidi() const
{
    return false;
}

bool HarmonizerAudioProcessor::isMidiEffect() const
{
    return false;
}

double HarmonizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HarmonizerAudioProcessor::getNumPrograms()
{
    return 1;
}

int HarmonizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HarmonizerAudioProcessor::setCurrentProgram (int)
{
}

const juce::String HarmonizerAudioProcessor::getProgramName (int)
{
    return {};
}

void HarmonizerAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void HarmonizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // FR-08: la libreria di preset si serializza dentro lo stato del plugin
    // (sessione host), non solo un riferimento a file esterni. Riordinare la
    // libreria globale non altera percio' i progetti gia' salvati.
    juce::ValueTree root ("HarmonizerState");
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (getPresetLibrary()->toValueTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid() || ! root.hasType ("HarmonizerState"))
        return;

    if (auto paramsTree = root.getChildWithName (apvts.state.getType()); paramsTree.isValid())
        apvts.replaceState (paramsTree);

    if (auto libTree = root.getChildWithName ("PresetLibrary"); libTree.isValid())
        editPresetLibrary ([&] (harmony::PresetLibrary& lib) { lib.loadFromValueTree (libTree); });
}

std::shared_ptr<const harmony::PresetLibrary> HarmonizerAudioProcessor::getPresetLibrary() const noexcept
{
    const juce::SpinLock::ScopedLockType sl (presetLibraryLock);
    return currentPresetLibrary;
}

void HarmonizerAudioProcessor::editPresetLibrary (const std::function<void (harmony::PresetLibrary&)>& mutator)
{
    // Non deve mai essere chiamata dall'audio thread (vedi commento in
    // header); alcuni host possono chiamare setStateInformation da un thread
    // di caricamento diverso dal message thread ma comunque non concorrente
    // con processBlock, quindi qui non si asserisce il message thread in
    // modo rigido — solo la garanzia "mai sull'audio thread" e' richiesta.
    auto mutated = std::make_shared<harmony::PresetLibrary> (*getPresetLibrary());
    mutator (*mutated);

    std::shared_ptr<const harmony::PresetLibrary> old;
    {
        const juce::SpinLock::ScopedLockType sl (presetLibraryLock);
        old = currentPresetLibrary;
        currentPresetLibrary = std::move (mutated);
    }
    // "old" resta viva qui (message thread) finche' non sostituiamo il valore
    // precedente di retiredPresetLibrary: la sua distruzione avviene quindi
    // sul message thread nel caso normale, mai nell'audio thread (PRD §9.4).
    retiredPresetLibrary = std::move (old);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerAudioProcessor();
}
