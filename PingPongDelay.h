#pragma once

#include "IDelayAlgorithm.h"

// ============================================================================
//  PingPongDelay
//  Dos lineas (L y R) con realimentacion cruzada:
//      realimentacion hacia la linea L = fb * (cross * salidaR + (1 - cross) * salidaL)
//      realimentacion hacia la linea R = fb * (cross * salidaL + (1 - cross) * salidaR)
//    - Cross-Feedback = 1: ping-pong completo (L -> R -> L ...); 0: cada linea se realimenta a si misma.
//    - Input Mono Sum: la entrada se suma a mono y se inyecta solo en la linea L (ping-pong clasico:
//      el primer eco sale por la izquierda, el segundo por la derecha...). Apagado: L entra en la
//      linea L y R en la linea R.
//    - Pan Spread: 1 = cada linea a su lado, 0 = las dos lineas centradas.
//  Los tiempos usan el lector dual-tap (sin Doppler al cambiarlos).
// ============================================================================
class PingPongDelay final : public IDelayAlgorithm
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
        crossSm.reset (sampleRate, 0.03);
        spreadSm.reset (sampleRate, 0.03);
        monoSm.reset (sampleRate, 0.02);

        reset();
    }

    void reset() override
    {
        for (auto& b : buffers)
            b.reset();

        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float cross    = std::clamp (p.pingPongCross, 0.0f, 1.0f);
        const float spread   = std::clamp (p.pingPongSpread, 0.0f, 1.0f);
        const float mono     = p.pingPongMonoSum ? 1.0f : 0.0f;

        targetDelay[0] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsL), sampleRate);
        targetDelay[1] = DelayTimeCalculator::msToSamples (DelayTimeCalculator::clampMs (p.timeMsR), sampleRate);

        if (snapPending)
        {
            feedbackSm.setCurrentAndTargetValue (feedback);
            crossSm.setCurrentAndTargetValue (cross);
            spreadSm.setCurrentAndTargetValue (spread);
            monoSm.setCurrentAndTargetValue (mono);

            for (int c = 0; c < 2; ++c)
                readers[c].reset (buffers[c], targetDelay[c]);

            snapPending = false;
        }
        else
        {
            feedbackSm.setTargetValue (feedback);
            crossSm.setTargetValue (cross);
            spreadSm.setTargetValue (spread);
            monoSm.setTargetValue (mono);
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
            const float fb     = feedbackSm.getNextValue();
            const float cross  = crossSm.getNextValue();
            const float spread = spreadSm.getNextValue();
            const float mono   = monoSm.getNextValue();

            const float dl = readers[0].process (buffers[0], targetDelay[0]);
            const float dr = readers[1].process (buffers[1], targetDelay[1]);

            // Entrada: estereo, o suma mono inyectada solo en la linea L (mezcla suave entre ambas)
            const float monoIn = 0.5f * (inL[i] + inR[i]);
            const float injL = inL[i] + (monoIn - inL[i]) * mono;
            const float injR = inR[i] * (1.0f - mono);

            // Red de realimentacion cruzada
            const float fbL = fb * (cross * dr + (1.0f - cross) * dl);
            const float fbR = fb * (cross * dl + (1.0f - cross) * dr);

            buffers[0].write (injL + fbL);
            buffers[1].write (injR + fbR);

            // Pan Spread de la salida humeda
            const float a = 0.5f + 0.5f * spread;
            const float b = 1.0f - a;
            wetL[i] = a * dl + b * dr;
            wetR[i] = a * dr + b * dl;
        }
    }

    const char* getName() const noexcept override { return "Ping-Pong"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    double sampleRate = 44100.0;
    CircularDelayBuffer buffers[2];
    DualTapThiranReader readers[2];
    Smoothed feedbackSm, crossSm, spreadSm, monoSm;

    float targetDelay[2] = { 100.0f, 100.0f };
    bool snapPending = true;
};
