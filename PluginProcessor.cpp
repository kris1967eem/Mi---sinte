#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SynthVoice.h"

static constexpr int kNumVoices = 16;

MiSinteProcessor::MiSinteProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output",
                      juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    VoiceParams vp;
    vp.wave    = apvts.getRawParameterValue ("wave");
    vp.attack  = apvts.getRawParameterValue ("attack");
    vp.decay   = apvts.getRawParameterValue ("decay");
    vp.sustain = apvts.getRawParameterValue ("sustain");
    vp.release = apvts.getRawParameterValue ("release");

    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new SynthVoice (vp));

    synth.addSound (new SynthSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout
MiSinteProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "volume", 1 }, "Volumen",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "wave", 1 }, "Onda",
        StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 2));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Ataque",
        NormalisableRange<float> (0.001f, 10.0f, 0.0f, 0.3f), 0.01f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay",
        NormalisableRange<float> (0.001f, 10.0f, 0.0f, 0.3f), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sustain", 1 }, "Sustain",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (0.001f, 15.0f, 0.0f, 0.3f), 0.3f));

    return layout;
}

void MiSinteProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    lastGain = apvts.getRawParameterValue ("volume")->load();
}

bool MiSinteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MiSinteProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    const float gain = apvts.getRawParameterValue ("volume")->load();
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, gain);
    lastGain = gain;
}

juce::AudioProcessorEditor* MiSinteProcessor::createEditor()
{
    return new MiSinteEditor (*this);
}

void MiSinteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MiSinteProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiSinteProcessor();
}
