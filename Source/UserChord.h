#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// Acorde de usuario para el modo CHORD (USER) del unison.
// Guarda hasta 16 intervalos (en semitonos) respecto a la nota mas grave del acorde.
// Todo son atomicos: el hilo de audio lo lee y lo escribe sin bloqueos ni reservas de memoria.
class UserChord
{
public:
    static constexpr int kMax = 16;

    UserChord()
    {
        // Por defecto: acorde mayor
        const int major[] = { 0, 4, 7 };
        for (int i = 0; i < kMax; ++i)
            intervals[(size_t) i].store (i < 3 ? major[i] : 0);
        count.store (3);
    }

    // Recibe numeros de nota MIDI (o intervalos). Los ordena, quita repetidos y los
    // normaliza respecto a la nota mas grave. Si hay mas de 16, se queda con las mas graves.
    void setFromNotes (const int* notes, int n)
    {
        bool present[128] = {};
        for (int i = 0; i < n; ++i)
            if (notes[i] >= 0 && notes[i] < 128)
                present[notes[i]] = true;

        int sorted[kMax];
        int cnt = 0;
        int root = -1;
        for (int v = 0; v < 128 && cnt < kMax; ++v)
        {
            if (! present[v])
                continue;
            if (root < 0)
                root = v;
            sorted[cnt++] = v - root;
        }

        if (cnt == 0)
            return;

        for (int i = 0; i < cnt; ++i)
            intervals[(size_t) i].store (sorted[i]);
        count.store (cnt);
        version.fetch_add (1);
    }

    // Texto con los intervalos separados por espacios, por ejemplo "0 4 7 11"
    void setFromText (const juce::String& text)
    {
        juce::StringArray tokens;
        tokens.addTokens (text, " ", "");

        int notes[128];
        int n = 0;
        for (const auto& t : tokens)
            if (t.isNotEmpty() && n < 128)
                notes[n++] = t.getIntValue();

        setFromNotes (notes, n);
    }

    juce::String toText() const
    {
        juce::String s;
        const int n = count.load();
        for (int i = 0; i < n; ++i)
        {
            if (i > 0)
                s << " ";
            s << intervals[(size_t) i].load();
        }
        return s;
    }

    int getVersion() const { return version.load(); }
    const std::atomic<int>* intervalsData() const { return intervals.data(); }
    const std::atomic<int>* countPtr() const { return &count; }

private:
    std::array<std::atomic<int>, kMax> intervals;
    std::atomic<int> count { 3 };
    std::atomic<int> version { 0 };
};

// Captura el acorde que se toca en el teclado. Al pulsar CAPTURE se "arma"; la primera nota
// que llega abre una ventana de unos 350 ms y las notas que entren en ella forman el acorde.
// Las notas suenan con normalidad mientras se captura.
class ChordCapture
{
public:
    void arm()    { armed.store (true); }
    void cancel() { armed.store (false); }
    bool isArmed() const { return armed.load(); }

    // Hilo de audio: se llama en cada bloque, antes de renderizar las voces
    void process (const juce::MidiBuffer& midi, int numSamples, double sampleRate, UserChord& chord)
    {
        if (! armed.load())
        {
            wasArmed = false;
            return;
        }

        if (! wasArmed)
        {
            wasArmed = true;
            windowOpen = false;
            count = 0;
            elapsed = 0;
        }

        bool openedThisBlock = false;

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (! msg.isNoteOn())
                continue;

            if (! windowOpen)
            {
                windowOpen = true;
                openedThisBlock = true;
                elapsed = (juce::int64) (numSamples - metadata.samplePosition);
                count = 0;
            }

            if (count < UserChord::kMax)
                notes[count++] = msg.getNoteNumber();
        }

        if (windowOpen && ! openedThisBlock)
            elapsed += (juce::int64) numSamples;

        if (windowOpen && elapsed >= (juce::int64) (0.35 * sampleRate))
        {
            chord.setFromNotes (notes, count);
            windowOpen = false;
            wasArmed = false;
            armed.store (false);
        }
    }

private:
    std::atomic<bool> armed { false };

    // Estado del hilo de audio
    bool wasArmed = false;
    bool windowOpen = false;
    juce::int64 elapsed = 0;
    int count = 0;
    int notes[UserChord::kMax] {};
};
