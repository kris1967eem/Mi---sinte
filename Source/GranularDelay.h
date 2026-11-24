#pragma once

#include <array>

#include "DelayDspExtras.h"
#include "IDelayAlgorithm.h"

// ============================================================================
//  GranularDelay
//  Motor granular sobre un buffer circular estereo.
// ============================================================================
class GranularDelay final : public IDelayAlgorithm
{
public:
    static constexpr int kMaxGrains = 32;
    static constexpr int kTableSize = 1024;

    void prepare (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;

        // Retardo maximo 5 s + dispersion del 50 % + grano de 500 ms + margen
        const int capacity = (int) std::ceil (sampleRate * 8.0);
        for (auto& b : buffers)
            b.prepare (capacity);

        feedbackSm.reset (sampleRate, 0.03);
        sizeSm.reset (sampleRate, 0.03);
        densitySm.reset (sampleRate, 0.03);
        spraySm.reset (sampleRate, 0.03);

        buildTables();
        reset();
    }

    void reset() override
    {
        for (auto& b : buffers)
            b.reset();
        for (auto& g : grains)
            g = Grain();

        samplesToNextGrain = 0.0;
        noise = DelayDsp::NoiseGenerator (0x9e3779b9u);
        snapPending = true;
    }

    void setParameters (const DelayAlgorithmParams& p) override
    {
        const float feedback = std::clamp (p.feedback, 0.0f, DelayDsp::kMaxFeedback);
        const float size     = std::clamp (p.grainSizeMs, 10.0f, 500.0f);
        const float density  = std::clamp (p.grainDensity, 1.0f, 80.0f);
        const float spray    = std::clamp (p.grainSpray, 0.0f, 1.0f);

        pitchRatio = std::exp2 (std::clamp (p.grainPitchSemitones, -12.0f, 12.0f) / 12.0f);
        delayMs[0] = DelayTimeCalculator::clampMs (p.timeMsL);
        delayMs[1] = DelayTimeCalculator::clampMs (p.timeMsR);

        if (snapPending)
        {
            feedbackSm.setCurrentAndTargetValue (feedback);
            sizeSm.setCurrentAndTargetValue (size);
            densitySm.setCurrentAndTargetValue (density);
            spraySm.setCurrentAndTargetValue (spray);
            snapPending = false;
        }
        else
        {
            feedbackSm.setTargetValue (feedback);
            sizeSm.setTargetValue (size);
            densitySm.setTargetValue (density);
            spraySm.setTargetValue (spray);
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
            const float fb      = feedbackSm.getNextValue();
            const float size    = sizeSm.getNextValue();
            const float density = densitySm.getNextValue();
            const float spray   = spraySm.getNextValue();

            // Nacimiento de granos
            samplesToNextGrain -= 1.0;
            while (samplesToNextGrain <= 0.0)
            {
                spawnGrain (size, density, spray);

                const double jitter = 1.0 + 0.8 * (double) spray * (double) noise.next();
                samplesToNextGrain += std::max (1.0, sampleRate / (double) density * jitter);
            }

            // Suma de los granos activos (con pitch shift para la salida)
            float accL = 0.0f, accR = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                const float w = windowAt (g.age * g.invLength, g.gaussian);

                if (g.fixedStep)
                {
                    const float l = g.coefL[0] * buffers[0].getSampleAbsolute (g.baseL - 1)
                                  + g.coefL[1] * buffers[0].getSampleAbsolute (g.baseL)
                                  + g.coefL[2] * buffers[0].getSampleAbsolute (g.baseL + 1)
                                  + g.coefL[3] * buffers[0].getSampleAbsolute (g.baseL + 2);
                    const float r = g.coefR[0] * buffers[1].getSampleAbsolute (g.baseR - 1)
                                  + g.coefR[1] * buffers[1].getSampleAbsolute (g.baseR)
                                  + g.coefR[2] * buffers[1].getSampleAbsolute (g.baseR + 1)
                                  + g.coefR[3] * buffers[1].getSampleAbsolute (g.baseR + 2);
                    accL += l * w * g.gainL;
                    accR += r * w * g.gainR;
                    ++g.baseL;
                    ++g.baseR;
                }
                else
                {
                    accL += buffers[0].readAbsolute (g.positionL) * w * g.gainL;
                    accR += buffers[1].readAbsolute (g.positionR) * w * g.gainR;
                    g.positionL += g.step;
                    g.positionR += g.step;
                }

                g.age += 1.0;
                if (g.age >= g.length)
                    g.active = false;
            }

            wetL[i] = accL;
            wetR[i] = accR;

            // Realimentacion sin acumulacion de pitch (evita el efecto escalera)
            const double dSampL = std::clamp ((double) delayMs[0] * 0.001 * sampleRate, 1.0, (double) buffers[0].getMaxDelay() - 2.0);
            const double dSampR = std::clamp ((double) delayMs[1] * 0.001 * sampleRate, 1.0, (double) buffers[1].getMaxDelay() - 2.0);
            const float fbSigL = buffers[0].readAbsolute ((double) buffers[0].getWriteIndex() - dSampL);
            const float fbSigR = buffers[1].readAbsolute ((double) buffers[1].getWriteIndex() - dSampR);

            buffers[0].write (inL[i] + DelayDsp::softLimit (fbSigL) * fb);
            buffers[1].write (inR[i] + DelayDsp::softLimit (fbSigR) * fb);
        }
    }

    const char* getName() const noexcept override { return "Granular"; }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    struct Grain
    {
        bool active = false;
        bool gaussian = true;
        double positionL = 0.0, positionR = 0.0;
        double step = 1.0;
        double age = 0.0, length = 1.0;
        double invLength = 1.0;
        float gainL = 1.0f, gainR = 1.0f;

        bool fixedStep = false;
        std::int64_t baseL = 0, baseR = 0;
        float coefL[4] {}, coefR[4] {};
    };

    void buildTables() noexcept
    {
        double sumG = 0.0, sumT = 0.0;
        for (int i = 0; i <= kTableSize; ++i)
        {
            const float t = (float) i / (float) kTableSize;
            gaussTable[(size_t) i] = DelayWindows::gaussian (t, 0.2f);
            tukeyTable[(size_t) i] = DelayWindows::tukey (t, 0.5f);
            sumG += gaussTable[(size_t) i];
            sumT += tukeyTable[(size_t) i];
        }
        meanGauss = (float) (sumG / (kTableSize + 1));
        meanTukey = (float) (sumT / (kTableSize + 1));
    }

    float windowAt (double phase, bool gaussian) const noexcept
    {
        const double pos = std::clamp (phase, 0.0, 1.0) * (double) kTableSize;
        const int i = std::min ((int) pos, kTableSize - 1);
        const float f = (float) (pos - (double) i);
        const auto& table = gaussian ? gaussTable : tukeyTable;
        return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
    }

    static void setLagrangeCoefficients (double position, std::int64_t& base, float* coef) noexcept
    {
        const double floorPos = std::floor (position);
        base = (std::int64_t) floorPos;
        const float f = (float) (position - floorPos);

        coef[0] = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
        coef[1] = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
        coef[2] = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
        coef[3] = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);
    }

    void spawnGrain (float sizeMs, float density, float spray) noexcept
    {
        Grain* slot = nullptr;
        double bestProgress = -1.0;
        for (auto& g : grains)
        {
            if (! g.active)
            {
                slot = &g;
                break;
            }

            const double progress = g.age / g.length;
            if (progress > bestProgress)
            {
                bestProgress = progress;
                slot = &g;
            }
        }

        const double length = std::max (32.0, (double) sizeMs * 0.001 * sampleRate);
        const double step = (double) pitchRatio;
        const double minDelay = 3.0 + std::max (0.0, step - 1.0) * length;
        const double maxDelay = (double) buffers[0].getMaxDelay() - std::max (0.0, 1.0 - step) * length - 4.0;

        const double rnd = (double) noise.next();
        double delay[2];
        for (int c = 0; c < 2; ++c)
        {
            const double base = (double) delayMs[c] * 0.001 * sampleRate;
            const double sprayed = base + (double) spray * 0.5 * base * rnd;
            delay[c] = std::clamp (sprayed, minDelay, std::max (minDelay, maxDelay));
        }

        const double writeIndex = (double) buffers[0].getWriteIndex();

        const float overlap = density * sizeMs * 0.001f;
        const bool gaussian = overlap >= 2.0f;
        const float meanWindow = gaussian ? meanGauss : meanTukey;
        const float s = std::max (1.0f, overlap * meanWindow);
        const float gain = (1.0f / s) * (1.0f + spray * (std::sqrt (s) - 1.0f));

        const float pan = spray * noise.next();
        slot->active = true;
        slot->gaussian = gaussian;
        slot->positionL = writeIndex - delay[0];
        slot->positionR = writeIndex - delay[1];
        slot->step = step;
        slot->age = 0.0;
        slot->length = length;
        slot->invLength = 1.0 / length;

        slot->fixedStep = std::abs (step - 1.0) < 1.0e-9;
        if (slot->fixedStep)
        {
            setLagrangeCoefficients (slot->positionL, slot->baseL, slot->coefL);
            setLagrangeCoefficients (slot->positionR, slot->baseR, slot->coefR);
        }
        slot->gainL = gain * (1.0f - std::max (0.0f, pan));
        slot->gainR = gain * (1.0f + std::min (0.0f, pan));
    }

    double sampleRate = 44100.0;
    CircularDelayBuffer buffers[2];
    Smoothed feedbackSm, sizeSm, densitySm, spraySm;

    std::array<Grain, kMaxGrains> grains;
    std::array<float, kTableSize + 1> gaussTable {}, tukeyTable {};
    float meanGauss = 0.5f, meanTukey = 0.75f;

    float pitchRatio = 1.0f;
    float delayMs[2] = { 300.0f, 300.0f };
    double samplesToNextGrain = 0.0;
    DelayDsp::NoiseGenerator noise { 0x9e3779b9u };
    bool snapPending = true;
};
