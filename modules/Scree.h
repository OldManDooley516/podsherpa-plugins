#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Scree: click remover. Audio path ported from the JSFX Scree v0.4.2: clicks are found as
    sudden spikes in the second difference of the signal (all channels linked), and each one is
    rebuilt from the sound on both sides by forward and backward linear prediction (order 32),
    crossfaded. The patch covers 3 samples before the click and 6 after. 8 ms latency.

    A detection is only repaired if its spike is at least twice the largest spike nearby: the
    20 ms before it and the few ms after it, each held off by 1 ms. A real click stands alone,
    while a voice's own pitch pulses come in a row of similar spikes. The verdict therefore
    waits for the sound that follows, up to just before the audio leaves the delay line, so the
    latency is unchanged.

    Listen plays only what was removed. Unlike the JSFX, it is a normal parameter here, so it is
    saved with the project and can be rendered by accident; revisit when the real interface is built.

    Not carried over yet: the scrolling waveform, the factory presets as a menu and the
    "fixing a lot" warning. Input and output gain and the bypass crossfade are handled by the
    shared core. */
class Scree : public Module
{
public:
    enum { pSensitivity, pLongest, pListen };
    Scree() : Module ({ ParamSpec::number ("sensitivity", "Sensitivity", "%", 0.0f, 100.0f, 50.0f, false, 0).says ("How sharp a click has to be before it gets patched. Higher catches more, but can start nibbling at hard consonants."),
                        ParamSpec::number ("longest", "Longest click", "ms", 0.2f, 3.0f, 1.5f).says ("The longest click it will repair. Anything longer is left for you to cut by hand."),
                        ParamSpec::toggle ("listen", "Listen to what's removed", false).notSaved() .says ("Play only what is being taken out, so you can check it is clicks and not speech.") }) {}
    juce::String getModuleId() const override { return "scree"; }
    juce::String getDisplayName() const override { return "Scree"; }
    juce::String getSubtitle() const override { return "Click remover"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return delay; }
    int getClicksFixed() const { return clicks.load (std::memory_order_relaxed); }
    void clearClicksFixed() { clicks.store (0, std::memory_order_relaxed); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr;
        delay = (int) std::floor (0.008 * sr);
        settle = juce::jmax (64, (int) std::floor (0.003 * sr));
        guard = (int) std::floor (0.001 * sr);
        span = (int) std::floor (0.02 * sr);
        hist.assign ((size_t) ring, 0.0);
        chans.assign ((size_t) channels, {});
        const size_t work = (size_t) (settle + (int) std::floor (0.003 * sr) + 64);
        tw.assign (work, 0.0); ff.assign (work, 0.0); bb.assign (work, 0.0);
        aup = 1.0 - std::exp (-1.0 / (0.005 * sr)); adn = 1.0 - std::exp (-1.0 / (0.1 * sr));
        reset();
    }
    void reset() override
    {
        for (auto& c : chans) { c.buf.fill (0.0); c.org.fill (0.0); c.p1 = c.p2 = 0.0; }
        std::fill (hist.begin(), hist.end(), 0.0);
        wp = 0; runOn = false; ageStart = ageEnd = 0; env = 0.0;
        clock = 0; pending = false; pendStart = pendEnd = 0; pendPeak = runPeak = 0.0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), (int) chans.size());
        const int maxS = (int) std::floor ((double) getParam (pLongest) * 0.001 * sampleRate);
        const double kk = 20.0 * std::exp (-std::log (4.0) * (double) getParam (pSensitivity) / 100.0);
        const bool listen = getParam (pListen) > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const int nI = wp;
            double hpm = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                auto& s = chans[(size_t) c]; const double x = b.getSample (c, i);
                s.buf[(size_t) nI] = x; s.org[(size_t) nI] = x;
                const double h = x - 2.0 * s.p1 + s.p2; s.p2 = s.p1; s.p1 = x;
                hpm = juce::jmax (hpm, std::abs (h));
            }
            if (runOn) { ++ageStart; ++ageEnd; }
            const bool fire = hpm > juce::jmax (0.0015, kk * env);
            hist[(size_t) nI] = hpm; ++clock;
            if (fire)
            {
                if (runOn) { ageEnd = 0; runPeak = juce::jmax (runPeak, hpm); }
                else { runOn = true; ageStart = 0; ageEnd = 0; runPeak = hpm; }
            }
            if (! runOn || ageStart > maxS) env += (hpm - env) * (hpm > env ? aup : adn);
            if (runOn && ageEnd >= settle)
            {
                runOn = false;
                const int gl = ageStart - ageEnd + 1 + before + after;
                if (gl <= maxS + before + after)
                {
                    // An earlier detection still waiting has had all the sound it is going to get.
                    if (pending) decide (nI, nc);
                    pending = true; pendStart = clock - ageStart; pendEnd = clock - ageEnd; pendPeak = runPeak;
                }
            }
            if (pending && clock - pendEnd >= juce::jmax (settle, delay - maxS - before - 4)) decide (nI, nc);
            wp = (wp + 1) & mask;
            const int ro = (nI - delay) & mask;
            for (int c = 0; c < nc; ++c)
            {
                auto& s = chans[(size_t) c];
                b.setSample (c, i, (float) (listen ? s.org[(size_t) ro] - s.buf[(size_t) ro] : s.buf[(size_t) ro]));
            }
        }
    }

    /** Factory presets from the JSFX: sensitivity %, longest click ms. */
    struct Preset { const char* name; float sensitivity, longest; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", 50, 1.5f }, { "Light De-Click", 35, 1 }, { "Mouth Clicks", 60, 1 },
            { "Strong Mouth Clicks", 75, 1.5f }, { "Digital Clicks", 80, 0.5f }, { "Lip Smacks", 65, 1.2f } } };
        return p;
    }

    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        setParam (pSensitivity, p.sensitivity); setParam (pLongest, p.longest);
    }

private:
    static constexpr int ring = 4096, mask = ring - 1, order = 32, before = 3, after = 6;
    static constexpr double ratio = 2.0;   // how far a click's spike must stand above its neighbours
    struct Chan { std::array<double, ring> buf {}, org {}; double p1 = 0.0, p2 = 0.0; };

    // Fits a linear predictor to x[0..M) (sine window, autocorrelation, Levinson) and extends x by gl samples.
    void extend (std::vector<double>& x, int M, int gl)
    {
        const double pi = juce::MathConstants<double>::pi;
        for (int i = 0; i < M; ++i) tw[(size_t) i] = x[(size_t) i] * std::sin (pi * (i + 0.5) / M);
        std::array<double, order + 1> rw {}, aw {}, lt {};
        for (int k = 0; k <= order; ++k)
        {
            double s = 0.0;
            for (int i = 0; i < M - k; ++i) s += tw[(size_t) i] * tw[(size_t) (i + k)];
            rw[(size_t) k] = s;
        }
        rw[0] = rw[0] * 1.0001 + 1.0e-12;
        aw[0] = 1.0; double e = rw[0];
        for (int i = 1; i <= order; ++i)
        {
            double acc = rw[(size_t) i];
            for (int j = 1; j < i; ++j) acc += aw[(size_t) j] * rw[(size_t) (i - j)];
            const double k = -acc / e;
            for (int j = 1; j < i; ++j) lt[(size_t) j] = aw[(size_t) j] + k * aw[(size_t) (i - j)];
            for (int j = 1; j < i; ++j) aw[(size_t) j] = lt[(size_t) j];
            aw[(size_t) i] = k; e *= (1.0 - k * k);
        }
        for (int i = 0; i < gl; ++i)
        {
            double s = 0.0;
            for (int k = 1; k <= order; ++k) s -= aw[(size_t) k] * x[(size_t) (M + i - k)];
            x[(size_t) (M + i)] = juce::jmax (-1.0, juce::jmin (1.0, s));
        }
    }
    // Rebuilds samples gs..ge of one channel from the sound on each side.
    void repair (std::array<double, ring>& buf, int gs, int ge)
    {
        const int gl = ge - gs + 1, M = settle - after - 4;
        if (gl <= 0 || M <= order || (size_t) (M + gl) > ff.size()) return;
        for (int i = 0; i < M; ++i) ff[(size_t) i] = buf[(size_t) ((gs - M + i) & mask)];
        extend (ff, M, gl);
        for (int i = 0; i < M; ++i) bb[(size_t) i] = buf[(size_t) ((ge + M - i) & mask)];
        extend (bb, M, gl);
        const double pi = juce::MathConstants<double>::pi;
        for (int i = 0; i < gl; ++i)
        {
            const double w = (i + 0.5) / gl, ws = 0.5 - 0.5 * std::cos (pi * w);
            buf[(size_t) ((gs + i) & mask)] = (1.0 - ws) * ff[(size_t) (M + i)] + ws * bb[(size_t) (M + gl - 1 - i)];
        }
    }
    /** Settles the detection that is waiting: repairs it only if its spike stands clear of the
        largest one nearby, looking back over the 20 ms before the click and forward over
        everything heard since, each starting 1 ms clear of the click itself. */
    void decide (int nI, int nc)
    {
        const int a = (int) (clock - pendStart), b = (int) (clock - pendEnd);
        double near = 0.0;
        int age = a + guard;
        for (int k = juce::jmin (span, 3900 - age); k > 0; --k, ++age)
            near = juce::jmax (near, hist[(size_t) ((nI - age) & mask)]);
        for (age = b - guard; age > 0; --age)
            near = juce::jmax (near, hist[(size_t) ((nI - age) & mask)]);
        if (pendPeak >= ratio * near)
        {
            for (int c = 0; c < nc; ++c) repair (chans[(size_t) c].buf, nI - a - before, nI - b + after);
            clicks.fetch_add (1, std::memory_order_relaxed);
        }
        pending = false;
    }
    std::vector<Chan> chans;
    std::vector<double> tw, ff, bb, hist;
    double sampleRate = 48000.0, env = 0.0, aup = 0.0, adn = 0.0, runPeak = 0.0, pendPeak = 0.0;
    int delay = 384, settle = 144, guard = 48, span = 960, wp = 0, ageStart = 0, ageEnd = 0;
    juce::int64 clock = 0, pendStart = 0, pendEnd = 0;
    bool runOn = false, pending = false;
    std::atomic<int> clicks { 0 };
};
} // namespace ps
