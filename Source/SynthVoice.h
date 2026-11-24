#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>

// Tamano de las tablas de onda de usuario de los LFO (potencia de 2)
constexpr int kLfoTableSize = 2048;
constexpr int kNumLfos = 4;

// Los LFOs (de voz y globales) se actualizan cada 16 muestras (tasa de control)
constexpr int kLfoControlRate = 16;

// Maximo de pasos de control que guarda el LFO global por bloque (2048 x 16 = 32768 muestras)
constexpr int kMaxGlobalSteps = 2048;

// Sync de los LFO: duracion de un ciclo en negras (0 = modo libre).
// FREE, 8/1, 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32,
// y los mismos en tresillo: 8/1T, 4/1T, 2/1T, 1/1T, 1/2T, 1/4T, 1/8T, 1/16T, 1/32T
inline constexpr int kNumLfoSyncs = 19;
inline constexpr double kLfoBeats[kNumLfoSyncs] =
{
    1.0,
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
    64.0 / 3.0, 32.0 / 3.0, 16.0 / 3.0, 8.0 / 3.0, 4.0 / 3.0,
    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0
};

// Destinos de LFO: 8..11 = LFO 1..4 RATE
inline constexpr int kLfoDestFirstRate = 8;

struct GlobalLfoBank;

// Punteros a los parametros (los lee la voz en cada bloque de audio)
struct VoiceParams
{
    // Oscilador
    std::atomic<float>* wave     = nullptr;
    std::atomic<float>* octave   = nullptr;
    std::atomic<float>* semi     = nullptr;
    std::atomic<float>* fine     = nullptr;   // cents
    std::atomic<float>* pw       = nullptr;   // ancho de pulso
    std::atomic<float>* sync     = nullptr;   // hard sync on/off
    std::atomic<float>* syncFreq = nullptr;   // semitonos sobre la frecuencia base
    std::atomic<float>* reset    = nullptr;   // reinicio de fase en cada nota
    std::atomic<float>* oscVol   = nullptr;   // volumen del oscilador VA antes del filtro

    // Envolvente de amplitud
    std::atomic<float>* attack  = nullptr;
    std::atomic<float>* decay   = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;

    // Filtro y su envolvente
    std::atomic<float>* filterType = nullptr;
    std::atomic<float>* cutoff     = nullptr;
    std::atomic<float>* resonance  = nullptr;
    std::atomic<float>* envAmount  = nullptr;
    std::atomic<float>* fAttack    = nullptr;
    std::atomic<float>* fDecay     = nullptr;
    std::atomic<float>* fSustain   = nullptr;
    std::atomic<float>* fRelease   = nullptr;

    // Unison, drift y glide
    std::atomic<float>* unison     = nullptr;
    std::atomic<float>* unisonMode = nullptr;
    std::atomic<float>* detune     = nullptr;   // cents
    std::atomic<float>* stereo     = nullptr;   // spread
    std::atomic<float>* tilt       = nullptr;   // pendiente de armonicos (modo HARMONIC)
    std::atomic<float>* drift      = nullptr;
    std::atomic<float>* glide      = nullptr;   // segundos

    // LFOs
    std::atomic<float>* lfoWave[kNumLfos]       {};
    std::atomic<float>* lfoRate[kNumLfos]       {};   // Hz (modo libre)
    std::atomic<float>* lfoSync[kNumLfos]       {};   // 0 = libre, resto = divisiones
    std::atomic<float>* lfoAmt[kNumLfos]        {};
    std::atomic<float>* lfoDest[kNumLfos]       {};
    std::atomic<float>* lfoRetrig[kNumLfos]     {};
    std::atomic<float>* lfoFade[kNumLfos]       {};
    std::atomic<float>* lfoPhaseStart[kNumLfos] {};   // grados
    std::atomic<float>* lfoPoly[kNumLfos]       {};   // POLY: 1 = un LFO por voz, 0 = LFO global
    const std::atomic<float>* lfoTable[kNumLfos] {};  // onda de usuario de cada LFO

    // Tempo del host (para el sync de los LFO)
    std::atomic<float>* bpm = nullptr;

    // LFOs globales (los que tienen POLY apagado): los calcula el procesador y todas las voces los leen
    const GlobalLfoBank* globalLfo = nullptr;

    // Ultima nota tocada (compartida por todas las voces, para el glide)
    std::atomic<float>* lastNote = nullptr;

    // Display del filtro: la nota mas reciente publica su cutoff y reso efectivos
    std::atomic<float>* dispCutoff  = nullptr;
    std::atomic<float>* dispReso    = nullptr;
    std::atomic<int>*   dispWrites  = nullptr;
    std::atomic<int>*   noteCounter = nullptr;

    // Acorde de usuario (modo CHORD): intervalos en semitonos y numero de notas
    const std::atomic<int>* chordIntervals = nullptr;
    const std::atomic<int>* chordCount     = nullptr;
};

// Lee la onda de usuario de un LFO (tabla de un ciclo, con interpolacion)
inline double readLfoTable (const std::atomic<float>* t, double ph)
{
    if (t == nullptr)
        return 0.0;

    const double pos = ph * (double) kLfoTableSize;
    const int i0 = (int) pos & (kLfoTableSize - 1);
    const int i1 = (i0 + 1) & (kLfoTableSize - 1);
    const double fr = pos - std::floor (pos);
    const double a = (double) t[i0].load (std::memory_order_relaxed);
    const double b = (double) t[i1].load (std::memory_order_relaxed);
    return a + (b - a) * fr;
}

// Forma de onda de un LFO (compartida por los LFOs de voz y los globales).
// ph entre 0 y 1; hold es el valor actual del RANDOM (sample & hold).
inline double lfoShape (int wave, double ph, double hold, const std::atomic<float>* table)
{
    switch (wave)
    {
        case 0:  return std::sin (juce::MathConstants<double>::twoPi * ph);
        case 1:  return ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0);   // triangulo (sube desde 0)
        case 2:  return 2.0 * ph - 1.0;
        case 3:  return ph < 0.5 ? 1.0 : -1.0;
        case 5:  return readLfoTable (table, ph);   // onda de usuario
        default: return hold;                        // random (sample & hold)
    }
}

// LFOs globales (POLY apagado). El procesador los avanza una vez por bloque de audio y guarda
// su valor cada 16 muestras; todas las voces leen el mismo valor para la misma posicion del bloque,
// asi todo un acorde se modula al unisono, sin desfases entre notas.
//
//  - Retrig: solo reinicia la fase (a PHASE) al tocar la primera nota cuando no sonaba ninguna voz.
//  - Fade In: sigue siendo por nota (lo aplica cada voz sobre la profundidad de la modulacion).
//  - Rate: un LFO global solo recibe modulacion de RATE de otros LFOs globales (un LFO global
//    tiene una unica velocidad; un LFO POLY tiene un valor distinto en cada voz).
struct GlobalLfoBank
{
    void setParams (const VoiceParams& p) { params = p; }

    void prepare (double sr)
    {
        if (sr > 0.0)
            sampleRate = sr;

        for (int l = 0; l < kNumLfos; ++l)
        {
            phase[l] = 0.25;
            hold[l] = 0.0;
            rateOct[l] = 0.0;
            values[l].fill (0.0f);
        }
        numSteps = 1;
    }

    // Calcula los valores de los LFOs globales para un bloque de numSamples muestras.
    // retrigSample: posicion de la primera nota del bloque si no sonaba ninguna voz (o -1).
    void renderBlock (int numSamples, int retrigSample)
    {
        if (numSamples <= 0)
            return;

        const double hostBpm = (double) params.bpm->load();
        const double bpm = hostBpm > 1.0 ? hostBpm : 120.0;

        struct Cfg { bool global; bool retrig; int wave, sync, dest; double rate, amt, startPhase; };
        Cfg cfg[kNumLfos];
        for (int l = 0; l < kNumLfos; ++l)
        {
            cfg[l].global     = params.lfoPoly[l]->load() < 0.5f;
            cfg[l].retrig     = params.lfoRetrig[l]->load() > 0.5f;
            cfg[l].wave       = (int) params.lfoWave[l]->load();
            cfg[l].sync       = juce::jlimit (0, kNumLfoSyncs - 1, (int) params.lfoSync[l]->load());
            cfg[l].dest       = (int) params.lfoDest[l]->load();
            cfg[l].rate       = (double) params.lfoRate[l]->load();
            cfg[l].amt        = (double) params.lfoAmt[l]->load();
            cfg[l].startPhase = (double) params.lfoPhaseStart[l]->load() / 360.0;
            cfg[l].startPhase -= std::floor (cfg[l].startPhase);
        }

        const int steps = juce::jmin (kMaxGlobalSteps, (numSamples + kLfoControlRate - 1) / kLfoControlRate);
        const int retrigStep = (retrigSample >= 0) ? retrigSample / kLfoControlRate : -1;

        for (int s = 0; s < steps; ++s)
        {
            // Muestras que cubre este paso (el ultimo cubre lo que quede del bloque)
            const int remaining = numSamples - s * kLfoControlRate;
            const int span = (s == steps - 1) ? remaining : juce::jmin (kLfoControlRate, remaining);
            const double dtSec = (double) span / sampleRate;

            double rateNew[kNumLfos] = {};

            for (int l = 0; l < kNumLfos; ++l)
            {
                const auto& c = cfg[l];
                if (! c.global)
                    continue;

                if (s == retrigStep && c.retrig)
                {
                    phase[l] = c.startPhase;
                    hold[l] = (double) random.nextFloat() * 2.0 - 1.0;
                }

                const double v = lfoShape (c.wave, phase[l], hold[l], params.lfoTable[l]);
                values[l][(size_t) s] = (float) v;

                double hz = c.rate;
                if (c.sync > 0)
                    hz = (bpm / 60.0) / kLfoBeats[c.sync];
                hz *= std::exp2 (rateOct[l]);   // modulacion de Rate por otros LFOs globales

                phase[l] += hz * dtSec;
                if (phase[l] >= 1.0)
                {
                    phase[l] -= std::floor (phase[l]);
                    hold[l] = (double) random.nextFloat() * 2.0 - 1.0;
                }

                if (c.dest >= kLfoDestFirstRate && c.dest < kLfoDestFirstRate + kNumLfos)
                    rateNew[c.dest - kLfoDestFirstRate] += v * c.amt * 3.0;   // +/- 3 octavas
            }

            for (int l = 0; l < kNumLfos; ++l)
                rateOct[l] = rateNew[l];
        }

        numSteps = steps;
    }

    // Valor (entre -1 y 1, sin Amount ni Fade) del LFO l en la posicion de muestra dada del bloque actual
    float getValue (int l, int samplePos) const
    {
        const int idx = juce::jlimit (0, numSteps - 1, samplePos / kLfoControlRate);
        return values[l][(size_t) idx];
    }

private:
    VoiceParams params;
    double sampleRate = 44100.0;
    double phase[kNumLfos] {};
    double hold[kNumLfos] {};
    double rateOct[kNumLfos] {};
    int numSteps = 1;
    juce::Random random;
    std::array<float, kMaxGlobalSteps> values[kNumLfos] {};
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Envolvente ADSR propia (usada para AMP ENV y FILTER ENV).
//
// Sustituye a juce::ADSR, que tenia este fallo: setParameters() recalcula la velocidad
// de release a partir del SUSTAIN (releaseRate = sustain / (release * sampleRate)) y
// pisa la que noteOff() acababa de calcular desde el nivel actual. Como el motor llama a
// setParameters() en cada bloque de audio, con Sustain = 0 la velocidad de release pasaba
// a ser 0 y la envolvente saltaba a "idle" al instante (corte en seco). Con Sustain > 0
// el release tampoco partia del nivel real, sino de una velocidad calculada con el sustain.
//
// Esta version:
//  - En Note Off, el Release parte exactamente del nivel actual (Attack, Decay o Sustain).
//  - Decay y Release son exponenciales y terminan al bajar del umbral de silencio (0.0001).
//  - setParameters() solo recalcula coeficientes: nunca toca el estado ni el nivel, asi
//    que se puede llamar en cada bloque sin cortar un Release en curso.
//  - Un noteOn arranca el Attack desde el nivel actual (sin saltos ni clics).
class Envelope
{
public:
    void setSampleRate (double newRate)
    {
        if (newRate > 0.0)
        {
            sampleRate = newRate;
            recalculate();
        }
    }

    void setParameters (float attackSec, float decaySec, float sustainLevel, float releaseSec)
    {
        attack = attackSec;
        decay = decaySec;
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        release = releaseSec;
        recalculate();
    }

    void noteOn()  { state = State::attack; }

    void noteOff()
    {
        // El nivel actual se conserva: el Release parte de aqui
        if (state != State::idle)
            state = State::release;
    }

    void reset()
    {
        state = State::idle;
        level = 0.0f;
    }

    bool isActive() const { return state != State::idle; }

    float getNextSample()
    {
        switch (state)
        {
            case State::idle:
                return 0.0f;

            case State::attack:
                level += attackInc;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    state = State::decay;
                }
                break;

            case State::decay:
                // Exponencial hacia el nivel de sustain
                level = sustain + (level - sustain) * decayCoef;
                if (std::abs (level - sustain) <= kSilence)
                {
                    level = sustain;
                    state = State::sustain;
                }
                break;

            case State::sustain:
                // Sigue suavemente los cambios del knob SUSTAIN (sin saltos)
                level += (sustain - level) * sustainSmooth;
                break;

            case State::release:
                // Exponencial desde el nivel actual hasta el umbral de silencio
                level *= releaseCoef;
                if (level <= kSilence)
                {
                    level = 0.0f;
                    state = State::idle;
                }
                break;
        }

        return level;
    }

private:
    enum class State { idle, attack, decay, sustain, release };

    static constexpr float kSilence = 0.0001f;   // umbral de silencio (-80 dB)

    void recalculate()
    {
        const double lnSilence = std::log ((double) kSilence);   // negativo

        // Los tiempos de Decay y Release son lo que tarda la curva en llegar al silencio
        attackInc     = (float) (1.0 / ((double) juce::jmax (attack, 0.0005f) * sampleRate));
        decayCoef     = (float) std::exp (lnSilence / ((double) juce::jmax (decay, 0.0005f) * sampleRate));
        releaseCoef   = (float) std::exp (lnSilence / ((double) juce::jmax (release, 0.0005f) * sampleRate));
        sustainSmooth = (float) (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));   // ~5 ms
    }

    State state = State::idle;
    double sampleRate = 44100.0;
    float level = 0.0f;

    float attack = 0.01f, decay = 0.3f, sustain = 0.7f, release = 0.3f;
    float attackInc = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f, sustainSmooth = 0.0f;
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int kMaxUnison = 16;

    explicit SynthVoice (const VoiceParams& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate > 0.0)
        {
            adsr.setSampleRate (newRate);
            filterAdsr.setSampleRate (newRate);
        }
    }

    void startNote (int midiNote, float velocity,
                    juce::SynthesiserSound*, int pitchWheelPos) override
    {
        targetNote = (double) midiNote;
        bendSemitones = wheelToSemitones (pitchWheelPos);
        level = velocity * 0.2f;

        // Glide: parte de la ultima nota tocada
        const double glideTime = (double) params.glide->load();
        const double last = (double) params.lastNote->load();
        glideNote = (glideTime > 0.0005 && last >= 0.0) ? last : targetNote;
        params.lastNote->store ((float) midiNote);
        noteId = params.noteCounter->fetch_add (1, std::memory_order_relaxed) + 1;

        // Fase inicial: aleatoria en unison (sonido mas grueso) o 0 si RESET esta activo
        const int n = juce::jlimit (1, kMaxUnison, (int) params.unison->load());
        const bool resetPhase = params.reset->load() > 0.5f;
        for (int u = 0; u < kMaxUnison; ++u)
        {
            phase[u] = (n > 1 && ! resetPhase) ? (double) random.nextFloat() : 0.0;
            masterPhase[u] = phase[u];
            syncDelta[u] = 0.0;
            driftVal[u] = driftTarget[u] = random.nextFloat() * 2.0f - 1.0f;
            driftTimer[u] = 0;
            dt[u] = 0.0;
            gainL[u] = gainR[u] = 0.0f;
        }

        // LFOs: con Retrig la fase empieza en el valor de Phase en cada nota.
        // (Solo la usan los LFOs en modo POLY; los globales leen el LFO del procesador,
        // pero el Fade In de la nota cuenta siempre desde aqui.)
        for (int l = 0; l < kNumLfos; ++l)
        {
            lfoPhase[l] = (params.lfoRetrig[l]->load() > 0.5f)
                              ? (double) params.lfoPhaseStart[l]->load() / 360.0
                              : (double) random.nextFloat();
            lfoHold[l] = (double) random.nextFloat() * 2.0 - 1.0;
            lfoRateOct[l] = 0.0;
        }
        lfoAge = 0.0;
        lfoPitch = lfoCut = lfoDetune = lfoPw = lfoReso = lfoSyncSemis = 0.0;
        lfoVol = 1.0f;

        sL1 = sL2 = sR1 = sR2 = 0.0;
        coefCounter = 0;
        updateEnvelopes();
        adsr.noteOn();
        filterAdsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
            filterAdsr.noteOff();
        }
        else
        {
            adsr.reset();
            filterAdsr.reset();
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

        updateEnvelopes();

        const double sr = getSampleRate();
        const int waveType = (int) params.wave->load();
        const int filterType = (int) params.filterType->load();
        const double cutoff = (double) params.cutoff->load();
        const double amount = (double) params.envAmount->load();
        const double resoBase = (double) params.resonance->load();
        const double pwBase = (double) params.pw->load();
        const bool syncOn = params.sync->load() > 0.5f;
        const double syncBase = (double) params.syncFreq->load();
        const double tilt = (double) params.tilt->load();
        const double oscVol = params.oscVol != nullptr
                                ? (double) params.oscVol->load (std::memory_order_relaxed)
                                : 1.0;

        const int n = juce::jlimit (1, kMaxUnison, (int) params.unison->load());
        const int mode = juce::jlimit (0, kNumModes - 1, (int) params.unisonMode->load());
        const double detuneCents = (double) params.detune->load();
        const double stereoAmt = (double) params.stereo->load();
        const double driftAmt = (double) params.drift->load();
        const double glideTime = (double) params.glide->load();

        // Afinacion del oscilador: octava, semitono y fine (en semitonos)
        tuneOffset = (double) params.octave->load() * 12.0
                   + (double) params.semi->load()
                   + (double) params.fine->load() / 100.0;

        // Configuracion de los LFO
        const double hostBpm = (double) params.bpm->load();
        bpm = hostBpm > 1.0 ? hostBpm : 120.0;
        for (int l = 0; l < kNumLfos; ++l)
        {
            auto& c = lfo[l];
            c.wave = (int) params.lfoWave[l]->load();
            c.sync = juce::jlimit (0, kNumLfoSyncs - 1, (int) params.lfoSync[l]->load());
            c.dest = (int) params.lfoDest[l]->load();
            c.rate = (double) params.lfoRate[l]->load();
            c.amt  = (double) params.lfoAmt[l]->load();
            c.fade = (double) params.lfoFade[l]->load();
            c.poly = params.lfoPoly[l]->load() > 0.5f;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            const float fenv = filterAdsr.getNextSample();

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            // LFOs, pitch, sync, glide, drift, panning y filtro se recalculan cada 16 muestras
            if (coefCounter-- <= 0)
            {
                coefCounter = 15;
                updateLfos (sr, startSample + i);

                pwNow = juce::jlimit (0.05, 0.95, pwBase + lfoPw);
                syncRatio = std::exp2 (juce::jlimit (0.0, 36.0, syncBase + lfoSyncSemis) / 12.0);

                const double resoEff = juce::jlimit (0.0, 1.0, resoBase + lfoReso);
                k = 1.0 / (0.5 * std::pow (40.0, resoEff));

                controlUpdate (sr, n, mode, detuneCents, stereoAmt, driftAmt, glideTime, tilt);

                const double mod = std::pow (2.0, amount * 7.0 * (double) fenv + lfoCut);
                const double fc = juce::jlimit (20.0, 0.45 * sr, cutoff * mod);
                g = std::tan (juce::MathConstants<double>::pi * fc / sr);
                h = 1.0 / (1.0 + g * (g + k));

                // Publica el cutoff y la reso efectivos para el display del filtro
                // (solo la nota mas reciente; son escrituras atomicas, sin bloqueos)
                if (noteId == params.noteCounter->load (std::memory_order_relaxed))
                {
                    params.dispCutoff->store ((float) fc, std::memory_order_relaxed);
                    params.dispReso->store ((float) resoEff, std::memory_order_relaxed);
                    params.dispWrites->fetch_add (1, std::memory_order_relaxed);
                }
            }

            double sumL = 0.0, sumR = 0.0;
            for (int u = 0; u < n; ++u)
            {
                const double dtM = dt[u];
                if (dtM <= 0.0)
                    continue;   // parcial silenciado (por encima de la frecuencia maxima)

                double o;

                if (! syncOn)
                {
                    o = (double) oscillator (waveType, (float) phase[u], (float) dtM, (float) pwNow);
                    phase[u] = frac (phase[u] + dtM);
                    masterPhase[u] = phase[u];
                }
                else
                {
                    // Hard sync: el oscilador maestro reinicia la fase del esclavo
                    const double dtS = juce::jmin (0.49, dtM * syncRatio);
                    const double mph = masterPhase[u];

                    o = (double) oscillator (waveType, (float) phase[u], (float) dtS, (float) pwNow);

                    // Fase del esclavo en el instante del reinicio (si ocurre cerca de esta muestra)
                    const double toWrap = (1.0 - mph) / dtM;
                    const double slaveAtWrap = frac (phase[u] + dtS * toWrap);

                    // Suaviza el salto del reinicio con PolyBLEP (antialiasing)
                    if (mph > 1.0 - dtM)
                    {
                        const double delta = (double) naiveShape (waveType, 0.0f, (float) pwNow)
                                           - (double) naiveShape (waveType, (float) slaveAtWrap, (float) pwNow);
                        o += 0.5 * delta * (double) polyBlep ((float) mph, (float) dtM);
                    }
                    else if (mph < dtM)
                    {
                        o += 0.5 * syncDelta[u] * (double) polyBlep ((float) mph, (float) dtM);
                    }

                    phase[u] = frac (phase[u] + dtS);
                    double mnext = mph + dtM;
                    if (mnext >= 1.0)
                    {
                        const double over = mnext - 1.0;
                        syncDelta[u] = (double) naiveShape (waveType, 0.0f, (float) pwNow)
                                     - (double) naiveShape (waveType, (float) slaveAtWrap, (float) pwNow);
                        phase[u] = frac (over / dtM * dtS);
                        mnext = over;
                    }
                    masterPhase[u] = mnext;
                }

                sumL += o * (double) gainL[u];
                sumR += o * (double) gainR[u];
            }

            const float amp = env * level * lfoVol;
            // VOL actua sobre la senal del oscilador antes de entrar en el filtro.
            const float outL = (float) processFilter (sumL * oscVol, filterType, sL1, sL2) * amp;
            const float outR = (float) processFilter (sumR * oscVol, filterType, sR1, sR2) * amp;

            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample (c, startSample + i, c == 0 ? outL : outR);
        }
    }

private:
    // Modos de unison: 0 NORMAL, 1 OCTAVES, 2 FIFTH, 3 MAJOR, 4 MINOR, 5 MAJOR 7,
    //                  6 MINOR 7, 7 SUB, 8 HARMONIC, 9 CHORD (USER)
    static constexpr int kNumModes = 10;
    static constexpr int kModeSub = 7;
    static constexpr int kModeHarmonic = 8;
    static constexpr int kModeChord = 9;

    // Intervalos (en semitonos) de cada voz del unison segun el modo.
    // SUB y HARMONIC se calculan aparte.
    static constexpr int kIntervals[kNumModes][kMaxUnison] =
    {
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 },
        {  0, 12, -12, 24,   0, 12, -12, 24,   0, 12, -12, 24,   0, 12, -12, 24 },
        {  0,  7,  12, 19,   0,  7,  12, 19,   0,  7,  12, 19,   0,  7,  12, 19 },
        {  0,  4,   7, 12,  16, 19,  24, 28,   0,  4,   7, 12,  16, 19,  24, 28 },
        {  0,  3,   7, 12,  15, 19,  24, 27,   0,  3,   7, 12,  15, 19,  24, 27 },
        {  0,  4,   7, 11,  12, 16,  19, 23,   0,  4,   7, 11,  12, 16,  19, 23 },
        {  0,  3,   7, 10,  12, 15,  19, 22,   0,  3,   7, 10,  12, 15,  19, 22 },
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 },
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 },
        {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0 }
    };

    // Destinos de los LFO
    static constexpr int kDestPitch    = 1;
    static constexpr int kDestCutoff   = 2;
    static constexpr int kDestReso     = 3;
    static constexpr int kDestVolume   = 4;
    static constexpr int kDestDetune   = 5;
    static constexpr int kDestPW       = 6;
    static constexpr int kDestSyncFreq = 7;
    static constexpr int kDestLfoRate  = kLfoDestFirstRate;   // 8..11 = LFO 1..4 RATE

    static double frac (double x) { return x - std::floor (x); }

    // Avanza los LFO y calcula su efecto sobre cada destino.
    // samplePos: posicion de esta actualizacion dentro del bloque de audio (para leer los LFOs globales).
    void updateLfos (double sr, int samplePos)
    {
        lfoAge += 16.0 / sr;

        double pitch = 0.0, cut = 0.0, vol = 0.0, det = 0.0, pwMod = 0.0;
        double reso = 0.0, syncSt = 0.0;
        double rateNew[kNumLfos] = {};

        for (int l = 0; l < kNumLfos; ++l)
        {
            const auto& c = lfo[l];
            double v;

            if (c.poly)
            {
                // POLY: LFO propio de esta voz
                double hz = c.rate;
                if (c.sync > 0)
                    hz = (bpm / 60.0) / kLfoBeats[c.sync];
                hz *= std::exp2 (lfoRateOct[l]);   // modulacion de Rate por otros LFOs

                lfoPhase[l] += hz * 16.0 / sr;
                if (lfoPhase[l] >= 1.0)
                {
                    lfoPhase[l] -= std::floor (lfoPhase[l]);
                    lfoHold[l] = (double) random.nextFloat() * 2.0 - 1.0;
                }

                v = lfoShape (c.wave, lfoPhase[l], lfoHold[l], params.lfoTable[l]);
            }
            else
            {
                // GLOBAL: el mismo valor para todas las voces (lo calcula el procesador)
                v = (params.globalLfo != nullptr) ? (double) params.globalLfo->getValue (l, samplePos) : 0.0;
            }

            if (c.fade > 0.0005)
                v *= juce::jmin (1.0, lfoAge / c.fade);

            switch (c.dest)
            {
                case kDestPitch:    pitch  += v * c.amt * std::abs (c.amt) * 12.0; break;
                case kDestCutoff:   cut    += v * c.amt * 5.0; break;
                case kDestReso:     reso   += v * c.amt; break;
                case kDestVolume:   vol    += v * c.amt; break;
                case kDestDetune:   det    += v * c.amt * 300.0; break;
                case kDestPW:       pwMod  += v * c.amt * 0.45; break;
                case kDestSyncFreq: syncSt += v * c.amt * 36.0; break;
                default:
                    if (c.dest >= kDestLfoRate && c.dest < kDestLfoRate + kNumLfos)
                        rateNew[c.dest - kDestLfoRate] += v * c.amt * 3.0;   // +/- 3 octavas
                    break;
            }
        }

        lfoPitch = pitch;
        lfoCut = cut;
        lfoReso = reso;
        lfoDetune = det;
        lfoPw = pwMod;
        lfoSyncSemis = syncSt;
        for (int l = 0; l < kNumLfos; ++l)
            lfoRateOct[l] = rateNew[l];
        lfoVol = (float) juce::jlimit (0.0, 2.0, 1.0 + vol);
    }

    void controlUpdate (double sr, int n, int mode, double detuneCents,
                        double stereoAmt, double driftAmt, double glideTime, double tilt)
    {
        // Glide (exponencial: llega al ~95% en el tiempo indicado)
        if (glideTime > 0.0005)
        {
            const double tau = glideTime / 3.0;
            const double coef = 1.0 - std::exp (-16.0 / (sr * tau));
            glideNote += (targetNote - glideNote) * coef;
        }
        else
        {
            glideNote = targetNote;
        }

        const double baseHz = 440.0 * std::exp2 ((glideNote - 69.0) / 12.0);
        const double driftCoef = 16.0 / (0.5 * sr);
        const double detuneEff = juce::jmax (0.0, detuneCents + lfoDetune);
        const bool harmonic = (mode == kModeHarmonic);

        double weight[kMaxUnison];
        double panArr[kMaxUnison];
        double sumW2 = 0.0;

        for (int u = 0; u < n; ++u)
        {
            // Drift: deriva lenta y aleatoria de cada oscilador (hasta +/- 20 cents)
            if (driftTimer[u]-- <= 0)
            {
                driftTarget[u] = random.nextFloat() * 2.0f - 1.0f;
                driftTimer[u] = (int) ((0.5 + (double) random.nextFloat()) * sr / 16.0);
            }
            driftVal[u] += (driftTarget[u] - driftVal[u]) * (float) driftCoef;

            // Posicion de la voz entre -1 y 1
            const double pos = (n > 1) ? 2.0 * (double) u / (double) (n - 1) - 1.0 : 0.0;
            const double cents = pos * detuneEff + (double) driftVal[u] * driftAmt * 20.0;

            // Intervalo de la voz
            double interval = (double) kIntervals[mode][u];
            if (mode == kModeSub && n >= 2 && (u % 3) == 1)
                interval = -12.0;                              // una de cada tres baja una octava
            else if (harmonic)
                interval = 12.0 * std::log2 ((double) (u + 1)); // parcial u+1 de la serie armonica
            else if (mode == kModeChord)
            {
                // Acorde de usuario: voz u = nota (u mod numero de notas) del acorde.
                // Con mas voces que notas el acorde se repite; con menos suenan las mas graves.
                const int cnt = juce::jlimit (1, kMaxUnison, params.chordCount->load (std::memory_order_relaxed));
                interval = (double) params.chordIntervals[u % cnt].load (std::memory_order_relaxed);
            }

            const double semis = (double) bendSemitones + tuneOffset + lfoPitch
                               + interval + cents / 100.0;

            const double dtRaw = baseHz * std::exp2 (semis / 12.0) / sr;

            if (harmonic && dtRaw > 0.45)
            {
                // Parcial por encima de la frecuencia maxima audible: se silencia
                dt[u] = 0.0;
                weight[u] = 0.0;
            }
            else
            {
                dt[u] = juce::jmin (0.49, dtRaw);
                weight[u] = harmonic ? std::pow (1.0 / (double) (u + 1), tilt) : 1.0;
            }

            sumW2 += weight[u] * weight[u];
            panArr[u] = pos * stereoAmt;
        }

        // Normalizacion: misma potencia total sea cual sea el modo
        const double norm = (sumW2 > 1.0e-9) ? 1.4142135 / std::sqrt (sumW2) : 0.0;

        for (int u = 0; u < n; ++u)
        {
            const double panPos = juce::jlimit (-1.0, 1.0, panArr[u]);
            const double angle = (panPos + 1.0) * juce::MathConstants<double>::pi / 4.0;
            gainL[u] = (float) (std::cos (angle) * norm * weight[u]);
            gainR[u] = (float) (std::sin (angle) * norm * weight[u]);
        }
    }

    static float wheelToSemitones (int wheel)
    {
        // Rango del pitch bend: +/- 2 semitonos
        return ((float) wheel - 8192.0f) / 8192.0f * 2.0f;
    }

    // Filtro de variable de estado (TPT). Salidas: 0 paso bajo, 1 paso alto,
    // 2 paso banda, 3 notch
    double processFilter (double x, int type, double& st1, double& st2)
    {
        const double hp = (x - (k + g) * st1 - st2) * h;
        const double bp = g * hp + st1;
        const double lp = g * bp + st2;
        st1 = g * hp + bp;
        st2 = g * bp + lp;

        switch (type)
        {
            case 0:  return lp;
            case 1:  return hp;
            case 2:  return k * bp;
            default: return hp + lp;
        }
    }

    // Correccion PolyBLEP: reduce el aliasing en sierra, cuadrada y en el reinicio del sync
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

    // Forma de onda "cruda" (sin antialiasing), usada para calcular saltos del sync
    static float naiveShape (int type, float ph, float pw)
    {
        switch (type)
        {
            case 0:  return std::sin (ph * juce::MathConstants<float>::twoPi);
            case 1:  return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
            case 2:  return 2.0f * ph - 1.0f;
            default: return ph < pw ? 1.0f : -1.0f;
        }
    }

    static float oscillator (int type, float ph, float dt, float pw)
    {
        switch (type)
        {
            case 0: // Seno
                return std::sin (ph * juce::MathConstants<float>::twoPi);

            case 1: // Triangulo: arranca en 0, sube a +1, baja a -1 y vuelve a 0
                return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);

            case 2: // Sierra
                return 2.0f * ph - 1.0f - polyBlep (ph, dt);

            default: // Cuadrada / pulso con ancho variable (pw entre 0.05 y 0.95)
            {
                float v = ph < pw ? 1.0f : -1.0f;
                v += polyBlep (ph, dt);
                float t2 = ph - pw;
                if (t2 < 0.0f)
                    t2 += 1.0f;
                v -= polyBlep (t2, dt);
                return v - (2.0f * pw - 1.0f);   // quita el offset de continua
            }
        }
    }

    void updateEnvelopes()
    {
        adsr.setParameters (params.attack->load(),
                            params.decay->load(),
                            params.sustain->load(),
                            params.release->load());

        filterAdsr.setParameters (params.fAttack->load(),
                                  params.fDecay->load(),
                                  params.fSustain->load(),
                                  params.fRelease->load());
    }

    struct LfoCfg
    {
        int wave = 0, sync = 0, dest = 0;
        double rate = 1.0, amt = 0.0, fade = 0.0;
        bool poly = true;
    };

    VoiceParams params;
    Envelope adsr;         // AMP ENV
    Envelope filterAdsr;   // FILTER ENV
    juce::Random random;

    // Osciladores del unison
    double phase[kMaxUnison] {};          // fase del oscilador (esclavo si hay sync)
    double masterPhase[kMaxUnison] {};    // fase del maestro (solo con sync)
    double syncDelta[kMaxUnison] {};      // ultimo salto provocado por el sync
    double dt[kMaxUnison] {};
    float gainL[kMaxUnison] {};
    float gainR[kMaxUnison] {};
    float driftVal[kMaxUnison] {};
    float driftTarget[kMaxUnison] {};
    int driftTimer[kMaxUnison] {};

    // LFOs
    LfoCfg lfo[kNumLfos];
    double lfoPhase[kNumLfos] {};
    double lfoHold[kNumLfos] {};
    double lfoRateOct[kNumLfos] {};
    double lfoAge = 0.0;
    double lfoPitch = 0.0, lfoCut = 0.0, lfoDetune = 0.0, lfoPw = 0.0;
    double lfoReso = 0.0, lfoSyncSemis = 0.0;
    float lfoVol = 1.0f;
    double bpm = 120.0;

    double pwNow = 0.5;
    double syncRatio = 1.0;
    double tuneOffset = 0.0;
    double targetNote = 60.0;
    double glideNote = 60.0;
    float bendSemitones = 0.0f;
    float level = 0.0f;

    // Estado del filtro (izquierda y derecha)
    double sL1 = 0.0, sL2 = 0.0, sR1 = 0.0, sR2 = 0.0;
    double g = 0.0, h = 1.0, k = 1.0;
    int coefCounter = 0;
    int noteId = 0;
};
