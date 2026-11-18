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

    struct LfoUI
    {
        juce::Label title, waveL, destL, syncL, waveName;
        juce::ComboBox wave, dest, sync;
        juce::ToggleButton retrig;
        juce::TextButton loadBtn;
        Knob rate, amt, fade, phase;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveA, destA, syncA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> retrigA;
    };

    void addToPage (int page, juce::Component& c);
    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                    const juce::String& suffix = {}, int decimals = -1, int page = 0);
    void setupLfo (LfoUI& u, int index);
    void chooseLfoWave (int index);
    void placeKnob (Knob& k, int x, int y, int sliderH, float s);
    void layoutLfo (LfoUI& u, int block, float s);
    void showPage (int page);
    void updateSwarmKnob();
    static void scaleLabel (juce::Label& l, float s);

    MiSinteProcessor& proc;
    int currentPage = 0;
    std::vector<juce::Component*> pages[2];
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton pageSoundBtn, pageModBtn;

    // Pagina 1: sonido. Oscilador y amplitud
    Knob octave, semi, fine, pulseWidth, volume, attack, decay, sustain, release;
    juce::ComboBox waveBox;
    juce::Label waveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAttach;

    // Filtro
    Knob cutoff, resonance, envAmt, fAttack, fDecay, fSustain, fRelease;
    juce::ComboBox filterBox;
    juce::Label filterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterAttach;

    // Unison, drift y glide
    Knob unisonVoices, unisonDetune, unisonStereo, drift, glide, swarmRate;
    juce::ComboBox unisonModeBox;
    juce::Label unisonModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> unisonModeAttach;

    // Pagina 2: modulacion (3 LFOs)
    LfoUI lfoUI[3];
};
