#include "../WTOscillator.h"
#include "../WTLoader.h"
#include "../WTDefault.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <iostream>
#include <vector>
#include <memory>

int main(int argc, char* argv[])
{
    

    const double sampleRate = 44100.0;
    const float durationSec = 4.0f;
    const int numSamples = static_cast<int>(sampleRate * durationSec);

    std::shared_ptr<wavetable::WavetableData> wt;

    if (argc > 1)
    {
        juce::File file(argv[1]);
        if (!file.existsAsFile())
        {
            std::cerr << "No existe el archivo: " << argv[1] << std::endl;
            return 1;
        }

        wavetable::WavetableLoader loader;
        wt = loader.loadFromFile(file);
        if (!wt)
        {
            std::cerr << "No se pudo cargar el WAV" << std::endl;
            return 1;
        }
        std::cout << "Cargado WAV: " << wt->name << " (" << wt->sourceNumFrames << " frames)" << std::endl;
    }
    else
    {
        wt = wavetable::createDefaultWavetable();
        std::cout << "Usando tabla por defecto (seno->tri->saw->square)" << std::endl;
    }

    wavetable::WTOscillator osc;
    osc.prepare(sampleRate);
    osc.setWavetable(wt);
    osc.setWarpMode(0);
    osc.setWarpAmount(0.0f);
    osc.setHighQuality(true);
    osc.setFrequency(220.0f);

    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);

    const int blockSize = 512;
    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        const int n = std::min(blockSize, numSamples - pos);
        const float t = static_cast<float>(pos) / static_cast<float>(numSamples);
        osc.setPosition(t);
        osc.process(outL.data() + pos, outR.data() + pos, n);
    }

    juce::File outFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                            .getChildFile("wto_scanner_test.wav");

    outFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::cerr << "No se pudo crear el WAV de salida" << std::endl;
        return 1;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0));

    if (writer == nullptr)
    {
        std::cerr << "No se pudo crear el writer" << std::endl;
        return 1;
    }

    stream.release();

    const float* const* channels = new const float*[2] { outL.data(), outR.data() };
    writer->writeFromFloatArrays(channels, 2, numSamples);
    delete[] channels;
    writer.reset();

    std::cout << "WAV generado: " << outFile.getFullPathName() << std::endl;
    return 0;
}

