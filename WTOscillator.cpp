#include "WTOscillator.h"
#include <algorithm>

namespace wavetable
{

void WTOscillator::prepare(double sampleRate)
{
    sr = sampleRate;
    phase = 0.0f;
    inc = 0.0f;
    currentMip = 0;
}

void WTOscillator::setWavetable(std::shared_ptr<const WavetableData> wt)
{
    wavetable = std::move(wt);
}

void WTOscillator::setPosition(float pos01)
{
    position = std::clamp(pos01, 0.0f, 1.0f);
}

void WTOscillator::setWarpMode(int mode)
{
    warpMode = std::clamp(mode, 0, 4);
}

void WTOscillator::setWarpAmount(float amt)
{
    warpAmount = std::clamp(amt, 0.0f, 1.0f);
}

void WTOscillator::setHighQuality(bool hq)
{
    highQuality = hq;
}

void WTOscillator::setFrequency(float hz)
{
    if (hz < 1.0f) hz = 1.0f;
    if (hz > static_cast<float>(sr) * 0.49f)
        hz = static_cast<float>(sr) * 0.49f;

    inc = hz / static_cast<float>(sr);

    if (wavetable)
        currentMip = WavetableData::mipForFrequency(hz, static_cast<float>(sr));
}

void WTOscillator::resetPhase(float phase01)
{
    phase = phase01 - std::floor(phase01);
}

inline float WTOscillator::readSample(int frame, int mip, float phase01) const
{
    const float* data = wavetable->frame(frame, mip);
    constexpr int N = WavetableData::kSamples;

    const float x = phase01 * static_cast<float>(N);
    const int i1 = static_cast<int>(x);
    const float t = x - static_cast<float>(i1);

    const int i0 = (i1 - 1 + N) & (N - 1);
    const int i2 = (i1 + 1) & (N - 1);
    const int i3 = (i1 + 2) & (N - 1);
    const int i1m = i1 & (N - 1);

    return hermite4(data[i0], data[i1m], data[i2], data[i3], t);
}

inline float WTOscillator::readFrame(int f0, int f1, float frac, int mip, float phase01) const
{
    if (f0 == f1)
        return readSample(f0, mip, phase01);

    const float a = readSample(f0, mip, phase01);

    if (!highQuality)
    {
        const float b = readSample(f1, mip, phase01);
        return a + (b - a) * frac;
    }

    // Hermite en Y (necesita f0-1 y f1+1)
    const int fm1 = std::max(0, f0 - 1);
    const int fp1 = std::min(WavetableData::kFrames - 1, f1 + 1);

    const float xm1 = readSample(fm1, mip, phase01);
    const float x0 = a;
    const float x1 = readSample(f1, mip, phase01);
    const float x2 = readSample(fp1, mip, phase01);

    return hermite4(xm1, x0, x1, x2, frac);
}

void WTOscillator::process(float* outL, float* outR, int numSamples)
{
    if (!wavetable)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        return;
    }

    const float fpos = position * static_cast<float>(WavetableData::kFrames - 1);
    const int f0 = std::min(static_cast<int>(fpos), WavetableData::kFrames - 1);
    const int f1 = std::min(f0 + 1, WavetableData::kFrames - 1);
    const float frac = fpos - static_cast<float>(f0);

    for (int i = 0; i < numSamples; ++i)
    {
        const float p = warpPhase(phase, warpMode, warpAmount);
        const float s = readFrame(f0, f1, frac, currentMip, p);

        outL[i] = s;
        outR[i] = s;

        phase += inc;
        if (phase >= 1.0f)
            phase -= std::floor(phase);
    }
}

} // namespace wavetable