#pragma once

#include <algorithm>
#include <cmath>

// ============================================================================
//  Utilidades DSP adicionales del modulo Delay (Bloque 3)
// ============================================================================
namespace DelayDsp
{
    // tanh por aproximacion racional (Lambert, orden 7): error < 1e-4 para |x| < 3, mucho mas barata que std::tanh.
    inline float fastTanh (float x) noexcept
    {
        if (x > 4.97f)
            return 1.0f;
        if (x < -4.97f)
            return -1.0f;

        const float x2 = x * x;
        const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return std::clamp (num / den, -1.0f, 1.0f);
    }

    // sin(2 pi t) para t en turnos (cualquier valor): parabola refinada, error < 0.002. Para LFOs de modulacion.
    inline float fastSinTurns (float t) noexcept
    {
        t -= std::floor (t + 0.5f);                  // [-0.5, 0.5)
        const float u = 2.0f * t;                     // [-1, 1)
        const float y = 4.0f * u * (1.0f - std::abs (u));
        return 0.775f * y + 0.225f * y * std::abs (y);
    }

    // Limitador suave para el camino de realimentacion: transparente por debajo de 0.8
    // y nunca supera 1.0. Es una red de seguridad: con ganancias de lazo <= 1 no actua.
    inline float softLimit (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;

        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f);
        return x < 0.0f ? -y : y;
    }
}
