#pragma once
#include "../core/Module.h"
#include "../core/LinearPhase.h"

namespace ps
{
/** Contour Dynamic: three-band dynamic EQ. Audio path ported from the JSFX Contour Dynamic
    v0.4.1: each band listens to its own frequency range (band-pass on the mono sum, 0.8 ms
    attack / 40 ms release envelope) and moves a bell up to its Range once the level passes the
    Threshold over a 12 dB smooth knee. Gains update every 32 samples. Linear-phase mode applies
    the same moving curve with a 2048-sample delay.

    Difference from the JSFX: the Phase switch is read when playback is prepared (the JSFX
    switches live). Not carried over yet: per-band Listen, the analyzer and graph, the gain
    history and the factory presets as a menu. Input and output gain and the bypass crossfade
    are handled by the shared core. */
class ContourDynamic : public Module
{
public:
    enum { perBand = 8, bFreq = 0, bQ, bDirection, bRange, bThreshold, bAttack, bRelease, bOn, pPhase = 24 };
    ContourDynamic() : Module (makeSpecs()) {}
    juce::String getModuleId() const override { return "contourdynamic"; }
    juce::String getDisplayName() const override { return "Contour Dynamic"; }
    juce::String getSubtitle() const override { return "Dynamic EQ"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return linear ? LinearPhase::latency : 0; }
    int getMaxLatencySamples() const override { return LinearPhase::latency; }
    juce::String getModuleMeterLabel() const override { return "Most movement"; }
    float readModuleMeter() override { return maxMove.exchange (0.0f); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; numChannels = juce::jmin (channels, (int) maxChannels);
        linear = getParam (pPhase) >= 0.5f;
        attackC = 1.0 - std::exp (-1.0 / (0.0008 * sr)); releaseC = 1.0 - std::exp (-1.0 / (0.04 * sr));
        fir.prepare (numChannels);
        reset();
    }
    void reset() override
    {
        for (auto& z : state) z.fill (0.0);
        for (int i = 0; i < 24; ++i) sm[i] = getParam (i);
        for (int k = 0; k < 3; ++k) { ev[k] = 0.0; gdb[k] = 0.0; dz[k][0] = dz[k][1] = 0.0; detector (k); }
        buildBells(); fir.reset(); lastCheck = -1.0; tick = 0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        if (nc == 0) return;
        // Phase follows the control while playing. Both paths are always prepared, so the
        // switch only clears the state the other one left behind; the host is told about the
        // latency change by the plugin wrapper.
        if (const bool wantLinear = getParam (pPhase) >= 0.5f; wantLinear != linear)
        {
            linear = wantLinear; fir.reset(); lastCheck = -1.0;
            for (auto& z : state) z.fill (0.0);
        }
        const double al = 1.0 - std::exp (-(double) n / (sampleRate * 0.015));
        for (int i = 0; i < 24; ++i) sm[i] += ((double) getParam (i) - sm[i]) * al;
        for (int k = 0; k < 3; ++k) detector (k);
        if (linear)
        {
            double ck = 0.0;
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 5; ++j) ck += cf[(size_t) i][(size_t) j] * (i * 5 + j + 1.37);
            for (int i = 0; i < 3; ++i) ck += (active[(size_t) i] ? 1.0 : 0.0) * (7 + i);
            if (std::abs (ck - lastCheck) > 1.0e-7) { designFir(); lastCheck = ck; }
        }
        float blockMax = 0.0f;
        double in[maxChannels] {}, out[maxChannels] {};
        for (int i = 0; i < n; ++i)
        {
            double mono = 0.0;
            for (int c = 0; c < nc; ++c) { in[c] = b.getSample (c, i); mono += in[c]; }
            mono /= nc;
            for (int k = 0; k < 3; ++k)
            {
                const auto& pc = dc[(size_t) k]; auto& z = dz[k];
                const double t = pc[0] * mono + z[0]; z[0] = pc[1] * mono - pc[3] * t + z[1]; z[1] = pc[2] * mono - pc[4] * t;
                const double a = std::abs (t);
                ev[k] += (a - ev[k]) * (a > ev[k] ? attackC : releaseC);
            }
            if (++tick >= 32)
            {
                tick = 0;
                for (int k = 0; k < 3; ++k)
                {
                    const int base = perBand * k;
                    const double lv = 8.685889638 * std::log (ev[k] + 1.0e-9);
                    double dp = (lv - (double) getParam (base + bThreshold)) / 12.0; dp = juce::jmin (1.0, juce::jmax (0.0, dp)); dp = dp * dp * (3.0 - 2.0 * dp);
                    const double target = (getParam (base + bDirection) > 0.5f ? 1.0 : -1.0) * sm[base + bRange] * dp * juce::jmin (1.0, juce::jmax (0.0, sm[base + bOn]));
                    const double g0 = gdb[k];
                    const double ms = std::abs (target) > std::abs (g0) ? juce::jmax (1.0, (double) getParam (base + bAttack)) : juce::jmax (10.0, (double) getParam (base + bRelease));
                    gdb[k] = g0 + (target - g0) * (1.0 - std::exp (-32.0 / (sampleRate * 0.001 * ms)));
                    blockMax = juce::jmax (blockMax, (float) std::abs (gdb[k]));
                }
                buildBells();
            }
            if (linear) fir.processFrame (in, out, nc);
            else
                for (int c = 0; c < nc; ++c)
                {
                    double y = in[c]; auto& z = state[(size_t) c];
                    for (int k = 0; k < 3; ++k)
                        if (active[(size_t) k])
                        {
                            const auto& q = cf[(size_t) k];
                            const double t = q[0] * y + z[(size_t) (2 * k)];
                            z[(size_t) (2 * k)] = q[1] * y - q[3] * t + z[(size_t) (2 * k + 1)];
                            z[(size_t) (2 * k + 1)] = q[2] * y - q[4] * t;
                            y = t;
                        }
                    out[c] = y;
                }
            for (int c = 0; c < nc; ++c) b.setSample (c, i, (float) out[c]);
        }
        float old = maxMove.load (std::memory_order_relaxed);
        while (blockMax > old && ! maxMove.compare_exchange_weak (old, blockMax)) {}
    }

    /** Factory presets from the JSFX. Per band: freq, Q, direction (0 cut / 1 boost), range, threshold, attack, release, on. */
    struct Preset { const char* name; float band[3][8]; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { {
            { "Default",  { { 250, 1.4f, 0, 3, -34, 20, 180, 1 }, { 3500, 1.4f, 1, 2, -40, 20, 180, 1 }, { 6800, 2, 0, 4, -38, 20, 180, 1 } } },
            { "De-Harsh", { { 250, 1.4f, 0, 3, -34, 20, 180, 0 }, { 3200, 1.2f, 0, 3.5f, -36, 6, 120, 1 }, { 5500, 2, 0, 2.5f, -38, 4, 100, 1 } } },
            { "Light De-Ess", { { 250, 1.4f, 0, 3, -34, 20, 180, 0 }, { 5500, 3, 0, 2.5f, -36, 2, 70, 1 }, { 7800, 3, 0, 3, -40, 2, 70, 1 } } },
            { "Proximity Control", { { 150, 0.9f, 0, 4, -30, 15, 250, 1 }, { 300, 1.2f, 0, 2, -34, 15, 200, 1 }, { 6800, 2, 0, 4, -38, 20, 180, 0 } } },
            { "Room Resonance Tamer", { { 190, 4.5f, 0, 4, -36, 25, 350, 1 }, { 380, 4, 0, 3.5f, -38, 25, 350, 1 }, { 6800, 2, 0, 4, -38, 20, 180, 0 } } },
            { "Gentle Air Lift", { { 250, 1.4f, 0, 3, -34, 20, 180, 0 }, { 3500, 1, 1, 1.5f, -38, 15, 200, 1 }, { 10000, 0.8f, 1, 3, -42, 10, 200, 1 } } } } };
        return p;
    }

    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        for (int b = 0; b < 3; ++b) for (int k = 0; k < perBand; ++k) setParam (b * perBand + k, p.band[b][k]);
    }

private:
    static constexpr int maxChannels = 8;
    using Coef = std::array<double, 5>;
    static std::vector<ParamSpec> makeSpecs()
    {
        std::vector<ParamSpec> s;
        const float def[3][7] = { { 250, 1.4f, 0, 3, -34, 20, 180 }, { 3500, 1.4f, 1, 2, -40, 20, 180 }, { 6800, 2.0f, 0, 4, -38, 20, 180 } };
        for (int k = 0; k < 3; ++k)
        {
            const juce::String id ("b" + juce::String (k + 1)), nm ("Band " + juce::String (k + 1));
            s.push_back (ParamSpec::number (id + ".freq", nm, "Hz", 20.0f, 20000.0f, def[k][0], true, 0)
                .says ("Which frequency this band watches. It only acts when that frequency gets loud."));
            s.push_back (ParamSpec::number (id + ".q", nm + " Q", "", 0.3f, 8.0f, def[k][1], true)
                .says ("How wide a range around the frequency this band touches. Higher is narrower."));
            s.push_back (ParamSpec::choice (id + ".direction", nm + " direction", { "Cut", "Boost" }, (int) def[k][2])
                .says ("Whether this band turns the frequency down when it gets loud, or up when it gets quiet."));
            s.push_back (ParamSpec::number (id + ".range", nm + " range", "dB", 0.0f, 18.0f, def[k][3])
                .says ("The most this band will ever move the frequency."));
            s.push_back (ParamSpec::number (id + ".threshold", nm + " threshold", "dB", -60.0f, 0.0f, def[k][4])
                .says ("How loud the frequency has to get before this band acts."));
            s.push_back (ParamSpec::number (id + ".attack", nm + " attack", "ms", 1.0f, 200.0f, def[k][5], false, 0)
                .says ("How quickly this band moves once it starts."));
            s.push_back (ParamSpec::number (id + ".release", nm + " release", "ms", 10.0f, 1000.0f, def[k][6], false, 0)
                .says ("How quickly this band settles back afterwards."));
            s.push_back (ParamSpec::toggle (id + ".on", nm + " on", true)
                .says ("Turn this band on or off."));
        }
        s.push_back (ParamSpec::choice ("phase", "Phase", { "Natural (0 ms)", "Linear (43 ms)" }, 0)
            .says ("Linear keeps the timing of the voice exact but adds delay. Natural adds none at all."));
        return s;
    }
    double omega (double f) const { return 2.0 * juce::MathConstants<double>::pi * juce::jmin (f, sampleRate * 0.45) / sampleRate; }
    void detector (int k)
    {
        const double w0 = omega (sm[perBand * k]), c = std::cos (w0), al = std::sin (w0) / (2.0 * sm[perBand * k + 1]), ia = 1.0 / (1.0 + al);
        dc[(size_t) k] = { al * ia, 0.0, -al * ia, -2.0 * c * ia, (1.0 - al) * ia };
    }
    void buildBells()
    {
        for (int k = 0; k < 3; ++k)
        {
            const double g = gdb[k];
            if (std::abs (g) > 0.004)
            {
                const double w0 = omega (sm[perBand * k]), c = std::cos (w0), al = std::sin (w0) / (2.0 * sm[perBand * k + 1]), A = std::exp (std::log (10.0) * g / 40.0);
                const double ia = 1.0 / (1.0 + al / A);
                cf[(size_t) k] = { (1.0 + al * A) * ia, -2.0 * c * ia, (1.0 - al * A) * ia, -2.0 * c * ia, (1.0 - al / A) * ia }; active[(size_t) k] = true;
            }
            else { cf[(size_t) k] = { 1.0, 0.0, 0.0, 0.0, 0.0 }; active[(size_t) k] = false; }
        }
    }
    void designFir()
    {
        fir.design ([this] (int k)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * k / 4096.0, cw = std::cos (w), sw = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
            double m = 1.0;
            for (int i = 0; i < 3; ++i)
                if (active[(size_t) i])
                {
                    const auto& p = cf[(size_t) i];
                    const double nr = p[0] + p[1] * cw + p[2] * c2, ni = p[1] * sw + p[2] * s2, dr = 1.0 + p[3] * cw + p[4] * c2, di = p[3] * sw + p[4] * s2;
                    m *= (nr * nr + ni * ni) / juce::jmax (1.0e-12, dr * dr + di * di);
                }
            return std::sqrt (m);
        });
    }
    double sampleRate = 48000.0, attackC = 0.0, releaseC = 0.0, sm[24] {}, ev[3] {}, gdb[3] {}, dz[3][2] {}, lastCheck = -1.0;
    int numChannels = 2, tick = 0; bool linear = false;
    std::array<Coef, 3> cf {}, dc {}; std::array<bool, 3> active {};
    std::array<std::array<double, 6>, maxChannels> state {};
    LinearPhase fir;
    std::atomic<float> maxMove { 0.0f };
};
} // namespace ps
