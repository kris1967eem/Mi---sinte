#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// Interfaz provisional de la fase 1: todos los parametros en una lista, mas
// botones para guardar y cargar presets. La interfaz final (con tu diseno)
// vendra despues.
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override = default;

    void resized() override;

private:
    PluginProcessor& proc;
    juce::TextButton saveButton { "Guardar preset" };
    juce::TextButton loadButton { "Cargar preset" };
    juce::GenericAudioProcessorEditor generic;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
