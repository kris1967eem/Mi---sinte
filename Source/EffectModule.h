#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstdint>

// ============================================================================
//  Modulos de la cadena EFFECTS
//  Los 9 modulos, la interfaz comun IEffect y las utilidades del orden de la cadena.
// ============================================================================

// Identificadores estables (se guardan en los proyectos y en los presets de cadena: no reordenar).
enum class EffectId : int
{
    Distortion = 0,
    Equalizer,
    Chorus,
    Phaser,
    Flanger,
    RingMod,       // Ring Modulator / Frequency Shifter
    Delay,
    Reverb,
    Compressor
};

inline constexpr int kNumEffects = 9;

namespace EffectInfo
{
    inline const char* getName (EffectId id) noexcept
    {
        switch (id)
        {
            case EffectId::Distortion:  return "DISTORTION";
            case EffectId::Equalizer:   return "EQUALIZER";
            case EffectId::Chorus:      return "CHORUS";
            case EffectId::Phaser:      return "PHASER";
            case EffectId::Flanger:     return "FLANGER";
            case EffectId::RingMod:     return "RING MOD";
            case EffectId::Delay:       return "DELAY";
            case EffectId::Reverb:      return "REVERB";
            case EffectId::Compressor:  return "COMPRESSOR";
        }
        return "";
    }
}

// Interfaz de un efecto de la cadena. Procesa el bus estereo "in place".
// El propio modulo decide si esta activo: si esta apagado debe volver casi sin hacer nada.
class IEffect
{
public:
    virtual ~IEffect() = default;

    virtual EffectId getId() const noexcept = 0;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;   // reserva toda la memoria
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, double bpm) noexcept = 0;
};

// ---------------------------------------------------------------------------
//  Orden de la cadena: una permutacion de los 9 modulos. order[i] = modulo en la posicion i.
//  Se guarda empaquetada en 36 bits (4 por posicion) para poder leerla de forma atomica.
// ---------------------------------------------------------------------------
using EffectOrder = std::array<std::uint8_t, (size_t) kNumEffects>;

namespace EffectOrderUtil
{
    inline EffectOrder getDefault() noexcept
    {
        EffectOrder o {};
        for (size_t i = 0; i < o.size(); ++i)
            o[i] = (std::uint8_t) i;
        return o;
    }

    // true si es una permutacion valida de 0..8
    inline bool isValid (const EffectOrder& o) noexcept
    {
        std::array<bool, (size_t) kNumEffects> seen {};
        for (auto v : o)
        {
            if (v >= kNumEffects || seen[v])
                return false;
            seen[v] = true;
        }
        return true;
    }

    inline std::uint64_t pack (const EffectOrder& o) noexcept
    {
        std::uint64_t packed = 0;
        for (size_t i = 0; i < o.size(); ++i)
            packed |= (std::uint64_t) (o[i] & 0xF) << (4 * i);
        return packed;
    }

    // Puede devolver un orden NO valido si el dato estaba corrupto: comprobar con isValid().
    inline EffectOrder unpack (std::uint64_t packed) noexcept
    {
        EffectOrder o {};
        for (size_t i = 0; i < o.size(); ++i)
            o[i] = (std::uint8_t) ((packed >> (4 * i)) & 0xF);
        return o;
    }

    inline juce::String toText (const EffectOrder& o)
    {
        juce::StringArray parts;
        for (auto v : o)
            parts.add (juce::String ((int) v));
        return parts.joinIntoString (",");
    }

    // Lee "6,0,1,..." y lo valida. Devuelve false (y no toca 'result') si no es una permutacion correcta.
    inline bool fromText (const juce::String& text, EffectOrder& result)
    {
        const auto parts = juce::StringArray::fromTokens (text, ",", "");
        if (parts.size() != kNumEffects)
            return false;

        EffectOrder o {};
        for (int i = 0; i < kNumEffects; ++i)
        {
            const auto s = parts[i].trim();
            if (! s.containsOnly ("0123456789") || s.isEmpty())
                return false;
            o[(size_t) i] = (std::uint8_t) juce::jlimit (0, 255, s.getIntValue());
        }

        if (! isValid (o))
            return false;

        result = o;
        return true;
    }
}
