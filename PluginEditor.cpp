#include "PluginEditor.h"

namespace
{
    // Tamano de diseno (100%). Todo se escala a partir de el.
    constexpr int kKnobW = 90;
    constexpr int kMargin = 20;
    constexpr int kDesignW = 2 * kMargin + 7 * kKnobW;   // 670
    constexpr int kDesignH = 580;

    // Tamano inicial: 75% del diseno, para que quepa en pantallas pequenas
    constexpr float kStartScale = 0.75f;
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // --- Oscilador y amplitud ---
    waveLabel.setText ("Onda", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (waveLabel);

    waveBox.addItemList (juce::StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 1);
    addAndMakeVisible (waveBox);
    waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "wave", waveBox);

    setupKnob (volume,  "Volumen", "volume");
    setupKnob (attack,  "Ataque",  "attack",  " s");
    setupKnob (decay,   "Decay",   "decay",   " s");
    setupKnob (sustain, "Sustain", "sustain");
    setupKnob (release, "Release", "release", " s");

    // --- Filtro ---
    filterLabel.setText ("Tipo", juce::dontSendNotification);
    filterLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (filterLabel);

    filterBox.addItemList (juce::StringArray { "Paso bajo", "Paso alto", "Paso banda", "Notch" }, 1);
    addAndMakeVisible (filterBox);
    filterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "ftype", filterBox);

    setupKnob (cutoff,    "Cutoff",     "cutoff", " Hz", 0);
    setupKnob (resonance, "Resonancia", "resonance");
    setupKnob (envAmt,    "Env cant.",  "envamt");
    setupKnob (fAttack,   "F Ataque",   "fattack",  " s");
    setupKnob (fDecay,    "F Decay",    "fdecay",   " s");
    setupKnob (fSustain,  "F Sustain",  "fsustain");
    setupKnob (fRelease,  "F Release",  "frelease", " s");

    // --- Unison, drift y glide ---
    unisonModeLabel.setText ("Modo", juce::dontSendNotification);
    unisonModeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (unisonModeLabel);

    unisonModeBox.addItemList (juce::StringArray { "Normal", "Octavas", "Quinta", "Mayor",
                                                   "Menor", "Mayor 7", "Menor 7",
                                                   "Sub", "Swarm" }, 1);
    addAndMakeVisible (unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);

    setupKnob (unisonVoices, "Voces",   "unison", "", 0);
    setupKnob (unisonDetune, "Detune",  "udetune", " ct", 0);
    setupKnob (unisonStereo, "Estereo", "ustereo", "", 2);
    setupKnob (drift,        "Drift",   "drift", "", 2);
    setupKnob (glide,        "Glide",   "glide", " s", 2);

    // Ventana redimensionable (arrastrando la esquina inferior derecha),
    // manteniendo siempre las proporciones
    setResizable (true, true);
    setResizeLimits ((int) (kDesignW * 0.5f), (int) (kDesignH * 0.5f),
                     (int) (kDesignW * 2.0f), (int) (kDesignH * 2.0f));
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    setSize ((int) (kDesignW * kStartScale), (int) (kDesignH * kStartScale));
}

void MiSinteEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                               const juce::String& suffix, int decimals)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    if (suffix.isNotEmpty())
        k.slider.setTextValueSuffix (suffix);
    if (decimals >= 0)
        k.slider.setNumDecimalPlacesToDisplay (decimals);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, k.slider);
}

void MiSinteEditor::scaleLabel (juce::Label& l, float s)
{
    l.setFont (juce::Font (juce::FontOptions (15.0f * s)));
}

void MiSinteEditor::layoutRow (std::initializer_list<Knob*> knobs, int labelY, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    int x = kMargin;
    for (auto* k : knobs)
    {
        scaleLabel (k->label, s);
        k->label.setBounds (S (x), S (labelY), S (kKnobW), S (22));

        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (80), S (18));
        k->slider.setBounds (S (x), S (labelY + 25), S (kKnobW), S (115));
        x += kKnobW;
    }
}

void MiSinteEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    g.fillAll (juce::Colour (0xff1e1e24));

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (13.0f * s);
    g.drawText ("OSCILADOR Y AMPLITUD", S (kMargin), S (8), S (250), S (18), juce::Justification::left);
    g.drawText ("FILTRO", S (kMargin), S (205), S (250), S (18), juce::Justification::left);
    g.drawText ("UNISON, DRIFT Y GLIDE", S (kMargin), S (402), S (250), S (18), juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (S (200), (float) S (kMargin), (float) (getWidth() - S (kMargin)));
    g.drawHorizontalLine (S (397), (float) S (kMargin), (float) (getWidth() - S (kMargin)));
}

void MiSinteEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    scaleLabel (waveLabel, s);
    waveLabel.setBounds (S (kMargin + 130), S (8), S (50), S (24));
    waveBox.setBounds (S (kMargin + 185), S (8), S (130), S (24));
    layoutRow ({ &volume, &attack, &decay, &sustain, &release }, 40, s);

    scaleLabel (filterLabel, s);
    filterLabel.setBounds (S (kMargin + 70), S (205), S (50), S (24));
    filterBox.setBounds (S (kMargin + 125), S (205), S (130), S (24));
    layoutRow ({ &cutoff, &resonance, &envAmt, &fAttack, &fDecay, &fSustain, &fRelease }, 237, s);

    scaleLabel (unisonModeLabel, s);
    unisonModeLabel.setBounds (S (kMargin + 160), S (402), S (50), S (24));
    unisonModeBox.setBounds (S (kMargin + 215), S (402), S (130), S (24));
    layoutRow ({ &unisonVoices, &unisonDetune, &unisonStereo, &drift, &glide }, 434, s);
}
