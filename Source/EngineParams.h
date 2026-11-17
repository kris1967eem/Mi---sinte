#pragma once

// Parametros del motor de sonido. No depende de JUCE, asi el motor se puede
// probar por separado con tests automaticos.

namespace synth
{
struct EngineParams
{
    // ---- Global ----
    float masterDb   = -6.0f;
    int   polyphony  = 8;        // 1 = mono ... 16
    bool  legato     = true;     // solo mono: notas ligadas no reinician envolventes
    float glideMs    = 0.0f;
    int   glideMode  = 0;        // 0 = siempre, 1 = solo legato
    bool  hold       = false;    // modo drone: las notas siguen sonando hasta apagarlo
    int   bendRange  = 2;        // semitonos

    // ---- Oscilador wavetable ----
    float oscLevel   = 0.8f;
    float wtPos      = 0.0f;     // 0..1
    int   warpMode   = 0;        // 0 off, 1 bend+, 2 bend-, 3 sync, 4 squeeze
    float warpAmt    = 0.0f;
    int   oscOct     = 0;
    int   oscSemi    = 0;
    float oscFine    = 0.0f;     // cents

    // ---- Unison ----
    int   uniVoices   = 1;       // 1..8
    int   uniMode     = 0;       // 0 clasico, 1 octavas, 2 power, 3 mayor, 4 menor, 5 sus2, 6 sus4, 7 maj7, 8 min7
    float uniDetune   = 20.0f;   // cents
    float uniSpread   = 0.5f;    // 0..1
    float uniBlend    = 1.0f;    // 0..1
    bool  uniRandPhase = true;
    float uniDrift    = 0.0f;    // cents

    // ---- Filtro ----
    int   filtMode    = 0;       // 0 LP, 1 HP, 2 BP, 3 Notch
    float cutoff      = 4000.0f;
    float resonance   = 0.2f;
    float keytrack    = 0.0f;
    float filtEnvAmt  = 0.0f;    // octavas
    float velFilter   = 0.0f;    // octavas

    // ---- Envolventes ----
    float ampA = 0.01f, ampD = 0.3f, ampS = 0.8f, ampR = 0.3f;
    float fltA = 0.01f, fltD = 0.4f, fltS = 0.3f, fltR = 0.4f;

    // ---- LFO 1 ----
    int   lfoShape  = 0;         // 0 sin, 1 tri, 2 saw up, 3 saw down, 4 square, 5 S&H
    float lfoRate   = 2.0f;      // Hz
    float lfoPitch  = 0.0f;      // semitonos
    float lfoCutoff = 0.0f;      // octavas
    float lfoWt     = 0.0f;      // -1..1

    // ---- Rueda de modulacion ----
    float mwVibrato = 0.5f;      // semitonos de vibrato (LFO) a rueda completa
    float mwCutoff  = 0.0f;      // octavas
    float mwWt      = 0.0f;      // -1..1

    // ---- Velocity ----
    float velAmp    = 0.7f;      // 0..1
};
} // namespace synth
