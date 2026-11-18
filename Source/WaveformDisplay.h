#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// Pantalla que dibuja la forma de onda del oscilador (con el ancho de pulso y el
// sync aplicados). Al hacer clic abre un menu para elegir SINE, TRIANGLE, SAW o SQUARE.
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WaveformDisplay (juce::AudioProcessorValueTreeState& state)
    {
        waveParam = state.getParameter ("wave");
        waveRaw   = state.getRawParameterValue ("wave");
        pwRaw     = state.getRawParameterValue ("pw");
        syncRaw   = state.getRawParameterValue ("oscsync");
        freqRaw   = state.getRawParameterValue ("syncfreq");

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

        const int type = (int) waveRaw->load();
        const float pw = pwRaw->load();
        const bool sync = syncRaw->load() > 0.5f;
        const float ratio = std::exp2 (freqRaw->load() / 12.0f);

        const auto area = bounds.reduced (12.0f, 14.0f);
        const float cy = area.getCentreY();
        const float amp = area.getHeight() * 0.5f;

        // Linea central
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) cy, area.getX(), area.getRight());

        // Un ciclo del oscilador maestro. Con sync, el esclavo dibuja "ratio" ciclos
        // y se reinicia al final del ciclo maestro.
        juce::Path p;
        const int steps = juce::jmax (8, (int) area.getWidth());
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

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        // Cierre del ciclo (caida vertical de la sierra, flancos de la cuadrada...)
        p.lineTo (area.getRight(), cy - shape (type, 0.0f, pw) * amp);

        const juce::PathStrokeType glow (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType line (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.strokePath (p, glow);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.strokePath (p, line);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        showMenu();
    }

private:
    static float shape (int type, float ph, float pw)
    {
        switch (type)
        {
            case 0:  return std::sin (ph * juce::MathConstants<float>::twoPi);
            case 1:  return 4.0f * std::abs (ph - 0.5f) - 1.0f;
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
        const int t = (int) waveRaw->load();
        const float pw = pwRaw->load();
        const bool sy = syncRaw->load() > 0.5f;
        const float fq = freqRaw->load();

        if (t != lastType || pw != lastPw || sy != lastSync || fq != lastFreq)
        {
            lastType = t;
            lastPw = pw;
            lastSync = sy;
            lastFreq = fq;
            repaint();
        }
    }

    juce::RangedAudioParameter* waveParam = nullptr;
    std::atomic<float>* waveRaw = nullptr;
    std::atomic<float>* pwRaw = nullptr;
    std::atomic<float>* syncRaw = nullptr;
    std::atomic<float>* freqRaw = nullptr;

    int lastType = -1;
    float lastPw = -1.0f;
    bool lastSync = false;
    float lastFreq = -1.0f;
};
