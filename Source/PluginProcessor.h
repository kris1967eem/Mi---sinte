#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "SynthVoice.h"
#include "FilterResponseDisplay.h"
#include "UserChord.h"
#include "EffectChain.h"

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

    // User waves de los LFO (un ciclo cargado desde un archivo de audio)
    bool loadLfoWave (int index, const juce::File& file);
    juce::String getLfoWaveName (int index) const;

    // Datos que el display del filtro lee para mostrar la modulacion en tiempo real
    FilterDisplayState& getFilterDisplay() { return filterDisplay; }

    // Acorde de usuario del modo CHORD (USER): captura desde el teclado o carga desde un MIDI
    UserChord& getUserChord() { return userChord; }
    void armChordCapture()    { chordCapture.arm(); }
    void cancelChordCapture() { chordCapture.cancel(); }
    bool isChordCaptureArmed() const { return chordCapture.isArmed(); }
    bool loadChordFromMidi (const juce::File& file);

    // Cadena de efectos (pestana EFFECTS)
    EffectChain& getEffectChain() noexcept { return effectChain; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::Synthesiser synth;
    float lastGain = 0.7f;
    std::atomic<float> lastNote { -1.0f };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<int> noteCounter { 0 };
    FilterDisplayState filterDisplay;
    GlobalLfoBank globalLfo;      // LFOs con POLY apagado (uno solo para todas las voces)
    EffectChain effectChain { apvts };   // cadena de efectos reordenable (hoy: Delay; el resto, reservado)
    UserChord userChord;
    ChordCapture chordCapture;

    std::array<std::atomic<float>, kLfoTableSize> lfoTables[kNumLfos];
    juce::String lfoWaveName[kNumLfos];
    bool lfoWaveLoaded[kNumLfos] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiSinteProcessor)
};
