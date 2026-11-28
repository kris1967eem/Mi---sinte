#pragma once

#include <memory>
#include <cmath>
#include "WTData.h"

namespace wavetable
{

class WTOscillator
{
public:
    WTOscillator() = default;
    ~WTOscillator() = default;

    void prepare(double sampleRate);
    void setWavetable(std::shared_ptr<const WavetableData> wt);

    void setPosition(float pos01);
    void setWarpMode(int mode);
    void setWarpAmount(float amt);
    void setHighQuality(bool hq);
    void setFrequency(float hz);

    void process(float* outL, float* outR, int numSamples);

    void resetPhase(float phase01 = 0.0f);

private:
    static inline float hermite4(float xm1, float x0, float x1, float x2, float t)
    {
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    static inline float warpPhase(float p, int mode, float amt)
    {
        switch (mode)
        {
            case 1: return std::pow(p, 1.0f + 3.0f * amt);
            case 2: return 1.0f - std::pow(1.0f - p, 1.0f + 3.0f * amt);
            case 3: { const float q = p * (1.0f + 7.0f * amt); return q - std::floor(q); }
            case 4: { const float t = 0.5f - 0.49f * amt;
                      return p < t ? 0.5f * p / t : 0.5f + 0.5f * (p - t) / (1.0f - t); }
            default: return p;
        }
    }

    inline float readSample(int frame, int mip, float phase01) const;
    inline float readFrame(int f0, int f1, float frac, int mip, float phase01) const;

    std::shared_ptr<const WavetableData> wavetable;
    double sr = 44100.0;
    float phase = 0.0f;
    float inc = 0.0f;
    float position = 0.0f;
    int warpMode = 0;
    float warpAmount = 0.0f;
    bool highQuality = true;
    int currentMip = 0;
};

} // namespace wavetable