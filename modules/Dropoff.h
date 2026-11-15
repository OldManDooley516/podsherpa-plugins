#pragma once
#include "../core/Module.h"
#include "../core/LinearPhase.h"

namespace ps
{
/** Dropoff: high-pass filter. Audio path ported from the JSFX Dropoff v0.4.1: six slopes with
    the same stage plan, cutoff smoothing (15 ms, once per block), a linear-phase mode with a
    2048-sample delay, and Listen, which plays only what the filter removes (in Natural mode a
    low-pass twin of the filter; in Linear mode the delayed input minus the filtered signal).

    Differences from the JSFX: the Phase switch is read when playback is prepared (the JSFX
    switches live), and Listen is a normal saved parameter. Not carried over yet: the analyzer
    and the factory presets as a menu. Input and output gain and the bypass crossfade are
    handled by the shared core. */
class Dropoff : public Module
{
public:
    enum { pFreq, pSlope, pPhase, pListen };
    Dropoff() : Module ({ ParamSpec::number ("freq", "Cutoff", "Hz", 20.0f, 500.0f, 80.0f, true, 0),
                          ParamSpec::choice ("slope", "Slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 3),
                          ParamSpec::choice ("phase", "Phase", { "Natural (0 ms)", "Linear (43 ms)" }, 0),
                          ParamSpec::toggle ("listen", "Listen to what's removed", false) }) {}
    juce::String getModuleId() const override { return "dropoff"; }
    juce::String getDisplayName() const override { return "Dropoff"; }
    juce::String getSubtitle() const override { return "High-pass filter"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return linear ? LinearPhase::latency : 0; }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; numChannels = juce::jmin (channels, (int) maxChannels);
        linear = getParam (pPhase) >= 0.5f;
        fir.prepare (numChannels);
        reset();
    }
    void reset() override
    {
        for (auto& c : z) c.fill (0.0);
        for (auto& c : zl) c.fill (0.0);
        for (auto& c : dry) c.fill (0.0);
        smoothed = getParam (pFreq); lastSlope = -1; lastListen = false; lastCheck = -1.0; wp = 0;
        fir.reset();
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), slope = juce::jlimit (0, 5, (int) std::floor (getParam (pSlope) + 0.5f));
        const int nc = juce::jmin (b.getNumChannels(), numChannels);
        const bool listen = getParam (pListen) > 0.5f;
        if (slope != lastSlope) { for (auto& c : z) c.fill (0.0); for (auto& c : zl) c.fill (0.0); lastSlope = slope; }
        smoothed += ((double) getParam (pFreq) - smoothed) * (1.0 - std::exp (-(double) n / (sampleRate * 0.015)));
        build (co, smoothed, slope, false);
        const int stages = plan()[(size_t) slope].count;
        const bool listenNatural = listen && ! linear;
        if (listenNatural) { build (cl, smoothed, slope, true); if (! lastListen) for (auto& c : zl) c.fill (0.0); }
        lastListen = listenNatural;

        if (linear)
        {
            double ck = 0.0;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 5; ++j) ck += (i < stages ? co[(size_t) i][(size_t) j] : (j == 0 ? 1.0 : 0.0)) * (i * 5 + j + 1.37);
            for (int i = 0; i < 4; ++i) ck += (i < stages ? 1.0 : 0.0) * (7 + i);
            if (std::abs (ck - lastCheck) > 1.0e-7) { designFir (stages); lastCheck = ck; }
            double in[maxChannels] {}, out[maxChannels] {};
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < nc; ++c) { in[c] = b.getSample (c, i); dry[(size_t) c][(size_t) wp] = in[c]; }
                const int rp = (wp - LinearPhase::latency) & ringMask;
                wp = (wp + 1) & ringMask;
                fir.processFrame (in, out, nc);
                for (int c = 0; c < nc; ++c) b.setSample (c, i, (float) (listen ? dry[(size_t) c][(size_t) rp] - out[c] : out[c]));
            }
            return;
        }
        const auto& use = listenNatural ? cl : co;
        for (int c = 0; c < nc; ++c)
        {
            float* d = b.getWritePointer (c); auto& s = listenNatural ? zl[(size_t) c] : z[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double y = d[i];
                for (int k = 0; k < stages; ++k)
                {
                    const auto& q = use[(size_t) k];
                    const double t = q[0] * y + s[(size_t) (2 * k)];
                    s[(size_t) (2 * k)] = q[1] * y - q[3] * t + s[(size_t) (2 * k + 1)];
                    s[(size_t) (2 * k + 1)] = q[2] * y - q[4] * t;
                    y = t;
                }
                d[i] = (float) y;
            }
        }
    }

    /** Factory presets from the JSFX: cutoff Hz, slope index. */
    struct Preset { const char* name; float freq; int slope; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", 80, 3 }, { "Rumble Only", 40, 2 }, { "Voice Standard", 90, 2 },
            { "Deep Voice Gentle", 60, 1 }, { "Aggressive Cleanup", 120, 4 }, { "Hum and Thump", 75, 4 } } };
        return p;
    }

private:
    static constexpr int maxChannels = 8, ring = 4096, ringMask = ring - 1;
    using Coefs = std::array<std::array<double, 5>, 4>;
    // Stage plan per slope: Q of each 2nd-order stage (0 = a first-order stage).
    struct Plan { int count; double q[4]; };
    static const std::array<Plan, 6>& plan()
    {
        static const std::array<Plan, 6> p { { { 1, { 0, 0, 0, 0 } }, { 1, { .70711, 0, 0, 0 } }, { 2, { 0, 1, 0, 0 } },
            { 2, { .54120, 1.30656, 0, 0 } }, { 3, { .51764, .70711, 1.93185, 0 } }, { 4, { .50980, .60134, .89998, 2.56292 } } } };
        return p;
    }
    // High-pass stages, or the matching low-pass stages for Listen.
    void build (Coefs& dst, double f, int slope, bool lowPass) const
    {
        const double pi = juce::MathConstants<double>::pi, fc = juce::jmin (f, sampleRate * 0.45);
        const auto& pl = plan()[(size_t) slope];
        for (int k = 0; k < pl.count; ++k)
        {
            if (pl.q[k] == 0.0)
            {
                const double kk = std::tan (pi * fc / sampleRate), ia = 1.0 / (1.0 + kk);
                dst[(size_t) k] = lowPass ? std::array<double, 5> { kk * ia, kk * ia, 0.0, (kk - 1.0) * ia, 0.0 }
                                          : std::array<double, 5> { ia, -ia, 0.0, (kk - 1.0) * ia, 0.0 };
            }
            else
            {
                const double w0 = 2.0 * pi * fc / sampleRate, c = std::cos (w0), al = std::sin (w0) / (2.0 * pl.q[k]), ia = 1.0 / (1.0 + al);
                dst[(size_t) k] = lowPass ? std::array<double, 5> { (1.0 - c) * 0.5 * ia, (1.0 - c) * ia, (1.0 - c) * 0.5 * ia, -2.0 * c * ia, (1.0 - al) * ia }
                                          : std::array<double, 5> { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - al) * ia };
            }
        }
    }
    void designFir (int stages)
    {
        fir.design ([this, stages] (int k)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * k / 4096.0, cw = std::cos (w), sw = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            double m = 1.0;
            for (int i = 0; i < stages; ++i)
            {
                const auto& p = co[(size_t) i];
                const double nr = p[0] + p[1] * cw + p[2] * c2, ni = p[1] * sw + p[2] * s2, dr = 1.0 + p[3] * cw + p[4] * c2, di = p[3] * sw + p[4] * s2;
                m *= (nr * nr + ni * ni) / juce::jmax (1.0e-12, dr * dr + di * di);
            }
            return std::sqrt (m);
        });
    }
    double sampleRate = 48000.0, smoothed = 80.0, lastCheck = -1.0;
    int numChannels = 2, lastSlope = -1, wp = 0; bool linear = false, lastListen = false;
    Coefs co {}, cl {};
    std::array<std::array<double, 8>, maxChannels> z {}, zl {};
    std::array<std::array<double, ring>, maxChannels> dry {};
    LinearPhase fir;
};
} // namespace ps
