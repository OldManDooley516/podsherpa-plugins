#pragma once
#include "../core/Module.h"

namespace ps
{
/** Dropoff: high-pass filter. Natural-phase audio path ported from the JSFX Dropoff v0.3.1:
    same six slopes and stage plan, same cutoff smoothing (15 ms, once per block).

    Not carried over yet: Linear phase mode (the 43 ms FFT filter), the Listen button (hear
    what is being removed), the analyzer and the factory presets. Input and output gain and
    the bypass crossfade are handled by the shared core. */
class Dropoff : public Module
{
public:
    enum { pFreq, pSlope };
    Dropoff() : Module ({ ParamSpec::number ("freq", "Cutoff", "Hz", 20.0f, 500.0f, 80.0f, true, 0),
                          ParamSpec::choice ("slope", "Slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 3) }) {}
    juce::String getModuleId() const override { return "dropoff"; }
    juce::String getDisplayName() const override { return "Dropoff"; }
    juce::String getSubtitle() const override { return "High-pass filter"; }
    int getVersion() const override { return 1; }

    void prepare (double sr, int, int channels) override { sampleRate = sr; z.assign ((size_t) channels, {}); reset(); }
    void reset() override { for (auto& c : z) c.fill (0.0); smoothed = getParam (pFreq); lastSlope = -1; }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), slope = juce::jlimit (0, 5, (int) std::floor (getParam (pSlope) + 0.5f));
        if (slope != lastSlope) { for (auto& c : z) c.fill (0.0); lastSlope = slope; }
        smoothed += ((double) getParam (pFreq) - smoothed) * (1.0 - std::exp (-(double) n / (sampleRate * 0.015)));
        build (smoothed, slope);
        const int nc = juce::jmin (b.getNumChannels(), (int) z.size()), stages = plan()[(size_t) slope].count;
        for (int c = 0; c < nc; ++c)
        {
            float* d = b.getWritePointer (c); auto& s = z[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double y = d[i];
                for (int k = 0; k < stages; ++k)
                {
                    const auto& q = co[(size_t) k];
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
    // Stage plan per slope: Q of each 2nd-order stage (0 = a first-order stage).
    struct Plan { int count; double q[4]; };
    static const std::array<Plan, 6>& plan()
    {
        static const std::array<Plan, 6> p { { { 1, { 0, 0, 0, 0 } }, { 1, { .70711, 0, 0, 0 } }, { 2, { 0, 1, 0, 0 } },
            { 2, { .54120, 1.30656, 0, 0 } }, { 3, { .51764, .70711, 1.93185, 0 } }, { 4, { .50980, .60134, .89998, 2.56292 } } } };
        return p;
    }
    void build (double f, int slope)
    {
        const double pi = juce::MathConstants<double>::pi, fc = juce::jmin (f, sampleRate * 0.45);
        const auto& pl = plan()[(size_t) slope];
        for (int k = 0; k < pl.count; ++k)
        {
            if (pl.q[k] == 0.0)
            {
                const double kk = std::tan (pi * fc / sampleRate), ia = 1.0 / (1.0 + kk);
                co[(size_t) k] = { ia, -ia, 0.0, (kk - 1.0) * ia, 0.0 };
            }
            else
            {
                const double w0 = 2.0 * pi * fc / sampleRate, c = std::cos (w0), al = std::sin (w0) / (2.0 * pl.q[k]), ia = 1.0 / (1.0 + al);
                co[(size_t) k] = { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - al) * ia };
            }
        }
    }
    double sampleRate = 48000.0, smoothed = 80.0; int lastSlope = -1;
    std::array<std::array<double, 5>, 4> co {};
    std::vector<std::array<double, 8>> z;
};
} // namespace ps
