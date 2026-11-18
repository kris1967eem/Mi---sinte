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
    vp.wave       = apvts.getRawParameterValue ("wave");
    vp.octave     = apvts.getRawParameterValue ("octave");
    vp.semi       = apvts.getRawParameterValue ("semi");
    vp.fine       = apvts.getRawParameterValue ("fine");
    vp.attack     = apvts.getRawParameterValue ("attack");
    vp.decay      = apvts.getRawParameterValue ("decay");
    vp.sustain    = apvts.getRawParameterValue ("sustain");
    vp.release    = apvts.getRawParameterValue ("release");
    vp.filterType = apvts.getRawParameterValue ("ftype");
    vp.cutoff     = apvts.getRawParameterValue ("cutoff");
    vp.resonance  = apvts.getRawParameterValue ("resonance");
    vp.envAmount  = apvts.getRawParameterValue ("envamt");
    vp.fAttack    = apvts.getRawParameterValue ("fattack");
    vp.fDecay     = apvts.getRawParameterValue ("fdecay");
    vp.fSustain   = apvts.getRawParameterValue ("fsustain");
    vp.fRelease   = apvts.getRawParameterValue ("frelease");
    vp.unison     = apvts.getRawParameterValue ("unison");
    vp.unisonMode = apvts.getRawParameterValue ("umode");
    vp.detune     = apvts.getRawParameterValue ("udetune");
    vp.stereo     = apvts.getRawParameterValue ("ustereo");
    vp.drift      = apvts.getRawParameterValue ("drift");
    vp.glide      = apvts.getRawParameterValue ("glide");

    for (int i = 0; i < 3; ++i)
    {
        const juce::String n (i + 1);
        vp.lfoWave[i]   = apvts.getRawParameterValue ("lfo" + n + "wave");
        vp.lfoRate[i]   = apvts.getRawParameterValue ("lfo" + n + "rate");
        vp.lfoSync[i]   = apvts.getRawParameterValue ("lfo" + n + "sync");
        vp.lfoAmt[i]    = apvts.getRawParameterValue ("lfo" + n + "amt");
        vp.lfoDest[i]   = apvts.getRawParameterValue ("lfo" + n + "dest");
        vp.lfoRetrig[i] = apvts.getRawParameterValue ("lfo" + n + "retrig");
        vp.lfoFade[i]   = apvts.getRawParameterValue ("lfo" + n + "fade");
    }

    vp.bpm        = &hostBpm;
    vp.lastNote   = &lastNote;

    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new SynthVoice (vp));

    synth.addSound (new SynthSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout
MiSinteProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto timeRange = [] (float maxSeconds)
    {
        return NormalisableRange<float> (0.001f, maxSeconds, 0.0f, 0.3f);
    };

    // --- Oscilador y amplitud ---
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "volume", 1 }, "Volumen",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "wave", 1 }, "Onda",
        StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 2));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "octave", 1 }, "Octava", -4, 4, 0));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "semi", 1 }, "Semitono", -12, 12, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fine", 1 }, "Fine",
        NormalisableRange<float> (-100.0f, 100.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Ataque", timeRange (10.0f), 0.01f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay", timeRange (10.0f), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sustain", 1 }, "Sustain",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release", timeRange (15.0f), 0.3f));

    // --- Filtro ---
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "ftype", 1 }, "Tipo de filtro",
        StringArray { "Paso bajo", "Paso alto", "Paso banda", "Notch" }, 0));

    NormalisableRange<float> cutoffRange (20.0f, 20000.0f, 0.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "cutoff", 1 }, "Cutoff", cutoffRange, 20000.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "resonance", 1 }, "Resonancia",
        NormalisableRange<float> (0.0f, 1.0f), 0.1f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "envamt", 1 }, "Cantidad env",
        NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fattack", 1 }, "Filtro ataque", timeRange (10.0f), 0.01f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fdecay", 1 }, "Filtro decay", timeRange (10.0f), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fsustain", 1 }, "Filtro sustain",
        NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "frelease", 1 }, "Filtro release", timeRange (15.0f), 0.3f));

    // --- Unison, drift y glide ---
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "unison", 1 }, "Unison voces", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "umode", 1 }, "Unison modo",
        StringArray { "Normal", "Octavas", "Quinta", "Mayor", "Menor", "Mayor 7", "Menor 7",
                      "Sub", "Swarm" }, 0));

    // Detune de 0 a 1200 cents (una octava). La primera mitad del recorrido
    // del knob cubre de 0 a 100 cents para tener control fino.
    NormalisableRange<float> detuneRange (0.0f, 1200.0f, 0.0f);
    detuneRange.setSkewForCentre (100.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "udetune", 1 }, "Unison detune", detuneRange, 25.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ustereo", 1 }, "Unison estereo",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "drift", 1 }, "Drift",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "glide", 1 }, "Glide",
        NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));

    // --- 3 LFOs ---
    for (int i = 1; i <= 3; ++i)
    {
        const String n (i);
        const String name = "LFO " + n + " ";

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "wave", 1 }, name + "onda",
            StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada", "Aleatorio" }, 0));

        NormalisableRange<float> rateRange (0.01f, 30.0f, 0.0f);
        rateRange.setSkewForCentre (2.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "rate", 1 }, name + "rate", rateRange, 2.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "sync", 1 }, name + "sync",
            StringArray { "Libre", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                          "1/2T", "1/4T", "1/8T", "1/16T", "1/32T" }, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "amt", 1 }, name + "cantidad",
            NormalisableRange<float> (-1.0f, 1.0f), 0.5f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "dest", 1 }, name + "destino",
            StringArray { "Ninguno", "Pitch", "Cutoff", "Volumen", "Detune" }, 0));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "lfo" + n + "retrig", 1 }, name + "retrig", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "fade", 1 }, name + "fade in",
            NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));
    }

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

    // Tempo del host, para los LFO sincronizados
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm.store ((float) *bpm);

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
