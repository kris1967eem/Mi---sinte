#pragma once

// Identificadores de parametros. No los cambies una vez que tengas presets
// guardados: los presets se apoyan en estos nombres.
namespace ids
{
// Global
constexpr const char* master      = "master";
constexpr const char* polyphony   = "polyphony";
constexpr const char* legato      = "legato";
constexpr const char* glide       = "glide";
constexpr const char* glideMode   = "glideMode";
constexpr const char* hold        = "hold";
constexpr const char* bendRange   = "bendRange";

// Oscilador wavetable
constexpr const char* oscLevel    = "oscLevel";
constexpr const char* wtPos       = "wtPos";
constexpr const char* warpMode    = "warpMode";
constexpr const char* warpAmt     = "warpAmt";
constexpr const char* oscOct      = "oscOct";
constexpr const char* oscSemi     = "oscSemi";
constexpr const char* oscFine     = "oscFine";

// Unison
constexpr const char* uniVoices   = "uniVoices";
constexpr const char* uniMode     = "uniMode";
constexpr const char* uniDetune   = "uniDetune";
constexpr const char* uniSpread   = "uniSpread";
constexpr const char* uniBlend    = "uniBlend";
constexpr const char* uniRandPhase = "uniRandPhase";
constexpr const char* uniDrift    = "uniDrift";

// Filtro
constexpr const char* filtMode    = "filtMode";
constexpr const char* cutoff      = "cutoff";
constexpr const char* resonance   = "resonance";
constexpr const char* keytrack    = "keytrack";
constexpr const char* filtEnvAmt  = "filtEnvAmt";
constexpr const char* velFilter   = "velFilter";

// Envolvente de amplitud
constexpr const char* ampA = "ampA";
constexpr const char* ampD = "ampD";
constexpr const char* ampS = "ampS";
constexpr const char* ampR = "ampR";

// Envolvente de filtro
constexpr const char* fltA = "fltA";
constexpr const char* fltD = "fltD";
constexpr const char* fltS = "fltS";
constexpr const char* fltR = "fltR";

// LFO 1
constexpr const char* lfoShape    = "lfoShape";
constexpr const char* lfoRate     = "lfoRate";
constexpr const char* lfoPitch    = "lfoPitch";
constexpr const char* lfoCutoff   = "lfoCutoff";
constexpr const char* lfoWt       = "lfoWt";

// Rueda de modulacion
constexpr const char* mwVibrato   = "mwVibrato";
constexpr const char* mwCutoff    = "mwCutoff";
constexpr const char* mwWt        = "mwWt";

// Velocity
constexpr const char* velAmp      = "velAmp";
} // namespace ids
