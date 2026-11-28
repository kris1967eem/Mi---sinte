#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ============================================================================
//  CircularDelayBuffer
//  Buffer circular mono para lineas de retardo. Toda la memoria se reserva en prepare()
//  (que NO se debe llamar desde el audio thread); write() y todas las lecturas son
//  noexcept y no asignan memoria.
//
//  Convencion de retardo (muy importante):
//    - Se LEE antes de ESCRIBIR la muestra actual x[n].
//    - read(D) devuelve x[n - D], es decir, un retardo de D muestras.
//    - El retardo minimo admitido es kMinDelay (2 muestras): los interpoladores necesitan
//      una muestra "futura" ya escrita. Los retardos se limitan solos al rango valido.
//
//  Lecturas disponibles:
//    - readInteger   : retardo entero.
//    - readLinear    : interpolacion lineal.
//    - readLagrange  : interpolacion cubica de Lagrange (4 puntos). Con el retardo cambiando
//                      muestra a muestra produce el "repitch" clasico de cinta.
//    - readThiran    : all-pass de Thiran de 1er orden (con estado). Respuesta plana en amplitud;
//                      ideal para retardos fijos o que cambian con crossfade (DualTapThiranReader).
//    - readAbsolute  : lectura por posicion absoluta del flujo (para cabezales libres,
//                      p. ej. lectura inversa o granos). Ver ReverseReadHead.
// ============================================================================
class CircularDelayBuffer
{
public:
    static constexpr int   kGuardSamples = 4;      // margen de seguridad para los interpoladores
    static constexpr float kMinDelay     = 2.0f;   // retardo minimo legible (muestras)

    // Reserva la memoria (potencia de 2). maxDelaySamples = retardo maximo que se va a leer.
    void prepare (int maxDelaySamples)
    {
        const int needed = std::max (maxDelaySamples, 8) + kGuardSamples + 1;
        int size = 16;
        while (size < needed)
            size <<= 1;

        data.assign ((size_t) size, 0.0f);
        mask = (std::uint64_t) size - 1u;
        capacity = size;
        writeIndex = 0;
    }

    // Borra el contenido (sin reasignar memoria).
    void reset() noexcept
    {
        std::fill (data.begin(), data.end(), 0.0f);
        writeIndex = 0;
    }

    // Escribe la muestra x[n] y avanza el cabezal de escritura.
    void write (float x) noexcept
    {
        data[(size_t) ((std::uint64_t) writeIndex & mask)] = x;
        ++writeIndex;
    }

    bool isPrepared() const noexcept          { return capacity > 0; }
    int  getCapacity() const noexcept         { return capacity; }
    float getMaxDelay() const noexcept        { return (float) (capacity - kGuardSamples); }

    // Indice absoluto de la PROXIMA muestra que se va a escribir (x[n]).
    std::int64_t getWriteIndex() const noexcept { return writeIndex; }

    // Limita un retardo al rango que se puede leer con seguridad.
    float clampDelay (float delaySamples) const noexcept
    {
        return std::clamp (delaySamples, kMinDelay, getMaxDelay());
    }

    // ---- Lecturas relativas al cabezal de escritura (x[n - D]) --------------

    float readInteger (int delaySamples) const noexcept
    {
        const int d = std::clamp (delaySamples, 1, capacity - 1);
        return at (d);
    }

    float readLinear (float delaySamples) const noexcept
    {
        const float d = clampDelay (delaySamples);
        const int   i = (int) d;
        const float f = d - (float) i;
        const float a = at (i);
        const float b = at (i + 1);
        return a + (b - a) * f;
    }

    // Lagrange cubico (4 puntos: retardos i-1, i, i+1, i+2 con i = floor(D)).
    float readLagrange (float delaySamples) const noexcept
    {
        const float d = clampDelay (delaySamples);
        const int   i = (int) d;                  // d >= 2: truncar equivale a floor
        const float f = d - (float) i;

        const float ym1 = at (i - 1);             // mas nueva
        const float y0  = at (i);
        const float y1  = at (i + 1);
        const float y2  = at (i + 2);             // mas antigua

        const float cm1 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
        const float c0  = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
        const float c1  = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
        const float c2  = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);

        return cm1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
    }

    // All-pass de Thiran de 1er orden. 'allpassState' es el estado y[n-1] de ESTE lector
    // (cada cabezal debe tener el suyo). El retardo total es N + d con d en [0.5, 1.5),
    // zona en la que el filtro es estable y sin transitorios.
    //   y[n] = a * (u[n] - y[n-1]) + u[n-1],   a = (1 - d) / (1 + d),   u[n] = x[n - N]
    float readThiran (float delaySamples, float& allpassState) const noexcept
    {
        const float dTotal = clampDelay (delaySamples);
        const int   n = (int) (dTotal - 0.5f);    // N >= 1
        const float d = dTotal - (float) n;       // [0.5, 1.5)
        const float a = (1.0f - d) / (1.0f + d);

        const float u0 = at (n);
        const float u1 = at (n + 1);
        const float y  = a * (u0 - allpassState) + u1;

        allpassState = y;
        return y;
    }

    // Estado inicial aproximado de un all-pass de Thiran que va a empezar a leer en 'delaySamples':
    // y[n-1] = x(n - 1 - D). Sirve para que el cabezal nuevo de un crossfade no meta un transitorio.
    float primeThiranState (float delaySamples) const noexcept
    {
        return readLagrange (delaySamples + 1.0f);
    }

    // ---- Lectura por posicion absoluta del flujo ----------------------------
    // Cada muestra escrita tiene un indice absoluto (0, 1, 2, ...). La proxima por escribir es
    // getWriteIndex(). Devuelve el valor (Lagrange cubico) en la posicion fraccionaria dada.
    // Si la posicion queda fuera de lo guardado, se limita al rango valido.
    float readAbsolute (double position) const noexcept
    {
        const double delay = (double) writeIndex - position;
        return readLagrange ((float) delay);
    }

    // Muestra con indice absoluto 'index' (debe estar ya escrita y dentro de lo guardado; sin comprobaciones).
    // La usan los lectores que ya han calculado sus propios coeficientes de interpolacion.
    float getSampleAbsolute (std::int64_t index) const noexcept
    {
        return data[(size_t) ((std::uint64_t) index & mask)];
    }

    // Retardo (en muestras) que corresponde a una posicion absoluta.
    float absoluteToDelay (double position) const noexcept
    {
        return (float) ((double) writeIndex - position);
    }

private:
    // x[n - d] para d entero (d >= 1 lee muestras ya escritas)
    float at (int d) const noexcept
    {
        return data[(size_t) (((std::uint64_t) (writeIndex - (std::int64_t) d)) & mask)];
    }

    std::vector<float> data;
    std::uint64_t mask = 0;
    std::int64_t  writeIndex = 0;
    int capacity = 0;
};

// ============================================================================
//  DualTapThiranReader
//  Lector de retardo SIN efecto Doppler al cambiar el tiempo (modo "Fade" del Simple Delay).
//  Dos cabezales (tap) con all-pass de Thiran propio. Si el tiempo objetivo salta mas de
//  'retargetThreshold' muestras, el cabezal inactivo se coloca en el nuevo retardo y se hace
//  un crossfade (coseno elevado) entre ambos. Los ajustes pequenos se aplican directamente.
//
//  IMPORTANTE: pasar el tiempo objetivo SIN suavizar (escalonado). Si se le diera una rampa
//  suave, el cabezal la seguiria muestra a muestra y se oiria el pitch-shift que se quiere evitar.
//  Llamar a process() una vez por muestra, ANTES de escribir la muestra actual en el buffer.
// ============================================================================
class DualTapThiranReader
{
public:
    void prepare (double sampleRate, float crossfadeMs = 25.0f) noexcept
    {
        const double samples = std::max (1.0, sampleRate * (double) crossfadeMs * 0.001);
        fadeIncrement = (float) (1.0 / samples);
    }

    void setRetargetThreshold (float samples) noexcept { retargetThreshold = std::max (0.0f, samples); }

    void reset (const CircularDelayBuffer& buffer, float initialDelaySamples) noexcept
    {
        const float d = buffer.clampDelay (initialDelaySamples);
        for (auto& t : taps)
        {
            t.delay = d;
            t.state = 0.0f;
        }
        active = 0;
        fading = false;
        fadePosition = 0.0f;
    }

    bool isFading() const noexcept { return fading; }

    float process (const CircularDelayBuffer& buffer, float targetDelaySamples) noexcept
    {
        const float target = buffer.clampDelay (targetDelaySamples);
        Tap& cur = taps[active];

        if (! fading)
        {
            if (std::abs (target - cur.delay) > retargetThreshold)
            {
                // Empieza un crossfade hacia el nuevo retardo
                Tap& nxt = taps[1 - active];
                nxt.delay = target;
                nxt.state = buffer.primeThiranState (target);
                fading = true;
                fadePosition = 0.0f;
            }
            else
            {
                cur.delay = target;   // ajuste fino: sin crossfade
            }
        }

        if (! fading)
            return buffer.readThiran (cur.delay, cur.state);

        Tap& nxt = taps[1 - active];
        const float a = buffer.readThiran (cur.delay, cur.state);
        const float b = buffer.readThiran (nxt.delay, nxt.state);

        // Coseno elevado: 0 -> 1
        const float w = 0.5f - 0.5f * std::cos (3.14159265358979f * fadePosition);
        const float out = a + (b - a) * w;

        fadePosition += fadeIncrement;
        if (fadePosition >= 1.0f)
        {
            active = 1 - active;
            fading = false;
            fadePosition = 0.0f;
        }

        return out;
    }

private:
    struct Tap
    {
        float delay = 2.0f;
        float state = 0.0f;
    };

    Tap taps[2];
    int active = 0;
    bool fading = false;
    float fadePosition = 0.0f;
    float fadeIncrement = 0.001f;
    float retargetThreshold = 1.0f;   // muestras
};

// ============================================================================
//  ReverseReadHead
//  Cabezal de lectura inversa: lee hacia atras en el flujo escrito. Cada muestra de salida
//  avanza 'step' muestras hacia el pasado (1.0 = velocidad normal, 2.0 = octava arriba).
//  La posicion es absoluta, asi que sigue siendo valida mientras el cabezal de escritura avanza.
//  Con step = 1 la "edad" del dato leido crece 2 muestras por muestra de salida, asi que un
//  segmento de L muestras necesita un buffer de al menos 2 * L muestras.
// ============================================================================
struct ReverseReadHead
{
    double position = 0.0;   // indice absoluto (fraccionario) de la proxima lectura

    // Coloca el cabezal 'offsetSamples' muestras por detras del cabezal de escritura
    // (minimo CircularDelayBuffer::kMinDelay).
    void start (const CircularDelayBuffer& buffer, double offsetSamples = (double) CircularDelayBuffer::kMinDelay) noexcept
    {
        position = (double) buffer.getWriteIndex() - std::max (offsetSamples, (double) CircularDelayBuffer::kMinDelay);
    }

    // Devuelve la muestra en la posicion actual y retrocede 'step' muestras.
    float read (const CircularDelayBuffer& buffer, double step = 1.0) noexcept
    {
        const float v = buffer.readAbsolute (position);
        position -= step;
        return v;
    }

    // Edad (retardo) actual del dato bajo el cabezal, en muestras.
    float getAgeSamples (const CircularDelayBuffer& buffer) const noexcept
    {
        return buffer.absoluteToDelay (position);
    }
};

// ============================================================================
//  Ventanas para cruces y granos (t entre 0 y 1)
// ============================================================================
namespace DelayWindows
{
    // Hann: dos ventanas desplazadas medio ciclo suman exactamente 1 (cruce sin bache de nivel)
    inline float hann (float t) noexcept
    {
        t = std::clamp (t, 0.0f, 1.0f);
        return 0.5f - 0.5f * std::cos (6.28318530717959f * t);
    }

    // Tukey: alpha = fraccion de la ventana que son flancos de coseno (0 = rectangular, 1 = Hann)
    inline float tukey (float t, float alpha) noexcept
    {
        t = std::clamp (t, 0.0f, 1.0f);
        alpha = std::clamp (alpha, 0.0f, 1.0f);

        if (alpha <= 0.0f)
            return 1.0f;

        const float edge = alpha * 0.5f;
        if (t < edge)
            return 0.5f - 0.5f * std::cos (3.14159265358979f * t / edge);
        if (t > 1.0f - edge)
            return 0.5f - 0.5f * std::cos (3.14159265358979f * (1.0f - t) / edge);
        return 1.0f;
    }

    // Gaussiana centrada en 0.5 (sigma como fraccion de la ventana; 0.2 es un buen valor),
    // restando el valor de los extremos para que empiece y acabe exactamente en 0.
    inline float gaussian (float t, float sigma) noexcept
    {
        t = std::clamp (t, 0.0f, 1.0f);
        sigma = std::max (sigma, 0.05f);

        const float edge = std::exp (-0.5f * (0.5f / sigma) * (0.5f / sigma));
        const float x = (t - 0.5f) / sigma;
        const float g = std::exp (-0.5f * x * x);
        return (g - edge) / (1.0f - edge);
    }
}
