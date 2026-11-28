#include "PluginEditor.h"

namespace
{
    constexpr int kDesignW = 1000;
    constexpr int kDesignH = 540;
    constexpr int kTop = 62;
    constexpr int kKnobW = 80;
    constexpr int kDialH = 96;

    constexpr int kTuningY = 93;
    constexpr int kMidY = 251;
    constexpr int kBottomY = 411;
    constexpr int kRowLineY = 380;

    constexpr int kLfoBlockH = 236;
    constexpr float kStartScale = 0.8f;

    const juce::Colour kAmber  (0xffe9b44c);
    const juce::Colour kOrange (0xfff08a5d);
    const juce::Colour kCyan   (0xff3fd0d4);
    const juce::Colour kGreen  (0xff8be04e);
    const juce::Colour kPink   (0xffe0679f);

    void fillCombo (juce::AudioProcessorValueTreeState& apvts, juce::ComboBox& box, const juce::String& paramID)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);
    }
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      waveDisplay (p.apvts, p.getWavetable()),
      filterDisplay (p.apvts, p.getFilterDisplay()),
      effectsPage (p.apvts, p.getEffectChain())
{
    waveRaw    = proc.apvts.getRawParameterValue ("wave");
    syncRaw    = proc.apvts.getRawParameterValue ("oscsync");
    modeRaw    = proc.apvts.getRawParameterValue ("umode");
    oscTypeRaw = proc.apvts.getRawParameterValue ("oscType");
    wtWarpRaw  = proc.apvts.getRawParameterValue ("wtWarpMode");
    oscTypeParam = proc.apvts.getParameter ("oscType");

    // --- Pestanas ---
    pageOscBtn.setButtonText ("OSC");
    pageLfoBtn.setButtonText ("LFOs");
    pageFxBtn.setButtonText ("EFFECTS");
    for (auto* b : { &pageOscBtn, &pageLfoBtn, &pageFxBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6ea5));
        addAndMakeVisible (*b);
    }
    pageOscBtn.setToggleState (true, juce::dontSendNotification);
    pageOscBtn.onClick = [this] { showPage (0); };
    pageLfoBtn.onClick = [this] { showPage (1); };
    pageFxBtn.onClick  = [this] { showPage (2); };

    // --- Selector VA / WT sobre el WaveDisplay ---
    oscVaBtn.setButtonText ("VA");
    oscWtBtn.setButtonText ("WT");
    for (auto* b : { &oscVaBtn, &oscWtBtn })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c2c36));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9a9aa8));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9843a));
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1208));
        addToPage (0, *b);
    }
    oscVaBtn.onClick = [this]
    {
        if (oscTypeParam != nullptr)
        {
            oscTypeParam->beginChangeGesture();
            oscTypeParam->setValueNotifyingHost (oscTypeParam->convertTo0to1 (0.0f));
            oscTypeParam->endChangeGesture();
        }
    };
    oscWtBtn.onClick = [this]
    {
        if (oscTypeParam != nullptr)
        {
            oscTypeParam->beginChangeGesture();
            oscTypeParam->setValueNotifyingHost (oscTypeParam->convertTo0to1 (1.0f));
            oscTypeParam->endChangeGesture();
        }
    };

    // --- Oscilador ---
    addToPage (0, waveDisplay);

    oscVolLabel.setText ("VOL", juce::dontSendNotification);
    oscVolLabel.setJustificationType (juce::Justification::centredLeft);
    oscVolLabel.setColour (juce::Label::textColourId, kOrange);
    oscVolLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addToPage (0, oscVolLabel);

    oscVolSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    oscVolSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    oscVolSlider.setColour (juce::Slider::trackColourId, kOrange);
    oscVolSl