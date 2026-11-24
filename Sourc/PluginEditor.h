#pragma once
#include "PluginProcessor.h"
#include "WaveformDisplay.h"
#include "FilterResponseDisplay.h"
#include "EffectsPage.h"

class MiSinteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MiSinteEditor (MiSinteProcessor&);
    ~MiSinteEditor() override = default;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    enum class KnobDisplayMode
    {
        Percent,      // 0 a 100 (sin simbolo %)
        Integer,      // Valor entero discreto (OCT, SEMI, VOICES)
        BipolarCents  // -100 a 0 a 100 (FINE)
    };

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
        juce::ToggleButton retrig, poly;
        juce::TextButton loadBtn;
        Knob rate, amt, fade, phase;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveA, destA, syncA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> retrigA, polyA;
    };

    void timerCallback() override;
    void addToPage (int page, juce::Component& c);
    void setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                    KnobDisplayMode displayMode = KnobDisplayMode::Percent,
                    juce::Colour accent = juce::Colour (0xffe9b44c), int page = 0);
    void setupLfo (LfoUI& u, int index);
    void chooseLfoWave (int index);
    void placeKnob (Knob& k, int x, int y, int w, int sliderH, float s);
    void layoutLfo (LfoUI& u, int index, float s);
    void showPage (int page);
    void applyEnabledStates();
    void updateChordControls();
    void chooseChordFile();
    static void setKnobEnabled (Knob& k, bool enabled);
    static void scaleLabel (juce::Label& l, float s);

    MiSinteProcessor& proc;
    int currentPage = 0;
    std::vector<juce::Component*> pages[3];
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton pageOscBtn, pageLfoBtn, pageFxBtn;

    // --- Pagina OSC: oscilador ---
    WaveformDisplay waveDisplay;
    FilterResponseDisplay filterDisplay;
    juce::TextButton syncBtn, resetBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncA, resetA;
    Knob pulseWidth, syncFreq, octave, semi, fine, glide, drift, volume;

    // Unison
    Knob unisonVoices, unisonDetune, unisonSpread, harmTilt;

    // Modo CHORD (USER): captura desde el teclado, carga desde MIDI y acorde actual
    juce::TextButton captureBtn, loadChordBtn;
    juce::Label chordLabel;
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

    // --- Pagina EFFECTS: cadena reordenable de efectos (por ahora solo el Delay esta implementado) ---
    EffectsPage effectsPage;

    // Estado para atenuar los knobs que no aplican
    std::atomic<float>* waveRaw = nullptr;
    std::atomic<float>* syncRaw = nullptr;
    std::atomic<float>* modeRaw = nullptr;
    int cachedWave = -1;
    int cachedMode = -1;
    int cachedChordVersion = -1;
    bool cachedSync = false;
    bool cachedValid = false;
};
