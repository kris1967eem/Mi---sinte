#pragma once

#include <memory>
#include <cmath>
#include "WTData.h"

namespace wavetable
{

// ============================================================================
//  WTOscillator
//  Oscilador wavetable con:
//    - Interpolación Hermite 4 puntos en X (muestras)
//    - Interpolación Hermite 4 puntos en Y (frames), configurable (Alta/Baja calidad)
//    - Selección de mip según la frecuencia (anti-aliasing)
//    - Morphing Normal (crossfade entre frames)
//    - Warp modes: bend+, bend-, sync, squeeze
//
//  NO hace allocaciones en audio. Todo se prepara en prepare().
// ============================================================================
class WTOscillator
{
public:
    WTOscillator() = default;
    ~WTOscillator() = default;

    // Prepara el oscilador. Se llama desde prepareToPlay() (fuera de audio).
    void prepare(double sampleRate);

    // Asigna la wavetable activa (compartida entre voces).
    void setWavetable(std::shared_ptr<const WavetableData> wt);

    // Parámetros (se pueden cambiar por bloque)
    void setPosition(float pos01);          // 0..1 (frame)
    void setWarpMode(int mode);             // 0=off, 1=bend+, 2=bend-, 3=sync, 4=squeeze
    void setWarpAmount(float amt);          // 0..1
    void setHighQuality(bool hq);           // true=Hermite en Y, false=lineal en Y
    void setFrequency(float hz);            // frecuencia de la nota

    // Procesa un bloque: escribe numSamples en outL/outR
    void process(float* outL, float* outR, int numSamples);

    // Resetea la fase (al hacer noteOn)
    void resetPhase(float phase01 = 0.0f);

private:
    // Interpolación Hermite 4 puntos
    static inline float hermite4(float xm1, float x0, float x1, float x2, float t)
    {
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Warp de fase (igual que el motor VA)
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

    // Lee una muestra de la wavetable con interpolación
    inline float readSample(int frame, int mip, float phase01) const;

    // Lee un frame interpolando entre mip 0 y mip N (lineal o Hermite en Y)
    inline float readFrame(int f0, int f1, float frac, int mip, float phase01) const;

    std::shared_ptr<const WavetableData> wavetable;
    double sr = 44100.0;
    float phase = 0.0f;          // 0..1
    float inc = 0.0f;            // incremento por muestra
    float position = 0.0f;       // 0..1
    int warpMode = 0;
    float warpAmount = 0.0f;
    bool highQuality = true;
    int currentMip = 0;
};

} // namespace wavetable