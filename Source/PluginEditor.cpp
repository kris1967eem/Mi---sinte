#include "PluginEditor.h"

namespace
{
    // Tamano de diseno (100%). Todo se escala a partir de el.
    constexpr int kDesignW = 1000;
    constexpr int kDesignH = 510;
    constexpr int kTop = 36;             // altura de la barra de pestanas
    constexpr int kKnobW = 80;
    constexpr int kDialH = 96;

    // Pestana OSC
    constexpr int kOscH = 304;           // altura del panel del oscilador
    constexpr int kRow2Y = kTop + 312;   // fila de filtro y envolventes
    constexpr int kRow2H = 154;

    // Pestana LFOs
    constexpr int kLfoBlockH = 236;

    constexpr float kStartScale = 0.8f;

    // Colores de los knobs (inspirados en los aros de colores de Dune 3)
    const juce::Colour kAmber  (0xffe9b44c);
    const juce::Colour kOrange (0xfff08a5d);
    const juce::Colour kCyan   (0xff3fd0d4);
    const juce::Colour kGreen  (0xff8be04e);
    const juce::Colour kPink   (0xffe0679f);
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveDisplay (p.apvts)
{
    waveRaw = proc.apvts.getRawParameterValue ("wave");
    syncRaw = proc.apvts.getRawParameterValue ("oscsync");
    modeRaw = proc.apvts.getRawParameterValue ("umode");

    // --- Pestanas ---
    pageOscBtn.setButtonText ("OSC");
    pageLfoBtn.setButtonText ("LFOs");
    for (auto* b : { &pageOscBtn, &pageLfoBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6ea5));
        addAndMakeVisible (*b);
    }
    pageOscBtn.setToggleState (true, juce::dontSendNotification);
    pageOscBtn.onClick = [this] { showPage (0); };
    pageLfoBtn.onClick = [this] { showPage (1); };

    // --- Oscilador ---
    addToPage (0, waveDisplay);

    syncBtn.setButtonText ("SYNC");
    resetBtn.setButtonText ("RESET");
    for (auto* b : { &syncBtn, &resetBtn })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9843a));
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1208));
        addToPage (0, *b);
    }
    syncA  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "oscsync", syncBtn);
    resetA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "oscreset", resetBtn);

    setupKnob (pulseWidth, "PW",    "pw",       "", 2, kOrange);
    setupKnob (syncFreq,   "FREQ",  "syncfreq", " st", 1, kOrange);

    setupKnob (octave, "OCT",   "octave", "", 0, kAmber);
    setupKnob (semi,   "SEMI",  "semi",   "", 0, kAmber);
    setupKnob (fine,   "FINE",  "fine",   " ct", 0, kAmber);
    setupKnob (glide,  "GLIDE", "glide",  " s", 2, kAmber);
    setupKnob (drift,  "DRIFT", "drift",  "", 2, kAmber);
    setupKnob (volume, "VOLUME", "volume", "", 2, kGreen);

    // --- Unison ---
    unisonModeBox.addItemList (juce::StringArray { "NORMAL", "OCTAVES", "FIFTH", "MAJOR",
                                                   "MINOR", "MAJOR 7", "MINOR 7",
                                                   "SUB", "HARMONIC" }, 1);
    addToPage (0, unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);

    setupKnob (unisonVoices, "VOICES", "unison",  "", 0, kCyan);
    setupKnob (unisonDetune, "DETUNE", "udetune", " ct", 0, kCyan);
    setupKnob (unisonSpread, "SPREAD", "ustereo", "", 2, kCyan);
    setupKnob (harmTilt,     "TILT",   "uharm",   "", 2, kCyan);

    // --- Filtro y su envolvente ---
    filterBox.addItemList (juce::StringArray { "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH" }, 1);
    addToPage (0, filterBox);
    filterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "ftype", filterBox);

    setupKnob (cutoff,   "CUTOFF",  "cutoff",    " Hz", 0, kOrange);
    setupKnob (reso,     "RESO",    "resonance", "", 2, kOrange);
    setupKnob (envAmt,   "ENV AMT", "envamt",    "", 2, kOrange);
    setupKnob (fAttack,  "ATTACK",  "fattack",   " s", 2, kPink);
    setupKnob (fDecay,   "DECAY",   "fdecay",    " s", 2, kPink);
    setupKnob (fSustain, "SUSTAIN", "fsustain",  "", 2, kPink);
    setupKnob (fRelease, "RELEASE", "frelease",  " s", 2, kPink);

    // --- Envolvente de amplitud ---
    setupKnob (attack,  "ATTACK",  "attack",  " s", 2, kGreen);
    setupKnob (decay,   "DECAY",   "decay",   " s", 2, kGreen);
    setupKnob (sustain, "SUSTAIN", "sustain", "", 2, kGreen);
    setupKnob (release, "RELEASE", "release", " s", 2, kGreen);

    // --- LFOs ---
    for (int i = 0; i < kNumLfos; ++i)
        setupLfo (lfoUI[i], i);

    showPage (0);

    // Ventana redimensionable (arrastrando la esquina inferior derecha),
    // manteniendo siempre las proporciones
    setResizable (true, true);
    setResizeLimits ((int) (kDesignW * 0.5f), (int) (kDesignH * 0.5f),
                     (int) (kDesignW * 2.0f), (int) (kDesignH * 2.0f));
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    setSize ((int) (kDesignW * kStartScale), (int) (kDesignH * kStartScale));

    timerCallback();        // estado inicial de los knobs atenuados
    startTimerHz (15);
}

// ------------------------------------------------------------------------------------

void MiSinteEditor::timerCallback()
{
    const int w = (int) waveRaw->load();
    const bool sy = syncRaw->load() > 0.5f;
    const int m = (int) modeRaw->load();

    if (! cachedValid || w != cachedWave || sy != cachedSync || m != cachedMode)
    {
        cachedValid = true;
        cachedWave = w;
        cachedSync = sy;
        cachedMode = m;
        applyEnabledStates();
        repaint();
    }
}

void MiSinteEditor::setKnobEnabled (Knob& k, bool enabled)
{
    k.slider.setEnabled (enabled);
    k.slider.setAlpha (enabled ? 1.0f : 0.4f);
    k.label.setAlpha (enabled ? 1.0f : 0.4f);
}

// Como en Dune 3: PW solo aplica a SQUARE, FREQ solo con SYNC y TILT solo en HARMONIC
void MiSinteEditor::applyEnabledStates()
{
    setKnobEnabled (pulseWidth, cachedWave == 3);
    setKnobEnabled (syncFreq,   cachedSync);
    setKnobEnabled (harmTilt,   cachedMode == 8);
}

void MiSinteEditor::addToPage (int page, juce::Component& c)
{
    addAndMakeVisible (c);
    pages[page].push_back (&c);
}

void MiSinteEditor::showPage (int page)
{
    currentPage = page;
    for (int i = 0; i < 2; ++i)
        for (auto* c : pages[i])
            c->setVisible (i == page);

    pageOscBtn.setToggleState (page == 0, juce::dontSendNotification);
    pageLfoBtn.setToggleState (page == 1, juce::dontSendNotification);
    repaint();
}

void MiSinteEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                               const juce::String& suffix, int decimals,
                               juce::Colour accent, int page)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a46));
    k.slider.setColour (juce::Slider::thumbColourId, accent.brighter (0.4f));
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    if (suffix.isNotEmpty())
        k.slider.setTextValueSuffix (suffix);
    if (decimals >= 0)
        k.slider.setNumDecimalPlacesToDisplay (decimals);
    addToPage (page, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    addToPage (page, k.label);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, k.slider);
}

void MiSinteEditor::setupLfo (LfoUI& u, int index)
{
    const juce::String n (index + 1);

    u.title.setText ("LFO " + n, juce::dontSendNotification);
    addToPage (1, u.title);

    u.waveL.setText ("WAVE", juce::dontSendNotification);
    u.destL.setText ("DESTINATION", juce::dontSendNotification);
    u.syncL.setText ("SYNC", juce::dontSendNotification);
    for (auto* l : { &u.waveL, &u.destL, &u.syncL })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
        addToPage (1, *l);
    }

    u.wave.addItemList (juce::StringArray { "SINE", "TRIANGLE", "SAW", "SQUARE",
                                            "RANDOM", "USER WAVE" }, 1);
    u.dest.addItemList (juce::StringArray { "NONE", "PITCH", "CUTOFF", "RESO", "VOLUME",
                                            "DETUNE", "PW", "SYNC FREQ",
                                            "LFO 1 RATE", "LFO 2 RATE",
                                            "LFO 3 RATE", "LFO 4 RATE" }, 1);
    u.sync.addItemList (juce::StringArray { "FREE", "8/1", "4/1", "2/1", "1/1", "1/2", "1/4",
                                            "1/8", "1/16", "1/32",
                                            "8/1T", "4/1T", "2/1T", "1/1T", "1/2T", "1/4T",
                                            "1/8T", "1/16T", "1/32T" }, 1);
    addToPage (1, u.wave);
    addToPage (1, u.dest);
    addToPage (1, u.sync);

    u.retrig.setButtonText ("RETRIG");
    addToPage (1, u.retrig);

    u.loadBtn.setButtonText ("LOAD...");
    u.loadBtn.onClick = [this, index] { chooseLfoWave (index); };
    addToPage (1, u.loadBtn);

    const auto loaded = proc.getLfoWaveName (index);
    u.waveName.setText (loaded.isNotEmpty() ? "USER WAVE: " + loaded : "USER WAVE: EMPTY",
                        juce::dontSendNotification);
    u.waveName.setJustificationType (juce::Justification::left);
    u.waveName.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    addToPage (1, u.waveName);

    u.waveA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "wave", u.wave);
    u.destA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "dest", u.dest);
    u.syncA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "sync", u.sync);
    u.retrigA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "lfo" + n + "retrig", u.retrig);

    setupKnob (u.rate,  "RATE",    "lfo" + n + "rate",  " Hz", 2, kCyan, 1);
    setupKnob (u.amt,   "AMOUNT",  "lfo" + n + "amt",   "", 2, kCyan, 1);
    setupKnob (u.fade,  "FADE IN", "lfo" + n + "fade",  " s", 2, kCyan, 1);
    setupKnob (u.phase, "PHASE",   "lfo" + n + "phase", "", 0, kCyan, 1);
}

void MiSinteEditor::chooseLfoWave (int index)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Choose a single-cycle waveform (WAV, AIFF or FLAC)",
        juce::File(), "*.wav;*.aif;*.aiff;*.flac");

    juce::Component::SafePointer<MiSinteEditor> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe, index] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;

        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        auto& ui = safe->lfoUI[index];
        if (safe->proc.loadLfoWave (index, file))
        {
            ui.waveName.setText ("USER WAVE: " + safe->proc.getLfoWaveName (index),
                                 juce::dontSendNotification);
            ui.wave.setSelectedId (6, juce::sendNotificationSync);   // selecciona USER WAVE
        }
        else
        {
            ui.waveName.setText ("COULD NOT LOAD FILE", juce::dontSendNotification);
        }
    });
}

void MiSinteEditor::scaleLabel (juce::Label& l, float s)
{
    l.setFont (juce::Font (juce::FontOptions (13.0f * s)));
}

// Coloca un knob: la etiqueta en (x, y) y el dial justo debajo
void MiSinteEditor::placeKnob (Knob& k, int x, int y, int w, int sliderH, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    scaleLabel (k.label, s);
    k.label.setBounds (S (x), S (y), S (w), S (22));

    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (w - 8), S (18));
    k.slider.setBounds (S (x), S (y + 24), S (w), S (sliderH));
}

void MiSinteEditor::layoutLfo (LfoUI& u, int index, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    const int bx = (index % 2 == 0) ? 24 : 512;
    const int by = kTop + 8 + (index / 2) * kLfoBlockH;

    u.title.setFont (juce::Font (juce::FontOptions (16.0f * s, juce::Font::bold)));
    u.title.setBounds (S (bx), S (by), S (60), S (26));
    u.retrig.setBounds (S (bx + 66), S (by), S (84), S (26));
    u.loadBtn.setBounds (S (bx + 156), S (by), S (70), S (26));

    scaleLabel (u.waveL, s);
    scaleLabel (u.destL, s);
    scaleLabel (u.syncL, s);
    u.waveName.setFont (juce::Font (juce::FontOptions (12.0f * s)));

    u.waveL.setBounds (S (bx), S (by + 38), S (88), S (26));
    u.wave.setBounds (S (bx + 92), S (by + 38), S (100), S (26));
    u.destL.setBounds (S (bx), S (by + 74), S (88), S (26));
    u.dest.setBounds (S (bx + 92), S (by + 74), S (100), S (26));
    u.syncL.setBounds (S (bx), S (by + 110), S (88), S (26));
    u.sync.setBounds (S (bx + 92), S (by + 110), S (100), S (26));
    u.waveName.setBounds (S (bx), S (by + 152), S (200), S (20));

    int x = bx + 200;
    for (auto* k : { &u.rate, &u.amt, &u.fade, &u.phase })
    {
        placeKnob (*k, x, by + 38, 66, 120, s);
        x += 66;
    }
}

// ------------------------------------------------------------------------------------

void MiSinteEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    const juce::Colour titleColour = juce::Colours::white.withAlpha (0.55f);
    const juce::Colour lineColour = juce::Colours::white.withAlpha (0.10f);

    g.fillAll (juce::Colour (0xff17171c));

    auto panel = [&] (int x, int y, int w, int h)
    {
        const juce::Rectangle<float> r ((float) S (x), (float) S (y), (float) S (w), (float) S (h));
        g.setColour (juce::Colour (0xff22222a));
        g.fillRoundedRectangle (r, 7.0f * s);
        g.setColour (juce::Colour (0xff343440));
        g.drawRoundedRectangle (r, 7.0f * s, 1.0f);
    };

    auto caption = [&] (const juce::String& text, int x, int y, int w)
    {
        g.setColour (titleColour);
        g.setFont (juce::Font (juce::FontOptions (12.5f * s, juce::Font::bold)));
        g.drawText (text, S (x), S (y), S (w), S (18), juce::Justification::left);
    };

    auto vline = [&] (int x, int y, int h)
    {
        g.setColour (lineColour);
        g.drawVerticalLine (S (x), (float) S (y), (float) S (y + h));
    };

    auto hline = [&] (int x1, int x2, int y)
    {
        g.setColour (lineColour);
        g.drawHorizontalLine (S (y), (float) S (x1), (float) S (x2));
    };

    if (currentPage == 0)
    {
        const int y1 = kTop;
        const int y2 = kRow2Y;

        // ---------- Panel del oscilador ----------
        panel (12, y1, 976, kOscH);
        caption ("OSCILLATOR", 24, y1 + 6, 120);
        caption ("TUNING", 296, y1 + 6, 80);
        caption ("UNISON", 296, y1 + 158, 60);
        caption ("OUTPUT", 896, y1 + 6, 80);

        // Selector de tipo de oscilador (como en Dune 3). Por ahora solo VA esta activo.
        auto pill = [&] (const juce::String& text, int x, bool active)
        {
            const juce::Rectangle<float> r ((float) S (x), (float) S (y1 + 30), (float) S (44), (float) S (22));
            g.setColour (active ? juce::Colour (0xffe9843a) : juce::Colour (0xff2c2c36));
            g.fillRoundedRectangle (r, 11.0f * s);
            g.setColour (active ? juce::Colour (0xff1a1208) : juce::Colour (0xff6c6c78));
            g.setFont (juce::Font (juce::FontOptions (12.0f * s, juce::Font::bold)));
            g.drawText (text, r.toNearestInt(), juce::Justification::centred);
        };
        pill ("VA", 24, true);
        pill ("WT", 72, false);
        pill ("FM", 120, false);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (11.0f * s)));
        g.drawText ("WAVEFORM", S (24), S (y1 + 148), S (230), S (16), juce::Justification::centred);

        vline (276, y1 + 10, kOscH - 20);
        hline (290, 976, y1 + 153);
        vline (886, y1 + 10, 140);

        // ---------- Filtro (con su envolvente) ----------
        panel (12, y2, 604, kRow2H);
        caption ("FILTER", 24, y2 + 6, 60);
        caption ("FILTER ENV", 284, y2 + 6, 100);
        vline (272, y2 + 10, kRow2H - 20);

        // ---------- Envolvente de amplitud ----------
        panel (628, y2, 360, kRow2H);
        caption ("AMP ENV", 640, y2 + 6, 100);
    }
    else
    {
        for (int i = 0; i < kNumLfos; ++i)
        {
            const int bx = (i % 2 == 0) ? 24 : 512;
            const int by = kTop + 8 + (i / 2) * kLfoBlockH;
            panel (bx - 12, by - 8, 482, 220);
        }
    }
}

// Cruz sobre los knobs atenuados, como en el panel de Dune 3
void MiSinteEditor::paintOverChildren (juce::Graphics& g)
{
    if (currentPage != 0)
        return;

    const float s = (float) getWidth() / (float) kDesignW;

    auto drawX = [&] (const Knob& k)
    {
        if (k.slider.isEnabled())
            return;

        const auto area = k.slider.getBounds().toFloat().withTrimmedBottom (18.0f * s);
        const auto c = area.getCentre();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.2f;

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawEllipse (c.x - r * 1.5f, c.y - r * 1.5f, r * 3.0f, r * 3.0f, 1.2f);
        g.drawLine (c.x - r * 0.7f, c.y - r * 0.7f, c.x + r * 0.7f, c.y + r * 0.7f, 1.6f);
        g.drawLine (c.x - r * 0.7f, c.y + r * 0.7f, c.x + r * 0.7f, c.y - r * 0.7f, 1.6f);
    };

    drawX (pulseWidth);
    drawX (syncFreq);
    drawX (harmTilt);
}

void MiSinteEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    pageOscBtn.setBounds (S (20), S (6), S (80), S (26));
    pageLfoBtn.setBounds (S (106), S (6), S (80), S (26));

    const int y1 = kTop;
    const int y2 = kRow2Y;

    // ---------- Oscilador: columna izquierda ----------
    waveDisplay.setBounds (S (24), S (y1 + 62), S (230), S (84));

    placeKnob (pulseWidth, 24,  y1 + 176, kKnobW, kDialH, s);
    placeKnob (syncFreq,   108, y1 + 176, kKnobW, kDialH, s);
    syncBtn.setBounds  (S (196), S (y1 + 206), S (58), S (26));
    resetBtn.setBounds (S (196), S (y1 + 240), S (58), S (26));

    // ---------- Oscilador: afinacion y salida ----------
    placeKnob (octave, 296, y1 + 30, kKnobW, kDialH, s);
    placeKnob (semi,   376, y1 + 30, kKnobW, kDialH, s);
    placeKnob (fine,   456, y1 + 30, kKnobW, kDialH, s);
    placeKnob (glide,  536, y1 + 30, kKnobW, kDialH, s);
    placeKnob (drift,  616, y1 + 30, kKnobW, kDialH, s);
    placeKnob (volume, 896, y1 + 30, kKnobW, kDialH, s);

    // ---------- Oscilador: unison ----------
    unisonModeBox.setBounds (S (360), S (y1 + 156), S (130), S (24));
    placeKnob (unisonVoices, 296, y1 + 184, kKnobW, kDialH, s);
    placeKnob (unisonDetune, 376, y1 + 184, kKnobW, kDialH, s);
    placeKnob (unisonSpread, 456, y1 + 184, kKnobW, kDialH, s);
    placeKnob (harmTilt,     536, y1 + 184, kKnobW, kDialH, s);

    // ---------- Filtro + filter env ----------
    filterBox.setBounds (S (84), S (y2 + 4), S (130), S (24));
    placeKnob (cutoff,   24,  y2 + 32, kKnobW, kDialH, s);
    placeKnob (reso,     104, y2 + 32, kKnobW, kDialH, s);
    placeKnob (envAmt,   184, y2 + 32, kKnobW, kDialH, s);
    placeKnob (fAttack,  284, y2 + 32, kKnobW, kDialH, s);
    placeKnob (fDecay,   364, y2 + 32, kKnobW, kDialH, s);
    placeKnob (fSustain, 444, y2 + 32, kKnobW, kDialH, s);
    placeKnob (fRelease, 524, y2 + 32, kKnobW, kDialH, s);

    // ---------- Amp env ----------
    placeKnob (attack,  640, y2 + 32, kKnobW, kDialH, s);
    placeKnob (decay,   720, y2 + 32, kKnobW, kDialH, s);
    placeKnob (sustain, 800, y2 + 32, kKnobW, kDialH, s);
    placeKnob (release, 880, y2 + 32, kKnobW, kDialH, s);

    // ---------- Pestana LFOs ----------
    for (int i = 0; i < kNumLfos; ++i)
        layoutLfo (lfoUI[i], i, s);
}
