#include "WTLoader.h"
#include <algorithm>
#include <cmath>

namespace wavetable
{

WavetableLoader::WavetableLoader()
    : fft(11)  // 2^11 = 2048
{
    formatManager.registerBasicFormats();
}

std::shared_ptr<WavetableData> WavetableLoader::generateDefault()
{
    auto wt = std::make_shared<WavetableData>();
    wt->name = "Default Init";
    wt->sourceNumFrames = WavetableData::kFrames;
    wt->sourceSampleRate = 44100.0;

    constexpr int N = WavetableData::kSamples;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // 4 waveforms clave en 256 frames con morphing suave
    for (int f = 0; f < WavetableData::kFrames; ++f)
    {
        float* dst = wt->frameWrite(f, 0);
        const float t = static_cast<float>(f) / static_cast<float>(WavetableData::kFrames - 1);

        // t: 0..1 -> seno -> tri -> saw -> square
        // Usamos 3 segmentos con crossfade
        for (int i = 0; i < N; ++i)
        {
            const double ph = static_cast<double>(i) / static_cast<double>(N);
            double v = 0.0;

            if (t < 1.0f / 3.0f)
            {
                const float u = t * 3.0f;
                const double a = std::sin(twoPi * ph);
                const double b = ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0);
                v = a * (1.0f - u) + b * u;
            }
            else if (t < 2.0f / 3.0f)
            {
                const float u = (t - 1.0f / 3.0f) * 3.0f;
                const double a = ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0);
                const double b = 2.0 * ph - 1.0;
                v = a * (1.0f - u) + b * u;
            }
            else
            {
                const float u = (t - 2.0f / 3.0f) * 3.0f;
                const double a = 2.0 * ph - 1.0;
                const double b = ph < 0.5 ? 1.0 : -1.0;
                v = a * (1.0f - u) + b * u;
            }

            dst[i] = static_cast<float>(v);
        }
    }

    generateMips(*wt);
    return wt;
}

std::shared_ptr<WavetableData> WavetableLoader::loadFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->numChannels < 1)
        return nullptr;

    const int len = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, 1 << 22));
    const int numCh = static_cast<int>(reader->numChannels);

    juce::AudioBuffer<float> buf(numCh, len);
    if (!reader->read(&buf, 0, len, 0, true, numCh > 1))
        return nullptr;

    // Mezcla a mono
    std::vector<float> mono(static_cast<size_t>(len), 0.0f);
    for (int c = 0; c < numCh; ++c)
    {
        const float* src = buf.getReadPointer(c);
        for (int i = 0; i < len; ++i)
            mono[static_cast<size_t>(i)] += src[i] / static_cast<float>(numCh);
    }

    auto wt = std::make_shared<WavetableData>();
    wt->name = file.getFileNameWithoutExtension();
    wt->sourceSampleRate = reader->sampleRate;

    splitIntoFrames(*wt, mono.data(), len);
    normalize(*wt);
    generateMips(*wt);

    return wt;
}

void WavetableLoader::splitIntoFrames(WavetableData& wt, const float* mono, int numSamples)
{
    constexpr int N = WavetableData::kSamples;
    const int numFramesAvail = juce::jmin(WavetableData::kFrames, numSamples / N);
    wt.sourceNumFrames = juce::jmax(1, numFramesAvail);

    for (int f = 0; f < wt.sourceNumFrames; ++f)
    {
        float* dst = wt.frameWrite(f, 0);
        const int srcOffset = f * N;
        for (int i = 0; i < N; ++i)
        {
            const int idx = srcOffset + i;
            dst[i] = (idx < numSamples) ? mono[idx] : 0.0f;
        }
    }

    fillMissingFrames(wt, wt.sourceNumFrames);
}

void WavetableLoader::fillMissingFrames(WavetableData& wt, int numSourceFrames)
{
    if (numSourceFrames >= WavetableData::kFrames)
        return;

    constexpr int N = WavetableData::kSamples;

    for (int f = numSourceFrames; f < WavetableData::kFrames; ++f)
    {
        const float t = static_cast<float>(f) / static_cast<float>(WavetableData::kFrames - 1);
        const float t0 = t * static_cast<float>(numSourceFrames - 1);
        const int i0 = juce::jlimit(0, numSourceFrames - 1, static_cast<int>(std::floor(t0)));
        const int i1 = juce::jlimit(0, numSourceFrames - 1, i0 + 1);
        const float frac = t0 - static_cast<float>(i0);

        const float* src0 = wt.frame(i0, 0);
        const float* src1 = wt.frame(i1, 0);
        float* dst = wt.frameWrite(f, 0);

        for (int i = 0; i < N; ++i)
            dst[i] = src0[i] + (src1[i] - src0[i]) * frac;
    }
}

void WavetableLoader::normalize(WavetableData& wt)
{
    constexpr int N = WavetableData::kSamples;

    for (int f = 0; f < WavetableData::kFrames; ++f)
    {
        float* dst = wt.frameWrite(f, 0);
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
            peak = juce::jmax(peak, std::abs(dst[i]));

        if (peak > 1.0e-6f)
        {
            const float gain = 1.0f / peak;
            for (int i = 0; i < N; ++i)
                dst[i] *= gain;
        }
    }
}

void WavetableLoader::generateMips(WavetableData& wt)
{
    constexpr int N = WavetableData::kSamples;
    constexpr int fftSize = N;

    std::vector<float> fftBuffer(static_cast<size_t>(fftSize * 2), 0.0f);

    for (int f = 0; f < WavetableData::kFrames; ++f)
    {
        const float* src = wt.frame(f, 0);

        for (int mip = 1; mip < WavetableData::kMips; ++mip)
        {
            // Copia mip 0 al buffer FFT
            std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                fftBuffer[static_cast<size_t>(i)] = src[i];

            // FFT forward
            fft.performFrequencyOnlyForwardTransform(fftBuffer.data());

            // Zero bins > Nyquist para este mip
            const int maxHarm = juce::jmax(1, 1024 >> mip);
            for (int h = maxHarm; h < fftSize / 2; ++h)
            {
                fftBuffer[static_cast<size_t>(h)] = 0.0f;
                fftBuffer[static_cast<size_t>(fftSize - h)] = 0.0f;
            }

            // IFFT (real only)
            fft.performRealOnlyInverseTransform(fftBuffer.data());

            // Copia al mip
            float* dst = wt.frameWrite(f, mip);
            for (int i = 0; i < N; ++i)
                dst[i] = fftBuffer[static_cast<size_t>(i)];
        }
    }
}

} // namespace wavetable
