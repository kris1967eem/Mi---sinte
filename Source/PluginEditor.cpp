#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), proc (p), generic (p)
{
    addAndMakeVisible (generic);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);

    saveButton.onClick = [this]
    {
        auto& processor = proc;   // el procesador vive mas que el editor
        chooser = std::make_unique<juce::FileChooser> ("Guardar preset",
                                                       PluginProcessor::getPresetFolder(), "*.mspreset");
        const auto flags = juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;

        chooser->launchAsync (flags, [&processor] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result != juce::File())
                processor.savePresetToFile (result.withFileExtension ("mspreset"));
        });
    };

    loadButton.onClick = [this]
    {
        auto& processor = proc;
        chooser = std::make_unique<juce::FileChooser> ("Cargar preset",
                                                       PluginProcessor::getPresetFolder(), "*.mspreset");
        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync (flags, [&processor] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result.existsAsFile())
                processor.loadPresetFromFile (result);
        });
    };

    setResizable (true, true);
    setSize (640, 760);
}

void PluginEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (40).reduced (6);
    saveButton.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    loadButton.setBounds (top.removeFromLeft (150));
    generic.setBounds (r);
}
