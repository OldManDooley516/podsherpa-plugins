#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Ridgeline: four-band compressor. Audio path ported from the JSFX Ridgeline v0.4.1:
    Linkwitz-Riley (LR4) crossovers with allpass compensation so the bands add back up flat,
    one linked power detector per band (15 ms), gain computed every 16 samples with a 6 dB
    soft knee, and per-band on/off switches.

    Not carried over yet: band solo (headphones), the spectrum display, the factory presets as
    a menu and the clip lamps. Input and output gain and the bypass crossfade are handled by
    the shared core. */
class Ridgeline : public Module
{
public:
    enum { pX1, pX2, pX3, pBand0 = 3, perBand = 5, pOn0 = 23 };   // band b: pBand0 + 5*b + {threshold, ratio, attack, release, makeup}
    Ridgeline() : Module (makeSpecs()) {}
    juce::String getModuleId() const override { return "ridgeline"; }
    juce::String getDisplayName() const override { return "Ridgeline"; }
    juce::String getSubtitle() const override { return "Multiband compressor"; }
    int getVersion() const override { return 1; }
    juce::String getModuleMeterLabel() const override { return "Most reduction"; }
    float readModuleMeter() override { return maxGr.exchange (0.0f); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; state.assign ((size_t) channels, {});
        ca = 1.0 - std::exp (-1.0 / (0.015 * sr));
        reset();
    }
    void reset() override
    {
        for (auto& s : state) s.fill (0.0);
        for (int k = 0; k < 4; ++k) { pw[k] = 0.0; grs[k] = 0.0; gs[k] = 1.0; gl[k] = 1.0; }
        for (int k = 0; k < 3; ++k) sm[k] = getParam (pX1 + k);
        tick = 0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), (int) state.size());
        if (nc == 0) return;
        const double al = 1.0 - std::exp (-(double) n / (sampleRate * 0.015)), ln10 = std::log (10.0);
        for (int k = 0; k < 3; ++k)
        {
            sm[k] += ((double) getParam (pX1 + k) - sm[k]) * al;
            const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jmin (sm[k], sampleRate * 0.45) / sampleRate;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70711), ia = 1.0 / (1.0 + alpha);
            lp[k] = { (1.0 - c) * 0.5 * ia, (1.0 - c) * ia, (1.0 - c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
            hp[k] = { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
        }
        float blockMax = 0.0f;
        std::array<std::array<double, 4>, maxChannels> band {};
        const int ncUse = juce::jmin (nc, (int) maxChannels);
        for (int i = 0; i < n; ++i)
        {
            double sq[4] = { 0.0, 0.0, 0.0, 0.0 };
            for (int ch = 0; ch < ncUse; ++ch)
            {
                double* z = state[(size_t) ch].data();
                const double x = b.getSample (ch, i);
                // split into four bands; the lower bands get the allpass of the higher crossovers
                const double h1 = f4 (hp[0], z + 4, x), lo = f4 (lp[0], z, x);
                const double h2 = f4 (hp[1], z + 12, h1), m = f4 (lp[1], z + 8, h1);
                const double h3 = f4 (hp[2], z + 20, h2), hm = f4 (lp[2], z + 16, h2);
                const double a = f4 (lp[1], z + 24, lo) + f4 (hp[1], z + 28, lo);
                const double b0 = f4 (lp[2], z + 32, a) + f4 (hp[2], z + 36, a);
                const double b1 = f4 (lp[2], z + 40, m) + f4 (hp[2], z + 44, m);
                band[(size_t) ch] = { b0, b1, hm, h3 };
                sq[0] += b0 * b0; sq[1] += b1 * b1; sq[2] += hm * hm; sq[3] += h3 * h3;
            }
            for (int k = 0; k < 4; ++k) { const double p = sq[k] / ncUse; pw[k] += (p - pw[k]) * ca; }   // linked detector (attack = release = 15 ms)

            if (++tick >= 16)   // control rate
            {
                tick = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const int base = pBand0 + perBand * k;
                    const bool on = getParam (pOn0 + k) > 0.5f;
                    const double lv = 4.342945 * std::log (2.0 * pw[k] + 1.0e-12);
                    const double over = lv - (double) getParam (base), rat = getParam (base + 1);
                    double gt = 0.0;
                    if (on)
                    {
                        if (over > 3.0) gt = over * (1.0 - 1.0 / rat);
                        else if (over > -3.0) gt = (1.0 - 1.0 / rat) * (over + 3.0) * (over + 3.0) / 12.0;
                    }
                    const double cc = 1.0 - std::exp (-16.0 / (sampleRate * 0.001 * (double) (gt > grs[k] ? getParam (base + 2) : getParam (base + 3))));
                    grs[k] += (gt - grs[k]) * cc;
                    gl[k] = std::exp (-grs[k] * ln10 / 20.0) * (on ? std::exp (ln10 * (double) getParam (base + 4) / 20.0) : 1.0);
                    blockMax = juce::jmax (blockMax, (float) grs[k]);
                }
            }
            for (int k = 0; k < 4; ++k) gs[k] += (gl[k] - gs[k]) * 0.1;
            for (int ch = 0; ch < ncUse; ++ch)
            {
                const auto& v = band[(size_t) ch];
                b.setSample (ch, i, (float) (v[0] * gs[0] + v[1] * gs[1] + v[2] * gs[2] + v[3] * gs[3]));
            }
        }
        float old = maxGr.load (std::memory_order_relaxed);
        while (blockMax > old && ! maxGr.compare_exchange_weak (old, blockMax)) {}
    }

    /** Factory presets from the JSFX. Crossovers are 150 / 1500 / 6000 Hz in all of them.
        Per band: threshold dB, ratio, attack ms, release ms, makeup dB. */
    struct Preset { const char* name; float band[4][5]; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { {
            { "Default",              { { -26, 2.5f, 15, 200, 0 }, { -28, 2.5f, 10, 150, 0 }, { -30, 2.5f, 8, 120, 0 }, { -32, 2, 5, 100, 0 } } },
            { "Broadcast Polish",     { { -30, 3, 12, 180, 1 }, { -32, 3, 8, 140, 1 }, { -34, 3, 6, 110, 1 }, { -36, 2.5f, 4, 90, 1.5f } } },
            { "Smooth Dynamics",      { { -28, 1.8f, 20, 250, 0 }, { -30, 1.8f, 15, 200, 0 }, { -32, 1.8f, 12, 160, 0 }, { -34, 1.8f, 8, 140, 0 } } },
            { "Warm Presence",        { { -28, 2, 20, 220, 0.5f }, { -30, 2, 15, 180, 1.5f }, { -32, 2.5f, 10, 140, 0.5f }, { -34, 3, 4, 100, -1 } } },
            { "Problem Voice Rescue", { { -34, 3.5f, 10, 160, 2 }, { -36, 4, 8, 130, 2 }, { -38, 4, 6, 100, 2 }, { -40, 3.5f, 4, 90, 2 } } },
            { "Tame Boom & Edge",     { { -36, 4, 15, 220, 0 }, { -28, 1.8f, 10, 150, 0 }, { -30, 1.8f, 8, 120, 0 }, { -38, 3.5f, 3, 90, 0 } } } } };
        return p;
    }

private:
    static constexpr int maxChannels = 8;
    static std::vector<ParamSpec> makeSpecs()
    {
        std::vector<ParamSpec> s {
            ParamSpec::number ("xover1", "Crossover 1", "Hz", 60.0f, 400.0f, 150.0f, false, 0),
            ParamSpec::number ("xover2", "Crossover 2", "Hz", 500.0f, 3500.0f, 1500.0f, false, 0),
            ParamSpec::number ("xover3", "Crossover 3", "Hz", 3500.0f, 12000.0f, 6000.0f, false, 0) };
        const char* ids[4] = { "low", "lowmid", "highmid", "high" };
        const char* names[4] = { "Low", "Low-mid", "High-mid", "High" };
        const float def[4][4] = { { -26, 2.5f, 15, 200 }, { -28, 2.5f, 10, 150 }, { -30, 2.5f, 8, 120 }, { -32, 2, 5, 100 } };
        for (int k = 0; k < 4; ++k)
        {
            const juce::String id (ids[k]), nm (names[k]);
            s.push_back (ParamSpec::number (id + ".threshold", nm + " threshold", "dB", -60.0f, 0.0f, def[k][0]));
            s.push_back (ParamSpec::number (id + ".ratio", nm + " ratio", ":1", 1.0f, 10.0f, def[k][1]));
            s.push_back (ParamSpec::number (id + ".attack", nm + " attack", "ms", 1.0f, 100.0f, def[k][2], false, 0));
            s.push_back (ParamSpec::number (id + ".release", nm + " release", "ms", 20.0f, 1000.0f, def[k][3], false, 0));
            s.push_back (ParamSpec::number (id + ".makeup", nm + " makeup", "dB", -12.0f, 12.0f, 0.0f));
        }
        for (int k = 0; k < 4; ++k) s.push_back (ParamSpec::toggle (juce::String (ids[k]) + ".on", juce::String (names[k]) + " band", true));
        return s;
    }
    // one LR4 filter = two identical Butterworth biquads in a row (4 state slots)
    static double f4 (const std::array<double, 5>& c, double* z, double x)
    {
        double t = c[0] * x + z[0]; z[0] = c[1] * x - c[3] * t + z[1]; z[1] = c[2] * x - c[4] * t; x = t;
        t = c[0] * x + z[2]; z[2] = c[1] * x - c[3] * t + z[3]; z[3] = c[2] * x - c[4] * t; return t;
    }
    double sampleRate = 48000.0, ca = 0.0;
    double sm[3] = { 150.0, 1500.0, 6000.0 }, pw[4] {}, grs[4] {}, gs[4] { 1, 1, 1, 1 }, gl[4] { 1, 1, 1, 1 };
    std::array<std::array<double, 5>, 3> lp {}, hp {};
    std::vector<std::array<double, 48>> state;
    int tick = 0;
    std::atomic<float> maxGr { 0.0f };
};
} // namespace ps
