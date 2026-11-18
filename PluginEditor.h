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

    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                    const juce::String& suffix = {}, int decimals = -1);
    void layoutRow (std::initializer_list<Knob*> knobs, int labelY);

    MiSinteProcessor& proc;

    // Oscilador y amplitud
    Knob volume, attack, decay, sustain, release;
    juce::ComboBox waveBox;
    juce::Label waveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAttach;

    // Filtro
    Knob cutoff, resonance, envAmt, fAttack, fDecay, fSustain, fRelease;
    juce::ComboBox filterBox;
    juce::Label filterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterAttach;

    // Unison, drift y glide
    Knob unisonVoices, unisonDetune, unisonStereo, drift, glide;
    juce::ComboBox unisonModeBox;
    juce::Label unisonModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> unisonModeAttach;
};
