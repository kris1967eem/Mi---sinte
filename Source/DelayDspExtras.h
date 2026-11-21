#pragma once

#include <algorithm>
#include <cmath>

// ============================================================================
//  Utilidades DSP adicionales del modulo Delay (Bloque 3)
// ============================================================================
namespace DelayDsp
{
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
