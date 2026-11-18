#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>

// Punteros a los parametros (los lee la voz en cada bloque de audio)
struct VoiceParams
{
    std::atomic<float>* wave    = nullptr;
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

    // Ultima nota tocada (compartida por todas las voces, para el glide)
    std::atomic<float>* lastNote   = nullptr;
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int kMaxUnison = 8;

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
            dt[u] = 0.0;
            gainL[u] = gainR[u] = 0.0f;
        }

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

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            const float fenv = filterAdsr.getNextSample();

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            // Pitch, glide, drift, panning y filtro se recalculan cada 16 muestras
            if (coefCounter-- <= 0)
            {
                coefCounter = 15;
                controlUpdate (sr, n, mode, detuneCents, stereoAmt, driftAmt, glideTime);

                const double mod = std::pow (2.0, amount * 7.0 * (double) fenv);
                const double fc = juce::jlimit (20.0, 0.45 * sr, cutoff * mod);
                g = std::tan (juce::MathConstants<double>::pi * fc / sr);
                h = 1.0 / (1.0 + g * (g + k));
            }

            double sumL = 0.0, sumR = 0.0;
            for (int u = 0; u < n; ++u)
            {
                const float o = oscillator (waveType, (float) phase[u], (float) dt[u]);
                sumL += (double) (o * gainL[u]);
                sumR += (double) (o * gainR[u]);

                phase[u] += dt[u];
                if (phase[u] >= 1.0)
                    phase[u] -= 1.0;
            }

            const float outL = (float) processFilter (sumL, filterType, sL1, sL2) * env * level;
            const float outR = (float) processFilter (sumR, filterType, sR1, sR2) * env * level;

            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, startSample + i, c == 0 ? outL : outR);
        }
    }

private:
    static constexpr int kNumModes = 7;

    // Intervalos (en semitonos) de cada voz del unison segun el modo:
    // Normal, Octavas, Quinta, Mayor, Menor, Mayor 7, Menor 7
    static constexpr int kIntervals[kNumModes][kMaxUnison] =
    {
        {  0,  0,   0,  0,   0,  0,   0,  0 },
        {  0, 12, -12, 24,   0, 12, -12, 24 },
        {  0,  7,  12, 19,   0,  7,  12, 19 },
        {  0,  4,   7, 12,  16, 19,  24, 28 },
        {  0,  3,   7, 12,  15, 19,  24, 27 },
        {  0,  4,   7, 11,  12, 16,  19, 23 },
        {  0,  3,   7, 10,  12, 15,  19, 22 }
    };

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
            const double pos = (n > 1) ? 2.0 * (double) u / (double) (n - 1) - 1.0 : 0.0;

            const double semis = (double) bendSemitones
                               + (double) kIntervals[mode][u]
                               + (pos * detuneCents + (double) driftVal[u] * driftAmt * 20.0) / 100.0;

            dt[u] = juce::jmin (0.49, baseHz * std::exp2 (semis / 12.0) / sr);

            // Panorama estereo con potencia constante
            const double angle = (pos * stereoAmt + 1.0) * juce::MathConstants<double>::pi / 4.0;
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

    static float oscillator (int type, float ph, float dt)
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
                float v = ph < 0.5f ? 1.0f : -1.0f;
                v += polyBlep (ph, dt);
                float t2 = ph + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                v -= polyBlep (t2, dt);
                return v;
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

    double targetNote = 60.0;
    double glideNote = 60.0;
    float bendSemitones = 0.0f;
    float level = 0.0f;

    // Estado del filtro (izquierda y derecha)
    double sL1 = 0.0, sL2 = 0.0, sR1 = 0.0, sR2 = 0.0;
    double g = 0.0, h = 1.0, k = 1.0;
    int coefCounter = 0;
};
