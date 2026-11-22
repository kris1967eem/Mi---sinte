#pragma once

#include "DelayEngine.h"
#include "EffectModule.h"

// Adaptador que mete el Delay existente en la cadena. No modifica el DelayEngine:
// el motor ya procesa el bus estereo "in place" y gestiona su propio ON/OFF.
class DelayModule final : public IEffect
{
public:
    explicit DelayModule (juce::AudioProcessorValueTreeState& apvts) : engine (apvts) {}

    EffectId getId() const noexcept override { return EffectId::Delay; }
    void prepare (double sampleRate, int maxBlockSize) override { engine.prepare (sampleRate, maxBlockSize); }
    void reset() override { engine.reset(); }
    void process (juce::AudioBuffer<float>& buffer, double bpm) noexcept override { engine.process (buffer, bpm); }

    DelayEngine& getEngine() noexcept { return engine; }

private:
    DelayEngine engine;
};
