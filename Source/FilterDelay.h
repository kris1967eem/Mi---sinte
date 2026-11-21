#pragma once

#include <juce_dsp/juce_dsp.h>

#include "DelayDspExtras.h"
#include "IDelayAlgorithm.h"

// ============================================================================
//  FilterDelay
//  Delay estereo con un filtro resonante ZDF (juce::dsp::StateVariableTPTFilter) DENTRO del
//  lazo de realimentacion: cada repeticion pasa otra vez por el filtro.
//    - Cutoff, Resonance, Filter Mode (LP / BP / HP).
//    - LFO Mod Depth: un LFO senoidal lento (0.4 Hz, L y R desfasados 90 grados) mueve el
//      cutoff hasta +/- 2 octavas.
//    - Estabilidad: la resonancia se limita automaticamente para que la ganancia del lazo en
//      el pico del filtro (realimentacion x pico) no pase de 0.95. Con realimentacion alta la
//      resonancia efectiva es menor; con realimentacion baja se puede llegar a Q = 12.
//    - Al cambiar de modo de filtro (LP/BP/HP) la salida del filtro baja a cero en 5 ms, se
//      cambia el tipo y vuelve a subir: sin clics.
//  El tiempo se aplica escalonado con el lector dual-tap (sin Doppler).
// ============================================================================
class FilterDelay final : public IDelayAlgorithm
{
public:
    static constexpr int   kControlInterval = 8;      // el filtro se actualiza cada 8 muestras
    static constexpr float kLfoRateHz       = 0.4f;
    static constexpr float kLfoOctaves      = 2.0f;

    void prepare (double newSampleRate, int maxBlockSize) override
    {
        sampleRate = newSampleRate;

        const int maxDelay = DelayDsp::getMaxDelaySamples (sampleRate);
        for (auto& b : buffers)
            b.prepare (maxDelay);
        for (auto& r : readers)
            r.prepare (sampleRate, 25.0f);

        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) std::max (maxBlockSize, 1), 1 };
        for (auto& f : filters)
            f.prepare (spec);

        feedbackSm.reset (sampleRate, 0.03);
        cutoffSm.reset (sampleRate, 0.03);
        resonanceSm.reset (sampleRate, 0.03);
        depthSm.reset (sampleRate, 0.05);
        modeGainSm.reset (sampleRate, 0.005);

        reset();
    }

    void reset() override
    {
        for (auto& b : buffers)
            b.reset();
        for (auto& f : filters)
            f.reset();

        lfoPhase = 0.0;
        controlCounter = 0;
        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback  = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float cutoff    = std::clamp (p.filterCutoffHz, 20.0f, 20000.0f);
        const float resonance = std::clamp (p.filterResonance, 0.0f, 1.0f);
        const float depth     = std::clamp (p.filterLfoDepth, 0.0f, 1.0f);

        targetMode = std::clamp (p.filterMode, 0, 2);

        targetDelay[0] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsL), sampleRate);
        targetDelay[1] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsR), sampleRate);

        if (snapPending)
        {
            feedbackSm.setCurrentAndTargetValue (feedback);
            cutoffSm.setCurrentAndTargetValue (cutoff);
            resonanceSm.setCurrentAndTargetValue (resonance);
            depthSm.setCurrentAndTargetValue (depth);
            modeGainSm.setCurrentAndTargetValue (1.0f);

            activeMode = targetMode;
            applyFilterType();

            for (int c = 0; c < 2; ++c)
                readers[c].reset (buffers[c], targetDelay[c]);

            curFeedback = feedback;
            curCutoff = cutoff;
            curResonance = resonance;
            curDepth = depth;
            updateFilters();

            snapPending = false;
        }
        else
        {
            feedbackSm.setTargetValue (feedback);
            cutoffSm.setTargetValue (cutoff);
            resonanceSm.setTargetValue (resonance);
            depthSm.setTargetValue (depth);
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
            curFeedback  = feedbackSm.getNextValue();
            curCutoff    = cutoffSm.getNextValue();
            curResonance = resonanceSm.getNextValue();
            curDepth     = depthSm.getNextValue();
            const float modeGain = modeGainSm.getNextValue();

            lfoPhase += (double) kLfoRateHz / sampleRate;
            lfoPhase -= std::floor (lfoPhase);

            if (controlCounter == 0)
                controlUpdate();
            controlCounter = (controlCounter + 1) % kControlInterval;

            const float in[2] = { inL[i], inR[i] };
            float* wet[2] = { wetL, wetR };

            for (int c = 0; c < 2; ++c)
            {
                const float delayed = readers[c].process (buffers[c], targetDelay[c]);
                const float filtered = filters[c].processSample (0, delayed) * modeGain;

                wet[c][i] = filtered;
                buffers[c].write (in[c] + filtered * curFeedback);
            }
        }
    }

    const char* getName() const noexcept override { return "Filter"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using SmoothedMul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

    void applyFilterType() noexcept
    {
        using Type = juce::dsp::StateVariableTPTFilterType;
        const Type t = activeMode == 0 ? Type::lowpass : (activeMode == 1 ? Type::bandpass : Type::highpass);
        for (auto& f : filters)
            f.setType (t);
    }

    // Cambio de modo sin clics: bajar a cero, cambiar el tipo, volver a subir
    void controlUpdate() noexcept
    {
        if (targetMode != activeMode)
        {
            modeGainSm.setTargetValue (0.0f);
            if (modeGainSm.getCurrentValue() <= 0.0001f)
            {
                activeMode = targetMode;
                applyFilterType();
                modeGainSm.setTargetValue (1.0f);
            }
        }
        else
        {
            modeGainSm.setTargetValue (1.0f);
        }

        updateFilters();
    }

    // Maxima Q que mantiene (realimentacion x ganancia de pico del filtro) <= 0.95.
    // Ganancia de pico de un filtro de 2 polos con factor Q: P(Q) = Q / sqrt(1 - 1/(4 Q^2)) para Q > 0.707.
    // Invirtiendo: Q^2 = (P^2 + P * sqrt(P^2 - 1)) / 2.
    static float stableQLimit (float feedback) noexcept
    {
        const float p = std::max (0.95f / std::max (feedback, 0.05f), 1.0f);
        return std::sqrt ((p * p + p * std::sqrt (p * p - 1.0f)) * 0.5f);
    }

    void updateFilters() noexcept
    {
        const float qWanted = 0.7071f * std::pow (17.0f, curResonance);          // 0.71 .. 12
        const float q = std::clamp (std::min (qWanted, stableQLimit (curFeedback)), 0.5f, 12.0f);
        const float maxHz = (float) std::min (20000.0, 0.45 * sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            const float lfo = std::sin (6.28318530717959f * (float) (lfoPhase + 0.25 * (double) c));
            float fc = curCutoff * std::exp2 (kLfoOctaves * curDepth * lfo);
            fc = std::clamp (fc, 20.0f, maxHz);

            filters[c].setCutoffFrequency (fc);
            filters[c].setResonance (q);
        }
    }

    double sampleRate = 44100.0;
    CircularDelayBuffer buffers[2];
    DualTapThiranReader readers[2];
    juce::dsp::StateVariableTPTFilter<float> filters[2];

    Smoothed feedbackSm, resonanceSm, depthSm, modeGainSm;
    SmoothedMul cutoffSm;

    float curFeedback = 0.35f, curCutoff = 2000.0f, curResonance = 0.3f, curDepth = 0.0f;
    float targetDelay[2] = { 100.0f, 100.0f };
    double lfoPhase = 0.0;
    int controlCounter = 0;
    int activeMode = 0, targetMode = 0;
    bool snapPending = true;
};
