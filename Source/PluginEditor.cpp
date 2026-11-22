#include "PluginEditor.h"

namespace
{
    // Tamano de diseno (100%). Todo se escala a partir de el.
    constexpr int kDesignW = 1000;
    constexpr int kDesignH = 540;
    constexpr int kTop = 62;             // altura de la barra superior (pestanas)
    constexpr int kKnobW = 80;
    constexpr int kDialH = 96;

    // Pestana OSC: filas de knobs (coordenada y de la etiqueta)
    constexpr int kTuningY = 93;
    constexpr int kMidY = 251;           // PW/FREQ, unison y filtro
    constexpr int kBottomY = 411;        // envolventes y volumen
    constexpr int kRowLineY = 380;       // separador entre las dos zonas

    // Pestana LFOs
    constexpr int kLfoBlockH = 236;

    constexpr float kStartScale = 0.8f;

    // Colores de los knobs (inspirados en los aros de colores de Dune 3)
    const juce::Colour kAmber  (0xffe9b44c);
    const juce::Colour kOrange (0xfff08a5d);
    const juce::Colour kCyan   (0xff3fd0d4);
    const juce::Colour kGreen  (0xff8be04e);
    const juce::Colour kPink   (0xffe0679f);

    // Rellena un menu con las opciones del propio parametro, para que el menu y el motor
    // nunca puedan quedar desincronizados (una sola lista, la del procesador)
    void fillCombo (juce::AudioProcessorValueTreeState& apvts, juce::ComboBox& box, const juce::String& paramID)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);
    }
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      waveDisplay (p.apvts), filterDisplay (p.apvts, p.getFilterDisplay()),
      delayEditor (p.apvts)
{
    waveRaw = proc.apvts.getRawParameterValue ("wave");
    syncRaw = proc.apvts.getRawParameterValue ("oscsync");
    modeRaw = proc.apvts.getRawParameterValue ("umode");

    // --- Pestanas ---
    pageOscBtn.setButtonText ("OSC");
    pageLfoBtn.setButtonText ("LFOs");
    pageFxBtn.setButtonText ("EFFECTS");
    for (auto* b : { &pageOscBtn, &pageLfoBtn, &pageFxBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6ea5));
        addAndMakeVisible (*b);
    }
    pageOscBtn.setToggleState (true, juce::dontSendNotification);
    pageOscBtn.onClick = [this] { showPage (0); };
    pageLfoBtn.onClick = [this] { showPage (1); };
    pageFxBtn.onClick  = [this] { showPage (2); };

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

    setupKnob (octave, "OCT",    "octave", "", 0, kAmber);
    setupKnob (semi,   "SEMI",   "semi",   "", 0, kAmber);
    setupKnob (fine,   "FINE",   "fine",   " ct", 0, kAmber);
    setupKnob (glide,  "GLIDE",  "glide",  " s", 2, kAmber);
    setupKnob (drift,  "DRIFT",  "drift",  "", 2, kAmber);
    setupKnob (volume, "VOLUME", "volume", "", 2, kGreen);

    // --- Unison ---
    fillCombo (proc.apvts, unisonModeBox, "umode");
    addToPage (0, unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);

    setupKnob (unisonVoices, "VOICES", "unison",  "", 0, kCyan);
    setupKnob (unisonDetune, "DETUNE", "udetune", " ct", 0, kCyan);
    setupKnob (unisonSpread, "SPREAD", "ustereo", "", 2, kCyan);
    setupKnob (harmTilt,     "TILT",   "uharm",   "", 2, kCyan);

    // --- CHORD (USER): CAPTURE, LOAD y el acorde actual (solo visibles en ese modo) ---
    captureBtn.setButtonText ("CAPTURE");
    captureBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9843a));
    captureBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1208));
    captureBtn.onClick = [this]
    {
        if (proc.isChordCaptureArmed())
            proc.cancelChordCapture();
        else
            proc.armChordCapture();
    };
    addToPage (0, captureBtn);

    loadChordBtn.setButtonText ("LOAD");
    loadChordBtn.onClick = [this] { chooseChordFile(); };
    addToPage (0, loadChordBtn);

    chordLabel.setJustificationType (juce::Justification::centredLeft);
    chordLabel.setColour (juce::Label::textColourId, kAmber);
    chordLabel.setMinimumHorizontalScale (0.6f);
    addToPage (0, chordLabel);

    // --- Filtro: tipo, display de respuesta y knobs ---
    fillCombo (proc.apvts, filterBox, "ftype");
    addToPage (0, filterBox);
    filterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "ftype", filterBox);

    addToPage (0, filterDisplay);

    setupKnob (cutoff,   "CUTOFF",  "cutoff",    " Hz", 0, kOrange);
    setupKnob (reso,     "RESO",    "resonance", "", 2, kOrange);
    setupKnob (envAmt,   "ENV AMT", "envamt",    "", 2, kOrange);

    // --- Envolvente del filtro ---
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

    // --- Pagina EFFECTS ---
    addToPage (2, delayEditor);

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

    // Acorde de usuario: refresca el texto cuando cambia y el boton CAPTURE mientras espera notas
    const int chordVersion = proc.getUserChord().getVersion();
    if (chordVersion != cachedChordVersion)
    {
        cachedChordVersion = chordVersion;
        chordLabel.setText (proc.getUserChord().toText(), juce::dontSendNotification);
    }

    const bool armed = proc.isChordCaptureArmed();
    if (armed != captureBtn.getToggleState())
        captureBtn.setToggleState (armed, juce::dontSendNotification);
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
    updateChordControls();
}

// CAPTURE, LOAD y el acorde actual solo se ven en el modo CHORD (USER)
void MiSinteEditor::updateChordControls()
{
    const bool show = (currentPage == 0) && (cachedMode == 9);
    captureBtn.setVisible (show);
    loadChordBtn.setVisible (show);
    chordLabel.setVisible (show);
}

void MiSinteEditor::chooseChordFile()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Choose a MIDI file with a chord", juce::File(), "*.mid;*.midi");

    juce::Component::SafePointer<MiSinteEditor> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr)
            return;

        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        if (safe->proc.loadChordFromMidi (file))
            safe->cachedChordVersion = -1;   // fuerza el refresco del texto
        else
            safe->chordLabel.setText ("MIDI ERROR", juce::dontSendNotification);
    });
}

void MiSinteEditor::addToPage (int page, juce::Component& c)
{
    addAndMakeVisible (c);
    pages[page].push_back (&c);
}

void MiSinteEditor::showPage (int page)
{
    currentPage = page;
    for (int i = 0; i < 3; ++i)
        for (auto* c : pages[i])
            c->setVisible (i == page);

    pageOscBtn.setToggleState (page == 0, juce::dontSendNotification);
    pageLfoBtn.setToggleState (page == 1, juce::dontSendNotification);
    pageFxBtn.setToggleState (page == 2, juce::dontSendNotification);
    updateChordControls();
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

    fillCombo (proc.apvts, u.wave, "lfo" + n + "wave");
    fillCombo (proc.apvts, u.dest, "lfo" + n + "dest");
    fillCombo (proc.apvts, u.sync, "lfo" + n + "sync");
    addToPage (1, u.wave);
    addToPage (1, u.dest);
    addToPage (1, u.sync);

    u.retrig.setButtonText ("RETRIG");
    addToPage (1, u.retrig);

    u.poly.setButtonText ("POLY");
    addToPage (1, u.poly);

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
    u.polyA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "lfo" + n + "poly", u.poly);

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
    u.poly.setBounds (S (bx + 154), S (by), S (68), S (26));
    u.loadBtn.setBounds (S (bx + 228), S (by), S (70), S (26));

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

    const juce::Colour titleColour = juce::Colours::white.withAlpha (0.65f);
    const juce::Colour lineColour = juce::Colours::white.withAlpha (0.10f);

    g.fillAll (juce::Colour (0xff1b1c21));

    // Barra superior con la tira de pestanas
    g.setColour (juce::Colour (0xff15161a));
    g.fillRect (0, 0, getWidth(), S (kTop - 1));
    g.setColour (juce::Colour (0xff101115));
    g.fillRoundedRectangle ((float) S (290), (float) S (10), (float) S (274), (float) S (33), 6.0f * s);
    g.setColour (lineColour);
    g.drawHorizontalLine (S (kTop - 1), 0.0f, (float) getWidth());

    auto caption = [&] (const juce::String& text, int x, int y, int w)
    {
        g.setColour (titleColour);
        g.setFont (juce::Font (juce::FontOptions (12.5f * s, juce::Font::bold)));
        g.drawText (text, S (x), S (y), S (w), S (18), juce::Justification::left);
    };

    if (currentPage == 0)
    {
        // ---------- Oscilador ----------
        caption ("OSCILLATOR", 44, 70, 120);
        caption ("TUNING", 303, 69, 80);
        caption ("UNISON", 308, 227, 70);

        // Selector de tipo de oscilador (como en Dune 3). Por ahora solo VA esta activo.
        auto pill = [&] (const juce::String& text, int x, bool active)
        {
            const juce::Rectangle<float> r ((float) S (x), (float) S (95), (float) S (44), (float) S (22));
            g.setColour (active ? juce::Colour (0xffe9843a) : juce::Colour (0xff2c2c36));
            g.fillRoundedRectangle (r, 11.0f * s);
            g.setColour (active ? juce::Colour (0xff1a1208) : juce::Colour (0xff6c6c78));
            g.setFont (juce::Font (juce::FontOptions (12.0f * s, juce::Font::bold)));
            g.drawText (text, r.toNearestInt(), juce::Justification::centred);
        };
        pill ("VA", 44, true);
        pill ("WT", 92, false);
        pill ("FM", 140, false);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (11.0f * s)));
        g.drawText ("WAVEFORM", S (44), S (213), S (228), S (16), juce::Justification::centred);

        // ---------- Filtro ----------
        caption ("FILTER", 729, 70, 60);

        // ---------- Separador y fila inferior ----------
        g.setColour (lineColour);
        g.drawHorizontalLine (S (kRowLineY), 0.0f, (float) getWidth());

        caption ("AMP ENV", 12, 386, 100);
        caption ("OUTPUT", 447, 388, 100);
        caption ("FILTER ENV", 657, 386, 100);
    }
    else if (currentPage == 1)
    {
        // Separadores entre los 4 LFOs
        g.setColour (lineColour);
        g.drawVerticalLine (S (500), (float) S (kTop + 10), (float) S (kDesignH - 20));
        g.drawHorizontalLine (S (kTop + 8 + kLfoBlockH - 14), (float) S (20), (float) S (kDesignW - 20));
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

    pageOscBtn.setBounds (S (297), S (13), S (79), S (26));
    pageLfoBtn.setBounds (S (382), S (13), S (80), S (26));
    pageFxBtn.setBounds (S (467), S (13), S (90), S (26));

    // ---------- Oscilador: display de onda, PW, FREQ, SYNC y RESET ----------
    waveDisplay.setBounds (S (44), S (127), S (228), S (83));

    placeKnob (pulseWidth, 45,  kMidY, kKnobW, kDialH, s);
    placeKnob (syncFreq,   129, kMidY, kKnobW, kDialH, s);
    syncBtn.setBounds  (S (217), S (279), S (57), S (25));
    resetBtn.setBounds (S (217), S (313), S (57), S (25));

    // ---------- Tuning y unison ----------
    placeKnob (octave, 304, kTuningY, kKnobW, kDialH, s);
    placeKnob (semi,   384, kTuningY, kKnobW, kDialH, s);
    placeKnob (fine,   464, kTuningY, kKnobW, kDialH, s);
    placeKnob (glide,  544, kTuningY, kKnobW, kDialH, s);
    placeKnob (drift,  624, kTuningY, kKnobW, kDialH, s);

    unisonModeBox.setBounds (S (413), S (224), S (130), S (24));
    captureBtn.setBounds (S (549), S (224), S (70), S (24));
    loadChordBtn.setBounds (S (623), S (224), S (46), S (24));
    chordLabel.setBounds (S (673), S (224), S (66), S (24));
    chordLabel.setFont (juce::Font (juce::FontOptions (12.0f * s)));
    placeKnob (unisonVoices, 349, kMidY, kKnobW, kDialH, s);
    placeKnob (unisonDetune, 429, kMidY, kKnobW, kDialH, s);
    placeKnob (unisonSpread, 509, kMidY, kKnobW, kDialH, s);
    placeKnob (harmTilt,     589, kMidY, kKnobW, kDialH, s);

    // ---------- Filtro: tipo, display de respuesta y knobs ----------
    filterBox.setBounds (S (784), S (91), S (129), S (24));
    filterDisplay.setBounds (S (744), S (127), S (228), S (83));   // mismo tamano que el display de onda

    placeKnob (cutoff, 733, kMidY, kKnobW, kDialH, s);
    placeKnob (reso,   813, kMidY, kKnobW, kDialH, s);
    placeKnob (envAmt, 893, kMidY, kKnobW, kDialH, s);

    // ---------- Fila inferior: AMP ENV, OUTPUT y FILTER ENV ----------
    placeKnob (attack,  12,  kBottomY, kKnobW, kDialH, s);
    placeKnob (decay,   92,  kBottomY, kKnobW, kDialH, s);
    placeKnob (sustain, 172, kBottomY, kKnobW, kDialH, s);
    placeKnob (release, 252, kBottomY, kKnobW, kDialH, s);

    placeKnob (volume, 447, kBottomY, kKnobW, kDialH, s);

    placeKnob (fAttack,  657, kBottomY, kKnobW, kDialH, s);
    placeKnob (fDecay,   737, kBottomY, kKnobW, kDialH, s);
    placeKnob (fSustain, 817, kBottomY, kKnobW, kDialH, s);
    placeKnob (fRelease, 897, kBottomY, kKnobW, kDialH, s);

    // ---------- Pestana LFOs ----------
    for (int i = 0; i < kNumLfos; ++i)
        layoutLfo (lfoUI[i], i, s);

    // ---------- Pestana EFFECTS: el Delay ocupa la parte superior; debajo quedara sitio para la Reverb ----------
    delayEditor.setBounds (0, S (kTop), S (kDesignW), S (DelayEditorComponent::kDesignH));
}
