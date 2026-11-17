#include "PluginEditor.h"

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    waveLabel.setText ("Onda", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (waveLabel);

    waveBox.addItemList (juce::StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 1);
    addAndMakeVisible (waveBox);
    waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "wave", waveBox);

    setupKnob (volume,  "Volumen", "volume");
    setupKnob (attack,  "Ataque",  "attack");
    setupKnob (decay,   "Decay",   "decay");
    setupKnob (sustain, "Sustain", "sustain");
    setupKnob (release, "Release", "release");

    setSize (560, 280);
}

void MiSinteEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramID)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, k.slider);
}

void MiSinteEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24));
}

void MiSinteEditor::resized()
{
    waveLabel.setBounds (20, 15, 80, 28);
    waveBox.setBounds (110, 15, 150, 28);

    Knob* knobs[] = { &volume, &attack, &decay, &sustain, &release };
    const int w = 104;
    int x = 20;
    for (auto* k : knobs)
    {
        k->label.setBounds (x, 70, w, 22);
        k->slider.setBounds (x, 95, w, 160);
        x += w;
    }
}
