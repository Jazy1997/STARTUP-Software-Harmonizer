#include "PluginEditor.h"

HarmonizerAudioProcessorEditor::HarmonizerAudioProcessorEditor (HarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setResizable (true, true);
    setResizeLimits (200, 150, 2000, 1500);
    setSize (400, 300);
}

HarmonizerAudioProcessorEditor::~HarmonizerAudioProcessorEditor() = default;

void HarmonizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("HARMONIZER - M0 placeholder", getLocalBounds(), juce::Justification::centred, 1);
}

void HarmonizerAudioProcessorEditor::resized()
{
}
