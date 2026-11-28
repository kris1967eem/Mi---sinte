#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <memory>
#include "Wavetable/WTData.h"
#include "Wavetable/WTOscillator.h"

class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WaveformDisplay (juce::AudioProcessorValueTreeState& state,
                              std::shared_ptr<const wavetable::WavetableData> wt = nullptr)
        : wavetable (std::move (wt))
    {
        waveParam    = state.getParameter ("wave");
        waveRaw      = state.getRawParameterValue ("wave");
        pwRaw        = state.getRawParameterValue ("pw");
        syncRaw      = state.getRawParameterValue ("oscsync");
        freqRaw      = state.getRawParameterValue ("syncfreq");

        oscTypeRaw   = state.getRawParameterValue ("oscType");
        wtPosRaw     = state.getRawParameterValue ("wtPos");
        wtWarpModeRaw = state.getRawParameterValue ("wtWarpMode");
        wtWarpAmtRaw  = state.getRawParameterValue ("wtWarpAmt");

        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (0xff101015));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff3d3d4a));
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        const int oscType = (oscTypeRaw != nullptr) ? (int) oscTypeRaw->load() : 0;
        const bool isWt = (oscType == 1) && (wavetable != nullptr);

        const auto area = bounds.reduced (12.0f, 14.0f);
        const float cy = area.getCentreY();
        const float amp = area.getHeight() * 0.48f;

        // Linea central
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) cy, area.getX(), area.getRight());

        juce::Path p;
        const int steps = juce::jmax (8, (int) area.getWidth());

        if (isWt)
        {
            const float pos01 = (wtPosRaw != nullptr) ? std::clamp (wtPosRaw->load(), 0.0f, 1.0f) : 0.0f;
            const int wMode   = (wtWarpModeRaw != nullptr) ? (int) wtWarpModeRaw->load() : 0;
            const float wAmt  = (wtWarpAmtRaw != nullptr) ? wtWarpAmtRaw->load() : 0.0f;

            const float fpos = pos01 * static_cast<float> (wavetable::WavetableData::kFrames - 1);
            const int f0 = std::min (static_cast<int> (fpos), wavetable::WavetableData::kFrames - 1);
            const int f1 = std::min (f0 + 1, wavetable::WavetableData::kFrames - 1);
            const float frac = fpos - static_cast<float> (f0);

            const float* d0 = wavetable->frame (f0, 0);
            const float* d1 = wavetable->frame (f1, 0);
            constexpr int N = wavetable::WavetableData::kSamples;

            for (int i = 0; i <= steps; ++i)
            {
                const float ph0 = juce::jmin (static_cast<float> (i) / static_cast<float> (steps), 0.99999f);
                const float ph = wavetable::WTOscillator::warpPhase (ph0, wMode, wAmt);

                const float idxF = ph * static_cast<float> (N);
                const int i0 = static_cast<int> (idxF) & (N - 1);
                const int i1 = (i0 + 1) & (N - 1);
                const float t = idxF - std::floor (idxF);

                const float s0 = d0[i0] + (d0[i1] - d0[i0]) * t;
                const float s1 = d1[i0] + (d1[i1] - d1[i0]) * t;
                const float s = std::clamp (s0 + (s1 - s0) * frac, -1.2f, 1.2f);

                const float x = area.getX() + area.getWidth() * (static_cast<float> (i) / static_cast<float> (steps));
                const float y = cy - s * amp;

                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }
        }
        else
        {
            const int type = (int) waveRaw->load();
            const float pw = pwRaw->load();
            const bool sync = syncRaw->load() > 0.5f;
            const float ratio = std::exp2 (freqRaw->load() / 12.0f);

            for (int i = 0; i <= steps; ++i)
            {
                const float m = juce::jmin ((float) i / (float) steps, 0.99999f);
                float ph = m;
                if (sync)
                {
                    ph = m * ratio;
                    ph -= std::floor (ph);
                }

                const float x = area.getX() + area.getWidth() * ((float) i / (float) steps);
                const float y = cy - shape (type, ph, pw) * amp;

                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }

            p.lineTo (area.getRight(), cy - shape (type, 0.0f, pw) * amp);
        }

        const juce::PathStrokeType glow (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType line (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const auto waveColour = isWt ? juce::Colour (0xff3fd0d4) : juce::Colours::white;
        g.setColour (waveColour.withAlpha (0.18f));
        g.strokePath (p, glow);
        g.setColour (waveColour.withAlpha (0.95f));
        g.strokePath (p, line);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        const int oscType = (oscTypeRaw != nullptr) ? (int) oscTypeRaw->load() : 0;
        if (oscType == 0)
            showMenu();
    }

private:
    static float shape (int type, float ph, float pw)
    {
        switch (type)
        {
            case 0:  return std::sin (ph * juce::MathConstants<float>::twoPi);
            case 1:  return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
            case 2:  return 2.0f * ph - 1.0f;
            default: return ph < pw ? 1.0f : -1.0f;
        }
    }

    void showMenu()
    {
        const int current = (int) waveRaw->load();

        juce::PopupMenu menu;
        menu.addItem (1, "SINE",     true, current == 0);
        menu.addItem (2, "TRIANGLE", true, current == 1);
        menu.addItem (3, "SAW",      true, current == 2);
        menu.addItem (4, "SQUARE",   true, current == 3);

        juce::Component::SafePointer<WaveformDisplay> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (auto* param = safe->waveParam)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) (result - 1)));
                param->endChangeGesture();
            }
        });
    }

    void timerCallback() override
    {
        const int t   = (int) waveRaw->load();
        const float pw = pwRaw->load();
        const bool sy  = syncRaw->load() > 0.5f;
        const float fq = freqRaw->load();

        const int ot   = (oscTypeRaw != nullptr) ? (int) oscTypeRaw->load() : 0;
        const float wp = (wtPosRaw != nullptr) ? wtPosRaw->load() : 0.0f;
        const int wm   = (wtWarpModeRaw != nullptr) ? (int) wtWarpModeRaw->load() : 0;
        const float wa = (wtWarpAmtRaw != nullptr) ? wtWarpAmtRaw->load() : 0.0f;

        if (t != lastType || pw != lastPw || sy != lastSync || fq != lastFreq
            || ot != lastOscType || wp != lastWtPos || wm != lastWtWarpMode || wa != lastWtWarpAmt)
        {
            lastType = t;
            lastPw = pw;
            lastSync = sy;
            lastFreq = fq;
            lastOscType = ot;
            lastWtPos = wp;
            lastWtWarpMode = wm;
            lastWtWarpAmt = wa;
            setMouseCursor (ot == 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    std::shared_ptr<const wavetable::WavetableData> wavetable;
    juce::RangedAudioParameter* waveParam = nullptr;
    std::atomic<float>* waveRaw = nullptr;
    std::atomic<float>* pwRaw = nullptr;
    std::atomic<float>* syncRaw = nullptr;
    std::atomic<float>* freqRaw = nullptr;

    std::atomic<float>* oscTypeRaw = nullptr;
    std::atomic<float>* wtPosRaw = nullptr;
    std::atomic<float>* wtWarpModeRaw = nullptr;
    std::atomic<float>* wtWarpAmtRaw = nullptr;

    int lastType = -1;
    float lastPw = -1.0f;
    bool lastSync = false;
    float lastFreq = -1.0f;
    int lastOscType = -1;
    float lastWtPos = -1.0f;
    int lastWtWarpMode = -1;
    float lastWtWarpAmt = -1.0f;
};