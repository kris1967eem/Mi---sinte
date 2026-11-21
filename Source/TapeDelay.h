#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

#include "IDelayAlgorithm.h"

// ============================================================================
//  TapeDelay
//  Delay de cinta:
//    - Lectura con interpolacion cubica de Lagrange. El tiempo se desliza (suavizado + limite de
//      velocidad), asi que al cambiarlo se oye el "repitch" clasico de la cinta.
//    - Saturacion no lineal (tanh asimetrica) en la GRABACION (entrada + realimentacion), con
//      sobremuestreo 4x (juce::dsp::Oversampling) para evitar aliasing.
//    - Wow & Flutter: modulacion lenta (wow), rapida (flutter) y ruido lento del tiempo de lectura.
//    - Tape Age: paso bajo (de 18 kHz a 2.5 kHz) en la cabeza de reproduccion y un siseo sutil que
//      solo aparece mientras suenan ecos.
//
//  Como funciona el procesado por bloques pequenos (chunks):
//    El sobremuestreador trabaja con bloques, pero la realimentacion necesita muestras ya escritas.
//    Se procesa en chunks de 16 muestras: dentro de un chunk todas las lecturas se hacen sobre datos
//    anteriores al chunk (el retardo siempre es mayor que el chunk) y al final se escribe el chunk.
//    La latencia del sobremuestreador se resta del tiempo de lectura, asi el eco sale a su tiempo.
// ============================================================================
class TapeDelay final : public IDelayAlgorithm
{
public:
    static constexpr int kChunk = 16;

    void prepare (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;

        const int maxDelay = DelayDsp::getMaxDelaySamples (sampleRate);
        for (auto& b : buffers)
            b.prepare (maxDelay);

        oversampler.initProcessing ((size_t) kChunk);
        oversampler.reset();
        latency = std::ceil (oversampler.getLatencyInSamples());

        for (auto& t : timeSm)
            t.reset (sampleRate, 0.05);
        feedbackSm.reset (sampleRate, 0.03);
        driveSm.reset (sampleRate, 0.03);
        wowSm.reset (sampleRate, 0.05);
        ageSm.reset (sampleRate, 0.10);

        // Velocidad maxima de cambio del tiempo: 0.5 ms por ms (la cinta nunca va a mas de x1.5 ni a menos de x0.5)
        maxTimeStepMs = (float) (0.5 * 1000.0 / sampleRate);

        dcCoef = 1.0f - (float) (2.0 * 3.14159265358979 * 8.0 / sampleRate);
        envDecay = (float) std::exp (-1.0 / (0.1 * sampleRate));

        // Ruido lento (~3 Hz) para el "random walk" de la velocidad de la cinta
        noiseLpCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 3.0 / sampleRate));
        const double rms = 0.5774 * std::sqrt ((double) noiseLpCoef / (2.0 - (double) noiseLpCoef));
        noiseScale = (float) (0.3 / rms);

        reset();
    }

    void reset() override
    {
        for (auto& b : buffers)
            b.reset();

        oversampler.reset();

        for (int c = 0; c < 2; ++c)
        {
            ageFilter[c].reset();
            dcX1[c] = dcY1[c] = 0.0f;
            env[c] = 0.0f;
            hissPrev[c] = 0.0f;
            slowNoise[c] = 0.0f;
        }

        wowPhase1[0] = 0.00; wowPhase1[1] = 0.31;
        wowPhase2[0] = 0.17; wowPhase2[1] = 0.64;
        flutterPhase[0] = 0.05; flutterPhase[1] = 0.52;
        lastAge = -1.0f;
        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float drive    = std::clamp (p.tapeDrive, 0.0f, 1.0f);
        const float wow      = std::clamp (p.tapeWowFlutter, 0.0f, 1.0f);
        const float age      = std::clamp (p.tapeAge, 0.0f, 1.0f);

        const float tL = DelayTimeCalculator::clampMs (p.timeMsL);
        const float tR = DelayTimeCalculator::clampMs (p.timeMsR);

        if (snapPending)
        {
            timeSm[0].setCurrentAndTargetValue (tL);
            timeSm[1].setCurrentAndTargetValue (tR);
            timeNow[0] = tL;
            timeNow[1] = tR;

            feedbackSm.setCurrentAndTargetValue (feedback);
            driveSm.setCurrentAndTargetValue (drive);
            wowSm.setCurrentAndTargetValue (wow);
            ageSm.setCurrentAndTargetValue (age);
            snapPending = false;
        }
        else
        {
            timeSm[0].setTargetValue (tL);
            timeSm[1].setTargetValue (tR);
            feedbackSm.setTargetValue (feedback);
            driveSm.setTargetValue (drive);
            wowSm.setTargetValue (wow);
            ageSm.setTargetValue (age);
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

        int pos = 0;
        while (pos < numSamples)
        {
            const int n = std::min (kChunk, numSamples - pos);
            processChunk (inL + pos, inR + pos, wetL + pos, wetR + pos, n);
            pos += n;
        }
    }

    const char* getName() const noexcept override { return "Tape"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    static constexpr float kTwoPi = 6.28318530717959f;

    void processChunk (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept
    {
        const float* in[2] = { inL, inR };
        float* wet[2] = { wetL, wetR };

        // ---- 1) Lectura (cabeza de reproduccion) y construccion de la senal de grabacion ----
        for (int j = 0; j < n; ++j)
        {
            const float fb    = feedbackSm.getNextValue();
            const float drive = driveSm.getNextValue();
            const float wowAmt = wowSm.getNextValue();
            const float age   = ageSm.getNextValue();

            // Tape Age: paso bajo de 18 kHz (nuevo) a 2.5 kHz (viejo), interpolado en escala logaritmica
            if (age != lastAge)
            {
                const float cutoff = 18000.0f * std::pow (2500.0f / 18000.0f, age);
                for (auto& f : ageFilter)
                    f.setCutoff (cutoff, sampleRate);
                lastAge = age;
            }

            // Saturacion: ganancia, sesgo (asimetria) y normalizacion segun el drive
            const float g = 0.3f + 7.7f * drive * drive;
            const float bias = 0.2f * drive;
            gains[j] = g;
            biases[j] = bias;
            biasTanh[j] = std::tanh (bias);
            norms[j] = kSatRef / std::tanh (g * kSatRef);

            const float hissAmp = 0.0012f * age * age;

            // Los 3 osciladores de modulacion avanzan una vez por muestra (compartidos por L y R con desfase)
            for (int c = 0; c < 2; ++c)
            {
                const double w1 = 0.55 / sampleRate, w2 = 1.31 / sampleRate, wf = 7.7 / sampleRate;
                wowPhase1[c] += w1;      wowPhase1[c] -= std::floor (wowPhase1[c]);
                wowPhase2[c] += w2;      wowPhase2[c] -= std::floor (wowPhase2[c]);
                flutterPhase[c] += wf;   flutterPhase[c] -= std::floor (flutterPhase[c]);

                slowNoise[c] += noiseLpCoef * (noise.next() - slowNoise[c]);
                const float walk = std::clamp (slowNoise[c] * noiseScale, -1.0f, 1.0f);

                const float wow = 0.7f * std::sin (kTwoPi * (float) wowPhase1[c])
                                + 0.3f * std::sin (kTwoPi * (float) wowPhase2[c]);
                const float flutter = std::sin (kTwoPi * (float) flutterPhase[c]);

                // Profundidad de la modulacion en ms (a 1.0: wow 2 ms, flutter 0.07 ms, deriva 0.12 ms)
                const float modMs = wowAmt * (2.0f * wow + 0.07f * flutter + 0.12f * walk);

                // Tiempo deslizante: suavizado y limite de velocidad
                const float target = timeSm[c].getNextValue();
                const float diff = target - timeNow[c];
                timeNow[c] += std::clamp (diff, -maxTimeStepMs, maxTimeStepMs);

                float delay = (timeNow[c] + modMs) * 0.001f * (float) sampleRate - latency;
                delay = std::max (delay, (float) j + 3.0f);   // lectura siempre sobre datos anteriores al chunk

                // El retardo se mide desde el inicio del chunk: la muestra j esta j muestras mas cerca
                const float played = buffers[c].readLagrange (delay - (float) j);
                const float aged = ageFilter[c].lowpass (played);

                // Envolvente para que el siseo solo suene con ecos activos
                env[c] = std::max (std::abs (aged), env[c] * envDecay);
                const float gate = std::min (1.0f, env[c] * 10.0f);

                const float white = noise.next();
                const float hiss = (white - hissPrev[c]) * 0.5f * hissAmp * gate;   // siseo sesgado a agudos
                hissPrev[c] = white;

                wet[c][j] = aged + hiss;
                rec[c][j] = in[c][j] + fb * aged;   // lo que se graba: entrada + realimentacion (sin siseo)
            }
        }

        // ---- 2) Saturacion con sobremuestreo 4x ----
        float* channels[2] = { rec[0].data(), rec[1].data() };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) n);

        auto upBlock = oversampler.processSamplesUp (block);
        const int upN = (int) upBlock.getNumSamples();

        for (int c = 0; c < 2; ++c)
        {
            float* d = upBlock.getChannelPointer ((size_t) c);
            for (int k = 0; k < upN; ++k)
            {
                const int j = std::min (k / 4, n - 1);
                d[k] = (std::tanh (gains[j] * d[k] + biases[j]) - biasTanh[j]) * norms[j];
            }
        }

        oversampler.processSamplesDown (block);

        // ---- 3) Bloqueo de continua y escritura en el buffer ----
        for (int c = 0; c < 2; ++c)
        {
            for (int j = 0; j < n; ++j)
            {
                const float x = rec[c][(size_t) j];
                const float y = x - dcX1[c] + dcCoef * dcY1[c];
                dcX1[c] = x;
                dcY1[c] = y;
                buffers[c].write (y);
            }
        }
    }

    static constexpr float kSatRef = 0.3f;   // nivel de referencia que la saturacion mantiene (~ -10 dBFS)

    double sampleRate = 44100.0;
    CircularDelayBuffer buffers[2];

    juce::dsp::Oversampling<float> oversampler { 2, 2,
                                                 juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                 true, true };   // 2 canales, 4x (2^2), IIR de baja latencia
    float latency = 0.0f;

    Smoothed timeSm[2];
    Smoothed feedbackSm, driveSm, wowSm, ageSm;
    float timeNow[2] = { 300.0f, 300.0f };
    float maxTimeStepMs = 0.01f;

    DelayDsp::TwoStageFilter ageFilter[2];
    DelayDsp::NoiseGenerator noise;
    float lastAge = -1.0f;

    double wowPhase1[2] {}, wowPhase2[2] {}, flutterPhase[2] {};
    float slowNoise[2] {};
    float noiseLpCoef = 0.0004f, noiseScale = 40.0f;

    float dcX1[2] {}, dcY1[2] {}, dcCoef = 0.999f;
    float env[2] {}, envDecay = 0.9998f, hissPrev[2] {};

    // Memoria de trabajo del chunk (reservada en el propio objeto: nada se asigna en process)
    std::array<float, kChunk> rec[2];
    std::array<float, kChunk> gains {}, biases {}, biasTanh {}, norms {};

    bool snapPending = true;
};
