#pragma once
#include "../core/Module.h"
#include "../core/LinearPhase.h"

namespace ps
{
/** Contour: five-band voice EQ. Audio path ported from the JSFX Contour v0.4.1: a high-pass
    (6 to 24 dB/oct), three bells and a high shelf, with 15 ms parameter smoothing once per
    block, and a linear-phase mode that applies the same curve with a 2048-sample delay.

    Difference from the JSFX: the Phase switch is read when playback is prepared, so changing it
    takes effect at the next start (the JSFX switches live). Not carried over yet: per-band
    Listen, the analyzer, the graph and the factory presets as a menu. Input and output gain
    and the bypass crossfade are handled by the shared core. */
class Contour : public Module
{
public:
    enum { pHpFreq, pHpSlope, pHpOn, pB2Freq, pB2Gain, pB2Q, pB2On, pB3Freq, pB3Gain, pB3Q, pB3On,
           pB4Freq, pB4Gain, pB4Q, pB4On, pShelfFreq, pShelfGain, pShelfOn, pPhase };
    Contour() : Module ({
        ParamSpec::number ("hp.freq", "High-pass", "Hz", 20.0f, 500.0f, 80.0f, true, 0),
        ParamSpec::choice ("hp.slope", "High-pass slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" }, 1),
        ParamSpec::toggle ("hp.on", "High-pass on", true),
        ParamSpec::number ("b2.freq", "Band 2", "Hz", 20.0f, 20000.0f, 200.0f, true, 0),
        ParamSpec::number ("b2.gain", "Band 2 gain", "dB", -18.0f, 18.0f, 0.0f),
        ParamSpec::number ("b2.q", "Band 2 Q", "", 0.3f, 8.0f, 1.2f, true),
        ParamSpec::toggle ("b2.on", "Band 2 on", true),
        ParamSpec::number ("b3.freq", "Band 3", "Hz", 20.0f, 20000.0f, 3200.0f, true, 0),
        ParamSpec::number ("b3.gain", "Band 3 gain", "dB", -18.0f, 18.0f, 0.0f),
        ParamSpec::number ("b3.q", "Band 3 Q", "", 0.3f, 8.0f, 1.0f, true),
        ParamSpec::toggle ("b3.on", "Band 3 on", true),
        ParamSpec::number ("b4.freq", "Band 4", "Hz", 20.0f, 20000.0f, 6500.0f, true, 0),
        ParamSpec::number ("b4.gain", "Band 4 gain", "dB", -18.0f, 18.0f, 0.0f),
        ParamSpec::number ("b4.q", "Band 4 Q", "", 0.3f, 8.0f, 3.0f, true),
        ParamSpec::toggle ("b4.on", "Band 4 on", true),
        ParamSpec::number ("shelf.freq", "High shelf", "Hz", 20.0f, 20000.0f, 10000.0f, true, 0),
        ParamSpec::number ("shelf.gain", "High shelf gain", "dB", -18.0f, 18.0f, 0.0f),
        ParamSpec::toggle ("shelf.on", "High shelf on", true),
        ParamSpec::choice ("phase", "Phase", { "Natural (0 ms)", "Linear (43 ms)" }, 0) }) {}
    juce::String getModuleId() const override { return "contour"; }
    juce::String getDisplayName() const override { return "Contour"; }
    juce::String getSubtitle() const override { return "Voice EQ"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return linear ? LinearPhase::latency : 0; }
    int getMaxLatencySamples() const override { return LinearPhase::latency; }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; numChannels = juce::jmin (channels, (int) maxChannels);
        linear = getParam (pPhase) >= 0.5f;
        fir.prepare (numChannels);
        reset();
    }
    void reset() override
    {
        for (auto& z : state) z.fill (0.0);
        for (int i = 0; i < 18; ++i) sm[i] = getParam (i);
        build(); fir.reset(); lastCheck = -1.0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        // Phase follows the control while playing. Both paths are always prepared, so the
        // switch only clears the state the other one left behind; the host is told about the
        // latency change by the plugin wrapper.
        if (const bool wantLinear = getParam (pPhase) >= 0.5f; wantLinear != linear)
        {
            linear = wantLinear; fir.reset(); lastCheck = -1.0;
            for (auto& z : state) z.fill (0.0);
        }
        const double al = 1.0 - std::exp (-(double) n / (sampleRate * 0.015));
        for (int i = 0; i < 18; ++i) sm[i] += ((double) getParam (i) - sm[i]) * al;
        sm[1] = getParam (pHpSlope);
        build();
        if (linear)
        {
            double ck = 0.0;
            for (int i = 0; i < 6; ++i) for (int j = 0; j < 5; ++j) ck += cf[(size_t) i][(size_t) j] * (i * 5 + j + 1.37);
            for (int i = 0; i < 6; ++i) ck += (active[(size_t) i] ? 1.0 : 0.0) * (7 + i);
            if (std::abs (ck - lastCheck) > 1.0e-7) { designFir(); lastCheck = ck; }
            double in[maxChannels] {}, out[maxChannels] {};
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < nc; ++c) in[c] = b.getSample (c, i);
                fir.processFrame (in, out, nc);
                for (int c = 0; c < nc; ++c) b.setSample (c, i, (float) out[c]);
            }
            return;
        }
        for (int c = 0; c < nc; ++c)
        {
            float* d = b.getWritePointer (c); auto& z = state[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double y = d[i];
                for (int k = 0; k < 6; ++k)
                    if (active[(size_t) k])
                    {
                        const auto& q = cf[(size_t) k];
                        const double t = q[0] * y + z[(size_t) (2 * k)];
                        z[(size_t) (2 * k)] = q[1] * y - q[3] * t + z[(size_t) (2 * k + 1)];
                        z[(size_t) (2 * k + 1)] = q[2] * y - q[4] * t;
                        y = t;
                    }
                d[i] = (float) y;
            }
        }
    }

    /** Factory presets from the JSFX. Values in parameter order, without Phase. */
    struct Preset { const char* name; float v[18]; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { {
            { "Default",      { 80, 1, 1, 200, 0, 1.2f, 1, 3200, 0, 1, 1, 6500, 0, 3, 1, 10000, 0, 1 } },
            { "Preset 1",     { 70, 1, 1, 150, 2, 0.9f, 1, 3200, -1.5f, 1.2f, 1, 6500, -1.5f, 2, 1, 10000, 0, 1 } },
            { "Preset 2",     { 100, 1, 1, 250, -1.5f, 1, 1, 3200, 1.5f, 1, 1, 6500, 0, 3, 1, 10000, 2.5f, 1 } },
            { "Preset 3",     { 80, 1, 1, 250, -3, 1, 1, 400, -1.5f, 1.5f, 1, 6500, 0, 3, 1, 10000, 0, 1 } },
            { "Preset 4",     { 80, 1, 1, 200, -1, 1.2f, 1, 3000, 2, 1, 1, 6500, 0, 3, 1, 10000, 1, 1 } },
            { "Preset 5",     { 100, 3, 1, 200, 0, 1.2f, 1, 3200, 0, 1, 1, 6500, 0, 3, 1, 10000, 0, 1 } } } };
        return p;   // preset names live in the JSFX text atlas; fill them in when the preset menu is built
    }

private:
    static constexpr int maxChannels = 8;
    using Coef = std::array<double, 5>;
    void set (int i, double b0, double b1, double b2, double a0, double a1, double a2)
    {
        const double ia = 1.0 / a0; cf[(size_t) i] = { b0 * ia, b1 * ia, b2 * ia, a1 * ia, a2 * ia }; active[(size_t) i] = true;
    }
    void ident (int i) { cf[(size_t) i] = { 1.0, 0.0, 0.0, 0.0, 0.0 }; active[(size_t) i] = false; }
    double omega (double f) const { return 2.0 * juce::MathConstants<double>::pi * juce::jmin (f, sampleRate * 0.45) / sampleRate; }
    void hp1 (int i, double f) { const double K = std::tan (juce::MathConstants<double>::pi * juce::jmin (f, sampleRate * 0.45) / sampleRate); set (i, 1.0, -1.0, 0.0, 1.0 + K, K - 1.0, 0.0); }
    void hp2 (int i, double f, double Q) { const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q); set (i, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al); }
    void bell (int i, double f, double g, double Q)
    {
        const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q), A = std::exp (std::log (10.0) * g / 40.0);
        set (i, 1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
    }
    void hshelf (int i, double f, double g)
    {
        const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / 1.41421, A = std::exp (std::log (10.0) * g / 40.0), sA = 2.0 * std::sqrt (A) * al;
        set (i, A * ((A + 1.0) + (A - 1.0) * c + sA), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sA),
             (A + 1.0) - (A - 1.0) * c + sA, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sA);
    }
    void build()
    {
        ident (0); ident (1);
        if (sm[2] >= 0.5)
        {
            const int sl = (int) std::floor (sm[1] + 0.5);
            if (sl == 0) hp1 (0, sm[0]);
            else if (sl == 1) hp2 (0, sm[0], 0.70711);
            else if (sl == 2) { hp1 (0, sm[0]); hp2 (1, sm[0], 1.0); }
            else { hp2 (0, sm[0], 0.5412); hp2 (1, sm[0], 1.30656); }
        }
        for (int k = 0; k < 3; ++k)
        {
            const double ge = sm[4 + 4 * k] * sm[6 + 4 * k];
            if (std::abs (ge) > 0.01) bell (2 + k, sm[3 + 4 * k], ge, sm[5 + 4 * k]); else ident (2 + k);
        }
        const double ge = sm[16] * sm[17];
        if (std::abs (ge) > 0.01) hshelf (5, sm[15], ge); else ident (5);
    }
    void designFir()
    {
        fir.design ([this] (int k)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * k / 4096.0, cw = std::cos (w), sw = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            double m = 1.0;
            for (int i = 0; i < 6; ++i)
                if (active[(size_t) i])
                {
                    const auto& p = cf[(size_t) i];
                    const double nr = p[0] + p[1] * cw + p[2] * c2, ni = p[1] * sw + p[2] * s2, dr = 1.0 + p[3] * cw + p[4] * c2, di = p[3] * sw + p[4] * s2;
                    m *= (nr * nr + ni * ni) / juce::jmax (1.0e-12, dr * dr + di * di);
                }
            return std::sqrt (m);
        });
    }
    double sampleRate = 48000.0, sm[18] {}, lastCheck = -1.0;
    int numChannels = 2; bool linear = false;
    std::array<Coef, 6> cf {}; std::array<bool, 6> active {};
    std::array<std::array<double, 12>, maxChannels> state {};
    LinearPhase fir;
};
} // namespace ps
