#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <array> 
#include "Wavetable/WTData.h"
#include "UserChord.h"
#include "EffectChain.h"
#include "FilterResponseDisplay.h"
#include "SynthVoice.h" // Necesario para GlobalLfoBank y kNumLfos

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

    // --- Getter para Wavetable ---
    std::shared_ptr<const wavetable::WavetableData> getWavetable() const { return defaultWavetable; }

    // --- Getter y funciones que pide el PluginEditor ---
    FilterDisplayState& getFilterDisplay() { return filterDisplay; }
    EffectChain& getEffectChain() { return effectChain; }
    UserChord& getUserChord() { return userChord; }
    bool isChordCaptureArmed() const { return chordCapture.isArmed(); }
    void armChordCapture() { chordCapture.arm(); }
    void cancelChordCapture() { chordCapture.cancel(); }
    bool loadChordFromMidi (const juce::File& f);
    
    juce::String getLfoWaveName (int index) const { return lfoWaveLoaded[index] ? lfoWaveName[index] : juce::String(); }
    bool loadLfoWave (int index, const juce::File& file);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::Synthesiser synth;
    float lastGain = 0.7f;
    std::atomic<float> lastNote { -1.0f };

    // --- Objetos del motor y la UI ---
    std::shared_ptr<const wavetable::WavetableData> defaultWavetable;
    FilterDisplayState filterDisplay;
    EffectChain effectChain { apvts };
    UserChord userChord;
    ChordCapture chordCapture;
    GlobalLfoBank globalLfo;
    std::atomic<int> noteCounter { 0 };

    // --- LFOs custom (Tablas de usuario) ---
    // Usamos 8192 floats para 4 LFOs de 2048 muestras
    std::array<std::atomic<float>, 8192> lfoTables; 
    std::array<juce::String, kNumLfos> lfoWaveName;
    std::array<bool, kNumLfos> lfoWaveLoaded {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiSinteProcessor)
};
