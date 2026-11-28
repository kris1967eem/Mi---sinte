#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <complex>

// Estado compartido entre el motor de audio (que lo escribe) y el display (que lo lee).
// Son solo atomicos: el hilo de audio nunca espera a la interfaz.
struct FilterDisplayState
{
    std::atomic<float> cutoff { 20000.0f };     // cutoff efectivo (con LFO y FILTER ENV) de la nota mas reciente
    std::atomic<float> reso { 0.1f };           // resonancia efectiva (0 a 1)
    std::atomic<int> writes { 0 };              // se incrementa mientras suena alguna nota
    std::atomic<float> sampleRate { 44100.0f };
};

// Display de la respuesta en frecuencia del filtro. Mismo marco, colores y estilo que
// WaveformDisplay. La curva se mueve con CUTOFF, el pico crece con RESO y, mientras
// suena una nota, refleja tambien la modulacion de los LFO y de FILTER ENV.
class FilterResponseDisplay : public juce::Component, private juce::Timer
{
public:
    FilterResponseDisplay (juce::AudioProcessorValueTreeState& state, FilterDisplayState& shared)
        : display (shared)
    {
        typeRaw   = state.getRawParameterValue ("ftype");
        cutoffRaw = state.getRawParameterValue ("cutoff");
        resoRaw   = state.getRawParameterValue ("resonance");

        smoothLogCutoff = std::log ((double) cutoffRaw->load());
        smoothReso = (double) resoRaw->load();

        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        // Marco: identico al de WaveformDisplay
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (0xff101015));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff3d3d4a));
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        const auto area = bounds.reduced (12.0f, 14.0f);

        const int type = (int) typeRaw->load();
        const double fs = (double) display.sampleRate.load();
        const double fc = std::exp (smoothLogCutoff);
        const double k = 1.0 / (0.5 * std::pow (40.0, smoothReso));

        // Escala vertical: de -40 dB a +30 dB (la resonancia maxima llega a ~+26 dB)
        constexpr double dbMin = -40.0;
        constexpr double dbMax = 30.0;

        auto dbToY = [&] (double db)
        {
            const double t = (juce::jlimit (dbMin, dbMax, db) - dbMin) / (dbMax - dbMin);
            return (float) ((double) area.getBottom() - t * (double) area.getHeight());
        };

        // Linea de 0 dB (equivale a la linea central del display de onda)
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) dbToY (0.0), area.getX(), area.getRight());

        // Curva: eje horizontal logaritmico de 20 Hz a 20 kHz
        juce::Path p;
        const int steps = juce::jmax (16, (int) area.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const double t = (double) i / (double) steps;
            const double f = 20.0 * std::pow (1000.0, t);

            const float x = area.getX() + area.getWidth() * (float) t;
            const float y = dbToY (responseDb (type, f, fc, k, fs));

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }

        const juce::PathStrokeType glow (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType line (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.strokePath (p, glow);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.strokePath (p, line);
    }

private:
    // Respuesta del mismo filtro que usa el motor: SVF de 2 polos (12 dB/oct) con
    // transformacion bilineal. Tipos: 0 LOWPASS, 1 HIGHPASS, 2 BANDPASS, 3 NOTCH.
    static double responseDb (int type, double f, double fc, double k, double fs)
    {
        const double pi = juce::MathConstants<double>::pi;
        const double wc = std::tan (pi * juce::jmin (fc, 0.45 * fs) / fs);
        const double wf = std::tan (pi * juce::jmin (f, 0.49 * fs) / fs);
        const double x = wf / wc;

        const std::complex<double> s (0.0, x);
        const std::complex<double> den = s * s + k * s + 1.0;

        std::complex<double> num;
        switch (type)
        {
            case 0:  num = 1.0; break;
            case 1:  num = s * s; break;
            case 2:  num = k * s; break;
            default: num = s * s + 1.0; break;
        }

        const double mag = std::abs (num / den);
        return 20.0 * std::log10 (juce::jmax (mag, 1.0e-6));
    }

    void timerCallback() override
    {
        // Mientras suena una nota, "writes" cambia; si no cambia, se muestran los valores de los knobs
        const int w = display.writes.load (std::memory_order_relaxed);
        if (w != lastWrites)
        {
            lastWrites = w;
            idleTicks = 0;
        }
        else if (idleTicks < 1000)
        {
            ++idleTicks;
        }
        const bool active = idleTicks < 4;

        const double targetCutoff = active ? (double) display.cutoff.load (std::memory_order_relaxed)
                                           : (double) cutoffRaw->load();
        const double targetReso = active ? (double) display.reso.load (std::memory_order_relaxed)
                                         : (double) resoRaw->load();

        // Suavizado (el cutoff en escala logaritmica) para que el movimiento sea fluido
        const double targetLog = std::log (juce::jlimit (20.0, 20000.0, targetCutoff));
        smoothLogCutoff += (targetLog - smoothLogCutoff) * 0.4;
        smoothReso += (juce::jlimit (0.0, 1.0, targetReso) - smoothReso) * 0.4;

        const int type = (int) typeRaw->load();

        if (std::abs (smoothLogCutoff - drawnLogCutoff) > 0.002
            || std::abs (smoothReso - drawnReso) > 0.002
            || type != drawnType)
        {
            drawnLogCutoff = smoothLogCutoff;
            drawnReso = smoothReso;
            drawnType = type;
            repaint();
        }
    }

    FilterDisplayState& display;
    std::atomic<float>* typeRaw = nullptr;
    std::atomic<float>* cutoffRaw = nullptr;
    std::atomic<float>* resoRaw = nullptr;

    double smoothLogCutoff = 0.0;
    double smoothReso = 0.0;
    double drawnLogCutoff = -1000.0;
    double drawnReso = -1.0;
    int drawnType = -1;
    int lastWrites = 0;
    int idleTicks = 1000;
};
