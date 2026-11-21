#pragma once

#include "DelayDspExtras.h"
#include "IDelayAlgorithm.h"

// ============================================================================
//  ReverseDelay
//  Delay inverso con DOBLE cabezal de lectura por canal.
//
//  Cada 'Delay Time' (T) arranca un segmento nuevo: un cabezal que lee HACIA ATRAS desde lo ultimo
//  grabado (la inversa de los ultimos T de audio). Los segmentos duran T + X y se solapan X con el
//  siguiente; los dos cabezales se alternan. En los cruces se aplica una ventana Hann (medio coseno
//  de subida y de bajada), asi que la suma de ambos cabezales es exactamente 1.
//    - Crossfade Smoothness: X entre ~2 ms (0) y T/2 (1).
//    - Forward/Reverse Mix: 0 = solo delay normal, 1 = solo inverso (mezcla lineal: ganancia <= 1).
//    - Octave Up: los cabezales leen a 2x (una octava arriba). Cambia con suavizado de 30 ms.
//  El tiempo del camino directo usa el lector dual-tap (sin Doppler). El tiempo del inverso se suaviza
//  (solo cambia el ritmo de los segmentos, nunca el tono).
//  Memoria: con Octave Up y T = 5 s la lectura llega a 4.5 x T de antiguedad, por eso el buffer
//  reserva 23 s por canal.
// ============================================================================
class ReverseDelay final : public IDelayAlgorithm
{
public:
    void prepare (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;

        const int capacity = (int) std::ceil (sampleRate * 23.0);
        for (auto& ch : channels)
        {
            ch.buffer.prepare (capacity);
            ch.forward.prepare (sampleRate, 25.0f);
            ch.timeSm.reset (sampleRate, 0.05);
        }

        feedbackSm.reset (sampleRate, 0.03);
        smoothSm.reset (sampleRate, 0.03);
        mixSm.reset (sampleRate, 0.03);
        stepSm.reset (sampleRate, 0.03);

        reset();
    }

    void reset() override
    {
        for (auto& ch : channels)
        {
            ch.buffer.reset();
            ch.heads[0] = Head();
            ch.heads[1] = Head();
            ch.nextHead = 0;
            ch.cycle = 1.0;     // arranca un segmento en la primera muestra
        }

        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float smooth   = std::clamp (p.reverseSmoothness, 0.0f, 1.0f);
        const float mix      = std::clamp (p.reverseMix, 0.0f, 1.0f);
        const float step     = p.reverseOctaveUp ? 2.0f : 1.0f;

        const float ms[2] = { DelayTimeCalculator::clampMs (p.timeMsL), DelayTimeCalculator::clampMs (p.timeMsR) };

        for (int c = 0; c < 2; ++c)
        {
            channels[c].targetDelay = DelayTimeCalculator::msToSamples (ms[c], sampleRate);

            if (snapPending)
            {
                channels[c].timeSm.setCurrentAndTargetValue (channels[c].targetDelay);
                channels[c].forward.reset (channels[c].buffer, channels[c].targetDelay);
            }
            else
            {
                channels[c].timeSm.setTargetValue (channels[c].targetDelay);
            }
        }

        if (snapPending)
        {
            feedbackSm.setCurrentAndTargetValue (feedback);
            smoothSm.setCurrentAndTargetValue (smooth);
            mixSm.setCurrentAndTargetValue (mix);
            stepSm.setCurrentAndTargetValue (step);
            snapPending = false;
        }
        else
        {
            feedbackSm.setTargetValue (feedback);
            smoothSm.setTargetValue (smooth);
            mixSm.setTargetValue (mix);
            stepSm.setTargetValue (step);
        }
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) noexcept override
    {
        if (! channels[0].buffer.isPrepared())
        {
            std::fill (wetL, wetL + numSamples, 0.0f);
            std::fill (wetR, wetR + numSamples, 0.0f);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float fb     = feedbackSm.getNextValue();
            const float smooth = smoothSm.getNextValue();
            const float mix    = mixSm.getNextValue();
            const double step  = (double) stepSm.getNextValue();

            const float in[2] = { inL[i], inR[i] };
            float* wet[2] = { wetL, wetR };

            for (int c = 0; c < 2; ++c)
            {
                Channel& ch = channels[c];
                const double t = (double) std::max (ch.timeSm.getNextValue(), 32.0f);

                // Reloj de segmentos: un segmento nuevo cada T muestras
                ch.cycle += 1.0 / t;
                if (ch.cycle >= 1.0)
                {
                    ch.cycle -= 1.0;
                    startHead (ch, t, smooth);
                }

                // Camino inverso: suma de los dos cabezales con su ventana
                float reverse = 0.0f;
                for (auto& h : ch.heads)
                {
                    if (! h.active)
                        continue;

                    reverse += ch.buffer.readAbsolute (h.position) * windowGain (h);
                    h.position -= step;
                    h.elapsed += 1.0;
                    if (h.elapsed >= h.length)
                        h.active = false;
                }

                // Camino directo
                const float forward = ch.forward.process (ch.buffer, ch.targetDelay);

                const float out = forward + (reverse - forward) * mix;
                wet[c][i] = out;

                ch.buffer.write (in[c] + DelayDsp::softLimit (out) * fb);
            }
        }
    }

    const char* getName() const noexcept override { return "Reverse"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    struct Head
    {
        bool active = false;
        double position = 0.0;   // indice absoluto de lectura
        double elapsed = 0.0;    // muestras transcurridas desde el inicio del segmento
        double length = 0.0;     // duracion total del segmento: T + X
        double fade = 1.0;       // X: duracion de cada flanco
    };

    struct Channel
    {
        CircularDelayBuffer buffer;
        DualTapThiranReader forward;
        Smoothed timeSm;
        Head heads[2];
        int nextHead = 0;
        double cycle = 1.0;
        float targetDelay = 100.0f;
    };

    void startHead (Channel& ch, double t, float smooth) noexcept
    {
        const double minFade = 0.002 * sampleRate;
        const double x = std::min (0.5 * t, std::max (minFade, (double) smooth * 0.5 * t));

        Head& h = ch.heads[ch.nextHead];
        ch.nextHead ^= 1;

        h.active = true;
        h.position = (double) ch.buffer.getWriteIndex() - (double) CircularDelayBuffer::kMinDelay;
        h.elapsed = 0.0;
        h.length = t + x;
        h.fade = x;
    }

    // Ventana Hann en los flancos (medio coseno de subida y de bajada), 1 en el centro
    static float windowGain (const Head& h) noexcept
    {
        if (h.elapsed < h.fade)
            return DelayWindows::hann (0.5f * (float) (h.elapsed / h.fade));

        const double tail = h.elapsed - (h.length - h.fade);
        if (tail > 0.0)
            return DelayWindows::hann (0.5f + 0.5f * (float) (tail / h.fade));

        return 1.0f;
    }

    double sampleRate = 44100.0;
    Channel channels[2];
    Smoothed feedbackSm, smoothSm, mixSm, stepSm;
    bool snapPending = true;
};
