#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "DelayParameters.h"

// ============================================================================
//  DelayEditorComponent
//  Interfaz del modulo Delay, pensada para la pestana EFFECTS del plugin.
// ============================================================================
namespace DelayUI
{
    inline const juce::Colour kAmber  { 0xffe9b44c };
    inline const juce::Colour kOrange { 0xfff08a5d };
    inline const juce::Colour kCyan   { 0xff3fd0d4 };
    inline const juce::Colour kGreen  { 0xff8be04e };
    inline const juce::Colour kPink   { 0xffe0679f };

    // Formato del texto de cada knob
    enum class Fmt { Ms, SignedMs, Hz, Percent, BipolarPercent, Semitones, PerSecond };

    inline juce::String formatValue (Fmt f, double v)
    {
        switch (f)
        {
            case Fmt::Ms:
                return v >= 100.0 ? juce::String (juce::roundToInt (v)) + " ms" : juce::String (v, 1) + " ms";
            case Fmt::SignedMs:
                return juce::String (v > 0.05 ? "+" : "") + juce::String (v, 1) + " ms";
            case Fmt::Hz:
                return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            case Fmt::Percent:
                return juce::String (juce::roundToInt (v * 100.0));
            case Fmt::BipolarPercent:
            {
                const int pct = juce::roundToInt ((v * 2.0 - 1.0) * 100.0);
                return juce::String (pct > 0 ? "+" : "") + juce::String (pct == 0 ? 0 : pct);
            }
            case Fmt::Semitones:
            {
                const int st = juce::roundToInt (v);
                return juce::String (st > 0 ? "+" : "") + juce::String (st == 0 ? 0 : st) + " st";
            }
            case Fmt::PerSecond:
                return juce::String (v, 1) + " /s";
        }
        return {};
    }

    inline double parseValue (Fmt f, const juce::String& text)
    {
        const double v = text.getDoubleValue();
        if (f == Fmt::Hz)
            return text.containsIgnoreCase ("k") ? v * 1000.0 : v;
        if (f == Fmt::Percent)
            return juce::jlimit (0.0, 100.0, v) / 100.0;
        if (f == Fmt::BipolarPercent)
            return (juce::jlimit (-100.0, 100.0, v) / 100.0 + 1.0) * 0.5;
        return v;
    }

    inline void fillCombo (juce::AudioProcessorValueTreeState& apvts, juce::ComboBox& box, const char* paramID)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);
    }

    inline void setupToggleButton (juce::Component& parent, juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9843a));
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1208));
        parent.addAndMakeVisible (b);
    }

    inline void setEnabledDimmed (juce::Component& c, bool enabled)
    {
        c.setEnabled (enabled);
        c.setAlpha (enabled ? 1.0f : 0.4f);
    }

    struct DelayKnob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;

        void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& text, const char* paramID, juce::Colour accent, Fmt fmt)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
            slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a46));
            slider.setColour (juce::Slider::thumbColourId, accent.brighter (0.4f));
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            parent.addAndMakeVisible (slider);

            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            parent.addAndMakeVisible (label);

            attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

            if (fmt == Fmt::BipolarPercent)
                slider.setDoubleClickReturnValue (true, 0.5);
            else if (fmt == Fmt::Semitones || fmt == Fmt::SignedMs)
                slider.setDoubleClickReturnValue (true, 0.0);

            slider.textFromValueFunction = [fmt] (double v) { return formatValue (fmt, v); };
            slider.valueFromTextFunction = [fmt] (const juce::String& t) { return parseValue (fmt, t); };
            slider.updateText();
        }

        void place (int x, int y, int w, int sliderH, float s)
        {
            auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

            label.setFont (juce::Font (juce::FontOptions (13.0f * s)));
            label.setBounds (S (x), S (y), S (w), S (22));

            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (w - 8), S (18));
            slider.setBounds (S (x), S (y + 24), S (w), S (sliderH));
        }

        void setActive (bool on)
        {
            slider.setEnabled (on);
            slider.setAlpha (on ? 1.0f : 0.4f);
            label.setAlpha (on ? 1.0f : 0.4f);
        }
    };

    class DelayPanel : public juce::Component
    {
    public:
        static constexpr int kDesignW = 1000;
        static constexpr int kDesignH = 124;
        static constexpr int kKnobW   = 80;
        static constexpr int kDialH   = 96;

    protected:
        float scale() const noexcept { return (float) getWidth() / (float) kDesignW; }
    };

    // ---- 1. Simple: Stereo Offset, Width, High-Cut, Low-Cut ------------------
    class SimpleDelayPanel final : public DelayPanel
    {
    public:
        explicit SimpleDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            offset .setup (*this, apvts, "OFFSET",        DelayParams::kSimpleOffset, kAmber, Fmt::SignedMs);
            width  .setup (*this, apvts, "WIDTH",         DelayParams::kSimpleWidth,  kAmber, Fmt::Percent);
            highCut.setup (*this, apvts, "HIGH-CUT",      DelayParams::kSimpleHiCut,  kOrange, Fmt::Hz);
            lowCut .setup (*this, apvts, "LOW-CUT",       DelayParams::kSimpleLoCut,  kOrange, Fmt::Hz);
        }

        void resized() override
        {
            const float s = scale();
            offset .place (20,  0, kKnobW, kDialH, s);
            width  .place (110, 0, kKnobW, kDialH, s);
            highCut.place (200, 0, kKnobW, kDialH, s);
            lowCut .place (290, 0, kKnobW, kDialH, s);
        }

    private:
        DelayKnob offset, width, highCut, lowCut;
    };

    // ---- 2. Ping-Pong: Cross-Feedback, Pan Spread, Input Mono Sum ------------
    class PingPongDelayPanel final : public DelayPanel
    {
    public:
        explicit PingPongDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            cross.setup  (*this, apvts, "CROSS-FEEDBACK", DelayParams::kPingPongCross,  kCyan, Fmt::Percent);
            spread.setup (*this, apvts, "PAN SPREAD",     DelayParams::kPingPongSpread, kCyan, Fmt::Percent);

            setupToggleButton (*this, monoSum, "MONO SUM");
            monoSumA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, DelayParams::kPingPongMonoSum, monoSum);
        }

        void resized() override
        {
            const float s = scale();
            auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

            cross .place (15,  0, 110, kDialH, s);
            spread.place (135, 0, kKnobW, kDialH, s);
            monoSum.setBounds (S (232), S (52), S (96), S (26));
        }

    private:
        DelayKnob cross, spread;
        juce::TextButton monoSum;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSumA;
    };

    // ---- 3. Tape: Drive, Wow & Flutter, Tape Age -----------------------------
    class TapeDelayPanel final : public DelayPanel
    {
    public:
        explicit TapeDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            drive.setup (*this, apvts, "DRIVE",         DelayParams::kTapeDrive, kOrange, Fmt::Percent);
            wow  .setup (*this, apvts, "WOW & FLUTTER", DelayParams::kTapeWow,   kOrange, Fmt::Percent);
            age  .setup (*this, apvts, "TAPE AGE",      DelayParams::kTapeAge,   kOrange, Fmt::Percent);
        }

        void resized() override
        {
            const float s = scale();
            drive.place (20,  0, kKnobW, kDialH, s);
            wow  .place (110, 0, 110, kDialH, s);
            age  .place (230, 0, kKnobW, kDialH, s);
        }

    private:
        DelayKnob drive, wow, age;
    };

    // ---- 4. Filter: Cutoff, Resonance, Filter Mode (LP/BP/HP), LFO Mod Depth -
    class FilterDelayPanel final : public DelayPanel
    {
    public:
        explicit FilterDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            cutoff   .setup (*this, apvts, "CUTOFF",    DelayParams::kFilterCutoff, kPink, Fmt::Hz);
            resonance.setup (*this, apvts, "RESONANCE", DelayParams::kFilterReso,   kPink, Fmt::Percent);
            lfoDepth .setup (*this, apvts, "LFO DEPTH", DelayParams::kFilterLfo,    kPink, Fmt::Percent);

            modeLabel.setText ("FILTER MODE", juce::dontSendNotification);
            modeLabel.setJustificationType (juce::Justification::centredLeft);
            modeLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
            addAndMakeVisible (modeLabel);

            fillCombo (apvts, modeBox, DelayParams::kFilterMode);
            addAndMakeVisible (modeBox);
            modeBoxA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, DelayParams::kFilterMode, modeBox);
        }

        void resized() override
        {
            const float s = scale();
            auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

            modeLabel.setFont (juce::Font (juce::FontOptions (13.0f * s)));
            modeLabel.setBounds (S (20), S (0), S (100), S (22));
            modeBox.setBounds (S (20), S (40), S (90), S (26));

            cutoff   .place (130, 0, kKnobW, kDialH, s);
            resonance.place (220, 0, kKnobW, kDialH, s);
            lfoDepth .place (310, 0, kKnobW, kDialH, s);
        }

    private:
        DelayKnob cutoff, resonance, lfoDepth;
        juce::Label modeLabel;
        juce::ComboBox modeBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeBoxA;
    };

    // ---- 5. Reverse: Crossfade Smoothness, Forward/Reverse Mix, Octave Up ----
    class ReverseDelayPanel final : public DelayPanel
    {
    public:
        explicit ReverseDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            smooth.setup (*this, apvts, "SMOOTHNESS",  DelayParams::kReverseSmooth, kGreen, Fmt::Percent);
            mix   .setup (*this, apvts, "FWD / REV",   DelayParams::kReverseMix,    kGreen, Fmt::BipolarPercent);

            setupToggleButton (*this, octaveUp, "OCTAVE UP");
            octaveUpA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts, DelayParams::kReverseOctave, octaveUp);
        }

        void resized() override
        {
            const float s = scale();
            auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

            smooth.place (15,  0, 100, kDialH, s);
            mix   .place (125, 0, kKnobW, kDialH, s);
            octaveUp.setBounds (S (232), S (52), S (96), S (26));
        }

    private:
        DelayKnob smooth, mix;
        juce::TextButton octaveUp;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> octaveUpA;
    };

    // ---- 6. Granular: Grain Size, Density, Pitch Shift, Spray ----------------
    class GranularDelayPanel final : public DelayPanel
    {
    public:
        explicit GranularDelayPanel (juce::AudioProcessorValueTreeState& apvts)
        {
            size   .setup (*this, apvts, "GRAIN SIZE", DelayParams::kGranSize,    kCyan, Fmt::Ms);
            density.setup (*this, apvts, "DENSITY",    DelayParams::kGranDensity, kCyan, Fmt::PerSecond);
            pitch  .setup (*this, apvts, "PITCH",      DelayParams::kGranPitch,   kCyan, Fmt::Semitones);
            spray  .setup (*this, apvts, "SPRAY",      DelayParams::kGranSpray,   kCyan, Fmt::Percent);
        }

        void resized() override
        {
            const float s = scale();
            size   .place (20,  0, kKnobW, kDialH, s);
            density.place (110, 0, kKnobW, kDialH, s);
            pitch  .place (200, 0, kKnobW, kDialH, s);
            spray  .place (290, 0, kKnobW, kDialH, s);
        }

    private:
        DelayKnob size, density, pitch, spray;
    };
}

// ============================================================================
class DelayEditorComponent final : public juce::Component, private juce::Timer
{
public:
    static constexpr int kDesignW = 1000;
    static constexpr int kDesignH = 330;

    explicit DelayEditorComponent (juce::AudioProcessorValueTreeState& apvts)
        : simplePanel (apvts), pingPongPanel (apvts), tapePanel (apvts),
          filterPanel (apvts), reversePanel (apvts), granularPanel (apvts)
    {
        using namespace DelayUI;

        rawMode  = apvts.getRawParameterValue (DelayParams::kMode);
        rawLink  = apvts.getRawParameterValue (DelayParams::kLink);
        rawSyncL = apvts.getRawParameterValue (DelayParams::kSyncL);
        rawSyncR = apvts.getRawParameterValue (DelayParams::kSyncR);
        modeNames = DelayParams::getModeNames();

        setupToggleButton (*this, onBtn, "ON");
        onA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, DelayParams::kOn, onBtn);

        fillCombo (apvts, modeBox, DelayParams::kMode);
        addAndMakeVisible (modeBox);
        modeA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, DelayParams::kMode, modeBox);

        setupToggleButton (*this, linkBtn, "LINK");
        linkA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, DelayParams::kLink, linkBtn);

        timeL.setup (*this, apvts, "TIME L", DelayParams::kTimeL, kCyan, Fmt::Ms);
        timeR.setup (*this, apvts, "TIME R", DelayParams::kTimeR, kCyan, Fmt::Ms);

        setupToggleButton (*this, syncBtnL, "SYNC");
        setupToggleButton (*this, syncBtnR, "SYNC");
        syncAL = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, DelayParams::kSyncL, syncBtnL);
        syncAR = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, DelayParams::kSyncR, syncBtnR);

        fillCombo (apvts, divBoxL, DelayParams::kDivL);
        fillCombo (apvts, divBoxR, DelayParams::kDivR);
        fillCombo (apvts, modBoxL, DelayParams::kModifierL);
        fillCombo (apvts, modBoxR, DelayParams::kModifierR);
        for (auto* c : { &divBoxL, &divBoxR, &modBoxL, &modBoxR })
            addAndMakeVisible (*c);

        divAL = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, DelayParams::kDivL, divBoxL);
        divAR = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, DelayParams::kDivR, divBoxR);
        modAL = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, DelayParams::kModifierL, modBoxL);
        modAR = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, DelayParams::kModifierR, modBoxR);

        feedback.setup (*this, apvts, "FEEDBACK", DelayParams::kFeedback, kAmber, Fmt::Percent);
        mix     .setup (*this, apvts, "MIX",      DelayParams::kMix,      kGreen, Fmt::Percent);

        panels = { &simplePanel, &pingPongPanel, &tapePanel, &filterPanel, &reversePanel, &granularPanel };
        for (auto* p : panels)
            addChildComponent (*p);
    }

    ~DelayEditorComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const float s = (float) getWidth() / (float) kDesignW;
        auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

        const juce::Colour titleColour = juce::Colours::white.withAlpha (0.65f);

        g.setColour (titleColour);
        g.setFont (juce::Font (juce::FontOptions (12.5f * s, juce::Font::bold)));
        g.drawText ("DELAY", S (20), S (9), S (60), S (18), juce::Justification::left);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine (S (176), 0.0f, (float) getWidth());

        g.setColour (titleColour);
        const juce::String modeName = modeNames[juce::jlimit (0, kNumDelayModes - 1, cachedMode < 0 ? 0 : cachedMode)];
        g.drawText (modeName.toUpperCase() + " SETTINGS", S (20), S (182), S (300), S (18), juce::Justification::left);
    }

    void resized() override
    {
        const float s = (float) getWidth() / (float) kDesignW;
        auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

        onBtn  .setBounds (S (88),  S (4), S (56),  S (26));
        modeBox.setBounds (S (160), S (4), S (150), S (26));
        linkBtn.setBounds (S (330), S (4), S (60),  S (26));

        timeL.place (20, 44, 80, 96, s);
        syncBtnL.setBounds (S (108), S (72),  S (92), S (24));
        divBoxL .setBounds (S (108), S (102), S (92), S (24));
        modBoxL .setBounds (S (108), S (132), S (92), S (24));

        timeR.place (222, 44, 80, 96, s);
        syncBtnR.setBounds (S (310), S (72),  S (92), S (24));
        divBoxR .setBounds (S (310), S (102), S (92), S (24));
        modBoxR .setBounds (S (310), S (132), S (92), S (24));

        feedback.place (470, 44, 80, 96, s);
        mix     .place (560, 44, 80, 96, s);

        for (auto* p : panels)
            p->setBounds (0, S (204), S (kDesignW), S (DelayUI::DelayPanel::kDesignH));
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            cacheValid = false;
            timerCallback();
            startTimerHz (15);
        }
        else
        {
            stopTimer();
        }
    }

private:
    void timerCallback() override
    {
        if (! isShowing())
            return;

        const int  mode = juce::jlimit (0, kNumDelayModes - 1, (int) rawMode->load());
        const bool link = rawLink->load()  > 0.5f;
        const bool syL  = rawSyncL->load() > 0.5f;
        const bool syR  = rawSyncR->load() > 0.5f;

        if (cacheValid && mode == cachedMode && link == cachedLink && syL == cachedSyncL && syR == cachedSyncR)
            return;

        const bool modeChanged = (! cacheValid) || mode != cachedMode;

        cacheValid = true;
        cachedMode = mode;
        cachedLink = link;
        cachedSyncL = syL;
        cachedSyncR = syR;

        for (size_t i = 0; i < panels.size(); ++i)
            panels[i]->setVisible ((int) i == mode);

        using DelayUI::setEnabledDimmed;
        timeL.setActive (! syL);
        setEnabledDimmed (divBoxL, syL);
        setEnabledDimmed (modBoxL, syL);

        timeR.setActive (! link && ! syR);
        setEnabledDimmed (syncBtnR, ! link);
        setEnabledDimmed (divBoxR, ! link && syR);
        setEnabledDimmed (modBoxR, ! link && syR);

        if (modeChanged)
            repaint();
    }

    DelayUI::DelayKnob timeL, timeR, feedback, mix;
    juce::TextButton onBtn, linkBtn, syncBtnL, syncBtnR;
    juce::ComboBox modeBox, divBoxL, divBoxR, modBoxL, modBoxR;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onA, linkA, syncAL, syncAR;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeA, divAL, divAR, modAL, modAR;

    DelayUI::SimpleDelayPanel   simplePanel;
    DelayUI::PingPongDelayPanel pingPongPanel;
    DelayUI::TapeDelayPanel     tapePanel;
    DelayUI::FilterDelayPanel   filterPanel;
    DelayUI::ReverseDelayPanel  reversePanel;
    DelayUI::GranularDelayPanel granularPanel;
    std::array<juce::Component*, 6> panels {};

    std::atomic<float>* rawMode = nullptr;
    std::atomic<float>* rawLink = nullptr;
    std::atomic<float>* rawSyncL = nullptr;
    std::atomic<float>* rawSyncR = nullptr;

    juce::StringArray modeNames;
    int cachedMode = -1;
    bool cachedLink = false, cachedSyncL = false, cachedSyncR = false, cacheValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayEditorComponent)
};
