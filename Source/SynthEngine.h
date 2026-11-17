#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Dsp.h"
#include "EngineParams.h"
#include "Wavetable.h"

namespace synth
{
class SynthEngine
{
public:
    static constexpr int kMaxVoices   = 16;
    static constexpr int kMaxUnison   = 8;
    static constexpr int kControlRate = 16;      // muestras entre actualizaciones de control
    static constexpr float kVoiceHeadroom = 0.3f;

    void prepare (double sr)
    {
        sampleRate = sr;
        bank();                                   // fuerza la construccion de las wavetables
        lfo.setSampleRate (sr);
        lfo.reset();
        for (auto& v : voices)
        {
            v = Voice {};
            v.ampEnv.setSampleRate (sr);
            v.filtEnv.setSampleRate (sr);
        }
        heldNotes.clear();
        deferredOffs.clear();
        lastNote = -1;
        masterSm = std::pow (10.0f, params.masterDb / 20.0f);
        applyEnvParams();
    }

    void setParams (const EngineParams& p)
    {
        params = p;
        applyEnvParams();
    }

    // ------------------------------------------------------------ MIDI
    void noteOn (int note, float velocity)
    {
        deferredOffs.erase (std::remove (deferredOffs.begin(), deferredOffs.end(), note), deferredOffs.end());

        const bool wasHeld = ! heldNotes.empty();
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        if (heldNotes.size() > 64) heldNotes.erase (heldNotes.begin());

        const int maxV = std::clamp (params.polyphony, 1, kMaxVoices);
        const int glideSamples = (int) (params.glideMs * 0.001f * (float) sampleRate);
        const bool glideOk = glideSamples > 0 && lastNote >= 0 && (params.glideMode == 0 || wasHeld);

        if (maxV == 1)
        {
            Voice& v = voices[0];
            const bool legatoPlay = params.legato && v.active && wasHeld && ! v.ampEnv.isReleasing();
            float startPitch = (float) note;
            if (glideOk) startPitch = v.active ? v.pitch : (float) lastNote;
            startVoice (v, note, velocity, ! legatoPlay, startPitch, glideSamples);
        }
        else
        {
            const int idx = allocateVoice (maxV);
            startVoice (voices[(size_t) idx], note, velocity, true,
                        glideOk ? (float) lastNote : (float) note, glideSamples);
        }
        lastNote = note;
    }

    void noteOff (int note)
    {
        if (sustainActive())
        {
            if (std::find (deferredOffs.begin(), deferredOffs.end(), note) == deferredOffs.end())
                deferredOffs.push_back (note);
            return;
        }
        doNoteOff (note);
    }

    void allNotesOff()
    {
        heldNotes.clear();
        deferredOffs.clear();
        for (auto& v : voices)
            if (v.active) release (v);
    }

    void setPitchBend (float v01bipolar) { bendTarget = std::clamp (v01bipolar, -1.0f, 1.0f); }
    void setModWheel (float v01)         { mwTarget   = std::clamp (v01, 0.0f, 1.0f); }
    void setSustainPedal (bool on)       { pedal = on; checkSustainRelease(); }

    int activeVoices() const
    {
        int c = 0;
        for (const auto& v : voices) if (v.active) ++c;
        return c;
    }

    // ------------------------------------------------------------ Audio
    // Escribe (sobrescribe) numSamples muestras estereo
    void render (float* outL, float* outR, int numSamples)
    {
        std::fill (outL, outL + numSamples, 0.0f);
        std::fill (outR, outR + numSamples, 0.0f);

        checkSustainRelease();

        const int maxV = std::clamp (params.polyphony, 1, kMaxVoices);
        for (int i = maxV; i < kMaxVoices; ++i)
            if (voices[(size_t) i].active && voices[(size_t) i].keyDown)
                release (voices[(size_t) i]);

        int pos = 0;
        while (pos < numSamples)
        {
            const int chunk = std::min (kControlRate, numSamples - pos);
            controlUpdate (chunk);
            for (auto& v : voices)
                if (v.active) processVoice (v, chunk, outL + pos, outR + pos);
            pos += chunk;
        }

        const float target = params.masterDb <= -60.0f ? 0.0f : std::pow (10.0f, params.masterDb / 20.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            masterSm += (target - masterSm) * 0.002f;
            outL[i] = softClip (outL[i] * masterSm);
            outR[i] = softClip (outR[i] * masterSm);
        }
    }

private:
    struct Voice
    {
        bool     active = false;
        bool     keyDown = false;
        int      note = -1;
        uint32_t age = 0;
        float    velocity = 0.0f;
        float    velAmpGain = 1.0f;
        float    pitch = 60.0f;          // nota MIDI actual (con glide)
        float    targetPitch = 60.0f;
        float    glideDelta = 0.0f;
        int      glideLeft = 0;
        Adsr     ampEnv, filtEnv;
        Svf      filtL, filtR;
        std::array<float, kMaxUnison> phase {};
        std::array<float, kMaxUnison> drift {};
    };

    static const WavetableBank& bank()
    {
        static const WavetableBank b;
        return b;
    }

    static float softClip (float x)
    {
        const float t = 0.9f;
        const float ax = std::fabs (x);
        if (ax <= t) return x;
        const float over = ax - t;
        const float y = t + (1.0f - t) * std::tanh (over / (1.0f - t));
        return x < 0.0f ? -y : y;
    }

    static float warpPhase (float p, int mode, float amt)
    {
        switch (mode)
        {
            case 1:  return std::pow (p, 1.0f + 3.0f * amt);
            case 2:  return 1.0f - std::pow (1.0f - p, 1.0f + 3.0f * amt);
            case 3:  { const float q = p * (1.0f + 7.0f * amt); return q - std::floor (q); }
            case 4:  { const float t = 0.5f - 0.49f * amt;
                       return p < t ? 0.5f * p / t : 0.5f + 0.5f * (p - t) / (1.0f - t); }
            default: return p;
        }
    }

    bool sustainActive() const { return params.hold || pedal; }

    void applyEnvParams()
    {
        for (auto& v : voices)
        {
            v.ampEnv.setParams (params.ampA, params.ampD, params.ampS, params.ampR);
            v.filtEnv.setParams (params.fltA, params.fltD, params.fltS, params.fltR);
        }
    }

    void checkSustainRelease()
    {
        const bool s = sustainActive();
        if (sustainPrev && ! s)
        {
            const auto pending = deferredOffs;
            deferredOffs.clear();
            for (int n : pending) doNoteOff (n);
        }
        sustainPrev = s;
    }

    void release (Voice& v)
    {
        v.keyDown = false;
        v.ampEnv.noteOff();
        v.filtEnv.noteOff();
    }

    void doNoteOff (int note)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        const int maxV = std::clamp (params.polyphony, 1, kMaxVoices);

        if (maxV == 1)
        {
            Voice& v = voices[0];
            if (! v.active) return;

            if (! heldNotes.empty())
            {
                const int top = heldNotes.back();               // prioridad a la ultima nota
                if (v.note != top)
                {
                    const int glideSamples = (int) (params.glideMs * 0.001f * (float) sampleRate);
                    v.note = top;
                    v.targetPitch = (float) top;
                    if (glideSamples > 0)
                    {
                        v.glideLeft = glideSamples;
                        v.glideDelta = ((float) top - v.pitch) / (float) glideSamples;
                    }
                    else
                    {
                        v.pitch = (float) top;
                        v.glideLeft = 0;
                    }
                }
            }
            else if (v.keyDown)
            {
                release (v);
            }
        }
        else
        {
            for (auto& v : voices)
                if (v.active && v.keyDown && v.note == note)
                    release (v);
        }
    }

    int allocateVoice (int maxV)
    {
        for (int i = 0; i < maxV; ++i)
            if (! voices[(size_t) i].active) return i;

        int best = -1;
        float bestLevel = 2.0f;
        for (int i = 0; i < maxV; ++i)                           // 1) la voz en release mas baja
        {
            const auto& v = voices[(size_t) i];
            if (v.ampEnv.isReleasing() && v.ampEnv.getValue() < bestLevel)
            {
                bestLevel = v.ampEnv.getValue();
                best = i;
            }
        }
        if (best >= 0) return best;

        uint32_t oldest = 0xffffffffu;                           // 2) la mas antigua
        for (int i = 0; i < maxV; ++i)
            if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; best = i; }
        return best < 0 ? 0 : best;
    }

    void startVoice (Voice& v, int note, float vel, bool retrigger, float startPitch, int glideSamples)
    {
        if (! v.active)
        {
            v.ampEnv.reset();
            v.filtEnv.reset();
            v.filtL.reset();
            v.filtR.reset();
            for (auto& d : v.drift) d = rng.bipolar() * 0.5f;
        }

        v.active = true;
        v.note = note;
        v.age = ++ageCounter;
        v.velocity = vel;
        v.keyDown = true;
        v.velAmpGain = (1.0f - params.velAmp) + params.velAmp * vel;
        v.targetPitch = (float) note;

        if (glideSamples > 0 && startPitch != (float) note)
        {
            v.pitch = startPitch;
            v.glideLeft = glideSamples;
            v.glideDelta = ((float) note - startPitch) / (float) glideSamples;
        }
        else
        {
            v.pitch = (float) note;
            v.glideLeft = 0;
            v.glideDelta = 0.0f;
        }

        if (retrigger)
        {
            v.ampEnv.noteOn();
            v.filtEnv.noteOn();
            for (auto& ph : v.phase) ph = params.uniRandPhase ? rng.uniform() : 0.0f;
        }
    }

    // ------------------------------------------------------------ Control
    void controlUpdate (int n)
    {
        bendSm += (bendTarget - bendSm) * 0.25f;
        mwSm   += (mwTarget   - mwSm)   * 0.25f;
        bendSemis = bendSm * (float) params.bendRange;
        lfoValue = lfo.advance (n, params.lfoRate, params.lfoShape);
        updateUnison();
    }

    void updateUnison()
    {
        static const int octPat[8] = { 0, 12, -12, 24, -24, 36, -36, 48 };
        static const int chordLen[9] = { 0, 0, 3, 4, 4, 4, 4, 4, 4 };
        static const int chordTab[9][4] = { { 0, 0, 0, 0 },  { 0, 0, 0, 0 },
                                            { 0, 7, 12, 0 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 },
                                            { 0, 2, 7, 12 }, { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 } };

        uniN = std::clamp (params.uniVoices, 1, kMaxUnison);
        const int mode = std::clamp (params.uniMode, 0, 8);
        const float det = params.uniDetune;
        const float blend = std::clamp (params.uniBlend, 0.0f, 1.0f);
        const float spread = std::clamp (params.uniSpread, 0.0f, 1.0f);

        float sumSq = 0.0f;
        for (int i = 0; i < uniN; ++i)
        {
            const float d = uniN == 1 ? 0.0f : 2.0f * (float) i / (float) (uniN - 1) - 1.0f;

            if (mode == 0)
            {
                uniCents[(size_t) i] = d * det;
            }
            else
            {
                int semis;
                if (mode == 1) semis = octPat[i];
                else { const int len = chordLen[mode]; semis = chordTab[mode][i % len] + 12 * (i / len); }
                uniCents[(size_t) i] = (float) semis * 100.0f + d * det * 0.2f;
            }

            const float g = 1.0f - std::fabs (d) * (1.0f - blend);
            const float angle = (d * spread * 0.5f + 0.5f) * (kPi * 0.5f);   // pan de potencia constante
            uniGL[(size_t) i] = g * std::cos (angle);
            uniGR[(size_t) i] = g * std::sin (angle);
            sumSq += g * g;
        }

        const float norm = sumSq > 0.0f ? 1.0f / std::sqrt (sumSq) : 1.0f;
        for (int i = 0; i < uniN; ++i) { uniGL[(size_t) i] *= norm; uniGR[(size_t) i] *= norm; }
    }

    // ------------------------------------------------------------ Voz
    void processVoice (Voice& v, int n, float* L, float* R)
    {
        const EngineParams& P = params;
        const WavetableBank& wb = bank();
        const float sr = (float) sampleRate;

        if (v.glideLeft > 0)
        {
            const int steps = std::min (n, v.glideLeft);
            v.pitch += v.glideDelta * (float) steps;
            v.glideLeft -= steps;
            if (v.glideLeft == 0) v.pitch = v.targetPitch;
        }

        for (auto& d : v.drift)
            d = std::clamp (d * 0.9995f + rng.bipolar() * 0.02f, -1.0f, 1.0f);

        const float fe = v.filtEnv.processBlock (n);

        const float lfoPitchSemis = lfoValue * (P.lfoPitch + mwSm * P.mwVibrato);
        const float semis = v.pitch + bendSemis + lfoPitchSemis
                          + (float) (P.oscOct * 12 + P.oscSemi) + P.oscFine * 0.01f;
        const float baseFreq = 440.0f * std::exp2 ((semis - 69.0f) * (1.0f / 12.0f));

        const float wtPos = std::clamp (P.wtPos + lfoValue * P.lfoWt + mwSm * P.mwWt, 0.0f, 1.0f);
        const float fpos = wtPos * (float) (WavetableBank::kFrames - 1);
        const int f0 = std::min ((int) fpos, WavetableBank::kFrames - 1);
        const int f1 = std::min (f0 + 1, WavetableBank::kFrames - 1);
        const float frac = fpos - (float) f0;

        const int N = uniN;
        const float* tabA[kMaxUnison];
        const float* tabB[kMaxUnison];
        float inc[kMaxUnison];
        for (int u = 0; u < N; ++u)
        {
            const float cents = uniCents[(size_t) u] + v.drift[(size_t) u] * P.uniDrift;
            float f = baseFreq * std::exp2 (cents * (1.0f / 1200.0f));
            f = std::clamp (f, 1.0f, 0.49f * sr);
            const int mip = WavetableBank::mipForFreq (f, sr);
            tabA[u] = wb.table (f0, mip);
            tabB[u] = wb.table (f1, mip);
            inc[u] = f / sr;
        }

        const float oct = P.filtEnvAmt * fe
                        + P.keytrack * (v.pitch - 60.0f) * (1.0f / 12.0f)
                        + P.velFilter * v.velocity
                        + lfoValue * P.lfoCutoff
                        + mwSm * P.mwCutoff;
        const float fc = std::clamp (P.cutoff * std::exp2 (oct), 20.0f, std::min (20000.0f, 0.45f * sr));
        const SvfCoeffs c = makeSvf (fc, P.resonance, sampleRate);

        const float levelGain = P.oscLevel * v.velAmpGain * kVoiceHeadroom;
        const int warpMode = P.warpMode;
        const float warpAmt = P.warpAmt;

        for (int i = 0; i < n; ++i)
        {
            const float env = v.ampEnv.process();
            if (! v.ampEnv.isActive()) { v.active = false; v.keyDown = false; break; }

            float mixL = 0.0f, mixR = 0.0f;
            for (int u = 0; u < N; ++u)
            {
                float p = v.phase[(size_t) u];
                const float w = warpPhase (p, warpMode, warpAmt);
                float idx = w * (float) WavetableBank::kSize;
                idx = std::clamp (idx, 0.0f, 2047.999f);
                const int i0 = (int) idx;
                const float fr = idx - (float) i0;
                const float a = tabA[u][i0] + (tabA[u][i0 + 1] - tabA[u][i0]) * fr;
                const float b = tabB[u][i0] + (tabB[u][i0 + 1] - tabB[u][i0]) * fr;
                const float s = a + (b - a) * frac;

                mixL += s * uniGL[(size_t) u];
                mixR += s * uniGR[(size_t) u];

                p += inc[u];
                if (p >= 1.0f) p -= 1.0f;
                v.phase[(size_t) u] = p;
            }

            const float g = env * levelGain;
            L[i] += v.filtL.process (mixL, c, P.filtMode) * g;
            R[i] += v.filtR.process (mixR, c, P.filtMode) * g;
        }
    }

    // ------------------------------------------------------------ Estado
    double sampleRate = 44100.0;
    EngineParams params;
    std::array<Voice, kMaxVoices> voices;
    Lfo lfo;
    Rng rng;

    std::vector<int> heldNotes, deferredOffs;
    int lastNote = -1;
    uint32_t ageCounter = 0;
    bool pedal = false, sustainPrev = false;

    float bendTarget = 0.0f, bendSm = 0.0f, bendSemis = 0.0f;
    float mwTarget = 0.0f, mwSm = 0.0f;
    float lfoValue = 0.0f;
    float masterSm = 0.5f;

    int uniN = 1;
    std::array<float, kMaxUnison> uniGL {}, uniGR {}, uniCents {};
};
} // namespace synth
