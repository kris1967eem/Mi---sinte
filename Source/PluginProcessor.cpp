#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SynthVoice.h"
#include "Wavetable/WTDefault.h"
#include <juce_audio_formats/juce_audio_formats.h>

static constexpr int kNumVoices = 16;

MiSinteProcessor::MiSinteProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    // 1. Cargar la wavetable por defecto
    defaultWavetable = wavetable::createDefaultWavetable();

    // 2. Inicializar las tablas de los LFOs (a cero para evitar basura)
    for (auto& t : lfoTables) t.store(0.0f);
    for (int i = 0; i < kNumLfos; ++i) {
        lfoWaveName[i] = "";
        lfoWaveLoaded[i] = false;
    }

    // 3. Configurar la voz
    VoiceParams vp;
    vp.wave       = apvts.getRawParameterValue ("wave");
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
    vp.lastNote   = &lastNote;

    vp.octave = apvts.getRawParameterValue("octave");
    vp.semi = apvts.getRawParameterValue("semi");
    vp.fine = apvts.getRawParameterValue("fine");
    vp.pw = apvts.getRawParameterValue("pw");
    vp.sync = apvts.getRawParameterValue("oscsync");
    vp.syncFreq = apvts.getRawParameterValue("syncfreq");
    vp.reset = apvts.getRawParameterValue("oscreset");
    vp.oscVol = apvts.getRawParameterValue("volume"); 
    vp.tilt = apvts.getRawParameterValue("uharm");
    
    // Wavetable params
    vp.oscType       = apvts.getRawParameterValue ("oscType");
    vp.wtPos         = apvts.getRawParameterValue ("wtPos");
    vp.wtWarpMode    = apvts.getRawParameterValue ("wtWarpMode");
    vp.wtWarpAmt     = apvts.getRawParameterValue ("wtWarpAmt");
    vp.wtHighQuality = apvts.getRawParameterValue ("wtHighQuality");
    vp.wavetableData = defaultWavetable;

    // BPM y LFOs
    vp.bpm = apvts.getRawParameterValue("bpm");
    for (int l = 0; l < kNumLfos; ++l) {
        juce::String n(l+1);
        vp.lfoWave[l] = apvts.getRawParameterValue("lfo" + n + "wave");
        vp.lfoRate[l] = apvts.getRawParameterValue("lfo" + n + "rate");
        vp.lfoSync[l] = apvts.getRawParameterValue("lfo" + n + "sync");
        vp.lfoAmt[l] = apvts.getRawParameterValue("lfo" + n + "amt");
        vp.lfoDest[l] = apvts.getRawParameterValue("lfo" + n + "dest");
        vp.lfoRetrig[l] = apvts.getRawParameterValue("lfo" + n + "retrig");
        vp.lfoFade[l] = apvts.getRawParameterValue("lfo" + n + "fade");
        vp.lfoPhaseStart[l] = apvts.getRawParameterValue("lfo" + n + "phase");
        vp.lfoPoly[l] = apvts.getRawParameterValue("lfo" + n + "poly");
        vp.lfoTable[l] = &lfoTables[l * kLfoTableSize]; // Asignar tabla de LFO
    }
    
    vp.dispCutoff  = &filterDisplay.cutoff;
    vp.dispReso    = &filterDisplay.reso;
    vp.dispWrites  = &filterDisplay.writes;
    vp.noteCounter = &noteCounter;

    vp.chordIntervals = userChord.intervalsData();
    vp.chordCount     = userChord.countPtr();
    vp.globalLfo      = &globalLfo;

    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new SynthVoice (vp));

    synth.addSound (new SynthSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout MiSinteProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto timeRange = [] (float maxSeconds) { return NormalisableRange<float> (0.001f, maxSeconds, 0.0f, 0.3f); };

    // 1. Controles básicos del oscilador
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volumen", NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "wave", 1 }, "Onda", StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 2));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, "Octava", -4, 4, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "semi", 1 }, "Semitono", -12, 12, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fine", 1 }, "Fine", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pw", 1 }, "PW", NormalisableRange<float> (0.05f, 0.95f), 0.5f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "oscsync", 1 }, "Sync", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "syncfreq", 1 }, "Sync Freq", NormalisableRange<float> (0.0f, 36.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "oscreset", 1 }, "Reset Fase", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "uharm", 1 }, "Tilt Armonico", NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    // 2. Parámetros NUEVOS del Wavetable
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "oscType", 1 }, "Oscilador", StringArray { "VA", "Wavetable" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "wtPos", 1 }, "WT Position", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "wtWarpMode", 1 }, "WT Warp Mode", StringArray { "Off", "Bend+", "Bend-", "Sync", "Squeeze" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "wtWarpAmt", 1 }, "WT Warp Amount", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "wtHighQuality", 1 }, "WT High Quality", true));

    // 3. Envolventes, Filtro y Unison
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Ataque", timeRange (10.0f), 0.01f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay", timeRange (10.0f), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", timeRange (15.0f), 0.3f));
    
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ftype", 1 }, "Tipo filtro", StringArray { "LP", "HP", "BP", "Notch" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.3f), 20000.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "resonance", 1 }, "Reso", NormalisableRange<float> (0.0f, 1.0f), 0.1f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "envamt", 1 }, "Env Amt", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fattack", 1 }, "F Ataque", timeRange (10.0f), 0.01f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fdecay", 1 }, "F Decay", timeRange (10.0f), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fsustain", 1 }, "F Sustain", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "frelease", 1 }, "F Release", timeRange (15.0f), 0.3f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "unison", 1 }, "Voces", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "umode", 1 }, "Modo", StringArray { "Normal", "Octavas", "Quinta", "Mayor", "Menor", "Mayor 7", "Menor 7", "Sub", "Swarm", "Chord" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "udetune", 1 }, "Detune", NormalisableRange<float> (0.0f, 1200.0f, 0.0f, 0.3f), 25.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ustereo", 1 }, "Stereo", NormalisableRange<float> (0.0f, 1.0f), 0.7f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drift", 1 }, "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "BPM", NormalisableRange<float> (20.0f, 300.0f, 1.0f), 120.0f));

    // 4. Parámetros de los LFOs
    for (int i = 1; i <= kNumLfos; ++i) {
        juce::String n(i);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo" + n + "wave", 1 }, "LFO" + n + " Wave", StringArray { "Sin", "Tri", "Saw", "Sqr", "S&H", "Custom" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfo" + n + "rate", 1 }, "LFO" + n + " Rate", NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 2.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo" + n + "sync", 1 }, "LFO" + n + " Sync", StringArray { "Free", "1/1", "1/2", "1/4", "1/8", "1/16" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfo" + n + "amt", 1 }, "LFO" + n + " Amt", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo" + n + "dest", 1 }, "LFO" + n + " Dest", StringArray { "Off", "Pitch", "Cutoff", "Reso", "Volume", "Detune", "PW", "Sync", "Rate 1", "Rate 2", "Rate 3", "Rate 4" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lfo" + n + "retrig", 1 }, "LFO" + n + " Retrig", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfo" + n + "fade", 1 }, "LFO" + n + " Fade", NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfo" + n + "phase", 1 }, "LFO" + n + " Phase", NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lfo" + n + "poly", 1 }, "LFO" + n + " Poly", false));
    }
    
    // NOTA: Aquí faltarían los parámetros del Delay que uses en EffectChain.
    // Si el EffectChain añade sus propios parámetros en otro sitio, esto compilará.
    // Si el EffectChain necesita que los declares aquí, es posible que falten algunos de DelayParams.
    // De momento, para que compile, dejamos los básicos.

    return layout;
}

void MiSinteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    globalLfo.prepare(sampleRate);
    lastGain = apvts.getRawParameterValue ("volume")->load();
    effectChain.prepare(sampleRate, samplesPerBlock);
}

bool MiSinteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MiSinteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Obtener el BPM del host si está disponible
    float hostBpm = 120.0f;
    if (auto* hostPlayHead = getPlayHead()) {
        if (auto pos = hostPlayHead->getPosition()) {
            if (auto bpm = pos->getBpm()) {
                hostBpm = static_cast<float> (*bpm);
                if (auto* param = apvts.getParameter("bpm")) 
                    param->setValueNotifyingHost(param->convertTo0to1(hostBpm));
            }
        }
    }

    // Procesar el chord capture y los LFOs globales
    chordCapture.process(midi, buffer.getNumSamples(), getSampleRate(), userChord);
    globalLfo.renderBlock(buffer.getNumSamples(), 0); // TODO: Ajustar retrigger si es necesario

    // Renderizar las voces
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    // Procesar la cadena de efectos
    effectChain.process(buffer, hostBpm);

    // Ganancia maestra
    const float gain = apvts.getRawParameterValue ("volume")->load();
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, gain);
    lastGain = gain;
}

juce::AudioProcessorEditor* MiSinteProcessor::createEditor() { return new MiSinteEditor (*this); }

void MiSinteProcessor::getStateInformation (juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    // Guardar la wavetable también si es necesario
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MiSinteProcessor::setStateInformation (const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

bool MiSinteProcessor::loadChordFromMidi (const juce::File& file)
{
    // JUCE MidiFile lee desde un InputStream, no directamente desde juce::File.
    juce::FileInputStream inputStream (file);
    if (! inputStream.openedOk())
        return false;

    juce::MidiFile midiFile;
    if (! midiFile.readFrom (inputStream))
        return false;

    // Recoger las notas MIDI sin duplicados para construir el acorde.
    juce::Array<int> notes;
    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        const auto* track = midiFile.getTrack (trackIndex);
        if (track == nullptr)
            continue;

        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
        {
            const auto* event = track->getEventPointer (eventIndex);
            if (event != nullptr && event->message.isNoteOn())
                notes.addIfNotAlreadyThere (event->message.getNoteNumber());
        }
    }

    if (notes.isEmpty())
        return false;
    
    userChord.setFromNotes(notes.getRawDataPointer(), notes.size());
    return true;
}

bool MiSinteProcessor::loadLfoWave (int index, const juce::File& file)
{
    if (index < 0 || index >= kNumLfos) return false;
    
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;

    // Leer 1 ciclo, simplificado
    const int numSamples = juce::jmin((int)reader->lengthInSamples, kLfoTableSize);
    juce::AudioBuffer<float> buffer(1, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, false);

    auto* dest = &lfoTables[index * kLfoTableSize];
    const float* src = buffer.getReadPointer(0);
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) peak = juce::jmax(peak, std::abs(src[i]));
    if (peak > 0.0001f) {
        for (int i = 0; i < kLfoTableSize; ++i) {
            dest[i].store(src[i % numSamples] / peak);
        }
    } else {
        for (int i = 0; i < kLfoTableSize; ++i) dest[i].store(0.0f);
    }

    lfoWaveName[index] = file.getFileNameWithoutExtension();
    lfoWaveLoaded[index] = true;
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiSinteProcessor(); }
