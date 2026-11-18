#include "PluginEditor.h"

namespace
{
    constexpr int kKnobW = 90;
    constexpr int kMargin = 20;
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
                                                   "Menor", "Mayor 7", "Menor 7" }, 1);
    addAndMakeVisible (unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);

    setupKnob (unisonVoices, "Voces",   "unison", "", 0);
    setupKnob (unisonDetune, "Detune",  "udetune", " ct", 0);
    setupKnob (unisonStereo, "Estereo", "ustereo", "", 2);
    setupKnob (drift,        "Drift",   "drift", "", 2);
    setupKnob (glide,        "Glide",   "glide", " s", 2);

    setSize (2 * kMargin + 7 * kKnobW, 580);
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

void MiSinteEditor::layoutRow (std::initializer_list<Knob*> knobs, int labelY)
{
    int x = kMargin;
    for (auto* k : knobs)
    {
        k->label.setBounds (x, labelY, kKnobW, 22);
        k->slider.setBounds (x, labelY + 25, kKnobW, 115);
        x += kKnobW;
    }
}

void MiSinteEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e24));

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (13.0f);
    g.drawText ("OSCILADOR Y AMPLITUD", kMargin, 8, 250, 18, juce::Justification::left);
    g.drawText ("FILTRO", kMargin, 205, 250, 18, juce::Justification::left);
    g.drawText ("UNISON, DRIFT Y GLIDE", kMargin, 402, 250, 18, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (200, (float) kMargin, (float) (getWidth() - kMargin));
    g.drawHorizontalLine (397, (float) kMargin, (float) (getWidth() - kMargin));
}

void MiSinteEditor::resized()
{
    waveLabel.setBounds (kMargin + 130, 8, 50, 24);
    waveBox.setBounds (kMargin + 185, 8, 130, 24);
    layoutRow ({ &volume, &attack, &decay, &sustain, &release }, 40);

    filterLabel.setBounds (kMargin + 70, 205, 50, 24);
    filterBox.setBounds (kMargin + 125, 205, 130, 24);
    layoutRow ({ &cutoff, &resonance, &envAmt, &fAttack, &fDecay, &fSustain, &fRelease }, 237);

    unisonModeLabel.setBounds (kMargin + 160, 402, 50, 24);
    unisonModeBox.setBounds (kMargin + 215, 402, 130, 24);
    layoutRow ({ &unisonVoices, &unisonDetune, &unisonStereo, &drift, &glide }, 434);
}
