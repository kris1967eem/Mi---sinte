#pragma once

#include "WavetableData.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace wavetable
{

// ============================================================================
//  WavetableLoader
//  Carga archivos WAV, los divide en frames de 2048 muestras, los normaliza
//  y genera los mipmaps con FFT (zero bins > Nyquist + IFFT).
//
//  TODO se hace FUERA del hilo de audio. Nunca usar en processBlock.
// ============================================================================
class WavetableLoader
{
public:
    WavetableLoader();
    ~WavetableLoader() = default;

    // Carga un WAV. Devuelve nullptr si falla.
    // Si el WAV tiene menos de 256 frames, se rellena con morphing Normal.
    std::shared_ptr<WavetableData> loadFromFile(const juce::File& file);

    // Genera la tabla por defecto (seno -> tri -> saw -> square, 256 frames)
    std::shared_ptr<WavetableData> generateDefault();

private:
    // Divide el audio mono en bloques de 2048 y genera los frames base (mip 0)
    void splitIntoFrames(WavetableData& wt, const float* mono, int numSamples);

    // Rellena los frames que falten con morphing Normal (crossfade)
    void fillMissingFrames(WavetableData& wt, int numSourceFrames);

    // Genera los mips 1..5 a partir del mip 0 usando FFT
    void generateMips(WavetableData& wt);

    // Normaliza el pico de cada frame del mip 0 a 1.0 y propaga el factor
    void normalize(WavetableData& wt);

    juce::AudioFormatManager formatManager;
    juce::dsp::FFT fft;
};

} // namespace wavetable
