#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "CpuMeter.h"
#include "DelayModule.h"
#include "EffectModule.h"

// ============================================================================
//  EffectChain
//  La cadena de efectos global: 9 posiciones cuyo ORDEN puede cambiar el usuario.
//
//    - El orden en que se procesan los modulos es exactamente el orden guardado en 'order'.
//      La interfaz lee ese mismo orden: lo que se ve es lo que suena.
//    - El orden se guarda como un unico valor atomico de 64 bits: el hilo de audio lo lee sin
//      bloqueos y nunca ve un orden a medias.
//    - Cambio de orden sin clics: la salida baja a cero en 5 ms con el orden antiguo, se cambia el
//      orden y sube en otros 5 ms con el nuevo. Los modulos conservan su estado interno.
//    - Los modulos aun no implementados (nullptr) se saltan: la senal pasa sin tocarse.
//    - Un modulo apagado se limita a volver enseguida (cada uno gestiona su propio ON/OFF).
//    - Mide el CPU de cada modulo (CpuMeter) para mostrarlo en la interfaz.
//    - process() no reserva memoria ni usa bloqueos.
// ============================================================================
class EffectChain
{
public:
    explicit EffectChain (juce::AudioProcessorValueTreeState& apvts)
    {
        modules[(size_t) EffectId::Delay] = std::make_unique<DelayModule> (apvts);
        orderPacked.store (EffectOrderUtil::pack (EffectOrderUtil::getDefault()));
    }

    // Registra un modulo en su posicion de identificador (para efectos nuevos y para pruebas).
    // Llamar antes de prepare().
    void setModule (EffectId id, std::unique_ptr<IEffect> module)
    {
        modules[(size_t) id] = std::move (module);
    }

    bool isImplemented (EffectId id) const noexcept { return modules[(size_t) id] != nullptr; }

    // --- Hilo de mensajes / prepareToPlay -------------------------------------------------

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        rampSamples = std::max (1, (int) std::lround (0.005 * sampleRate));

        for (auto& m : modules)
            if (m != nullptr)
                m->prepare (sampleRate, maxBlockSize);

        activeOrder = getOrder();
        pendingOrder = activeOrder;
        phase = Phase::Stable;
        counter = 0;

        for (auto& meter : moduleMeters)
            meter.reset();
        totalMeter.reset();
    }

    void reset()
    {
        for (auto& m : modules)
            if (m != nullptr)
                m->reset();

        phase = Phase::Stable;
        counter = 0;
        activeOrder = getOrder();
        pendingOrder = activeOrder;
    }

    // Cambia el orden de la cadena (se ignora si no es una permutacion valida). Seguro desde cualquier hilo.
    void setOrder (const EffectOrder& order) noexcept
    {
        if (EffectOrderUtil::isValid (order))
            orderPacked.store (EffectOrderUtil::pack (order), std::memory_order_release);
    }

    void resetOrder() noexcept { setOrder (EffectOrderUtil::getDefault()); }

    EffectOrder getOrder() const noexcept
    {
        const auto o = EffectOrderUtil::unpack (orderPacked.load (std::memory_order_acquire));
        return EffectOrderUtil::isValid (o) ? o : EffectOrderUtil::getDefault();
    }

    // --- Medidores de CPU (solo lectura, para la interfaz) ---------------------------------
    const CpuMeter& getModuleMeter (EffectId id) const noexcept { return moduleMeters[(size_t) id]; }
    const CpuMeter& getTotalMeter() const noexcept { return totalMeter; }

    // --- Hilo de audio ----------------------------------------------------------------------
    void process (juce::AudioBuffer<float>& buffer, double bpm) noexcept
    {
        const int total = buffer.getNumSamples();
        if (total <= 0 || buffer.getNumChannels() < 2)
            return;

        // Cambio de orden pedido desde fuera
        const EffectOrder requested = getOrder();
        if (phase == Phase::Stable)
        {
            if (requested != activeOrder)
            {
                pendingOrder = requested;
                phase = Phase::FadeOut;
                counter = rampSamples;
            }
        }
        else
        {
            pendingOrder = requested;
        }

        float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

        int pos = 0;
        while (pos < total)
        {
            int len = total - pos;

            if (phase != Phase::Stable)
                len = std::min (len, counter);

            processSegment (channels, pos, len, bpm);

            // Rampa de bajada (con el orden antiguo) o de subida (con el nuevo)
            if (phase == Phase::FadeOut)
            {
                for (int i = 0; i < len; ++i)
                {
                    const float g = (float) (counter - i) / (float) rampSamples;
                    channels[0][pos + i] *= g;
                    channels[1][pos + i] *= g;
                }
            }
            else if (phase == Phase::FadeIn)
            {
                for (int i = 0; i < len; ++i)
                {
                    const float g = (float) (rampSamples - counter + i) / (float) rampSamples;
                    channels[0][pos + i] *= g;
                    channels[1][pos + i] *= g;
                }
            }

            pos += len;

            if (phase == Phase::FadeOut)
            {
                counter -= len;
                if (counter <= 0)
                {
                    activeOrder = pendingOrder;   // aqui cambia el orden de proceso
                    phase = Phase::FadeIn;
                    counter = rampSamples;
                }
            }
            else if (phase == Phase::FadeIn)
            {
                counter -= len;
                if (counter <= 0)
                {
                    phase = Phase::Stable;
                    counter = 0;
                }
            }
        }

        // Publica el CPU de este bloque
        for (auto& meter : moduleMeters)
            meter.publish (total, sampleRate);
        totalMeter.publish (total, sampleRate);
    }

private:
    enum class Phase { Stable, FadeOut, FadeIn };

    void processSegment (float* const* channels, int start, int len, double bpm) noexcept
    {
        if (len <= 0)
            return;

        juce::AudioBuffer<float> view (channels, 2, start, len);   // vista sin copia ni reserva

        for (size_t i = 0; i < activeOrder.size(); ++i)
        {
            const size_t id = activeOrder[i];
            IEffect* module = modules[id].get();
            if (module == nullptr)
                continue;

            const auto t0 = juce::Time::getHighResolutionTicks();
            module->process (view, bpm);
            const auto ticks = juce::Time::getHighResolutionTicks() - t0;

            moduleMeters[id].addTicks (ticks);
            totalMeter.addTicks (ticks);
        }
    }

    std::array<std::unique_ptr<IEffect>, (size_t) kNumEffects> modules;
    std::array<CpuMeter, (size_t) kNumEffects> moduleMeters;
    CpuMeter totalMeter;

    std::atomic<std::uint64_t> orderPacked { 0 };

    double sampleRate = 44100.0;
    int rampSamples = 220;

    // Estado del hilo de audio
    EffectOrder activeOrder = EffectOrderUtil::getDefault();
    EffectOrder pendingOrder = EffectOrderUtil::getDefault();
    Phase phase = Phase::Stable;
    int counter = 0;
};
