#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "SynthEngine.h"

class MiSinteProcessor : public juce::AudioProcessor
{
public:
    MiSinteProcessor();
    ~MiSinteProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MiSinte"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 15.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParamsToEngine();

    synth::SynthEngine engine;
    juce::AudioBuffer<float> renderBuffer;

    struct ParamPtrs
    {
        std::atomic<float>* wave = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* ftype = nullptr;
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* resonance = nullptr;
        std::atomic<float>* envamt = nullptr;
        std::atomic<float>* fattack = nullptr;
        std::atomic<float>* fdecay = nullptr;
        std::atomic<float>* fsustain = nullptr;
        std::atomic<float>* frelease = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* umode = nullptr;
        std::atomic<float>* udetune = nullptr;
        std::atomic<float>* ustereo = nullptr;
        std::atomic<float>* drift = nullptr;
        std::atomic<float>* glide = nullptr;
        std::atomic<float>* volume = nullptr;
    } params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiSinteProcessor)
};