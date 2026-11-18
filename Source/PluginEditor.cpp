#include "PluginEditor.h"

namespace
{
    // Tamano de diseno (100%). Todo se escala a partir de el.
    constexpr int kKnobW = 80;
    constexpr int kMargin = 20;
    constexpr int kDesignW = 1000;
    constexpr int kDesignH = 420;
    constexpr int kTop = 36;            // altura de la barra de pestanas
    constexpr int kSectionH = 180;      // altura de cada fila de la pestana OSC
    constexpr int kLfoBlockH = 126;     // altura de cada LFO

    // Posiciones (x) de los grupos de la pestana OSC
    constexpr int kUnisonX = 440;
    constexpr int kVolumeX = 900;
    constexpr int kEnvFilterX = 280;
    constexpr int kEnvAmpX = 620;

    // Tamano inicial: 85% del diseno, para que quepa en pantallas pequenas
    constexpr float kStartScale = 0.85f;
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // --- Barra de pestanas ---
    pageSoundBtn.setButtonText ("OSC");
    pageModBtn.setButtonText ("LFOs");
    for (auto* b : { &pageSoundBtn, &pageModBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6ea5));
        addAndMakeVisible (*b);
    }
    pageSoundBtn.setToggleState (true, juce::dontSendNotification);
    pageSoundBtn.onClick = [this] { showPage (0); };
    pageModBtn.onClick = [this] { showPage (1); };

    // --- Oscilador: onda y afinacion ---
    waveLabel.setText ("Onda", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centredRight);
    addToPage (0, waveLabel);

    waveBox.addItemList (juce::StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 1);
    addToPage (0, waveBox);
    waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "wave", waveBox);

    setupKnob (octave,     "Octava", "octave", "", 0);
    setupKnob (semi,       "Semi",   "semi", "", 0);
    setupKnob (fine,       "Fine",   "fine", " ct", 0);
    setupKnob (pulseWidth, "PW",     "pw", "", 2);
    setupKnob (glide,      "Glide",  "glide", " s", 2);

    // --- Oscilador: unison ---
    unisonModeLabel.setText ("Modo", juce::dontSendNotification);
    unisonModeLabel.setJustificationType (juce::Justification::centredRight);
    addToPage (0, unisonModeLabel);

    unisonModeBox.addItemList (juce::StringArray { "Normal", "Octavas", "Quinta", "Mayor",
                                                   "Menor", "Mayor 7", "Menor 7",
                                                   "Sub", "Swarm" }, 1);
    addToPage (0, unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);
    // El knob Rate solo aparece cuando el modo de unison es Swarm
    unisonModeBox.onChange = [this] { updateSwarmKnob(); };

    setupKnob (unisonVoices, "Voces",   "unison", "", 0);
    setupKnob (unisonDetune, "Detune",  "udetune", " ct", 0);
    setupKnob (unisonStereo, "Spread",  "ustereo", "", 2);
    setupKnob (drift,        "Drift",   "drift", "", 2);
    setupKnob (swarmRate,    "Rate",    "swarmrate", " Hz", 2);

    setupKnob (volume, "Volumen", "volume");

    // --- Filtro ---
    filterLabel.setText ("Tipo", juce::dontSendNotification);
    filterLabel.setJustificationType (juce::Justification::centredRight);
    addToPage (0, filterLabel);

    filterBox.addItemList (juce::StringArray { "Paso bajo", "Paso alto", "Paso banda", "Notch" }, 1);
    addToPage (0, filterBox);
    filterAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "ftype", filterBox);

    setupKnob (cutoff,    "Cutoff",     "cutoff", " Hz", 0);
    setupKnob (resonance, "Resonancia", "resonance");
    setupKnob (envAmt,    "Env cant.",  "envamt");

    // --- Envolvente del filtro y de amplitud ---
    setupKnob (fAttack,   "Ataque",  "fattack",  " s");
    setupKnob (fDecay,    "Decay",   "fdecay",   " s");
    setupKnob (fSustain,  "Sustain", "fsustain");
    setupKnob (fRelease,  "Release", "frelease", " s");

    setupKnob (attack,    "Ataque",  "attack",  " s");
    setupKnob (decay,     "Decay",   "decay",   " s");
    setupKnob (sustain,   "Sustain", "sustain");
    setupKnob (release,   "Release", "release", " s");

    // --- LFOs ---
    for (int i = 0; i < 3; ++i)
        setupLfo (lfoUI[i], i);

    showPage (0);

    // Ventana redimensionable (arrastrando la esquina inferior derecha),
    // manteniendo siempre las proporciones
    setResizable (true, true);
    setResizeLimits ((int) (kDesignW * 0.5f), (int) (kDesignH * 0.5f),
                     (int) (kDesignW * 2.0f), (int) (kDesignH * 2.0f));
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    setSize ((int) (kDesignW * kStartScale), (int) (kDesignH * kStartScale));
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

    pageSoundBtn.setToggleState (page == 0, juce::dontSendNotification);
    pageModBtn.setToggleState (page == 1, juce::dontSendNotification);
    updateSwarmKnob();
    repaint();
}

void MiSinteEditor::updateSwarmKnob()
{
    // Swarm es el elemento 9 de la lista de modos (los ids empiezan en 1)
    const bool show = (currentPage == 0) && (unisonModeBox.getSelectedId() == 9);
    swarmRate.slider.setVisible (show);
    swarmRate.label.setVisible (show);
}

void MiSinteEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                               const juce::String& suffix, int decimals, int page)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    if (suffix.isNotEmpty())
        k.slider.setTextValueSuffix (suffix);
    if (decimals >= 0)
        k.slider.setNumDecimalPlacesToDisplay (decimals);
    addToPage (page, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addToPage (page, k.label);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, k.slider);
}

void MiSinteEditor::setupLfo (LfoUI& u, int index)
{
    const juce::String n (index + 1);

    u.title.setText ("LFO " + n, juce::dontSendNotification);
    addToPage (1, u.title);

    u.waveL.setText ("Onda", juce::dontSendNotification);
    u.destL.setText ("Destino", juce::dontSendNotification);
    u.syncL.setText ("Sync", juce::dontSendNotification);
    for (auto* l : { &u.waveL, &u.destL, &u.syncL })
    {
        l->setJustificationType (juce::Justification::centredRight);
        addToPage (1, *l);
    }

    u.wave.addItemList (juce::StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada",
                                            "Aleatorio", "Personal" }, 1);
    u.dest.addItemList (juce::StringArray { "Ninguno", "Pitch", "Cutoff", "Volumen",
                                            "Detune", "PW", "Rate" }, 1);
    u.sync.addItemList (juce::StringArray { "Libre", "8/1", "4/1", "2/1", "1/1", "1/2", "1/4",
                                            "1/8", "1/16", "1/32",
                                            "8/1T", "4/1T", "2/1T", "1/1T", "1/2T", "1/4T",
                                            "1/8T", "1/16T", "1/32T" }, 1);
    addToPage (1, u.wave);
    addToPage (1, u.dest);
    addToPage (1, u.sync);

    u.retrig.setButtonText ("Retrig");
    addToPage (1, u.retrig);

    u.loadBtn.setButtonText ("Cargar...");
    u.loadBtn.onClick = [this, index] { chooseLfoWave (index); };
    addToPage (1, u.loadBtn);

    const auto loaded = proc.getLfoWaveName (index);
    u.waveName.setText (loaded.isNotEmpty() ? "Personal: " + loaded : "Personal: sin cargar",
                        juce::dontSendNotification);
    u.waveName.setJustificationType (juce::Justification::left);
    addToPage (1, u.waveName);

    u.waveA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "wave", u.wave);
    u.destA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "dest", u.dest);
    u.syncA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "lfo" + n + "sync", u.sync);
    u.retrigA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "lfo" + n + "retrig", u.retrig);

    setupKnob (u.rate,  "Rate",     "lfo" + n + "rate",  " Hz", 2, 1);
    setupKnob (u.amt,   "Cantidad", "lfo" + n + "amt",   "", 2, 1);
    setupKnob (u.fade,  "Fade in",  "lfo" + n + "fade",  " s", 2, 1);
    setupKnob (u.phase, "Fase",     "lfo" + n + "phase", " deg", 0, 1);
}

void MiSinteEditor::chooseLfoWave (int index)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Elige una forma de onda de un ciclo (WAV, AIFF o FLAC)",
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
            ui.waveName.setText ("Personal: " + safe->proc.getLfoWaveName (index),
                                 juce::dontSendNotification);
            ui.wave.setSelectedId (6, juce::sendNotificationSync);   // elige "Personal"
        }
        else
        {
            ui.waveName.setText ("No se pudo cargar el archivo", juce::dontSendNotification);
        }
    });
}

void MiSinteEditor::scaleLabel (juce::Label& l, float s)
{
    l.setFont (juce::Font (juce::FontOptions (14.0f * s)));
}

// Coloca un knob: la etiqueta en (x, y) y el dial justo debajo
void MiSinteEditor::placeKnob (Knob& k, int x, int y, int sliderH, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    scaleLabel (k.label, s);
    k.label.setBounds (S (x), S (y), S (kKnobW), S (22));

    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (70), S (18));
    k.slider.setBounds (S (x), S (y + 24), S (kKnobW), S (sliderH));
}

void MiSinteEditor::layoutLfo (LfoUI& u, int block, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };
    const int y = kTop + block * kLfoBlockH;

    u.title.setFont (juce::Font (juce::FontOptions (16.0f * s, juce::Font::bold)));
    u.title.setBounds (S (kMargin), S (y), S (60), S (24));
    u.retrig.setBounds (S (kMargin + 65), S (y), S (85), S (24));
    u.waveName.setFont (juce::Font (juce::FontOptions (12.0f * s)));
    u.waveName.setBounds (S (kMargin + 155), S (y + 2), S (215), S (20));

    scaleLabel (u.waveL, s);
    scaleLabel (u.destL, s);
    scaleLabel (u.syncL, s);

    u.waveL.setBounds (S (kMargin), S (y + 30), S (62), S (24));
    u.wave.setBounds (S (kMargin + 68), S (y + 30), S (100), S (24));
    u.loadBtn.setBounds (S (kMargin + 172), S (y + 30), S (70), S (24));
    u.destL.setBounds (S (kMargin), S (y + 58), S (62), S (24));
    u.dest.setBounds (S (kMargin + 68), S (y + 58), S (110), S (24));
    u.syncL.setBounds (S (kMargin), S (y + 86), S (62), S (24));
    u.sync.setBounds (S (kMargin + 68), S (y + 86), S (110), S (24));

    int x = 400;
    for (auto* k : { &u.rate, &u.amt, &u.fade, &u.phase })
    {
        placeKnob (*k, x, y + 4, 96, s);
        x += 90;
    }
}

void MiSinteEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    g.fillAll (juce::Colour (0xff1e1e24));

    const float x1 = (float) S (kMargin);
    const float x2 = (float) (getWidth() - S (kMargin));
    const juce::Colour lineColour = juce::Colours::white.withAlpha (0.15f);
    const juce::Colour titleColour = juce::Colours::white.withAlpha (0.55f);

    g.setColour (lineColour);
    g.drawHorizontalLine (S (kTop - 4), x1, x2);

    if (currentPage == 0)
    {
        const int y1 = kTop;
        const int y2 = kTop + kSectionH;

        g.setColour (titleColour);
        g.setFont (13.0f * s);
        g.drawText ("OSCILADOR", S (kMargin), S (y1 + 6), S (90), S (18), juce::Justification::left);
        g.drawText ("UNISON", S (kUnisonX), S (y1 + 6), S (60), S (18), juce::Justification::left);
        g.drawText ("FILTRO", S (kMargin), S (y2 + 6), S (60), S (18), juce::Justification::left);
        g.drawText ("ENV FILTRO", S (kEnvFilterX), S (y2 + 6), S (100), S (18), juce::Justification::left);
        g.drawText ("ENV AMP", S (kEnvAmpX), S (y2 + 6), S (100), S (18), juce::Justification::left);

        g.setColour (lineColour);
        g.drawHorizontalLine (S (y2 - 6), x1, x2);

        // Separadores verticales entre grupos
        auto vline = [&] (int x, int yTop, int h)
        {
            g.drawVerticalLine (S (x), (float) S (yTop), (float) S (yTop + h));
        };
        vline (kUnisonX - 10, y1 + 6, kSectionH - 22);
        vline (kVolumeX - 10, y1 + 6, kSectionH - 22);
        vline (kEnvFilterX - 10, y2 + 6, kSectionH - 22);
        vline (kEnvAmpX - 10, y2 + 6, kSectionH - 22);
    }
    else
    {
        g.setColour (lineColour);
        for (int b = 1; b < 3; ++b)
            g.drawHorizontalLine (S (kTop + b * kLfoBlockH - 6), x1, x2);
    }
}

void MiSinteEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    pageSoundBtn.setBounds (S (kMargin), S (6), S (90), S (26));
    pageModBtn.setBounds (S (kMargin + 96), S (6), S (90), S (26));

    const int y1 = kTop;
    const int y2 = kTop + kSectionH;
    const int knobY1 = y1 + 32;
    const int knobY2 = y2 + 32;
    const int sliderH = 108;

    // --- Fila 1: oscilador (afinacion + unison) y volumen ---
    scaleLabel (waveLabel, s);
    waveLabel.setBounds (S (kMargin + 88), S (y1 + 4), S (44), S (24));
    waveBox.setBounds (S (kMargin + 136), S (y1 + 4), S (110), S (24));

    placeKnob (octave,     20,  knobY1, sliderH, s);
    placeKnob (semi,       100, knobY1, sliderH, s);
    placeKnob (fine,       180, knobY1, sliderH, s);
    placeKnob (pulseWidth, 260, knobY1, sliderH, s);
    placeKnob (glide,      340, knobY1, sliderH, s);

    scaleLabel (unisonModeLabel, s);
    unisonModeLabel.setBounds (S (kUnisonX + 56), S (y1 + 4), S (44), S (24));
    unisonModeBox.setBounds (S (kUnisonX + 104), S (y1 + 4), S (110), S (24));

    placeKnob (unisonVoices, kUnisonX,       knobY1, sliderH, s);
    placeKnob (unisonDetune, kUnisonX + 80,  knobY1, sliderH, s);
    placeKnob (unisonStereo, kUnisonX + 160, knobY1, sliderH, s);
    placeKnob (drift,        kUnisonX + 240, knobY1, sliderH, s);
    placeKnob (swarmRate,    kUnisonX + 320, knobY1, sliderH, s);

    placeKnob (volume, kVolumeX, knobY1, sliderH, s);

    // --- Fila 2: filtro y envolventes ---
    scaleLabel (filterLabel, s);
    filterLabel.setBounds (S (kMargin + 56), S (y2 + 4), S (38), S (24));
    filterBox.setBounds (S (kMargin + 98), S (y2 + 4), S (122), S (24));

    placeKnob (cutoff,    20,  knobY2, sliderH, s);
    placeKnob (resonance, 100, knobY2, sliderH, s);
    placeKnob (envAmt,    180, knobY2, sliderH, s);

    placeKnob (fAttack,  kEnvFilterX,       knobY2, sliderH, s);
    placeKnob (fDecay,   kEnvFilterX + 80,  knobY2, sliderH, s);
    placeKnob (fSustain, kEnvFilterX + 160, knobY2, sliderH, s);
    placeKnob (fRelease, kEnvFilterX + 240, knobY2, sliderH, s);

    placeKnob (attack,  kEnvAmpX,       knobY2, sliderH, s);
    placeKnob (decay,   kEnvAmpX + 80,  knobY2, sliderH, s);
    placeKnob (sustain, kEnvAmpX + 160, knobY2, sliderH, s);
    placeKnob (release, kEnvAmpX + 240, knobY2, sliderH, s);

    // --- Pestana LFOs ---
    for (int i = 0; i < 3; ++i)
        layoutLfo (lfoUI[i], i, s);
}
