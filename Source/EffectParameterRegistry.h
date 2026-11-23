#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DelayParameters.h"
#include "EffectModule.h"

// ============================================================================
//  Registro de los parametros que pertenecen a cada modulo de la cadena EFFECTS.
//  Lo usan los presets de cadena y "Reset Chain": solo estos parametros se guardan, se cargan
//  y se restauran, asi que un preset de cadena NUNCA toca osciladores, filtro, envolventes ni LFOs.
//
//  Para anadir un efecto nuevo: devolver aqui la lista de sus IDs de APVTS.
// ============================================================================
namespace EffectParams
{
    inline juce::StringArray getDelayIds()
    {
        juce::StringArray ids;

        // Parametros que no son float
        for (auto* id : { DelayParams::kOn, DelayParams::kMode, DelayParams::kLink,
                          DelayParams::kSyncL, DelayParams::kSyncR, DelayParams::kDivL, DelayParams::kDivR,
                          DelayParams::kModifierL, DelayParams::kModifierR,
                          DelayParams::kPingPongMonoSum, DelayParams::kFilterMode, DelayParams::kReverseOctave })
            ids.add (id);

        // Todos los float (los modulables)
        for (int i = 0; i < kNumDelayModTargets; ++i)
            ids.add (DelayParams::kDelayFloatParams[i].id);

        return ids;
    }

    inline juce::StringArray getParameterIds (EffectId effect)
    {
        switch (effect)
        {
            case EffectId::Delay:       return getDelayIds();
            case EffectId::Distortion:
            case EffectId::Equalizer:
            case EffectId::Chorus:
            case EffectId::Phaser:
            case EffectId::Flanger:
            case EffectId::RingMod:
            case EffectId::Reverb:
            case EffectId::Compressor:  break;   // aun no implementados: sin parametros
        }

        return {};
    }

    inline juce::StringArray getAllEffectParameterIds()
    {
        juce::StringArray all;
        for (int i = 0; i < kNumEffects; ++i)
            all.addArray (getParameterIds ((EffectId) i));
        return all;
    }
}
