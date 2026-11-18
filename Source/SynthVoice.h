#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>

// Tamano de las tablas de onda personalizadas de los LFO (potencia de 2)
constexpr int kLfoTableSize = 2048;

// Punteros a los parametros (los lee la voz en cada bloque de audio)
struct VoiceParams
{
    std::atomic<float>* wave    = nullptr;
    std::atomic<float>* octave  = nullptr;
    std::atomic<float>* semi    = nullptr;
    std::atomic<float>* fine    = nullptr;   // en cents
    std::atomic<float>* pw      = nullptr;   // ancho de pulso de la onda cuadrada
    std::atomic<float>* attack  = nullptr;
    std::atomic<float>* decay   = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;

    std::atomic<float>* filterType = nullptr;
    std::atomic<float>* cutoff     = nullptr;
    std::atomic<float>* resonance  = nullptr;
    std::atomic<float>* envAmount  = nullptr;
    std::atomic<float>* fAttack    = nullptr;
    std::atomic<float>* fDecay     = nullptr;
    std::atomic<float>* fSustain   = nullptr;
    std::atomic<float>* fRelease   = nullptr;

    std::atomic<float>* unison     = nullptr;
    std::atomic<float>* unisonMode = nullptr;
    std::atomic<float>* detune     = nullptr;   // en cents
    std::atomic<float>* stereo     = nullptr;
    std::atomic<float>* drift      = nullptr;
    std::atomic<float>* glide      = nullptr;   // en segundos
    std::atomic<float>* swarmRate  = nullptr;   // velocidad de modulacion del modo Swarm (Hz)

    // 3 LFOs
    std::atomic<float>* lfoWave[3]   {};
    std::atomic<float>* lfoRate[3]   {};   // Hz (modo libre)
    std::atomic<float>* lfoSync[3]   {};   // 0 = libre, resto = divisiones
    std::atomic<float>* lfoAmt[3]    {};
    std::atomic<float>* lfoDest[3]   {};
    std::atomic<float>* lfoRetrig[3] {};
    std::atomic<float>* lfoFade[3]   {};
    std::atomic<float>* lfoPhaseStart[3] {};   // fase inicial con Retrig (grados)
    const std::atomic<float>* lfoTable[3] {};  // onda personalizada de cada LFO

    // Tempo del host (para el sync de los LFO)
    std::atomic<float>* bpm = nullptr;

    // Ultima nota tocada (compartida por todas las voces, para el glide)
    std::atomic<float>* lastNote = nullptr;
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int kMaxUnison = 16;
    static constexpr int kNumLfos = 3;

    explicit SynthVoice (const VoiceParams& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate > 0.0)
        {
            adsr.setSampleRate (newRate);
            filterAdsr.setSampleRate (newRate);
        }
    }

    void startNote (int midiNote, float velocity,
                    juce::SynthesiserSound*, int pitchWheelPos) override
    {
        targetNote = (double) midiNote;
        bendSemitones = wheelToSemitones (pitchWheelPos);
        level = velocity * 0.2f;

        // Glide: parte de la ultima nota tocada
        const double glideTime = (double) params.glide->load();
        const double last = (double) params.lastNote->load();
        glideNote = (glideTime > 0.0005 && last >= 0.0) ? last : targetNote;
        params.lastNote->store ((float) midiNote);

        // Fases aleatorias en unison para un sonido mas grueso
        const int n = juce::jlimit (1, kMaxUnison, (int) params.unison->load());
        for (int u = 0; u < kMaxUnison; ++u)
        {
            phase[u] = (n > 1) ? (double) random.nextFloat() : 0.0;
            driftVal[u] = driftTarget[u] = random.nextFloat() * 2.0f - 1.0f;
            driftTimer[u] = 0;
            swarmPhase[u] = (double) random.nextFloat();
            swarmRate[u] = 0.6 + 0.8 * (double) random.nextFloat();   // factor individual de velocidad
            dt[u] = 0.0;
            gainL[u] = gainR[u] = 0.0f;
        }

        // LFOs: con Retrig la fase empieza en 0 en cada nota
        for (int l = 0; l < kNumLfos; ++l)
        {
            lfoPhase[l] = (params.lfoRetrig[l]->load() > 0.5f)
                              ? (double) params.lfoPhaseStart[l]->load() / 360.0
                              : (double) random.nextFloat();
            lfoHold[l] = (double) random.nextFloat() * 2.0 - 1.0;
        }
        lfoAge = 0.0;
        lfoPitch = lfoCut = lfoDetune = lfoPw = 0.0;
        lfoVol = 1.0f;

        sL1 = sL2 = sR1 = sR2 = 0.0;
        coefCounter = 0;
        updateEnvelopes();
        adsr.noteOn();
        filterAdsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
            filterAdsr.noteOff();
        }
        else
        {
            adsr.reset();
            filterAdsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        bendSemitones = wheelToSemitones (newValue);
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        updateEnvelopes();

        const double sr = getSampleRate();
        const int waveType = (int) params.wave->load();
        const int filterType = (int) params.filterType->load();
        const double cutoff = (double) params.cutoff->load();
        const double amount = (double) params.envAmount->load();
        const double q = 0.5 * std::pow (40.0, (double) params.resonance->load());
        k = 1.0 / q;

        const int n = juce::jlimit (1, kMaxUnison, (int) params.unison->load());
        const int mode = juce::jlimit (0, kNumModes - 1, (int) params.unisonMode->load());
        const double detuneCents = (double) params.detune->load();
        const double stereoAmt = (double) params.stereo->load();
        const double driftAmt = (double) params.drift->load();
        const double glideTime = (double) params.glide->load();
        const double pwBase = (double) params.pw->load();
        swarmHz = (double) params.swarmRate->load();

        // Afinacion del oscilador: octava, semitono y fine (en semitonos)
        tuneOffset = (double) params.octave->load() * 12.0
                   + (double) params.semi->load()
                   + (double) params.fine->load() / 100.0;

        // Configuracion de los LFO
        const double hostBpm = (double) params.bpm->load();
        bpm = hostBpm > 1.0 ? hostBpm : 120.0;
        for (int l = 0; l < kNumLfos; ++l)
        {
            auto& c = lfo[l];
            c.wave  = (int) params.lfoWave[l]->load();
            c.sync  = juce::jlimit (0, kNumSyncs - 1, (int) params.lfoSync[l]->load());
            c.dest  = (int) params.lfoDest[l]->load();
            c.rate  = (double) params.lfoRate[l]->load();
            c.amt   = (double) params.lfoAmt[l]->load();
            c.fade  = (double) params.lfoFade[l]->load();
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            const float fenv = filterAdsr.getNextSample();

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            // LFOs, pitch, glide, drift, panning y filtro se recalculan cada 16 muestras
            if (coefCounter-- <= 0)
            {
                coefCounter = 15;
                updateLfos (sr);
                pwNow = juce::jlimit (0.05, 0.95, pwBase + lfoPw);
                controlUpdate (sr, n, mode, detuneCents, stereoAmt, driftAmt, glideTime);

                const double mod = std::pow (2.0, amount * 7.0 * (double) fenv + lfoCut);
                const double fc = juce::jlimit (20.0, 0.45 * sr, cutoff * mod);
                g = std::tan (juce::MathConstants<double>::pi * fc / sr);
                h = 1.0 / (1.0 + g * (g + k));
            }

            double sumL = 0.0, sumR = 0.0;
            for (int u = 0; u < n; ++u)
            {
                const float o = oscillator (waveType, (float) phase[u], (float) dt[u], (float) pwNow);
                sumL += (double) (o * gainL[u]);
                sumR += (double) (o * gainR[u]);

                phase[u] += dt[u];
                if (phase[u] >= 1.0)
                    phase[u] -= 1.0;
            }

            const float amp = env * level * lfoVol;
            const float outL = (float) processFilter (sumL, filterType, sL1, sL2) * amp;
            const float outR = (float) processFilter (sumR, filterType, sR1, sR2) * amp;

            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, startSample + i, c == 0 ? outL : outR);
        }
    }

private:
    // Modos: 0 Normal, 1 Octavas, 2 Quinta, 3 Mayor, 4 Menor, 5 Mayor 7,
    //        6 Menor 7, 7 Sub, 8 Swarm
    static constexpr int kNumModes = 9;
    static constexpr int kModeSub = 7;
    static constexpr int kModeSwarm = 8;

    // Intervalos (en semitonos) de cada voz del unison segun el modo
    static constexpr int kIntervals[kNumModes][kMaxUnison] =
    {
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 },
        {  0, 12, -12, 24,   0, 12, -12, 24,   0, 12, -12, 24,   0, 12, -12, 24 },
        {  0,  7,  12, 19,   0,  7,  12, 19,   0,  7,  12, 19,   0,  7,  12, 19 },
        {  0,  4,   7, 12,  16, 19,  24, 28,   0,  4,   7, 12,  16, 19,  24, 28 },
        {  0,  3,   7, 12,  15, 19,  24, 27,   0,  3,   7, 12,  15, 19,  24, 27 },
        {  0,  4,   7, 11,  12, 16,  19, 23,   0,  4,   7, 11,  12, 16,  19, 23 },
        {  0,  3,   7, 10,  12, 15,  19, 22,   0,  3,   7, 10,  12, 15,  19, 22 },
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 },
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 }
    };

    // Sync de los LFO: duracion de un ciclo en negras (0 = modo libre).
    // Libre, 8/1, 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32,
    // y los mismos en tresillo: 8/1T, 4/1T, 2/1T, 1/1T, 1/2T, 1/4T, 1/8T, 1/16T, 1/32T
    static constexpr int kNumSyncs = 19;
    static constexpr double kBeats[kNumSyncs] =
    {
        1.0,
        32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
        64.0 / 3.0, 32.0 / 3.0, 16.0 / 3.0, 8.0 / 3.0, 4.0 / 3.0,
        2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0
    };

    // Lee la onda personalizada de un LFO (tabla de un ciclo, con interpolacion)
    double readTable (int l, double ph) const
    {
        const auto* t = params.lfoTable[l];
        if (t == nullptr)
            return 0.0;

        const double pos = ph * (double) kLfoTableSize;
        const int i0 = (int) pos & (kLfoTableSize - 1);
        const int i1 = (i0 + 1) & (kLfoTableSize - 1);
        const double fr = pos - std::floor (pos);
        const double a = (double) t[i0].load (std::memory_order_relaxed);
        const double b = (double) t[i1].load (std::memory_order_relaxed);
        return a + (b - a) * fr;
    }

    // Avanza los 3 LFO y calcula su efecto sobre cada destino:
    // 0 Ninguno, 1 Pitch, 2 Cutoff, 3 Volumen, 4 Detune, 5 PW
    void updateLfos (double sr)
    {
        lfoAge += 16.0 / sr;

        double pitch = 0.0, cut = 0.0, vol = 0.0, det = 0.0, pwMod = 0.0;

        for (int l = 0; l < kNumLfos; ++l)
        {
            const auto& c = lfo[l];

            double hz = c.rate;
            if (c.sync > 0)
                hz = (bpm / 60.0) / kBeats[c.sync];

            lfoPhase[l] += hz * 16.0 / sr;
            if (lfoPhase[l] >= 1.0)
            {
                lfoPhase[l] -= std::floor (lfoPhase[l]);
                lfoHold[l] = (double) random.nextFloat() * 2.0 - 1.0;
            }

            const double ph = lfoPhase[l];
            double v;
            switch (c.wave)
            {
                case 0:  v = std::sin (juce::MathConstants<double>::twoPi * ph); break;
                case 1:  v = 4.0 * std::abs (ph - 0.5) - 1.0; break;
                case 2:  v = 2.0 * ph - 1.0; break;
                case 3:  v = ph < 0.5 ? 1.0 : -1.0; break;
                case 5:  v = readTable (l, ph); break;   // onda personalizada
                default: v = lfoHold[l]; break;   // aleatorio (sample & hold)
            }

            if (c.fade > 0.0005)
                v *= juce::jmin (1.0, lfoAge / c.fade);

            switch (c.dest)
            {
                case 1:  pitch += v * c.amt * std::abs (c.amt) * 12.0; break;
                case 2:  cut   += v * c.amt * 5.0; break;
                case 3:  vol   += v * c.amt; break;
                case 4:  det   += v * c.amt * 300.0; break;
                case 5:  pwMod += v * c.amt * 0.45; break;
                default: break;
            }
        }

        lfoPitch = pitch;
        lfoCut = cut;
        lfoDetune = det;
        lfoPw = pwMod;
        lfoVol = (float) juce::jlimit (0.0, 2.0, 1.0 + vol);
    }

    void controlUpdate (double sr, int n, int mode, double detuneCents,
                        double stereoAmt, double driftAmt, double glideTime)
    {
        // Glide (exponencial: llega al ~95% en el tiempo indicado)
        if (glideTime > 0.0005)
        {
            const double tau = glideTime / 3.0;
            const double coef = 1.0 - std::exp (-16.0 / (sr * tau));
            glideNote += (targetNote - glideNote) * coef;
        }
        else
        {
            glideNote = targetNote;
        }

        const double baseHz = 440.0 * std::exp2 ((glideNote - 69.0) / 12.0);
        const double driftCoef = 16.0 / (0.5 * sr);
        const double norm = 1.4142135 / std::sqrt ((double) n);
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double detuneEff = juce::jmax (0.0, detuneCents + lfoDetune);
        const bool swarmMode = (mode == kModeSwarm);

        for (int u = 0; u < n; ++u)
        {
            // Drift: deriva lenta y aleatoria de cada oscilador (hasta +/- 20 cents)
            if (driftTimer[u]-- <= 0)
            {
                driftTarget[u] = random.nextFloat() * 2.0f - 1.0f;
                driftTimer[u] = (int) ((0.5 + (double) random.nextFloat()) * sr / 16.0);
            }
            driftVal[u] += (driftTarget[u] - driftVal[u]) * (float) driftCoef;

            // Posicion de la voz entre -1 y 1
            double pos = (n > 1) ? 2.0 * (double) u / (double) (n - 1) - 1.0 : 0.0;
            double panPos = 0.0;
            double cents = 0.0;

            if (swarmMode)
            {
                // Swarm: reparto tipo supersaw (voces interiores mas juntas) y
                // modulacion lenta e individual de cada oscilador
                pos = 0.5 * pos + 0.5 * pos * std::abs (pos);

                swarmPhase[u] += swarmRate[u] * swarmHz * 16.0 / sr;
                if (swarmPhase[u] >= 1.0)
                    swarmPhase[u] -= 1.0;

                cents += std::sin (twoPi * swarmPhase[u]) * (5.0 + 0.05 * detuneEff);
                panPos += 0.25 * stereoAmt * std::cos (twoPi * swarmPhase[u]);
            }

            cents += pos * detuneEff + (double) driftVal[u] * driftAmt * 20.0;
            panPos += pos * stereoAmt;

            // Intervalo de la voz. En modo Sub, una de cada tres voces baja una
            // octava (con la misma onda) y todas se desafinan con los mismos controles.
            int interval = kIntervals[mode][u];
            if (mode == kModeSub && n >= 2 && (u % 3) == 1)
                interval = -12;

            const double semis = (double) bendSemitones + tuneOffset + lfoPitch
                               + (double) interval + cents / 100.0;

            dt[u] = juce::jmin (0.49, baseHz * std::exp2 (semis / 12.0) / sr);

            // Panorama estereo con potencia constante
            panPos = juce::jlimit (-1.0, 1.0, panPos);
            const double angle = (panPos + 1.0) * juce::MathConstants<double>::pi / 4.0;
            gainL[u] = (float) (std::cos (angle) * norm);
            gainR[u] = (float) (std::sin (angle) * norm);
        }
    }

    static float wheelToSemitones (int wheel)
    {
        // Rango del pitch bend: +/- 2 semitonos
        return ((float) wheel - 8192.0f) / 8192.0f * 2.0f;
    }

    // Filtro de variable de estado (TPT). Salidas: 0 paso bajo, 1 paso alto,
    // 2 paso banda, 3 notch
    double processFilter (double x, int type, double& st1, double& st2)
    {
        const double hp = (x - (k + g) * st1 - st2) * h;
        const double bp = g * hp + st1;
        const double lp = g * bp + st2;
        st1 = g * hp + bp;
        st2 = g * bp + lp;

        switch (type)
        {
            case 0:  return lp;
            case 1:  return hp;
            case 2:  return k * bp;
            default: return hp + lp;
        }
    }

    // Correccion PolyBLEP: reduce el aliasing en sierra y cuadrada
    static float polyBlep (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    static float oscillator (int type, float ph, float dt, float pw)
    {
        switch (type)
        {
            case 0: // Seno
                return std::sin (ph * juce::MathConstants<float>::twoPi);

            case 1: // Triangulo
                return 4.0f * std::abs (ph - 0.5f) - 1.0f;

            case 2: // Sierra
                return 2.0f * ph - 1.0f - polyBlep (ph, dt);

            default: // Cuadrada
            {
                // Cuadrada / pulso con ancho variable (pw entre 0.05 y 0.95)
                float v = ph < pw ? 1.0f : -1.0f;
                v += polyBlep (ph, dt);
                float t2 = ph - pw;
                if (t2 < 0.0f)
                    t2 += 1.0f;
                v -= polyBlep (t2, dt);
                return v - (2.0f * pw - 1.0f);   // quita el offset de continua
            }
        }
    }

    void updateEnvelopes()
    {
        adsr.setParameters ({ params.attack->load(),
                              params.decay->load(),
                              params.sustain->load(),
                              params.release->load() });

        filterAdsr.setParameters ({ params.fAttack->load(),
                                    params.fDecay->load(),
                                    params.fSustain->load(),
                                    params.fRelease->load() });
    }

    struct LfoCfg
    {
        int wave = 0, sync = 0, dest = 0;
        double rate = 1.0, amt = 0.0, fade = 0.0;
    };

    VoiceParams params;
    juce::ADSR adsr;
    juce::ADSR filterAdsr;
    juce::Random random;

    // Osciladores del unison
    double phase[kMaxUnison] {};
    double dt[kMaxUnison] {};
    float gainL[kMaxUnison] {};
    float gainR[kMaxUnison] {};
    float driftVal[kMaxUnison] {};
    float driftTarget[kMaxUnison] {};
    int driftTimer[kMaxUnison] {};
    double swarmPhase[kMaxUnison] {};
    double swarmRate[kMaxUnison] {};

    // LFOs
    LfoCfg lfo[kNumLfos];
    double lfoPhase[kNumLfos] {};
    double lfoHold[kNumLfos] {};
    double lfoAge = 0.0;
    double lfoPitch = 0.0, lfoCut = 0.0, lfoDetune = 0.0, lfoPw = 0.0;
    double pwNow = 0.5;
    double swarmHz = 0.45;
    float lfoVol = 1.0f;
    double bpm = 120.0;

    double tuneOffset = 0.0;
    double targetNote = 60.0;
    double glideNote = 60.0;
    float bendSemitones = 0.0f;
    float level = 0.0f;

    // Estado del filtro (izquierda y derecha)
    double sL1 = 0.0, sL2 = 0.0, sR1 = 0.0, sR2 = 0.0;
    double g = 0.0, h = 1.0, k = 1.0;
    int coefCounter = 0;
};
