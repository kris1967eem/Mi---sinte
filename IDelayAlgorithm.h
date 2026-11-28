#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "CircularDelayBuffer.h"
#include "DelayParameters.h"
#include "DelayTimeCalculator.h"

// ============================================================================
//  DelayAlgorithmParams
//  Todos los parametros del delay, YA RESUELTOS para un bloque: el DelayEngine (Bloque 3)
//  lee el APVTS, suma los DelayModOffsets, resuelve los tiempos Free/Sync/Link con
//  DelayTimeCalculator y rellena esta estructura una vez por bloque. Cada algoritmo solo
//  lee los campos que le interesan. Los valores por defecto coinciden con los del APVTS.
// ============================================================================
struct DelayAlgorithmParams
{
    // ---- Globales (tiempos en ms, ya resueltos) -----------------------------
    float timeMsL  = 300.0f;
    float timeMsR  = 300.0f;
    float feedback = 0.35f;            // 0 .. 0.95

    // ---- Simple -------------------------------------------------------------
    float simpleStereoOffsetMs = 0.0f; // se suma al tiempo R (-50 .. 50 ms)
    float simpleWidth          = 1.0f; // 0 = mono, 1 = original, 2 = ancho
    float simpleHighCutHz      = 20000.0f;
    float simpleLowCutHz       = 20.0f;

    // ---- Ping-Pong ----------------------------------------------------------
    float pingPongCross        = 1.0f; // 0 = cada linea a si misma, 1 = ping-pong completo
    float pingPongSpread       = 1.0f; // 0 = centro, 1 = L y R duros
    bool  pingPongMonoSum      = false;

    // ---- Tape ---------------------------------------------------------------
    float tapeDrive            = 0.3f;
    float tapeWowFlutter       = 0.2f;
    float tapeAge              = 0.3f;

    // ---- Filter (Bloque 3) --------------------------------------------------
    float filterCutoffHz       = 2000.0f;
    float filterResonance      = 0.3f;
    int   filterMode           = 0;    // 0 = LP, 1 = BP, 2 = HP
    float filterLfoDepth       = 0.0f;

    // ---- Reverse (Bloque 3) -------------------------------------------------
    float reverseSmoothness    = 0.5f;
    float reverseMix           = 1.0f; // 0 = solo directo, 1 = solo inverso
    bool  reverseOctaveUp      = false;

    // ---- Granular (Bloque 3) ------------------------------------------------
    float grainSizeMs          = 100.0f;
    float grainDensity         = 20.0f;   // granos por segundo
    float grainPitchSemitones  = 0.0f;
    float grainSpray           = 0.2f;
};

// ============================================================================
//  IDelayAlgorithm
//  Interfaz comun de los 6 motores. Cada motor produce SOLO la senal humeda (100 % wet) y
//  lleva su propia realimentacion. El mix seco/humedo y el crossfade entre modos los hace el
//  DelayEngine.
//
//  Reglas:
//    - prepare() reserva toda la memoria. Se llama fuera del audio thread.
//    - setParameters() y process() se llaman desde el audio thread: sin asignaciones, sin locks.
//    - Las entradas y las salidas no deben solaparse (buffers distintos).
//    - El motor compensa su propia latencia interna: el eco sale exactamente 'tiempo' despues
//      de la entrada, asi que getLatencySamples() es 0 en los tres primeros motores.
// ============================================================================
class IDelayAlgorithm
{
public:
    virtual ~IDelayAlgorithm() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setParameters (const DelayAlgorithmParams& params) = 0;
    virtual void process (const float* inL, const float* inR,
                          float* wetL, float* wetR, int numSamples) noexcept = 0;

    virtual int getLatencySamples() const noexcept { return 0; }
    virtual const char* getName() const noexcept = 0;
};

// ============================================================================
//  Utilidades DSP comunes (solo cabecera)
// ============================================================================
namespace DelayDsp
{
    inline constexpr float kMaxFeedback = 0.95f;
    inline constexpr float kPi = 3.14159265358979f;

    // Retardo maximo que hay que poder guardar: 5000 ms + 50 ms de offset + 150 ms de margen
    inline int getMaxDelaySamples (double sampleRate) noexcept
    {
        return (int) std::ceil (sampleRate * 5.2);
    }

    // Filtro de un polo TPT (zero-delay feedback). processLow() devuelve el paso bajo;
    // el paso alto es x - processLow(x).
    struct TptOnePole
    {
        void reset() noexcept { s = 0.0f; }

        void setCutoff (float hz, double sampleRate) noexcept
        {
            const double fc = std::clamp ((double) hz, 1.0, 0.49 * sampleRate);
            const double g = std::tan (3.14159265358979 * fc / sampleRate);
            G = (float) (g / (1.0 + g));
        }

        float processLow (float x) noexcept
        {
            const float v = (x - s) * G;
            const float lp = v + s;
            s = lp + v;
            return lp;
        }

        float G = 0.5f;
        float s = 0.0f;
    };

    // Dos polos TPT en cascada: 12 dB/octava, paso bajo o paso alto
    struct TwoStageFilter
    {
        void reset() noexcept { a.reset(); b.reset(); }

        void setCutoff (float hz, double sampleRate) noexcept
        {
            a.setCutoff (hz, sampleRate);
            b.G = a.G;
        }

        float lowpass (float x) noexcept
        {
            return b.processLow (a.processLow (x));
        }

        float highpass (float x) noexcept
        {
            const float h1 = x - a.processLow (x);
            return h1 - b.processLow (h1);
        }

        TptOnePole a, b;
    };

    // Ruido blanco barato (xorshift32), salida en [-1, 1)
    struct NoiseGenerator
    {
        explicit NoiseGenerator (std::uint32_t seed = 0x1234abcdu) noexcept : state (seed) {}

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
        }

        std::uint32_t state;
    };
}
