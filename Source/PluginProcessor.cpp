#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SynthVoice.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>

static constexpr int kNumVoices = 16;

MiSinteProcessor::MiSinteProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output",
                      juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    // Las user waves de los LFO empiezan siendo un seno
    for (int i = 0; i < kNumLfos; ++i)
        for (int j = 0; j < kLfoTableSize; ++j)
            lfoTables[i][(size_t) j].store (std::sin (juce::MathConstants<float>::twoPi
                                                      * (float) j / (float) kLfoTableSize));

    VoiceParams vp;
    vp.wave       = apvts.getRawParameterValue ("wave");
    vp.octave     = apvts.getRawParameterValue ("octave");
    vp.semi       = apvts.getRawParameterValue ("semi");
    vp.fine       = apvts.getRawParameterValue ("fine");
    vp.pw         = apvts.getRawParameterValue ("pw");
    vp.sync       = apvts.getRawParameterValue ("oscsync");
    vp.syncFreq   = apvts.getRawParameterValue ("syncfreq");
    vp.reset      = apvts.getRawParameterValue ("oscreset");
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
    vp.tilt       = apvts.getRawParameterValue ("uharm");
    vp.drift      = apvts.getRawParameterValue ("drift");
    vp.glide      = apvts.getRawParameterValue ("glide");

    for (int i = 0; i < kNumLfos; ++i)
    {
        const juce::String n (i + 1);
        vp.lfoWave[i]       = apvts.getRawParameterValue ("lfo" + n + "wave");
        vp.lfoRate[i]       = apvts.getRawParameterValue ("lfo" + n + "rate");
        vp.lfoSync[i]       = apvts.getRawParameterValue ("lfo" + n + "sync");
        vp.lfoAmt[i]        = apvts.getRawParameterValue ("lfo" + n + "amt");
        vp.lfoDest[i]       = apvts.getRawParameterValue ("lfo" + n + "dest");
        vp.lfoRetrig[i]     = apvts.getRawParameterValue ("lfo" + n + "retrig");
        vp.lfoFade[i]       = apvts.getRawParameterValue ("lfo" + n + "fade");
        vp.lfoPhaseStart[i] = apvts.getRawParameterValue ("lfo" + n + "phase");
        vp.lfoPoly[i]       = apvts.getRawParameterValue ("lfo" + n + "poly");
        vp.lfoTable[i]      = lfoTables[i].data();
    }

    vp.bpm        = &hostBpm;
    vp.dispCutoff  = &filterDisplay.cutoff;
    vp.dispReso    = &filterDisplay.reso;
    vp.dispWrites  = &filterDisplay.writes;
    vp.noteCounter = &noteCounter;
    vp.chordIntervals = userChord.intervalsData();
    vp.chordCount     = userChord.countPtr();
    vp.lastNote   = &lastNote;

    // LFOs globales (POLY apagado): los calcula el procesador y las voces los leen
    globalLfo.setParams (vp);
    vp.globalLfo = &globalLfo;

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

    // --- Oscillator ---
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "wave", 1 }, "Osc Wave",
        StringArray { "SINE", "TRIANGLE", "SAW", "SQUARE" }, 2));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "octave", 1 }, "Octave", -4, 4, 0));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "semi", 1 }, "Semitone", -12, 12, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fine", 1 }, "Fine Tune",
        NormalisableRange<float> (-100.0f, 100.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "pw", 1 }, "Pulse Width",
        NormalisableRange<float> (0.05f, 0.95f), 0.5f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "oscsync", 1 }, "Osc Sync", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "syncfreq", 1 }, "Sync Freq",
        NormalisableRange<float> (0.0f, 36.0f), 12.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "oscreset", 1 }, "Osc Reset", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "glide", 1 }, "Glide",
        NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "volume", 1 }, "Volume",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    // --- Unison ---
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "unison", 1 }, "Unison Voices", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "umode", 1 }, "Unison Mode",
        StringArray { "NORMAL", "OCTAVES", "FIFTH", "MAJOR", "MINOR", "MAJOR 7", "MINOR 7",
                      "SUB", "HARMONIC", "CHORD (USER)" }, 0));

    // Detune de 0 a 1200 cents (una octava). La primera mitad del recorrido
    // del knob cubre de 0 a 100 cents para tener control fino.
    NormalisableRange<float> detuneRange (0.0f, 1200.0f, 0.0f);
    detuneRange.setSkewForCentre (100.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "udetune", 1 }, "Unison Detune", detuneRange, 25.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ustereo", 1 }, "Unison Spread",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "uharm", 1 }, "Harmonic Tilt",
        NormalisableRange<float> (0.0f, 1.0f), 0.6f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "drift", 1 }, "Drift",
        NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // --- Filter ---
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "ftype", 1 }, "Filter Type",
        StringArray { "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH" }, 0));

    NormalisableRange<float> cutoffRange (20.0f, 20000.0f, 0.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "cutoff", 1 }, "Cutoff", cutoffRange, 20000.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "resonance", 1 }, "Reso",
        NormalisableRange<float> (0.0f, 1.0f), 0.1f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "envamt", 1 }, "Filter Env Amount",
        NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fattack", 1 }, "Filter Attack", timeRange (10.0f), 0.01f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fdecay", 1 }, "Filter Decay", timeRange (10.0f), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fsustain", 1 }, "Filter Sustain",
        NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "frelease", 1 }, "Filter Release", timeRange (15.0f), 0.3f));

    // --- Amp envelope ---
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Amp Attack", timeRange (10.0f), 0.01f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Amp Decay", timeRange (10.0f), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sustain", 1 }, "Amp Sustain",
        NormalisableRange<float> (0.0f, 1.0f), 0.7f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Amp Release", timeRange (15.0f), 0.3f));

    // --- LFOs ---
    for (int i = 1; i <= kNumLfos; ++i)
    {
        const String n (i);
        const String name = "LFO " + n + " ";

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "wave", 1 }, name + "Wave",
            StringArray { "SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM", "USER WAVE" }, 0));

        NormalisableRange<float> rateRange (0.01f, 30.0f, 0.0f);
        rateRange.setSkewForCentre (2.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "rate", 1 }, name + "Rate", rateRange, 2.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "sync", 1 }, name + "Sync",
            StringArray { "FREE", "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                          "8/1T", "4/1T", "2/1T", "1/1T", "1/2T", "1/4T", "1/8T", "1/16T", "1/32T" }, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "amt", 1 }, name + "Amount",
            NormalisableRange<float> (-1.0f, 1.0f), 0.5f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "lfo" + n + "dest", 1 }, name + "Destination",
            StringArray { "NONE", "PITCH", "CUTOFF", "RESO", "VOLUME", "DETUNE", "PW", "SYNC FREQ",
                          "LFO 1 RATE", "LFO 2 RATE", "LFO 3 RATE", "LFO 4 RATE" }, 0));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "lfo" + n + "retrig", 1 }, name + "Retrig", true));

        // Fase inicial (en grados) cuando Retrig esta activado. 90 = el seno
        // empieza en su maximo, asi el LFO actua desde el primer instante.
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "phase", 1 }, name + "Phase",
            NormalisableRange<float> (0.0f, 360.0f), 90.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "lfo" + n + "fade", 1 }, name + "Fade In",
            NormalisableRange<float> (0.0f, 5.0f, 0.0f, 0.5f), 0.0f));

        // POLY activado = un LFO por voz (comportamiento anterior, por defecto).
        // POLY apagado = un unico LFO global compartido por todas las voces.
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "lfo" + n + "poly", 1 }, name + "Poly", true));
    }

    return layout;
}

void MiSinteProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    globalLfo.prepare (sampleRate);
    filterDisplay.sampleRate.store ((float) sampleRate);
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

    // Captura de acorde (boton CAPTURE): lee las notas entrantes antes de tocarlas
    chordCapture.process (midi, buffer.getNumSamples(), getSampleRate(), userChord);

    // LFOs globales: se calculan una vez por bloque, antes de que las voces los lean.
    // Con Retrig, la primera nota que llega cuando no suena ninguna voz reinicia su fase.
    int retrigSample = -1;
    bool anyVoiceSounding = false;
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (synth.getVoice (i)->isVoiceActive())
        {
            anyVoiceSounding = true;
            break;
        }
    }

    if (! anyVoiceSounding)
    {
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn())
            {
                retrigSample = metadata.samplePosition;
                break;
            }
        }
    }

    globalLfo.renderBlock (buffer.getNumSamples(), retrigSample);

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

    // Guarda las user waves cargadas en los LFO
    for (int i = 0; i < kNumLfos; ++i)
    {
        if (! lfoWaveLoaded[i])
            continue;

        std::vector<float> table ((size_t) kLfoTableSize);
        for (int j = 0; j < kLfoTableSize; ++j)
            table[(size_t) j] = lfoTables[i][(size_t) j].load();

        const juce::MemoryBlock mb (table.data(), sizeof (float) * (size_t) kLfoTableSize);
        state.setProperty (juce::Identifier ("lfoWave" + juce::String (i + 1)),
                           mb.toBase64Encoding(), nullptr);
        state.setProperty (juce::Identifier ("lfoWaveName" + juce::String (i + 1)),
                           lfoWaveName[i], nullptr);
    }

    state.setProperty ("userChord", userChord.toText(), nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MiSinteProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        apvts.replaceState (tree);

        // Recupera el acorde de usuario
        if (tree.hasProperty ("userChord"))
            userChord.setFromText (tree.getProperty ("userChord").toString());

        // Recupera las user waves de los LFO
        for (int i = 0; i < kNumLfos; ++i)
        {
            const juce::Identifier id ("lfoWave" + juce::String (i + 1));
            if (! tree.hasProperty (id))
                continue;

            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (tree.getProperty (id).toString())
                && mb.getSize() == sizeof (float) * (size_t) kLfoTableSize)
            {
                const auto* f = static_cast<const float*> (mb.getData());
                for (int j = 0; j < kLfoTableSize; ++j)
                    lfoTables[i][(size_t) j].store (f[j]);

                lfoWaveName[i] = tree.getProperty (juce::Identifier ("lfoWaveName" + juce::String (i + 1))).toString();
                lfoWaveLoaded[i] = true;
            }
        }
    }
}

juce::String MiSinteProcessor::getLfoWaveName (int index) const
{
    return (index >= 0 && index < kNumLfos && lfoWaveLoaded[index]) ? lfoWaveName[index] : juce::String();
}

// Carga un archivo de audio como onda de un ciclo para un LFO: el archivo completo
// se interpreta como un solo ciclo y se remuestrea a kLfoTableSize puntos.
bool MiSinteProcessor::loadLfoWave (int index, const juce::File& file)
{
    if (index < 0 || index >= kNumLfos)
        return false;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->numChannels < 1)
        return false;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 1 << 20);
    const int numCh = (int) reader->numChannels;

    juce::AudioBuffer<float> buf (numCh, len);
    if (! reader->read (&buf, 0, len, 0, true, numCh > 1))
        return false;

    // Mezcla a mono
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < numCh; ++c)
    {
        const float* src = buf.getReadPointer (c);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] += src[i] / (float) numCh;
    }

    // Remuestreo con interpolacion lineal (el ciclo se cierra sobre si mismo)
    std::vector<float> table ((size_t) kLfoTableSize);
    for (int i = 0; i < kLfoTableSize; ++i)
    {
        const double pos = (double) i * (double) len / (double) kLfoTableSize;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % len;
        const float fr = (float) (pos - (double) i0);
        table[(size_t) i] = mono[(size_t) i0] + (mono[(size_t) i1] - mono[(size_t) i0]) * fr;
    }

    // Normaliza al pico (rango -1 a 1)
    float peak = 0.0f;
    for (auto v : table)
        peak = juce::jmax (peak, std::abs (v));
    if (peak < 1.0e-6f)
        return false;

    for (int i = 0; i < kLfoTableSize; ++i)
        lfoTables[index][(size_t) i].store (table[(size_t) i] / peak);

    lfoWaveName[index] = file.getFileNameWithoutExtension();
    lfoWaveLoaded[index] = true;
    return true;
}

// Lee un archivo MIDI y usa su acorde como acorde de usuario. Se toman las notas que empiezan
// a la vez que la primera (con una tolerancia de 60 ms). Si hay menos de dos, se usan todas
// las notas distintas del archivo.
bool MiSinteProcessor::loadChordFromMidi (const juce::File& file)
{
    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return false;

    juce::MidiFile midiFile;
    if (! midiFile.readFrom (stream))
        return false;

    midiFile.convertTimestampTicksToSeconds();

    struct NoteEvent { double time; int note; };
    std::vector<NoteEvent> events;

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        if (const auto* track = midiFile.getTrack (t))
        {
            for (int e = 0; e < track->getNumEvents(); ++e)
            {
                const auto& msg = track->getEventPointer (e)->message;
                if (msg.isNoteOn())
                    events.push_back ({ msg.getTimeStamp(), msg.getNoteNumber() });
            }
        }
    }

    if (events.empty())
        return false;

    std::sort (events.begin(), events.end(),
               [] (const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });

    const double t0 = events.front().time;

    int together[128];
    int all[128];
    int nTogether = 0, nAll = 0;
    bool seenTogether[128] = {};
    bool seenAll[128] = {};

    for (const auto& ev : events)
    {
        if (ev.note < 0 || ev.note > 127)
            continue;

        if (! seenAll[ev.note])
        {
            seenAll[ev.note] = true;
            all[nAll++] = ev.note;
        }

        if (ev.time - t0 <= 0.06 && ! seenTogether[ev.note])
        {
            seenTogether[ev.note] = true;
            together[nTogether++] = ev.note;
        }
    }

    if (nTogether >= 2)
        userChord.setFromNotes (together, nTogether);
    else
        userChord.setFromNotes (all, nAll);

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiSinteProcessor();
}
