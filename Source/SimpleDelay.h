#pragma once

#include "IDelayAlgorithm.h"

// ============================================================================
//  SimpleDelay
//  Delay estereo con:
//    - Interpolacion Thiran all-pass y Dual-Tap Crossfade (DualTapThiranReader): al cambiar el
//      tiempo no hay efecto Doppler ni clics.
//    - Stereo Offset: milisegundos que se suman al tiempo del canal R.
//    - Low-Cut y High-Cut (12 dB/oct) dentro del lazo de realimentacion: cada repeticion
//      queda mas filtrada que la anterior.
//    - Width (Mid/Side) aplicado a la salida humeda.
//  El tiempo se aplica ESCALONADO (sin suavizar): el crossfade del lector ya hace de suavizado.
// ============================================================================
class SimpleDelay final : public IDelayAlgorithm
{
public:
    void prepare (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;

        const int maxDelay = DelayDsp::getMaxDelaySamples (sampleRate);
        for (auto& b : buffers)
            b.prepare (maxDelay);
        for (auto& r : readers)
            r.prepare (sampleRate, 25.0f);

        feedbackSm.reset (sampleRate, 0.03);
        widthSm.reset (sampleRate, 0.03);
        highCutSm.reset (sampleRate, 0.03);
        lowCutSm.reset (sampleRate, 0.03);

        reset();
    }

    void reset() override
    {
        for (auto& b : buffers)
            b.reset();
        for (auto& f : highCut)
            f.reset();
        for (auto& f : lowCut)
            f.reset();

        lastHighCut = -1.0f;
        lastLowCut = -1.0f;
        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float width    = std::clamp (p.simpleWidth, 0.0f, 2.0f);
        const float hiCut    = std::clamp (p.simpleHighCutHz, 200.0f, 20000.0f);
        const float loCut    = std::clamp (p.simpleLowCutHz, 20.0f, 2000.0f);

        targetDelay[0] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsL), sampleRate);
        targetDelay[1] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsR + p.simpleStereoOffsetMs),
                                                           sampleRate);

        if (snapPending)
        {
            feedbackSm.setCurrentAndTargetValue (feedback);
            widthSm.setCurrentAndTargetValue (width);
            highCutSm.setCurrentAndTargetValue (hiCut);
            lowCutSm.setCurrentAndTargetValue (loCut);

            for (int c = 0; c < 2; ++c)
                readers[c].reset (buffers[c], targetDelay[c]);

            snapPending = false;
        }
        else
        {
            feedbackSm.setTargetValue (feedback);
            widthSm.setTargetValue (width);
            highCutSm.setTargetValue (hiCut);
            lowCutSm.setTargetValue (loCut);
        }
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) noexcept override
    {
        if (! buffers[0].isPrepared())
        {
            std::fill (wetL, wetL + numSamples, 0.0f);
            std::fill (wetR, wetR + numSamples, 0.0f);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float fb    = feedbackSm.getNextValue();
            const float width = widthSm.getNextValue();
            const float hi    = highCutSm.getNextValue();
            const float lo    = lowCutSm.getNextValue();

            if (hi != lastHighCut)
            {
                for (auto& f : highCut)
                    f.setCutoff (hi, sampleRate);
                lastHighCut = hi;
            }

            if (lo != lastLowCut)
            {
                for (auto& f : lowCut)
                    f.setCutoff (lo, sampleRate);
                lastLowCut = lo;
            }

            // Con el filtro "abierto" se usa la senal sin filtrar, pero el filtro sigue corriendo
            // para que su estado sea coherente cuando se cierre.
            const bool hiOpen = hi >= 19900.0f;
            const bool loOpen = lo <= 20.5f;

            float wet[2];
            const float in[2] = { inL[i], inR[i] };

            for (int c = 0; c < 2; ++c)
            {
                const float delayed = readers[c].process (buffers[c], targetDelay[c]);

                const float lp = highCut[c].lowpass (delayed);
                const float afterHi = hiOpen ? delayed : lp;

                const float hp = lowCut[c].highpass (afterHi);
                wet[c] = loOpen ? afterHi : hp;

                buffers[c].write (in[c] + wet[c] * fb);
            }

            // Width (Mid/Side) sobre la salida humeda
            const float mid  = 0.5f * (wet[0] + wet[1]);
            const float side = 0.5f * (wet[0] - wet[1]) * width;
            wetL[i] = mid + side;
            wetR[i] = mid - side;
        }
    }

    const char* getName() const noexcept override { return "Simple"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using SmoothedMul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

    double sampleRate = 44100.0;
    CircularDelayBuffer buffers[2];
    DualTapThiranReader readers[2];
    DelayDsp::TwoStageFilter highCut[2], lowCut[2];

    Smoothed feedbackSm, widthSm;
    SmoothedMul highCutSm, lowCutSm;

    float targetDelay[2] = { 100.0f, 100.0f };
    float lastHighCut = -1.0f, lastLowCut = -1.0f;
    bool snapPending = true;
};
