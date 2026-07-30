#include "PluginProcessor.h"
#include "PluginEditor.h"

HarmonizerAudioProcessor::HarmonizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor() = default;

void HarmonizerAudioProcessor::prepareToPlay (double, int)
{
    // M0: nessuna allocazione richiesta ancora — il DSP arriva in M1/M2/M3.
    // Quando arriva, ogni buffer va dimensionato qui sul caso peggiore (§9.4 PRD).
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

void HarmonizerAudioProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // Placeholder M0: dry passthrough (il buffer in ingresso non viene toccato).
    // Nessuna allocazione, nessun lock, nessun I/O — vedi CLAUDE.md regola 1.
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

void HarmonizerAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // Serializzazione reale arriva con state/StateSerializer in M4 (FR-08, NFR-06).
}

void HarmonizerAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerAudioProcessor();
}
