#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

#include "DelayEditorComponent.h"
#include "EffectChain.h"
#include "EffectModule.h"

// ============================================================================
//  EffectsPage
//  Contenido de la pestana EFFECTS:
//    - Arriba, la CADENA: los 9 modulos como fichas, en el mismo orden en que se procesan.
//      Se arrastra una ficha para cambiar su posicion: el orden se envia a la EffectChain (DSP real).
//      Cada ficha muestra nombre, indicador ON, CPU y, si esta implementada, su interruptor y su
//      control principal. Las no implementadas se ven en gris y sin controles: la senal pasa por ellas
//      sin cambios y quedan reservadas en la cadena.
//    - Debajo, el editor completo del modulo seleccionado (hoy el Delay).
//  Espacio de diseno: 1000 x 478 px (se escala segun el ancho real).
// ============================================================================
namespace EffectsUI
{
    inline const juce::Colour kPanelColour { 0xff22232b };

    inline juce::Colour getAccent (EffectId id)
    {
        switch (id)
        {
            case EffectId::Distortion:  return juce::Colour (0xfff08a5d);
            case EffectId::Equalizer:   return juce::Colour (0xff3fd0d4);
            case EffectId::Chorus:      return juce::Colour (0xff8be04e);
            case EffectId::Phaser:      return juce::Colour (0xffe0679f);
            case EffectId::Flanger:     return juce::Colour (0xffe9b44c);
            case EffectId::RingMod:     return juce::Colour (0xfff08a5d);
            case EffectId::Delay:       return juce::Colour (0xff3fd0d4);
            case EffectId::Reverb:      return juce::Colour (0xff8be04e);
            case EffectId::Compressor:  return juce::Colour (0xffe9b44c);
        }
        return juce::Colours::white;
    }
}

// ---------------------------------------------------------------------------
//  Una ficha de la cadena (espacio de diseno de 100 x 100)
// ---------------------------------------------------------------------------
class EffectTile final : public juce::Component
{
public:
    EffectTile (EffectId effectId, bool isImplemented)
        : id (effectId), implemented (isImplemented), accent (EffectsUI::getAccent (effectId))
    {
    }

    // Controles principales de la ficha (solo para efectos implementados)
    void setupControls (juce::AudioProcessorValueTreeState& apvts, const char* onParamId,
                        const char* mainParamId, const juce::String& mainLabel)
    {
        onRaw = apvts.getRawParameterValue (onParamId);

        onBtn.setButtonText ("ON");
        onBtn.setClickingTogglesState (true);
        onBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9843a));
        onBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1208));
        addAndMakeVisible (onBtn);
        onA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, onParamId, onBtn);

        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setColour (juce::Slider::rotarySliderFillColourId, accent);
        knob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a3a46));
        knob.setColour (juce::Slider::thumbColourId, accent.brighter (0.4f));
        addAndMakeVisible (knob);
        knobA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, mainParamId, knob);

        knobLabel.setText (mainLabel, juce::dontSendNotification);
        knobLabel.setJustificationType (juce::Justification::centred);
        knobLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (knobLabel);

        resized();
    }

    EffectId getId() const noexcept { return id; }
    bool isImplemented() const noexcept { return implemented; }

    void setSelected (bool shouldBeSelected)
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    void setCpu (float percent) noexcept { cpuPercent = percent; }

    void paint (juce::Graphics& g) override
    {
        const float s = (float) getWidth() / 100.0f;
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const float alpha = implemented ? 1.0f : 0.45f;

        g.setColour (EffectsUI::kPanelColour.withAlpha (implemented ? 1.0f : 0.6f));
        g.fillRoundedRectangle (bounds, 7.0f * s);

        g.setColour (selected ? accent : juce::Colours::white.withAlpha (implemented ? 0.16f : 0.08f));
        g.drawRoundedRectangle (bounds, 7.0f * s, selected ? 2.0f : 1.0f);

        // Asa de arrastre (dos columnas de puntos)
        g.setColour (juce::Colours::white.withAlpha (0.35f * alpha));
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 3; ++row)
                g.fillEllipse ((7.0f + 4.0f * (float) col) * s, (8.0f + 4.5f * (float) row) * s, 2.2f * s, 2.2f * s);

        // Nombre
        g.setColour (juce::Colours::white.withAlpha (0.92f * alpha));
        g.setFont (juce::Font (juce::FontOptions (10.5f * s, juce::Font::bold)));
        g.drawText (EffectInfo::getName (id), juce::roundToInt (19.0f * s), juce::roundToInt (4.0f * s),
                    juce::roundToInt (62.0f * s), juce::roundToInt (16.0f * s), juce::Justification::centredLeft);

        if (! implemented)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::Font (juce::FontOptions (9.0f * s)));
            g.drawText ("NOT IMPLEMENTED", juce::roundToInt (4.0f * s), juce::roundToInt (44.0f * s),
                        juce::roundToInt (92.0f * s), juce::roundToInt (14.0f * s), juce::Justification::centred);
            g.drawText ("(passes through)", juce::roundToInt (4.0f * s), juce::roundToInt (58.0f * s),
                        juce::roundToInt (92.0f * s), juce::roundToInt (14.0f * s), juce::Justification::centred);
            return;
        }

        // Indicador de activacion
        const bool on = (onRaw != nullptr) && onRaw->load() > 0.5f;
        g.setColour (on ? accent : juce::Colour (0xff4a4a55));
        g.fillEllipse (88.0f * s, 8.0f * s, 7.0f * s, 7.0f * s);

        // CPU del modulo
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (9.5f * s)));
        g.drawText ("CPU " + juce::String (cpuPercent, 1) + "%", juce::roundToInt (4.0f * s), juce::roundToInt (84.0f * s),
                    juce::roundToInt (92.0f * s), juce::roundToInt (12.0f * s), juce::Justification::centredRight);
    }

    void resized() override
    {
        const float s = (float) getWidth() / 100.0f;
        auto S = [s] (float v) { return juce::roundToInt (v * s); };

        onBtn.setBounds (S (58.0f), S (28.0f), S (34.0f), S (22.0f));
        knob.setBounds (S (8.0f), S (24.0f), S (46.0f), S (46.0f));
        knobLabel.setFont (juce::Font (juce::FontOptions (9.5f * s)));
        knobLabel.setBounds (S (6.0f), S (70.0f), S (50.0f), S (13.0f));
    }

    void mouseDown (const juce::MouseEvent& e) override { if (onPress) onPress (*this, e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (*this, e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (onRelease) onRelease (*this, e); }

    std::function<void (EffectTile&, const juce::MouseEvent&)> onPress, onDrag, onRelease;

private:
    EffectId id;
    bool implemented;
    juce::Colour accent;
    bool selected = false;
    float cpuPercent = 0.0f;

    std::atomic<float>* onRaw = nullptr;
    juce::TextButton onBtn;
    juce::Slider knob;
    juce::Label knobLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobA;
};

// ---------------------------------------------------------------------------
class EffectsPage final : public juce::Component, private juce::Timer
{
public:
    static constexpr int kDesignW = 1000;
    static constexpr int kDesignH = 478;

    EffectsPage (juce::AudioProcessorValueTreeState& apvts, EffectChain& chainToUse)
        : chain (chainToUse), delayEditor (apvts)
    {
        for (int i = 0; i < kNumEffects; ++i)
        {
            const auto id = (EffectId) i;
            auto tile = std::make_unique<EffectTile> (id, chain.isImplemented (id));

            tile->onPress   = [this] (EffectTile& t, const juce::MouseEvent& e) { tilePressed (t, e); };
            tile->onDrag    = [this] (EffectTile& t, const juce::MouseEvent& e) { tileDragged (t, e); };
            tile->onRelease = [this] (EffectTile& t, const juce::MouseEvent& e) { tileReleased (t, e); };

            addAndMakeVisible (*tile);
            tiles[(size_t) i] = std::move (tile);
        }

        // Controles principales de las fichas implementadas
        tiles[(size_t) EffectId::Delay]->setupControls (apvts, DelayParams::kOn, DelayParams::kMix, "MIX");

        resetOrderBtn.setButtonText ("RESET ORDER");
        resetOrderBtn.onClick = [this] { chain.resetOrder(); };
        addAndMakeVisible (resetOrderBtn);

        addAndMakeVisible (delayEditor);

        notImplementedLabel.setJustificationType (juce::Justification::centred);
        notImplementedLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addChildComponent (notImplementedLabel);

        viewOrder = chain.getOrder();
        select (EffectId::Delay);
    }

    ~EffectsPage() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const float s = scale();
        auto S = [s] (float v) { return juce::roundToInt (v * s); };

        const juce::Colour caption = juce::Colours::white.withAlpha (0.65f);

        g.setColour (caption);
        g.setFont (juce::Font (juce::FontOptions (12.5f * s, juce::Font::bold)));
        g.drawText ("EFFECT CHAIN", S (kMarginX), S (8.0f), S (140.0f), S (18.0f), juce::Justification::left);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (11.0f * s)));
        g.drawText ("drag a module to change the processing order (left = first)", S (170.0f), S (8.0f), S (480.0f), S (18.0f),
                    juce::Justification::left);

        // Flujo de senal: IN, flechas entre fichas y OUT
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (9.0f * s, juce::Font::bold)));
        g.drawText ("IN", S (kMarginX), S (kTileY - 11.0f), S (30.0f), S (10.0f), juce::Justification::left);
        g.drawText ("OUT", S (kDesignW - kMarginX - 30.0f), S (kTileY - 11.0f), S (30.0f), S (10.0f), juce::Justification::right);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        for (int i = 0; i < kNumEffects - 1; ++i)
        {
            const float cx = (kMarginX + (float) (i + 1) * kPitch - kGap * 0.5f) * s;
            const float cy = (kTileY + 50.0f) * s;
            juce::Path arrow;
            arrow.addTriangle (cx - 2.5f * s, cy - 3.5f * s, cx - 2.5f * s, cy + 3.5f * s, cx + 3.0f * s, cy);
            g.fillPath (arrow);
        }

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine (S (kDetailY - 6.0f), 0.0f, (float) getWidth());
    }

    void resized() override
    {
        const float s = scale();
        auto S = [s] (float v) { return juce::roundToInt (v * s); };

        resetOrderBtn.setBounds (S (kDesignW - kMarginX - 120.0f), S (5.0f), S (120.0f), S (24.0f));
        layoutTiles();

        delayEditor.setBounds (0, S (kDetailY), S ((float) DelayEditorComponent::kDesignW), S ((float) DelayEditorComponent::kDesignH));
        notImplementedLabel.setFont (juce::Font (juce::FontOptions (15.0f * s)));
        notImplementedLabel.setBounds (S (kMarginX), S (kDetailY + 20.0f), S (kDesignW - 2.0f * kMarginX), S (60.0f));
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            timerCallback();
            startTimerHz (15);
        }
        else
        {
            stopTimer();
        }
    }

private:
    static constexpr float kMarginX = 20.0f;
    static constexpr float kTileW = 100.0f;
    static constexpr float kGap = 7.5f;
    static constexpr float kPitch = kTileW + kGap;
    static constexpr float kTileY = 34.0f;
    static constexpr float kDetailY = 146.0f;

    float scale() const noexcept { return (float) getWidth() / (float) kDesignW; }

    int slotX (int slot) const noexcept { return juce::roundToInt ((kMarginX + (float) slot * kPitch) * scale()); }

    // Coloca cada ficha en la posicion que le toca segun 'viewOrder' (la que se esta arrastrando se salta)
    void layoutTiles()
    {
        const float s = scale();
        for (int slot = 0; slot < kNumEffects; ++slot)
        {
            auto& tile = *tiles[(size_t) viewOrder[(size_t) slot]];
            if (&tile == draggedTile)
                continue;

            tile.setBounds (slotX (slot), juce::roundToInt (kTileY * s), juce::roundToInt (kTileW * s), juce::roundToInt (100.0f * s));
        }
    }

    int slotOf (const EffectTile& tile) const noexcept
    {
        for (int slot = 0; slot < kNumEffects; ++slot)
            if (viewOrder[(size_t) slot] == (std::uint8_t) tile.getId())
                return slot;
        return 0;
    }

    void select (EffectId id)
    {
        selectedId = id;
        for (auto& t : tiles)
            t->setSelected (t->getId() == id);

        const bool isDelay = (id == EffectId::Delay);
        delayEditor.setVisible (isDelay);

        const bool unimplemented = ! chain.isImplemented (id);
        notImplementedLabel.setVisible (unimplemented);
        if (unimplemented)
            notImplementedLabel.setText (juce::String (EffectInfo::getName (id)) + " is not implemented yet.\n"
                                         "Its slot is reserved in the chain: the signal passes through it untouched.",
                                         juce::dontSendNotification);
    }

    // ---- Arrastrar y soltar --------------------------------------------------------------------
    void tilePressed (EffectTile& tile, const juce::MouseEvent& e)
    {
        select (tile.getId());
        pressedTile = &tile;
        dragging = false;
        grabOffsetX = e.getEventRelativeTo (this).getPosition().x - tile.getX();
    }

    void tileDragged (EffectTile& tile, const juce::MouseEvent& e)
    {
        if (pressedTile != &tile)
            return;

        if (! dragging)
        {
            if (e.getDistanceFromDragStart() < 6)
                return;

            dragging = true;
            draggedTile = &tile;
            tile.toFront (false);
        }

        const float s = scale();
        const int mouseX = e.getEventRelativeTo (this).getPosition().x;
        const int minX = slotX (0) - juce::roundToInt (10.0f * s);
        const int maxX = slotX (kNumEffects - 1) + juce::roundToInt (10.0f * s);
        tile.setTopLeftPosition (juce::jlimit (minX, maxX, mouseX - grabOffsetX), tile.getY());

        // Posicion de insercion segun el centro de la ficha arrastrada
        const float centre = ((float) tile.getX() + (float) tile.getWidth() * 0.5f) / s;
        const int target = juce::jlimit (0, kNumEffects - 1, (int) std::floor ((centre - kMarginX) / kPitch));
        const int current = slotOf (tile);

        if (target != current)
        {
            const auto id = (std::uint8_t) tile.getId();
            const int step = target > current ? 1 : -1;
            for (int slot = current; slot != target; slot += step)
                viewOrder[(size_t) slot] = viewOrder[(size_t) (slot + step)];
            viewOrder[(size_t) target] = id;

            layoutTiles();
        }
    }

    void tileReleased (EffectTile& tile, const juce::MouseEvent&)
    {
        if (pressedTile != &tile)
            return;

        if (dragging)
        {
            dragging = false;
            draggedTile = nullptr;
            layoutTiles();
            tile.setBounds (slotX (slotOf (tile)), juce::roundToInt (kTileY * scale()),
                            juce::roundToInt (kTileW * scale()), juce::roundToInt (100.0f * scale()));

            chain.setOrder (viewOrder);     // aqui el orden pasa al DSP
        }

        pressedTile = nullptr;
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;

        // Si el orden cambia desde fuera (cargar proyecto, reset), la interfaz lo sigue
        if (! dragging)
        {
            const auto current = chain.getOrder();
            if (current != viewOrder)
            {
                viewOrder = current;
                layoutTiles();
            }
        }

        for (auto& t : tiles)
        {
            t->setCpu (chain.getModuleMeter (t->getId()).getAverage());
            t->repaint();
        }
    }

    EffectChain& chain;
    std::array<std::unique_ptr<EffectTile>, (size_t) kNumEffects> tiles;
    DelayEditorComponent delayEditor;
    juce::Label notImplementedLabel;
    juce::TextButton resetOrderBtn;

    EffectOrder viewOrder = EffectOrderUtil::getDefault();
    EffectId selectedId = EffectId::Delay;

    EffectTile* pressedTile = nullptr;
    EffectTile* draggedTile = nullptr;
    bool dragging = false;
    int grabOffsetX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPage)
};
