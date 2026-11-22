#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>

// ============================================================================
//  DelayParameters
//  IDs del APVTS, rangos, enums y puntos de enganche para la futura matriz de modulacion
//  del modulo Delay. Solo cabecera.
//
//  Uso desde el procesador (se hara en el Bloque 4):
//      DelayParams::addDelayParameters (layout);      // dentro de createLayout()
// ============================================================================

// ---------------------------------------------------------------------------
//  Enums
// ---------------------------------------------------------------------------
enum class DelayMode : int
{
    Simple = 0,
    PingPong,
    Tape,
    Filter,
    Reverse,
    Granular
};
inline constexpr int kNumDelayModes = 6;

enum class DelayFilterMode : int
{
    LowPass = 0,
    BandPass,
    HighPass
};
inline constexpr int kNumDelayFilterModes = 3;

// Parametros de tipo float que se pueden modular (globales y especificos de cada modo).
// Los booleanos y las listas (Link, Sync, Division, Modifier, Mode, Mono Sum, Octave Up,
// Filter Mode) no se modulan. El orden coincide con la tabla kDelayFloatParams.
enum class DelayModTarget : int
{
    // Globales
    TimeL = 0,
    TimeR,
    Feedback,
    Mix,
    // Simple
    SimpleStereoOffset,
    SimpleWidth,
    SimpleHighCut,
    SimpleLowCut,
    // Ping-Pong
    PingPongCrossFeedback,
    PingPongPanSpread,
    // Tape
    TapeDrive,
    TapeWowFlutter,
    TapeAge,
    // Filter
    FilterCutoff,
    FilterResonance,
    FilterLfoDepth,
    // Reverse
    ReverseCrossfadeSmoothness,
    ReverseForwardReverseMix,
    // Granular
    GranularGrainSize,
    GranularDensity,
    GranularPitchShift,
    GranularSpray,

    Count
};
inline constexpr int kNumDelayModTargets = (int) DelayModTarget::Count;

// ---------------------------------------------------------------------------
//  IDs del APVTS (estables: no renombrar sin avisar)
// ---------------------------------------------------------------------------
namespace DelayParams
{
    // Globales
    inline constexpr const char* kOn       = "delay_on";
    inline constexpr const char* kMode     = "delay_mode";
    inline constexpr const char* kLink     = "delay_link";
    inline constexpr const char* kSyncL    = "delay_sync_l";
    inline constexpr const char* kSyncR    = "delay_sync_r";
    inline constexpr const char* kDivL     = "delay_div_l";
    inline constexpr const char* kDivR     = "delay_div_r";
    inline constexpr const char* kModifierL = "delay_mod_l";
    inline constexpr const char* kModifierR = "delay_mod_r";
    inline constexpr const char* kTimeL    = "delay_time_l";
    inline constexpr const char* kTimeR    = "delay_time_r";
    inline constexpr const char* kFeedback = "delay_feedback";
    inline constexpr const char* kMix      = "delay_mix";

    // Simple
    inline constexpr const char* kSimpleOffset = "delay_simple_offset";
    inline constexpr const char* kSimpleWidth  = "delay_simple_width";
    inline constexpr const char* kSimpleHiCut  = "delay_simple_hicut";
    inline constexpr const char* kSimpleLoCut  = "delay_simple_locut";

    // Ping-Pong
    inline constexpr const char* kPingPongCross   = "delay_pp_cross";
    inline constexpr const char* kPingPongSpread  = "delay_pp_spread";
    inline constexpr const char* kPingPongMonoSum = "delay_pp_monosum";

    // Tape
    inline constexpr const char* kTapeDrive = "delay_tape_drive";
    inline constexpr const char* kTapeWow   = "delay_tape_wow";
    inline constexpr const char* kTapeAge   = "delay_tape_age";

    // Filter
    inline constexpr const char* kFilterCutoff = "delay_filt_cutoff";
    inline constexpr const char* kFilterReso   = "delay_filt_reso";
    inline constexpr const char* kFilterMode   = "delay_filt_mode";
    inline constexpr const char* kFilterLfo    = "delay_filt_lfo";

    // Reverse
    inline constexpr const char* kReverseSmooth = "delay_rev_smooth";
    inline constexpr const char* kReverseMix    = "delay_rev_mix";
    inline constexpr const char* kReverseOctave = "delay_rev_oct";

    // Granular
    inline constexpr const char* kGranSize    = "delay_gran_size";
    inline constexpr const char* kGranDensity = "delay_gran_density";
    inline constexpr const char* kGranPitch   = "delay_gran_pitch";
    inline constexpr const char* kGranSpray   = "delay_gran_spray";

    // ---- Parametros float: rango, valor por defecto y curva ----------------
    struct FloatParamInfo
    {
        const char* id;
        const char* name;
        float min;
        float max;
        float def;
        float centre;       // valor que queda en el centro del recorrido del knob (0 = lineal)
        const char* label;  // unidad
    };

    // Indexada por DelayModTarget (mismo orden que el enum)
    inline constexpr FloatParamInfo kDelayFloatParams[kNumDelayModTargets] =
    {
        // Globales
        { kTimeL,    "Delay Time L",        1.0f, 5000.0f,  300.0f,  70.7f, " ms" },   // curva logaritmica
        { kTimeR,    "Delay Time R",        1.0f, 5000.0f,  300.0f,  70.7f, " ms" },
        { kFeedback, "Delay Feedback",      0.0f,    0.95f,   0.35f,  0.0f, "" },
        { kMix,      "Delay Mix",           0.0f,    1.0f,    0.3f,   0.0f, "" },

        // Simple
        { kSimpleOffset, "Simple Stereo Offset", -50.0f,   50.0f,     0.0f,    0.0f, " ms" },
        { kSimpleWidth,  "Simple Width",           0.0f,    2.0f,     1.0f,    0.0f, "" },
        { kSimpleHiCut,  "Simple High-Cut",      200.0f, 20000.0f, 20000.0f, 2000.0f, " Hz" },
        { kSimpleLoCut,  "Simple Low-Cut",        20.0f,  2000.0f,    20.0f,  200.0f, " Hz" },

        // Ping-Pong
        { kPingPongCross,  "Ping-Pong Cross-Feedback", 0.0f, 1.0f, 1.0f, 0.0f, "" },
        { kPingPongSpread, "Ping-Pong Pan Spread",     0.0f, 1.0f, 1.0f, 0.0f, "" },

        // Tape
        { kTapeDrive, "Tape Drive",        0.0f, 1.0f, 0.3f, 0.0f, "" },
        { kTapeWow,   "Tape Wow & Flutter", 0.0f, 1.0f, 0.2f, 0.0f, "" },
        { kTapeAge,   "Tape Age",          0.0f, 1.0f, 0.3f, 0.0f, "" },

        // Filter
        { kFilterCutoff, "Filter Cutoff",    20.0f, 20000.0f, 2000.0f, 1000.0f, " Hz" },
        { kFilterReso,   "Filter Resonance",  0.0f,     1.0f,    0.3f,    0.0f, "" },
        { kFilterLfo,    "Filter LFO Depth",  0.0f,     1.0f,    0.0f,    0.0f, "" },

        // Reverse
        { kReverseSmooth, "Reverse Crossfade Smoothness", 0.0f, 1.0f, 0.5f, 0.0f, "" },
        { kReverseMix,    "Reverse Fwd/Rev Mix",          0.0f, 1.0f, 1.0f, 0.0f, "" },

        // Granular
        { kGranSize,    "Grain Size",     10.0f, 500.0f, 100.0f,  80.0f, " ms" },
        { kGranDensity, "Grain Density",   1.0f,  80.0f,  20.0f,  15.0f, " /s" },
        { kGranPitch,   "Grain Pitch",   -12.0f,  12.0f,   0.0f,   0.0f, " st" },
        { kGranSpray,   "Grain Spray",     0.0f,   1.0f,   0.2f,   0.0f, "" }
    };

    // Comprobacion en compilacion: la tabla tiene una entrada por cada DelayModTarget
    static_assert (kDelayFloatParams[kNumDelayModTargets - 1].id != nullptr,
                   "kDelayFloatParams debe tener una entrada por cada DelayModTarget");

    inline const FloatParamInfo& getInfo (DelayModTarget target) noexcept
    {
        return kDelayFloatParams[std::clamp ((int) target, 0, kNumDelayModTargets - 1)];
    }

    inline const char* getId (DelayModTarget target) noexcept
    {
        return getInfo (target).id;
    }

    inline juce::NormalisableRange<float> makeRange (const FloatParamInfo& info)
    {
        juce::NormalisableRange<float> range (info.min, info.max);
        if (info.centre > info.min && info.centre < info.max)
            range.setSkewForCentre (info.centre);
        return range;
    }

    // Nombres de las listas
    inline juce::StringArray getModeNames()       { return { "Simple", "Ping-Pong", "Tape", "Filter", "Reverse", "Granular" }; }
    inline juce::StringArray getDivisionNames()   { return { "1/1", "1/2", "3/8", "1/4", "3/16", "1/8", "1/16", "1/32" }; }
    inline juce::StringArray getModifierNames()   { return { "Normal", "Dotted", "Triplet" }; }
    inline juce::StringArray getFilterModeNames() { return { "LP", "BP", "HP" }; }

    // ---- Registro de TODOS los parametros del delay en el layout del APVTS --
    inline void addDelayParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        using juce::ParameterID;

        // Globales
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kOn, 1 }, "Delay On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kMode, 1 }, "Delay Mode",
                                                                  getModeNames(), (int) DelayMode::Simple));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kLink, 1 }, "Delay Link", true));

        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kSyncL, 1 }, "Delay Sync L", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kSyncR, 1 }, "Delay Sync R", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kDivL, 1 }, "Delay Division L",
                                                                  getDivisionNames(), 3));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kDivR, 1 }, "Delay Division R",
                                                                  getDivisionNames(), 3));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kModifierL, 1 }, "Delay Modifier L",
                                                                  getModifierNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kModifierR, 1 }, "Delay Modifier R",
                                                                  getModifierNames(), 0));

        // Parametros especificos que no son float
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kPingPongMonoSum, 1 },
                                                                "Ping-Pong Input Mono Sum", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { kFilterMode, 1 }, "Filter Delay Mode",
                                                                  getFilterModeNames(), (int) DelayFilterMode::LowPass));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { kReverseOctave, 1 },
                                                                "Reverse Octave Up", false));

        // Todos los float (los modulables), en el orden del enum
        for (int i = 0; i < kNumDelayModTargets; ++i)
        {
            const auto& info = kDelayFloatParams[i];
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                ParameterID { info.id, 1 },
                info.name,
                makeRange (info),
                info.def,
                juce::AudioParameterFloatAttributes().withLabel (info.label)));
        }
    }
}

// ---------------------------------------------------------------------------
//  Hooks para la matriz de modulacion
//
//  Array lock-free con un desplazamiento por parametro modulable, en las UNIDADES NATIVAS del
//  parametro (ms, Hz, semitonos...). Lo escribe quien module (p. ej. el procesador o la futura
//  matriz) y lo lee el audio thread al empezar cada bloque:
//      valor = std::clamp (base + offset, min, max)       // antes de suavizar
// ---------------------------------------------------------------------------
class DelayModOffsets
{
public:
    DelayModOffsets() noexcept { clear(); }

    void set (DelayModTarget target, float offset) noexcept
    {
        values[index (target)].store (offset, std::memory_order_relaxed);
    }

    float get (DelayModTarget target) const noexcept
    {
        return values[index (target)].load (std::memory_order_relaxed);
    }

    void clear() noexcept
    {
        for (auto& v : values)
            v.store (0.0f, std::memory_order_relaxed);
    }

    // Valor final de un parametro: base del APVTS + desplazamiento, limitado a su rango.
    float apply (DelayModTarget target, float baseValue) const noexcept
    {
        const auto& info = DelayParams::getInfo (target);
        return std::clamp (baseValue + get (target), info.min, info.max);
    }

private:
    static size_t index (DelayModTarget t) noexcept
    {
        return (size_t) std::clamp ((int) t, 0, kNumDelayModTargets - 1);
    }

    std::array<std::atomic<float>, (size_t) kNumDelayModTargets> values;
};
