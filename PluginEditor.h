#pragma once
#include "PluginProcessor.h"

class MiSinteEditor : public juce::AudioProcessorEditor
{
public:
    explicit MiSinteEditor (MiSinteProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramID);

    MiSinteProcessor& proc;

    Knob volume, attack, decay, sustain, release;
    juce::ComboBox waveBox;
    juce::Label waveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAttach;
};
