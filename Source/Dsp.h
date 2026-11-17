#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synth
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

// ---------------------------------------------------------------- Rng
struct Rng
{
    uint32_t s = 22222u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float bipolar() { return uniform() * 2.0f - 1.0f; }                        // -1..1
};

// ---------------------------------------------------------------- ADSR
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double sr) { sampleRate = sr; }

    // Tiempos en segundos
    void setParams (float attack, float decay, float sustain, float release)
    {
        sustainLevel = std::clamp (sustain, 0.0f, 1.0f);
        attackInc    = 1.0f / std::max (1.0f, attack * (float) sampleRate);
        decayCoef    = timeToCoef (decay);
        releaseCoef  = timeToCoef (release);
    }

    void reset()   { stage = Stage::Idle; value = 0.0f; }
    void noteOn()  { stage = Stage::Attack; }                       // arranca desde el valor actual (sin clic)
    void noteOff() { if (stage != Stage::Idle) stage = Stage::Release; }

    bool  isActive()    const { return stage != Stage::Idle; }
    bool  isReleasing() const { return stage == Stage::Release; }
    float getValue()    const { return value; }

    float process()
    {
        switch (stage)
        {
            case Stage::Attack:
                value += attackInc;
                if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                value = sustainLevel + (value - sustainLevel) * decayCoef;
                if (std::fabs (value - sustainLevel) < 1.0e-4f) { value = sustainLevel; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                value += (sustainLevel - value) * 0.002f;           // suaviza cambios en vivo
                break;
            case Stage::Release:
                value *= releaseCoef;
                if (value < 1.0e-4f) { value = 0.0f; stage = Stage::Idle; }
                break;
            case Stage::Idle:
            default:
                break;
        }
        return value;
    }

    float processBlock (int n)
    {
        float v = value;
        for (int i = 0; i < n; ++i) v = process();
        return v;
    }

private:
    float timeToCoef (float seconds) const
    {
        const double samples = std::max (1.0, (double) seconds * sampleRate);
        return (float) std::exp (std::log (0.001) / samples);       // -60 dB en 'seconds'
    }

    double sampleRate = 44100.0;
    Stage  stage = Stage::Idle;
    float  value = 0.0f;
    float  sustainLevel = 0.8f;
    float  attackInc = 0.01f, decayCoef = 0.999f, releaseCoef = 0.999f;
};

// ---------------------------------------------------------------- Filtro SVF (TPT)
struct SvfCoeffs
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1;
};

inline SvfCoeffs makeSvf (float cutoffHz, float resonance01, double sampleRate)
{
    SvfCoeffs c;
    const float g = std::tan (kPi * cutoffHz / (float) sampleRate);
    c.k  = 2.0f - 1.96f * std::clamp (resonance01, 0.0f, 1.0f);
    c.a1 = 1.0f / (1.0f + g * (g + c.k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    // mode: 0 LP, 1 HP, 2 BP, 3 Notch
    float process (float x, const SvfCoeffs& c, int mode)
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        switch (mode)
        {
            case 0:  return v2;                         // paso bajo
            case 1:  return x - c.k * v1 - v2;          // paso alto
            case 2:  return c.k * v1;                   // paso banda (pico normalizado a 1)
            default: return x - c.k * v1;               // notch
        }
    }
};

// ---------------------------------------------------------------- LFO
class Lfo
{
public:
    void setSampleRate (double sr) { sampleRate = sr; }
    void reset() { phase = 0.0; held = 0.0f; seeded = false; }

    // Avanza n muestras y devuelve el valor (-1..1)
    float advance (int n, float rateHz, int shape)
    {
        phase += (double) rateHz * (double) n / sampleRate;
        bool wrapped = false;
        while (phase >= 1.0) { phase -= 1.0; wrapped = true; }
        if (wrapped || ! seeded) { held = rng.bipolar(); seeded = true; }

        const float p = (float) phase;
        switch (shape)
        {
            case 0:  return std::sin (kTwoPi * p);
            case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case 2:  return 2.0f * p - 1.0f;
            case 3:  return 1.0f - 2.0f * p;
            case 4:  return p < 0.5f ? 1.0f : -1.0f;
            default: return held;
        }
    }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    float  held = 0.0f;
    bool   seeded = false;
    Rng    rng;
};
} // namespace synth
