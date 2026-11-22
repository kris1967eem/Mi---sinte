#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// ============================================================================
//  CpuMeter
//  Mide cuanto tiempo REAL tarda un trozo de codigo del hilo de audio, como porcentaje del
//  tiempo que dura el bloque de audio (el mismo criterio que el medidor de CPU del DAW).
//
//  Uso desde el hilo de audio:
//      meter.begin();   ...codigo a medir...   meter.end();     // se puede repetir varias veces por bloque
//      meter.publish (numSamples, sampleRate);                   // una vez por bloque
//  Los valores (media y pico) se leen desde la interfaz con getAverage() / getPeak().
// ============================================================================
class CpuMeter
{
public:
    void begin() noexcept { startTicks = juce::Time::getHighResolutionTicks(); }
    void end() noexcept   { accumulatedTicks += juce::Time::getHighResolutionTicks() - startTicks; }

    void addTicks (std::int64_t ticks) noexcept { accumulatedTicks += ticks; }

    // Convierte lo acumulado en este bloque en porcentaje y actualiza media (0.5 s) y pico (cae en 2 s)
    void publish (int numSamples, double sampleRate) noexcept
    {
        if (numSamples <= 0 || sampleRate <= 0.0)
        {
            accumulatedTicks = 0;
            return;
        }

        const double blockSeconds = (double) numSamples / sampleRate;
        const double usedSeconds = juce::Time::highResolutionTicksToSeconds (accumulatedTicks);
        accumulatedTicks = 0;

        const float percent = (float) (100.0 * usedSeconds / blockSeconds);
        const float a = (float) (1.0 - std::exp (-blockSeconds / 0.5));

        average += a * (percent - average);
        peak = std::max (percent, peak * (float) std::exp (-blockSeconds / 2.0));

        publishedAverage.store (average, std::memory_order_relaxed);
        publishedPeak.store (peak, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        accumulatedTicks = 0;
        average = peak = 0.0f;
        publishedAverage.store (0.0f, std::memory_order_relaxed);
        publishedPeak.store (0.0f, std::memory_order_relaxed);
    }

    float getAverage() const noexcept { return publishedAverage.load (std::memory_order_relaxed); }
    float getPeak() const noexcept    { return publishedPeak.load (std::memory_order_relaxed); }

private:
    std::int64_t startTicks = 0;
    std::int64_t accumulatedTicks = 0;
    float average = 0.0f, peak = 0.0f;          // solo los toca el hilo de audio
    std::atomic<float> publishedAverage { 0.0f };
    std::atomic<float> publishedPeak { 0.0f };
};
