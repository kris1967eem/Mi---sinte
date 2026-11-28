#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Dsp.h"

namespace synth
{
// Banco de wavetables con 8 tramas integradas. Cada trama se guarda en 11 niveles
// "mip" limitados en banda (uno por octava) para evitar aliasing en notas agudas.
// Mas adelante se podran cargar tablas desde archivos .wav.
class WavetableBank
{
public:
    static constexpr int kSize   = 2048;   // muestras por ciclo
    static constexpr int kMips   = 11;     // mip m contiene (1024 >> m) armonicos
    static constexpr int kFrames = 8;

    WavetableBank() { build(); }

    // Puntero a kSize + 1 muestras (la ultima repite la primera)
    const float* table (int frame, int mip) const
    {
        return data.data() + ((size_t) frame * kMips + (size_t) mip) * (kSize + 1);
    }

    // Mip mas detallado que no produce aliasing para esa frecuencia
    static int mipForFreq (float freqHz, float sampleRate)
    {
        const float maxHarm = 0.5f * sampleRate / std::max (freqHz, 1.0f);
        int m = 0;
        while (m < kMips - 1 && (float) (1024 >> m) > maxHarm) ++m;
        return m;
    }

    static const char* frameName (int frame)
    {
        static const char* names[kFrames] = { "Seno", "Triangulo", "Sierra", "Cuadrada",
                                              "Organo", "Formante", "Hueca", "Sierra brillante" };
        return names[frame];
    }

private:
    // Amplitud del armonico h (>= 1) en la trama dada (fases = senos)
    static float amp (int frame, int h)
    {
        const float hf = (float) h;
        switch (frame)
        {
            case 0:  return h == 1 ? 1.0f : 0.0f;
            case 1:  return (h & 1) ? ((((h - 1) / 2) & 1) ? -1.0f : 1.0f) / (hf * hf) : 0.0f;
            case 2:  return 1.0f / hf;
            case 3:  return (h & 1) ? 1.0f / hf : 0.0f;
            case 4:
                switch (h)
                {
                    case 1: return 1.0f;  case 2: return 0.8f;  case 3: return 0.6f;
                    case 4: return 0.5f;  case 6: return 0.35f; case 8: return 0.25f;
                    default: return 0.0f;
                }
            case 5:
            {
                const float x = (std::log2 (hf) - std::log2 (9.0f)) / 0.5f;
                return (1.0f / hf) * (0.2f + 2.0f * std::exp (-0.5f * x * x));
            }
            case 6:  return (h & 1) ? 1.0f / std::pow (hf, 0.75f) : 0.0f;
            default: return 1.0f / std::sqrt (hf);
        }
    }

    void build()
    {
        data.assign ((size_t) kFrames * kMips * (kSize + 1), 0.0f);

        std::vector<float> sinT (kSize);
        for (int i = 0; i < kSize; ++i)
            sinT[(size_t) i] = std::sin (kTwoPi * (float) i / (float) kSize);

        for (int f = 0; f < kFrames; ++f)
        {
            float scale = 1.0f;
            for (int m = 0; m < kMips; ++m)
            {
                float* t = data.data() + ((size_t) f * kMips + (size_t) m) * (kSize + 1);
                const int maxH = std::min (1023, 1024 >> m);

                for (int h = 1; h <= maxH; ++h)
                {
                    const float a = amp (f, h);
                    if (a == 0.0f) continue;
                    for (int i = 0; i < kSize; ++i)
                        t[i] += a * sinT[(size_t) ((h * i) & (kSize - 1))];
                }

                if (m == 0)   // la normalizacion de la trama se calcula con su mip mas completo
                {
                    float peak = 0.0f;
                    for (int i = 0; i < kSize; ++i) peak = std::max (peak, std::fabs (t[i]));
                    scale = peak > 0.0f ? 1.0f / peak : 1.0f;
                }

                for (int i = 0; i < kSize; ++i) t[i] *= scale;
                t[kSize] = t[0];
            }
        }
    }

    std::vector<float> data;
};
} // namespace synth
