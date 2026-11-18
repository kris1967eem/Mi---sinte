#include "PluginEditor.h"

namespace
{
    // Tamano de diseno (100%). Todo se escala a partir de el.
    constexpr int kKnobW = 90;
    constexpr int kMargin = 20;
    constexpr int kDesignW = 2 * kMargin + 9 * kKnobW;   // 850
    constexpr int kDesignH = 620;
    constexpr int kTop = 36;           // altura de la barra de paginas
    constexpr int kLfoBlockH = 150;

    // Tamano inicial: 75% del diseno, para que quepa en pantallas pequenas
    constexpr float kStartScale = 0.75f;
}

MiSinteEditor::MiSinteEditor (MiSinteProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // --- Barra de paginas ---
    pageSoundBtn.setButtonText ("SONIDO");
    pageModBtn.setButtonText ("MODULACION");
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

    // --- Oscilador y amplitud ---
    waveLabel.setText ("Onda", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centredRight);
    addToPage (0, waveLabel);

    waveBox.addItemList (juce::StringArray { "Seno", "Triangulo", "Sierra", "Cuadrada" }, 1);
    addToPage (0, waveBox);
    waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "wave", waveBox);

    setupKnob (octave,     "Octava",  "octave", "", 0);
    setupKnob (semi,       "Semi",    "semi", "", 0);
    setupKnob (fine,       "Fine",    "fine", " ct", 0);
    setupKnob (pulseWidth, "PW",      "pw", "", 2);
    setupKnob (attack,     "Ataque",  "attack",  " s");
    setupKnob (decay,      "Decay",   "decay",   " s");
    setupKnob (sustain,    "Sustain", "sustain");
    setupKnob (release,    "Release", "release", " s");
    setupKnob (volume,     "Volumen", "volume");

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
    setupKnob (fAttack,   "F Ataque",   "fattack",  " s");
    setupKnob (fDecay,    "F Decay",    "fdecay",   " s");
    setupKnob (fSustain,  "F Sustain",  "fsustain");
    setupKnob (fRelease,  "F Release",  "frelease", " s");

    // --- Unison, drift y glide ---
    unisonModeLabel.setText ("Modo", juce::dontSendNotification);
    unisonModeLabel.setJustificationType (juce::Justification::centredRight);
    addToPage (0, unisonModeLabel);

    unisonModeBox.addItemList (juce::StringArray { "Normal", "Octavas", "Quinta", "Mayor",
                                                   "Menor", "Mayor 7", "Menor 7",
                                                   "Sub", "Swarm" }, 1);
    addToPage (0, unisonModeBox);
    unisonModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, "umode", unisonModeBox);

    setupKnob (unisonVoices, "Voces",      "unison", "", 0);
    setupKnob (unisonDetune, "Detune",     "udetune", " ct", 0);
    setupKnob (unisonStereo, "Estereo",    "ustereo", "", 2);
    setupKnob (drift,        "Drift",      "drift", "", 2);
    setupKnob (glide,        "Glide",      "glide", " s", 2);
    setupKnob (swarmRate,    "Swarm rate", "swarmrate", " Hz", 2);

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
    repaint();
}

void MiSinteEditor::setupKnob (Knob& k, const juce::String& text, const juce::String& paramID,
                               const juce::String& suffix, int decimals, int page)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
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
                                            "Detune", "PW" }, 1);
    u.sync.addItemList (juce::StringArray { "Libre", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                            "1/2T", "1/4T", "1/8T", "1/16T", "1/32T" }, 1);
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
    l.setFont (juce::Font (juce::FontOptions (15.0f * s)));
}

void MiSinteEditor::layoutRow (std::initializer_list<Knob*> knobs, int labelY, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    int x = kMargin;
    for (auto* k : knobs)
    {
        scaleLabel (k->label, s);
        k->label.setBounds (S (x), S (labelY), S (kKnobW), S (22));

        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (80), S (18));
        k->slider.setBounds (S (x), S (labelY + 25), S (kKnobW), S (115));
        x += kKnobW;
    }
}

void MiSinteEditor::layoutLfo (LfoUI& u, int block, float s)
{
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };
    const int y = kTop + block * kLfoBlockH;

    u.title.setFont (juce::Font (juce::FontOptions (16.0f * s, juce::Font::bold)));
    u.title.setBounds (S (kMargin), S (y), S (70), S (24));
    u.retrig.setBounds (S (kMargin + 80), S (y), S (90), S (24));

    scaleLabel (u.waveL, s);
    scaleLabel (u.destL, s);
    scaleLabel (u.syncL, s);
    u.waveName.setFont (juce::Font (juce::FontOptions (12.0f * s)));

    u.waveL.setBounds (S (kMargin), S (y + 30), S (62), S (24));
    u.wave.setBounds (S (kMargin + 68), S (y + 30), S (100), S (24));
    u.loadBtn.setBounds (S (kMargin + 172), S (y + 30), S (70), S (24));
    u.destL.setBounds (S (kMargin), S (y + 60), S (62), S (24));
    u.dest.setBounds (S (kMargin + 68), S (y + 60), S (130), S (24));
    u.syncL.setBounds (S (kMargin), S (y + 90), S (62), S (24));
    u.sync.setBounds (S (kMargin + 68), S (y + 90), S (130), S (24));
    u.waveName.setBounds (S (kMargin), S (y + 120), S (230), S (20));

    int x = 270;
    for (auto* k : { &u.rate, &u.amt, &u.fade, &u.phase })
    {
        scaleLabel (k->label, s);
        k->label.setBounds (S (x), S (y + 4), S (kKnobW), S (22));
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, S (80), S (18));
        k->slider.setBounds (S (x), S (y + 27), S (kKnobW), S (105));
        x += kKnobW + 10;
    }
}

void MiSinteEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    g.fillAll (juce::Colour (0xff1e1e24));

    const float x1 = (float) S (kMargin);
    const float x2 = (float) (getWidth() - S (kMargin));

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (S (kTop - 4), x1, x2);

    if (currentPage == 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (13.0f * s);
        g.drawText ("OSCILADOR Y AMPLITUD", S (kMargin), S (kTop + 8), S (200), S (18), juce::Justification::left);
        g.drawText ("FILTRO", S (kMargin), S (kTop + 205), S (200), S (18), juce::Justification::left);
        g.drawText ("UNISON, DRIFT Y GLIDE", S (kMargin), S (kTop + 402), S (200), S (18), juce::Justification::left);

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (S (kTop + 200), x1, x2);
        g.drawHorizontalLine (S (kTop + 397), x1, x2);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        for (int b = 1; b < 3; ++b)
            g.drawHorizontalLine (S (kTop + b * kLfoBlockH - 6), x1, x2);
    }
}

void MiSinteEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    auto S = [s] (int v) { return juce::roundToInt ((float) v * s); };

    pageSoundBtn.setBounds (S (kMargin), S (6), S (120), S (26));
    pageModBtn.setBounds (S (kMargin + 128), S (6), S (140), S (26));

    // Pagina de sonido
    scaleLabel (waveLabel, s);
    waveLabel.setBounds (S (kMargin + 190), S (kTop + 8), S (50), S (24));
    waveBox.setBounds (S (kMargin + 245), S (kTop + 8), S (130), S (24));
    layoutRow ({ &octave, &semi, &fine, &pulseWidth, &attack, &decay, &sustain, &release, &volume },
               kTop + 40, s);

    scaleLabel (filterLabel, s);
    filterLabel.setBounds (S (kMargin + 60), S (kTop + 205), S (50), S (24));
    filterBox.setBounds (S (kMargin + 115), S (kTop + 205), S (130), S (24));
    layoutRow ({ &cutoff, &resonance, &envAmt, &fAttack, &fDecay, &fSustain, &fRelease },
               kTop + 237, s);

    scaleLabel (unisonModeLabel, s);
    unisonModeLabel.setBounds (S (kMargin + 190), S (kTop + 402), S (50), S (24));
    unisonModeBox.setBounds (S (kMargin + 245), S (kTop + 402), S (130), S (24));
    layoutRow ({ &unisonVoices, &unisonDetune, &unisonStereo, &drift, &glide, &swarmRate },
               kTop + 434, s);

    // Pagina de modulacion
    for (int i = 0; i < 3; ++i)
        layoutLfo (lfoUI[i], i, s);
}
