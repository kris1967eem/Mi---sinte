// Tests del motor de sonido (sin JUCE).
// Compilar:  g++ -O2 -std=c++17 -I../Source engine_test.cpp -o engine_test
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "SynthEngine.h"

using namespace synth;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FALLO: %s\n", msg); ++failures; } else { std::printf("  ok: %s\n", msg); } } while (0)

static const double SR = 44100.0;

struct Audio { std::vector<float> l, r; };

static Audio renderSeconds (SynthEngine& e, double seconds)
{
    const int n = (int) (seconds * SR);
    Audio a; a.l.resize ((size_t) n); a.r.resize ((size_t) n);
    int pos = 0;
    while (pos < n) { int c = std::min (512, n - pos); e.render (a.l.data() + pos, a.r.data() + pos, c); pos += c; }
    return a;
}

static bool allFinite (const Audio& a, float limit = 1.0001f)
{
    for (size_t i = 0; i < a.l.size(); ++i)
        if (! std::isfinite (a.l[i]) || ! std::isfinite (a.r[i]) || std::fabs (a.l[i]) > limit || std::fabs (a.r[i]) > limit) return false;
    return true;
}

static float peak (const Audio& a, size_t from = 0, size_t to = 0)
{
    if (to == 0) to = a.l.size();
    float p = 0; for (size_t i = from; i < to; ++i) p = std::max ({ p, std::fabs (a.l[i]), std::fabs (a.r[i]) });
    return p;
}

static double estimateFreq (const std::vector<float>& x, size_t from, size_t to)
{
    int crossings = 0; double first = -1, last = -1;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            double frac = -x[i - 1] / (x[i] - x[i - 1]);
            double t = (double) (i - 1) + frac;
            if (first < 0) first = t;
            last = t; ++crossings;
        }
    if (crossings < 2) return 0.0;
    return (crossings - 1) * SR / (last - first);
}

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        double ang = -2.0 * 3.14159265358979323846 / (double) len;
        std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static EngineParams basic()
{
    EngineParams p;
    p.cutoff = 20000.0f; p.resonance = 0.0f; p.velAmp = 0.0f;
    p.ampA = 0.001f; p.ampD = 0.1f; p.ampS = 1.0f; p.ampR = 0.05f;
    p.oscLevel = 1.0f; p.masterDb = 0.0f; p.uniRandPhase = false;
    return p;
}

int main()
{
    std::printf("== 1. Afinacion (seno, varias notas) ==\n");
    for (int note : { 36, 60, 69, 84, 100 })
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); e.setParams (p);
        e.noteOn (note, 1.0f);
        auto a = renderSeconds (e, 1.0);
        double expect = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        double got = estimateFreq (a.l, 4000, a.l.size());
        char msg[128]; std::snprintf (msg, sizeof msg, "nota %d: esperado %.2f Hz, medido %.2f Hz", note, expect, got);
        CHECK (std::fabs (got - expect) / expect < 0.01, msg);
        CHECK (allFinite (a), "salida finita y dentro de rango");
    }

    std::printf("== 2. Aliasing (sierra aguda, nota 105) ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.wtPos = 2.0f / 7.0f; e.setParams (p);
        e.noteOn (105, 1.0f);
        auto a = renderSeconds (e, 1.0);
        const size_t N = 32768; std::vector<std::complex<double>> X (N);
        for (size_t i = 0; i < N; ++i)
        {
            double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) N);
            X[i] = a.l[8000 + i] * w;
        }
        fft (X);
        double f0 = 440.0 * std::pow (2.0, (105 - 69) / 12.0), tot = 0, bad = 0;
        for (size_t k = 1; k < N / 2; ++k)
        {
            double f = (double) k * SR / (double) N, pw = std::norm (X[k]); tot += pw;
            double h = f / f0; double dist = std::fabs (h - std::round (h)) * f0;
            if (dist > 60.0) bad += pw;
        }
        double db = 10.0 * std::log10 (bad / tot + 1e-30);
        char msg[128]; std::snprintf (msg, sizeof msg, "energia no armonica: %.1f dB", db);
        CHECK (db < -50.0, msg);
    }

    std::printf("== 3. Todos los modos de unison / warp / filtros ==\n");
    {
        bool ok = true;
        for (int mode = 0; mode <= 8; ++mode)
            for (int vox : { 1, 2, 3, 5, 8 })
                for (int warp = 0; warp <= 4; ++warp)
                {
                    SynthEngine e; e.prepare (SR); auto p = basic();
                    p.uniMode = mode; p.uniVoices = vox; p.warpMode = warp; p.warpAmt = 1.0f;
                    p.uniDetune = 100; p.uniDrift = 30; p.uniSpread = 1.0f; p.filtMode = (mode + warp) % 4;
                    p.cutoff = 300; p.resonance = 1.0f; p.polyphony = 4;
                    e.setParams (p);
                    e.noteOn (24, 1.0f); e.noteOn (60, 1.0f); e.noteOn (127, 1.0f);
                    auto a = renderSeconds (e, 0.3);
                    if (! allFinite (a)) { ok = false; std::printf ("   problema: modo %d voces %d warp %d\n", mode, vox, warp); }
                }
        CHECK (ok, "salida finita (maximo 1.0) en todas las combinaciones");
    }

    std::printf("== 4. Polifonia: 16 voces x 8 unison, rendimiento ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.polyphony = 16; p.uniVoices = 8; p.warpMode = 1; p.warpAmt = 0.5f;
        p.cutoff = 3000; p.resonance = 0.5f; p.lfoCutoff = 1.0f; p.lfoPitch = 0.1f; e.setParams (p);
        for (int i = 0; i < 16; ++i) e.noteOn (40 + i * 3, 0.8f);
        CHECK (e.activeVoices() == 16, "16 voces activas");
        auto t0 = std::chrono::steady_clock::now();
        auto a = renderSeconds (e, 5.0);
        double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        char msg[160]; std::snprintf (msg, sizeof msg, "5 s de audio en %.2f s (CPU de un nucleo ~ %.0f%%, peor caso, sin SIMD)", secs, 100.0 * secs / 5.0);
        CHECK (secs < 5.0, msg);
        CHECK (allFinite (a), "salida finita");
        e.noteOn (100, 1.0f);
        CHECK (e.activeVoices() == 16, "robo de voz cuando se supera la polifonia");
    }

    std::printf("== 5. Release termina y libera la voz ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.ampR = 0.2f; e.setParams (p);
        e.noteOn (60, 1.0f); renderSeconds (e, 0.2);
        e.noteOff (60);
        auto a = renderSeconds (e, 1.0);
        CHECK (e.activeVoices() == 0, "voz inactiva tras el release");
        CHECK (peak (a, a.l.size() - 2000) == 0.0f, "silencio total al final");
    }

    std::printf("== 6. Hold (drone) ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.hold = true; p.ampR = 0.1f; e.setParams (p);
        e.noteOn (60, 1.0f); renderSeconds (e, 0.2); e.noteOff (60);
        auto a = renderSeconds (e, 1.0);
        CHECK (e.activeVoices() == 1 && peak (a, a.l.size() - 1000) > 0.1f, "la nota sigue sonando con hold activo");
        p.hold = false; e.setParams (p);
        renderSeconds (e, 1.0);
        CHECK (e.activeVoices() == 0, "al desactivar hold la nota se libera");
    }

    std::printf("== 7. Mono, legato y glide ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.polyphony = 1; p.legato = true; p.glideMs = 400.0f; p.glideMode = 1; e.setParams (p);
        e.noteOn (60, 1.0f);
        auto a1 = renderSeconds (e, 0.3);
        double f1 = estimateFreq (a1.l, 3000, a1.l.size());
        e.noteOn (72, 1.0f);
        auto mid = renderSeconds (e, 0.2);
        auto end = renderSeconds (e, 0.6);
        double fm = estimateFreq (mid.l, 0, mid.l.size());
        double f2 = estimateFreq (end.l, 8000, end.l.size());
        char msg[160];
        std::snprintf (msg, sizeof msg, "f inicial %.1f, durante glide %.1f, final %.1f Hz", f1, fm, f2);
        CHECK (std::fabs (f1 - 261.63) < 3 && fm > 262 && fm < 520 && std::fabs (f2 - 523.25) < 5, msg);
        CHECK (e.activeVoices() == 1, "una sola voz en mono");
        e.noteOff (72); renderSeconds (e, 0.01);
        CHECK (e.activeVoices() == 1, "al soltar la ultima nota (60 sigue pulsada) la voz continua");
        renderSeconds (e, 0.5);   // deja terminar el glide (400 ms)
        double target = estimateFreq (renderSeconds (e, 0.5).l, 4000, 20000);
        CHECK (std::fabs (target - 261.63) < 5, "vuelve a la nota 60 con prioridad de ultima nota");
    }

    std::printf("== 8. Pitch bend, rueda de modulacion, velocity ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic(); p.bendRange = 12; e.setParams (p);
        e.noteOn (60, 1.0f); e.setPitchBend (1.0f);
        auto a = renderSeconds (e, 1.0);
        double f = estimateFreq (a.l, 20000, a.l.size());
        char msg[128]; std::snprintf (msg, sizeof msg, "bend +12 st: %.1f Hz (esperado 523.3)", f);
        CHECK (std::fabs (f - 523.25) < 6, msg);

        SynthEngine e2; e2.prepare (SR); auto q = basic(); q.velAmp = 1.0f; e2.setParams (q);
        e2.noteOn (60, 0.25f); float pl = peak (renderSeconds (e2, 0.3), 3000);
        SynthEngine e3; e3.prepare (SR); e3.setParams (q);
        e3.noteOn (60, 1.0f); float ph = peak (renderSeconds (e3, 0.3), 3000);
        std::snprintf (msg, sizeof msg, "velocity: 0.25 -> %.3f, 1.0 -> %.3f", pl, ph);
        CHECK (ph > pl * 3.0f, msg);
    }

    std::printf("== 9. Envolvente de filtro y rango de parametros extremos ==\n");
    {
        SynthEngine e; e.prepare (SR); auto p = basic();
        p.wtPos = 2.0f / 7.0f; p.cutoff = 200; p.filtEnvAmt = 5.0f; p.fltA = 0.001f; p.fltD = 0.3f; p.fltS = 0.0f; e.setParams (p);
        e.noteOn (48, 1.0f);
        auto a = renderSeconds (e, 1.0);
        auto rms = [&] (size_t from, size_t to) { double s = 0; for (size_t i = from; i < to; ++i) s += a.l[i] * a.l[i]; return std::sqrt (s / (double) (to - from)); };
        CHECK (rms (500, 2500) > 1.5 * rms (30000, 32000), "el filtro se cierra con la envolvente (suena mas brillante al inicio)");

        bool ok = true;
        for (float res : { 0.0f, 1.0f }) for (float cut : { 20.0f, 20000.0f })
        {
            SynthEngine e4; e4.prepare (SR); auto q = basic(); q.resonance = res; q.cutoff = cut;
            q.ampA = 30; q.ampD = 30; q.ampR = 30; q.glideMs = 5000; q.lfoRate = 50; q.lfoShape = 5; q.lfoPitch = 12; q.lfoCutoff = 4; q.lfoWt = 1;
            e4.setParams (q); e4.noteOn (0, 1.0f); e4.noteOn (127, 1.0f);
            if (! allFinite (renderSeconds (e4, 0.5))) ok = false;
        }
        CHECK (ok, "parametros extremos no producen NaN ni desbordes");
    }

    std::printf("\n%s (%d fallos)\n", failures == 0 ? "TODOS LOS TESTS OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
