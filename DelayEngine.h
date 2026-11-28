#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

#include "DelayParameters.h"
#include "DelayTimeCalculator.h"
#include "IDelayAlgorithm.h"
#include "SimpleDelay.h"
#include "PingPongDelay.h"
#include "TapeDelay.h"
#include "FilterDelay.h"
#include "ReverseDelay.h"
#include "GranularDelay.h"

// ============================================================================
//  DelayEngine
//  Gestor principal del modulo Delay. Es independiente del resto del plugin: recibe el buffer
//  de audio estereo y el BPM del host, y lo procesa "in place". En el PluginProcessor se podra
//  encadenar otro efecto (p. ej. la Reverb) justo detras.
//
//  Cada bloque:
//    1. Lee los parametros del APVTS y les suma los DelayModOffsets (clamp al rango del parametro).
//    2. Resuelve los tiempos L/R (Free o Sync con el BPM, modificadores, Link).
//    3. Procesa el algoritmo activo (y el anterior, mientras dure el crossfade de 20 ms).
//    4. Mezcla seco/humedo con Mix y con el interruptor Delay On (rampas de 20 ms).
//
//  Notas de uso:
//    - prepare() reserva TODA la memoria (6 algoritmos + buffers de trabajo): fuera del audio thread.
//    - process() no asigna memoria, no usa locks y puede recibir bloques de cualquier tamano.
//    - Los algoritmos que no suenan no gastan CPU: al activarse empiezan siempre desde silencio.
//    - Con Delay On apagado el motor no procesa nada (0 % CPU) tras una rampa de 20 ms.
//    - Dormir en silencio: si no entra audio (< -90 dB) y la salida humeda tambien esta en silencio
//      durante mas que el bucle mas largo del modo (1 s + 2.5 x el tiempo; 1 s + 6 x en Reverse y Granular;
//      maximo 45 s), el motor vacia el algoritmo y deja de procesar hasta que vuelva a entrar sonido. Es
//      inaudible: lo que se descarta esta por debajo de -90 dB. El CPU en reposo cae a casi cero.
//      Al despertar, los moduladores internos (LFO de Tape, reparto de granos) empiezan de cero.
//    - Con MIX a 0 (y ya estabilizado) tampoco se procesa: el algoritmo se vacia, asi que al subir MIX
//      de nuevo los ecos empiezan desde ese momento (no recuerdan el audio anterior).
//    - Mientras dura un crossfade, un nuevo cambio de modo espera a que termine el anterior.
// ============================================================================
class DelayEngine
{
public:
    explicit DelayEngine (juce::AudioProcessorValueTreeState& apvtsToUse)
    {
        auto get = [&apvtsToUse] (const char* id)
        {
            auto* p = apvtsToUse.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        };

        pOn       = get (DelayParams::kOn);
        pMode     = get (DelayParams::kMode);
        pLink     = get (DelayParams::kLink);
        pSyncL    = get (DelayParams::kSyncL);
        pSyncR    = get (DelayParams::kSyncR);
        pDivL     = get (DelayParams::kDivL);
        pDivR     = get (DelayParams::kDivR);
        pModifierL = get (DelayParams::kModifierL);
        pModifierR = get (DelayParams::kModifierR);
        pMonoSum  = get (DelayParams::kPingPongMonoSum);
        pFilterMode = get (DelayParams::kFilterMode);
        pOctaveUp = get (DelayParams::kReverseOctave);

        for (int i = 0; i < kNumDelayModTargets; ++i)
            pFloat[(size_t) i] = get (DelayParams::kDelayFloatParams[i].id);

        algorithms[(size_t) DelayMode::Simple]   = std::make_unique<SimpleDelay>();
        algorithms[(size_t) DelayMode::PingPong] = std::make_unique<PingPongDelay>();
        algorithms[(size_t) DelayMode::Tape]     = std::make_unique<TapeDelay>();
        algorithms[(size_t) DelayMode::Filter]   = std::make_unique<FilterDelay>();
        algorithms[(size_t) DelayMode::Reverse]  = std::make_unique<ReverseDelay>();
        algorithms[(size_t) DelayMode::Granular] = std::make_unique<GranularDelay>();
    }

    // Hooks para la matriz de modulacion: se escriben desde fuera, se leen en cada bloque.
    DelayModOffsets modOffsets;

    // Reserva toda la memoria. Llamar desde prepareToPlay().
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlock = std::max (maxBlockSize, 1);

        for (auto& a : algorithms)
            a->prepare (sampleRate, maxBlock);

        for (auto* v : { &dryL, &dryR, &wetCurL, &wetCurR, &wetPrevL, &wetPrevR })
            v->assign ((size_t) maxBlock, 0.0f);

        fadeStep = 1.0f / (float) std::max (1, (int) std::lround (0.02 * sampleRate));
        enableSm.reset (sampleRate, 0.02);
        mixSm.reset (sampleRate, 0.02);

        reset();
    }

    void reset()
    {
        for (auto& a : algorithms)
            a->reset();

        fading = false;
        fadePosition = 0.0f;
        active = false;
        asleep = false;
        mixIdle = false;
        silentSamples = 0;
        activeMode = std::clamp ((int) pMode->load(), 0, kNumDelayModes - 1);
        previousMode = activeMode;

        const bool on = pOn->load() > 0.5f;
        enableSm.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
        mixSm.setCurrentAndTargetValue (std::clamp (pFloat[(size_t) DelayModTarget::Mix]->load(), 0.0f, 1.0f));
    }

    int getCurrentMode() const noexcept { return activeMode; }
    bool isCrossfading() const noexcept { return fading; }

    // True mientras el motor esta dormido (sin audio ni cola) o con MIX a 0: no gasta CPU.
    bool isIdle() const noexcept { return asleep || mixIdle; }

    // Solo para diagnostico y pruebas: desactiva el "dormir en silencio" (por defecto activado).
    void setIdleSleepEnabled (bool shouldBeEnabled) noexcept { sleepEnabled = shouldBeEnabled; }

    // Procesa un buffer estereo in place. bpm = tempo del host (<= 0 si no hay).
    void process (juce::AudioBuffer<float>& buffer, double bpm) noexcept
    {
        if (buffer.getNumChannels() < 2 || dryL.empty())
            return;

        juce::ScopedNoDenormals noDenormals;

        float* left  = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);
        const int total = buffer.getNumSamples();

        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (maxBlock, total - pos);
            processSubBlock (left + pos, right + pos, n, bpm);
            pos += n;
        }
    }

private:
    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    // Valor base del APVTS + desplazamiento de modulacion, limitado a su rango
    float value (DelayModTarget t) const noexcept
    {
        return modOffsets.apply (t, pFloat[(size_t) t]->load());
    }

    DelayAlgorithmParams readParameters (double bpm) const noexcept
    {
        DelayAlgorithmParams p;

        // Tiempos: Free / Sync, modificadores y Link
        DelayChannelTime l, r;
        l.freeMs   = value (DelayModTarget::TimeL);
        r.freeMs   = value (DelayModTarget::TimeR);
        l.sync     = pSyncL->load() > 0.5f;
        r.sync     = pSyncR->load() > 0.5f;
        l.division = (int) pDivL->load();
        r.division = (int) pDivR->load();
        l.modifier = (int) pModifierL->load();
        r.modifier = (int) pModifierR->load();

        const auto times = DelayTimeCalculator::calculate (l, r, pLink->load() > 0.5f, bpm);
        p.timeMsL  = times.leftMs;
        p.timeMsR  = times.rightMs;
        p.feedback = value (DelayModTarget::Feedback);

        p.simpleStereoOffsetMs = value (DelayModTarget::SimpleStereoOffset);
        p.simpleWidth          = value (DelayModTarget::SimpleWidth);
        p.simpleHighCutHz      = value (DelayModTarget::SimpleHighCut);
        p.simpleLowCutHz       = value (DelayModTarget::SimpleLowCut);

        p.pingPongCross        = value (DelayModTarget::PingPongCrossFeedback);
        p.pingPongSpread       = value (DelayModTarget::PingPongPanSpread);
        p.pingPongMonoSum      = pMonoSum->load() > 0.5f;

        p.tapeDrive            = value (DelayModTarget::TapeDrive);
        p.tapeWowFlutter       = value (DelayModTarget::TapeWowFlutter);
        p.tapeAge              = value (DelayModTarget::TapeAge);

        p.filterCutoffHz       = value (DelayModTarget::FilterCutoff);
        p.filterResonance      = value (DelayModTarget::FilterResonance);
        p.filterMode           = (int) pFilterMode->load();
        p.filterLfoDepth       = value (DelayModTarget::FilterLfoDepth);

        p.reverseSmoothness    = value (DelayModTarget::ReverseCrossfadeSmoothness);
        p.reverseMix           = value (DelayModTarget::ReverseForwardReverseMix);
        p.reverseOctaveUp      = pOctaveUp->load() > 0.5f;

        p.grainSizeMs          = value (DelayModTarget::GranularGrainSize);
        p.grainDensity         = value (DelayModTarget::GranularDensity);
        p.grainPitchSemitones  = value (DelayModTarget::GranularPitchShift);
        p.grainSpray           = value (DelayModTarget::GranularSpray);

        return p;
    }

    void processSubBlock (float* left, float* right, int n, double bpm) noexcept
    {
        const bool on = pOn->load() > 0.5f;
        enableSm.setTargetValue (on ? 1.0f : 0.0f);

        // Apagado del todo: no se procesa nada (y se vacian los buffers una sola vez)
        if (! on && ! enableSm.isSmoothing() && enableSm.getCurrentValue() <= 0.0f)
        {
            if (active)
            {
                for (auto& a : algorithms)
                    a->reset();
                fading = false;
                active = false;
            }

            asleep = false;
            awake = false;
            mixIdle = false;
            silentSamples = 0;

            activeMode = std::clamp ((int) pMode->load(), 0, kNumDelayModes - 1);
            return;
        }

        const int targetMode = std::clamp ((int) pMode->load(), 0, kNumDelayModes - 1);

        if (! active)
        {
            activeMode = targetMode;
            algorithms[(size_t) activeMode]->reset();
            fading = false;
            active = true;
        }
        else if (! fading && targetMode != activeMode)
        {
            // Empieza el crossfade de 20 ms: el modo nuevo arranca desde silencio
            previousMode = activeMode;
            activeMode = targetMode;
            algorithms[(size_t) activeMode]->reset();
            fading = true;
            fadePosition = 0.0f;
        }

        const float mixValue = std::clamp (value (DelayModTarget::Mix), 0.0f, 1.0f);
        mixSm.setTargetValue (mixValue);

        // Nivel de entrada de este bloque
        float inputPeak = 0.0f;
        for (int i = 0; i < n; ++i)
            inputPeak = std::max (inputPeak, std::max (std::abs (left[i]), std::abs (right[i])));

        // MIX a 0 ya estabilizado: no hay nada que oir del delay -> no se procesa
        const bool mixOff = mixValue < 0.0001f && ! mixSm.isSmoothing() && mixSm.getCurrentValue() < 0.0001f;
        mixIdle = mixOff;

        if (mixOff)
        {
            if (awake)
            {
                for (auto& a : algorithms)
                    a->reset();
                fading = false;
                awake = false;
            }

            activeMode = targetMode;
            silentSamples = 0;
            return;     // la senal seca queda intacta
        }

        // Dormido: sin entrada nueva no hay nada que procesar; el seco solo recibe la parte de (1 - mix)
        if (asleep)
        {
            if (inputPeak < kSilence)
            {
                activeMode = targetMode;

                for (int i = 0; i < n; ++i)
                {
                    const float amount = mixSm.getNextValue() * enableSm.getNextValue();
                    left[i]  *= (1.0f - amount);
                    right[i] *= (1.0f - amount);
                }
                return;
            }

            // Vuelve el sonido: se despierta desde el estado limpio dejado al dormirse
            asleep = false;
            silentSamples = 0;
            activeMode = targetMode;
            algorithms[(size_t) activeMode]->reset();
        }

        awake = true;

        const auto params = readParameters (bpm);

        algorithms[(size_t) activeMode]->setParameters (params);
        if (fading)
            algorithms[(size_t) previousMode]->setParameters (params);

        std::copy (left, left + n, dryL.begin());
        std::copy (right, right + n, dryR.begin());

        algorithms[(size_t) activeMode]->process (dryL.data(), dryR.data(), wetCurL.data(), wetCurR.data(), n);
        if (fading)
            algorithms[(size_t) previousMode]->process (dryL.data(), dryR.data(), wetPrevL.data(), wetPrevR.data(), n);

        bool fadeFinished = false;
        float wetPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float wl = wetCurL[(size_t) i];
            float wr = wetCurR[(size_t) i];
            wetPeak = std::max (wetPeak, std::max (std::abs (wl), std::abs (wr)));

            if (fading)
            {
                // Crossfade de potencia constante entre el modo anterior y el nuevo
                const float gNew = std::sin (1.5707963f * fadePosition);
                const float gOld = std::cos (1.5707963f * fadePosition);
                wl = gNew * wl + gOld * wetPrevL[(size_t) i];
                wr = gNew * wr + gOld * wetPrevR[(size_t) i];

                fadePosition += fadeStep;
                if (fadePosition >= 1.0f)
                {
                    fading = false;
                    fadeFinished = true;
                }
            }

            const float amount = mixSm.getNextValue() * enableSm.getNextValue();
            left[i]  = dryL[(size_t) i] + (wl - dryL[(size_t) i]) * amount;
            right[i] = dryR[(size_t) i] + (wr - dryR[(size_t) i]) * amount;
        }

        // El modo anterior ya no suena: se vacia para que no guarde restos
        if (fadeFinished)
            algorithms[(size_t) previousMode]->reset();

        // Dormir en silencio: entrada y salida humeda por debajo de -100 dB durante mas que el bucle mas largo
        if (sleepEnabled && ! fading && inputPeak < kSilence && wetPeak < kSilence)
        {
            silentSamples += n;

            // Reverse y Granular leen material de hasta ~5 veces el tiempo; los demas solo recirculan cada T
            const double longestSeconds = (double) std::max (params.timeMsL, params.timeMsR) * 0.001;
            const double loopFactor = (activeMode == (int) DelayMode::Reverse || activeMode == (int) DelayMode::Granular) ? 6.0 : 2.5;
            const double windowSeconds = std::min (45.0, 1.0 + loopFactor * longestSeconds);

            if ((double) silentSamples >= windowSeconds * sampleRate)
            {
                for (auto& a : algorithms)
                    a->reset();

                asleep = true;
                awake = false;
                silentSamples = 0;
            }
        }
        else
        {
            silentSamples = 0;
        }
    }

    // Parametros del APVTS (punteros atomicos, validos durante toda la vida del procesador)
    std::atomic<float>* pOn = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pSyncL = nullptr;
    std::atomic<float>* pSyncR = nullptr;
    std::atomic<float>* pDivL = nullptr;
    std::atomic<float>* pDivR = nullptr;
    std::atomic<float>* pModifierL = nullptr;
    std::atomic<float>* pModifierR = nullptr;
    std::atomic<float>* pMonoSum = nullptr;
    std::atomic<float>* pFilterMode = nullptr;
    std::atomic<float>* pOctaveUp = nullptr;
    std::array<std::atomic<float>*, (size_t) kNumDelayModTargets> pFloat {};

    std::array<std::unique_ptr<IDelayAlgorithm>, (size_t) kNumDelayModes> algorithms;

    // Memoria de trabajo (se reserva en prepare)
    std::vector<float> dryL, dryR, wetCurL, wetCurR, wetPrevL, wetPrevR;

    double sampleRate = 44100.0;
    int maxBlock = 512;

    Smoothed enableSm, mixSm;
    static constexpr float kSilence = 3.0e-5f;   // -90 dBFS (por debajo del ruido de 16 bits)

    bool active = false;
    bool asleep = false;
    bool awake = false;          // el algoritmo activo tiene estado (se ha procesado desde el ultimo reset)
    bool mixIdle = false;
    bool sleepEnabled = true;
    std::int64_t silentSamples = 0;
    bool fading = false;
    float fadePosition = 0.0f;
    float fadeStep = 0.001f;
    int activeMode = 0, previousMode = 0;
};
