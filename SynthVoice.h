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
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
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
        noteHz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        bendSemitones = wheelToSemitones (pitchWheelPos);
        level = velocity * 0.2f;
        phase = 0.0;
        s1 = 0.0;
        s2 = 0.0;
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

        const double freq = noteHz * std::pow (2.0, bendSemitones / 12.0);
        const double dt = freq / sr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            const float fenv = filterAdsr.getNextSample();

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            // Los coeficientes del filtro se recalculan cada 16 muestras (ahorra CPU)
            if (coefCounter-- <= 0)
            {
                coefCounter = 15;
                const double mod = std::pow (2.0, amount * 7.0 * (double) fenv);
                const double fc = juce::jlimit (20.0, 0.45 * sr, cutoff * mod);
                g = std::tan (juce::MathConstants<double>::pi * fc / sr);
                h = 1.0 / (1.0 + g * (g + k));
            }

            const float raw = oscillator (waveType, (float) phase, (float) dt);
            const float filtered = (float) processFilter ((double) raw, filterType);
            const float s = filtered * env * level;

            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, startSample + i, s);

            phase += dt;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    static float wheelToSemitones (int wheel)
    {
        // Rango del pitch bend: +/- 2 semitonos
        return ((float) wheel - 8192.0f) / 8192.0f * 2.0f;
    }

    // Filtro de variable de estado (TPT). Salidas: 0 paso bajo, 1 paso alto,
    // 2 paso banda, 3 notch
    double processFilter (double x, int type)
    {
        const double hp = (x - (k + g) * s1 - s2) * h;
        const double bp = g * hp + s1;
        const double lp = g * bp + s2;
        s1 = g * hp + bp;
        s2 = g * bp + lp;

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

    double phase = 0.0;
    double noteHz = 440.0;
    float bendSemitones = 0.0f;
    float level = 0.0f;

    // Estado del filtro
    double s1 = 0.0, s2 = 0.0;
    double g = 0.0, h = 1.0, k = 1.0;
    int coefCounter = 0;
};
