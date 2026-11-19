#pragma once
#include "PluginProcessor.h"
#include "WaveformDisplay.h"
#include "FilterResponseDisplay.h"

class MiSinteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MiSinteEditor (MiSinteProcessor&);
    ~MiSinteEditor() override = default;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
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

    void timerCallback() override;
    void addToPage (int page, juce::Component& c);
    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                    const juce::String& suffix = {}, int decimals = -1,
                    juce::Colour accent = juce::Colour (0xffe9b44c), int page = 0);
    void setupLfo (LfoUI& u, int index);
    void chooseLfoWave (int index);
    void placeKnob (Knob& k, int x, int y, int w, int sliderH, float s);
    void layoutLfo (LfoUI& u, int index, float s);
    void showPage (int page);
    void applyEnabledStates();
    static void setKnobEnabled (Knob& k, bool enabled);
    static void scaleLabel (juce::Label& l, float s);

    MiSinteProcessor& proc;
    int currentPage = 0;
    std::vector<juce::Component*> pages[2];
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton pageOscBtn, pageLfoBtn;

    // --- Pagina OSC: oscilador ---
    WaveformDisplay waveDisplay;
    FilterResponseDisplay filterDisplay;
    juce::TextButton syncBtn, resetBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncA, resetA;
    Knob pulseWidth, syncFreq, octave, semi, fine, glide, drift, volume;

    // Unison
    Knob unisonVoices, unisonDetune, unisonSpread, harmTilt;
    juce::ComboBox unisonModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> unisonModeAttach;

    // Filtro (con su envolvente)
    juce::ComboBox filterBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterAttach;
    Knob cutoff, reso, envAmt, fAttack, fDecay, fSustain, fRelease;

    // Envolvente de amplitud
    Knob attack, decay, sustain, release;

    // --- Pagina LFOs ---
    LfoUI lfoUI[kNumLfos];

    // Estado para atenuar los knobs que no aplican
    std::atomic<float>* waveRaw = nullptr;
    std::atomic<float>* syncRaw = nullptr;
    std::atomic<float>* modeRaw = nullptr;
    int cachedWave = -1;
    int cachedMode = -1;
    bool cachedSync = false;
    bool cachedValid = false;
};
