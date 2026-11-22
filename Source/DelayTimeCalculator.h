#pragma once

#include <algorithm>
#include <cmath>

// ============================================================================
//  DelayTimeCalculator
//  Convierte los ajustes de tiempo de cada canal (L / R) en milisegundos:
//    - Modo FREE: 1 ms a 5000 ms (la curva logaritmica la da el rango del parametro).
//    - Modo SYNC: division musical segun el BPM del DAW.
//    - Modificador ritmico: Normal (x1), Dotted (x1.5), Triplet (x2/3).
//    - Link: el canal R usa exactamente los mismos ajustes que el L.
//  Solo cabecera (inline), sin asignaciones de memoria: se puede llamar desde el audio thread.
// ============================================================================

struct DelayChannelTime
{
    float freeMs   = 300.0f;   // tiempo en modo FREE (ms)
    bool  sync     = false;    // true = tiempo sincronizado al BPM
    int   division = 3;        // indice en la tabla de divisiones (3 = 1/4)
    int   modifier = 0;        // 0 = Normal, 1 = Dotted, 2 = Triplet
};

struct DelayTimes
{
    float leftMs  = 0.0f;
    float rightMs = 0.0f;
};

class DelayTimeCalculator
{
public:
    static constexpr float  kMinMs         = 1.0f;
    static constexpr float  kMaxMs         = 5000.0f;
    static constexpr int    kNumDivisions  = 8;
    static constexpr int    kNumModifiers  = 3;
    static constexpr double kFallbackBpm   = 120.0;   // si el host no da tempo

    // Duracion de cada division en negras (beats):
    // 1/1, 1/2, 3/8, 1/4, 3/16, 1/8, 1/16, 1/32
    static double getDivisionBeats (int division) noexcept
    {
        static constexpr double kBeats[kNumDivisions] = { 4.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.25, 0.125 };
        return kBeats[std::clamp (division, 0, kNumDivisions - 1)];
    }

    // Normal (x1.0), Dotted / Puntillo (x1.5), Triplet / Tresillo (x2/3)
    static double getModifierFactor (int modifier) noexcept
    {
        static constexpr double kFactors[kNumModifiers] = { 1.0, 1.5, 2.0 / 3.0 };
        return kFactors[std::clamp (modifier, 0, kNumModifiers - 1)];
    }

    static float clampMs (float ms) noexcept
    {
        return std::clamp (ms, kMinMs, kMaxMs);
    }

    // Tiempo sincronizado en ms (limitado a 1..5000 ms: p. ej. 1/1 a 40 BPM se queda en 5000 ms)
    static float syncedMs (int division, int modifier, double bpm) noexcept
    {
        const double safeBpm = (std::isfinite (bpm) && bpm >= 1.0) ? bpm : kFallbackBpm;
        const double ms = getDivisionBeats (division) * (60000.0 / safeBpm) * getModifierFactor (modifier);
        return clampMs ((float) ms);
    }

    static float channelMs (const DelayChannelTime& c, double bpm) noexcept
    {
        return c.sync ? syncedMs (c.division, c.modifier, bpm) : clampMs (c.freeMs);
    }

    // Calcula L y R. Con link activo, R copia por completo los ajustes de L.
    static DelayTimes calculate (const DelayChannelTime& left, const DelayChannelTime& right,
                                 bool link, double bpm) noexcept
    {
        DelayTimes t;
        t.leftMs  = channelMs (left, bpm);
        t.rightMs = link ? t.leftMs : channelMs (right, bpm);
        return t;
    }

    static float msToSamples (float ms, double sampleRate) noexcept
    {
        return (float) ((double) ms * 0.001 * sampleRate);
    }
};
