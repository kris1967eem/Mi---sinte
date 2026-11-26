#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <cmath>

namespace wavetable
{

// ============================================================================
//  WavetableData
//  Estructura inmutable que contiene una wavetable completa con mipmaps.
//  Se comparte entre todas las voces mediante std::shared_ptr<const WavetableData>.
//
//  Layout: data[frame][mip][sample]
//    - frame:  0..255 (256 frames)
//    - mip:    0..5   (6 niveles, uno por octava)
//    - sample: 0..2047 (2048 muestras por frame)
// ============================================================================
struct WavetableData
{
    static constexpr int kFrames = 256;
    static constexpr int kSamples = 2048;
    static constexpr int kMips = 6;
    static constexpr int kSizePerFrame = kMips * kSamples;
    static constexpr int kTotalFloats = kFrames * kSizePerFrame;

    std::vector<float> data;
    juce::String name;
    int sourceNumFrames = 0;
    double sourceSampleRate = 44100.0;

    WavetableData()
    {
        data.resize(static_cast<size_t>(kTotalFloats), 0.0f);
    }

    inline const float* frame(int f, int mip) const noexcept
    {
        const int ff = juce::jlimit(0, kFrames - 1, f);
        const int mm = juce::jlimit(0, kMips - 1, mip);
        return data.data() + static_cast<size_t>(ff * kSizePerFrame + mm * kSamples);
    }

    inline float* frameWrite(int f, int mip) noexcept
    {
        const int ff = juce::jlimit(0, kFrames - 1, f);
        const int mm = juce::jlimit(0, kMips - 1, mip);
        return data.data() + static_cast<size_t>(ff * kSizePerFrame + mm * kSamples);
    }

    static int mipForFrequency(float freqHz, float sampleRate) noexcept
    {
        const float maxHarm = 0.5f * sampleRate / juce::jmax(freqHz, 1.0f);
        int m = 0;
        while (m < kMips - 1 && static_cast<float>(1024 >> m) > maxHarm)
            ++m;
        return m;
    }

    static const char* mipName(int mip)
    {
        static const char* names[kMips] = { "0", "1", "2", "3", "4", "5" };
        return (mip >= 0 && mip < kMips) ? names[mip] : "?";
    }
};

using WavetablePtr = std::shared_ptr<const WavetableData>;

} // namespace wavetable
