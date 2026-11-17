#include "PluginProcessor.h"

#include <cmath>

#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto addFloat = [&] (const char* id, const char* name, float lo, float hi, float def,
                         float skew = 1.0f, const char* unit = "")
    {
        NormalisableRange<float> range (lo, hi, 0.0f, skew);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addInt = [&] (const char* id, const char* name, int lo, int hi, int def)
    {
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto addChoice = [&] (const char* id, const char* name, StringArray choices, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };
    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    // ---- Global
    addFloat (ids::master,    "Volumen general",       -60.0f, 6.0f, -6.0f, 1.0f, " dB");
    addInt   (ids::polyphony, "Polifonia (1 = mono)",   1, 16, 8);
    addBool  (ids::legato,    "Mono: legato",           true);
    addFloat (ids::glide,     "Glide",                  0.0f, 5000.0f, 0.0f, 0.4f, " ms");
    addChoice (ids::glideMode, "Glide: modo",           { "Siempre", "Solo legato" }, 1);
    addBool  (ids::hold,      "Hold (drone)",           false);
    addInt   (ids::bendRange, "Pitch bend: rango (semitonos)", 0, 24, 2);

    // ---- Oscilador wavetable
    addFloat (ids::oscLevel,  "Osc: nivel",             0.0f, 1.0f, 0.8f);
    addFloat (ids::wtPos,     "Osc: posicion wavetable", 0.0f, 1.0f, 0.0f);
    addChoice (ids::warpMode, "Osc: warp",              { "Off", "Bend +", "Bend -", "Sync", "Squeeze" }, 0);
    addFloat (ids::warpAmt,   "Osc: warp cantidad",     0.0f, 1.0f, 0.0f);
    addInt   (ids::oscOct,    "Osc: octava",           -4, 4, 0);
    addInt   (ids::oscSemi,   "Osc: semitonos",        -12, 12, 0);
    addFloat (ids::oscFine,   "Osc: afinacion fina",   -100.0f, 100.0f, 0.0f, 1.0f, " cents");

    // ---- Unison
    addInt   (ids::uniVoices, "Unison: voces",          1, 8, 1);
    addChoice (ids::uniMode,  "Unison: modo",
               { "Clasico", "Octavas", "Power (5a)", "Acorde mayor", "Acorde menor",
                 "Sus2", "Sus4", "Maj7", "Min7" }, 0);
    addFloat (ids::uniDetune, "Unison: detune",         0.0f, 100.0f, 20.0f, 1.0f, " cents");
    addFloat (ids::uniSpread, "Unison: spread estereo", 0.0f, 1.0f, 0.5f);
    addFloat (ids::uniBlend,  "Unison: blend",          0.0f, 1.0f, 1.0f);
    addBool  (ids::uniRandPhase, "Unison: fase aleatoria", true);
    addFloat (ids::uniDrift,  "Unison: drift",          0.0f, 30.0f, 0.0f, 1.0f, " cents");

    // ---- Filtro
    addChoice (ids::filtMode, "Filtro: modo",           { "Paso bajo", "Paso alto", "Paso banda", "Notch" }, 0);
    addFloat (ids::cutoff,    "Filtro: cutoff",         20.0f, 20000.0f, 4000.0f, 0.3f, " Hz");
    addFloat (ids::resonance, "Filtro: resonancia",     0.0f, 1.0f, 0.2f);
    addFloat (ids::keytrack,  "Filtro: key tracking",   0.0f, 1.0f, 0.0f);
    addFloat (ids::filtEnvAmt, "Filtro: cantidad env",  -8.0f, 8.0f, 0.0f, 1.0f, " oct");
    addFloat (ids::velFilter, "Filtro: velocity",       0.0f, 4.0f, 0.0f, 1.0f, " oct");

    // ---- Envolventes (segundos)
    addFloat (ids::ampA, "Amp env: attack",  0.001f, 30.0f, 0.01f, 0.3f, " s");
    addFloat (ids::ampD, "Amp env: decay",   0.001f, 30.0f, 0.3f,  0.3f, " s");
    addFloat (ids::ampS, "Amp env: sustain", 0.0f,   1.0f,  0.8f);
    addFloat (ids::ampR, "Amp env: release", 0.001f, 30.0f, 0.3f,  0.3f, " s");

    addFloat (ids::fltA, "Filtro env: attack",  0.001f, 30.0f, 0.01f, 0.3f, " s");
    addFloat (ids::fltD, "Filtro env: decay",   0.001f, 30.0f, 0.4f,  0.3f, " s");
    addFloat (ids::fltS, "Filtro env: sustain", 0.0f,   1.0f,  0.3f);
    addFloat (ids::fltR, "Filtro env: release", 0.001f, 30.0f, 0.4f,  0.3f, " s");

    // ---- LFO 1
    addChoice (ids::lfoShape, "LFO: forma",
               { "Seno", "Triangulo", "Sierra arriba", "Sierra abajo", "Cuadrada", "Sample & hold" }, 0);
    addFloat (ids::lfoRate,   "LFO: velocidad",         0.01f, 50.0f, 2.0f, 0.35f, " Hz");
    addFloat (ids::lfoPitch,  "LFO -> pitch",           0.0f, 12.0f, 0.0f, 1.0f, " st");
    addFloat (ids::lfoCutoff, "LFO -> cutoff",         -4.0f, 4.0f, 0.0f, 1.0f, " oct");
    addFloat (ids::lfoWt,     "LFO -> wavetable",      -1.0f, 1.0f, 0.0f);

    // ---- Rueda de modulacion
    addFloat (ids::mwVibrato, "Rueda -> vibrato",       0.0f, 12.0f, 0.5f, 1.0f, " st");
    addFloat (ids::mwCutoff,  "Rueda -> cutoff",       -4.0f, 4.0f, 0.0f, 1.0f, " oct");
    addFloat (ids::mwWt,      "Rueda -> wavetable",    -1.0f, 1.0f, 0.0f);

    // ---- Velocity
    addFloat (ids::velAmp,    "Velocity -> volumen",    0.0f, 1.0f, 0.7f);

    return { p.begin(), p.end() };
}

//==============================================================================
synth::EngineParams PluginProcessor::readParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto i = [&] (const char* id) { return (int) std::lround (f (id)); };
    auto b = [&] (const char* id) { return f (id) >= 0.5f; };

    synth::EngineParams e;
    e.masterDb = f (ids::master);
    e.polyphony = i (ids::polyphony);
    e.legato = b (ids::legato);
    e.glideMs = f (ids::glide);
    e.glideMode = i (ids::glideMode);
    e.hold = b (ids::hold);
    e.bendRange = i (ids::bendRange);

    e.oscLevel = f (ids::oscLevel);
    e.wtPos = f (ids::wtPos);
    e.warpMode = i (ids::warpMode);
    e.warpAmt = f (ids::warpAmt);
    e.oscOct = i (ids::oscOct);
    e.oscSemi = i (ids::oscSemi);
    e.oscFine = f (ids::oscFine);

    e.uniVoices = i (ids::uniVoices);
    e.uniMode = i (ids::uniMode);
    e.uniDetune = f (ids::uniDetune);
    e.uniSpread = f (ids::uniSpread);
    e.uniBlend = f (ids::uniBlend);
    e.uniRandPhase = b (ids::uniRandPhase);
    e.uniDrift = f (ids::uniDrift);

    e.filtMode = i (ids::filtMode);
    e.cutoff = f (ids::cutoff);
    e.resonance = f (ids::resonance);
    e.keytrack = f (ids::keytrack);
    e.filtEnvAmt = f (ids::filtEnvAmt);
    e.velFilter = f (ids::velFilter);

    e.ampA = f (ids::ampA); e.ampD = f (ids::ampD); e.ampS = f (ids::ampS); e.ampR = f (ids::ampR);
    e.fltA = f (ids::fltA); e.fltD = f (ids::fltD); e.fltS = f (ids::fltS); e.fltR = f (ids::fltR);

    e.lfoShape = i (ids::lfoShape);
    e.lfoRate = f (ids::lfoRate);
    e.lfoPitch = f (ids::lfoPitch);
    e.lfoCutoff = f (ids::lfoCutoff);
    e.lfoWt = f (ids::lfoWt);

    e.mwVibrato = f (ids::mwVibrato);
    e.mwCutoff = f (ids::mwCutoff);
    e.mwWt = f (ids::mwWt);

    e.velAmp = f (ids::velAmp);
    return e;
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    engine.setParams (readParams());
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    engine.setParams (readParams());

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    int pos = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int t = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (t > pos)
        {
            engine.render (left + pos, right + pos, t - pos);
            pos = t;
        }

        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine.noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel())
            engine.setPitchBend ((float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController() && msg.getControllerNumber() == 1)
            engine.setModWheel ((float) msg.getControllerValue() / 127.0f);
        else if (msg.isController() && msg.getControllerNumber() == 64)
            engine.setSustainPedal (msg.getControllerValue() >= 64);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.allNotesOff();
    }

    if (pos < numSamples)
        engine.render (left + pos, right + pos, numSamples - pos);
}

//==============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("formatVersion", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::File PluginProcessor::getPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("MiSinte").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool PluginProcessor::savePresetToFile (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty ("formatVersion", 1, nullptr);
    if (auto xml = state.createXml())
        return xml->writeTo (file);
    return false;
}

bool PluginProcessor::loadPresetFromFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            return true;
        }
    }
    return false;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
