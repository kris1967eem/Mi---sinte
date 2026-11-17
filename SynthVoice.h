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
            adsr.setSampleRate (newRate);
    }

    void startNote (int midiNote, float velocity,
                    juce::SynthesiserSound*, int pitchWheelPos) override
    {
        noteHz = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        bendSemitones = wheelToSemitones (pitchWheelPos);
        level = velocity * 0.2f;
        phase = 0.0;
        updateEnvelope();
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
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

        updateEnvelope();

        const int waveType = (int) params.wave->load();
        const double freq = noteHz * std::pow (2.0, bendSemitones / 12.0);
        const double dt = freq / getSampleRate();

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            const float s = oscillator (waveType, (float) phase, (float) dt) * env * level;
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

    void updateEnvelope()
    {
        adsr.setParameters ({ params.attack->load(),
                              params.decay->load(),
                              params.sustain->load(),
                              params.release->load() });
    }

    VoiceParams params;
    juce::ADSR adsr;
    double phase = 0.0;
    double noteHz = 440.0;
    float bendSemitones = 0.0f;
    float level = 0.0f;
};
