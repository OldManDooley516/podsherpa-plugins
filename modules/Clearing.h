#pragma once
#include "../core/Module.h"
#include "../core/Fft.h"
#include "../core/Meters.h"

namespace ps
{
/** Clearing: noise reduction. Audio path ported from the JSFX Clearing v0.4.1: 2048-point
    frames every 512 samples, a running noise estimate (minimum of the smoothed spectrum over
    the last few seconds) or a learned noise profile, and a per-frequency gain that never drops
    below the Reduction setting. 2048 samples of latency.

    As in 0.4.1, the noise estimate only measures frames that hold real audio from end to end
    and is kept across playback starts, so reduction is there from the first moment.

    Not carried over yet: the Learn button and saving a learned profile with the project (the
    engine supports both through learnStart() / learnStop(), but there is no button or saved
    state yet, so the Noise source switch has nothing to select), the spectrum display and the
    factory presets as a menu. Listen is a normal saved parameter here, unlike the JSFX.
    Input and output gain and the bypass crossfade are handled by the shared core. */
class Clearing : public Module
{
public:
    enum { pReduction, pSensitivity, pSmoothing, pSource, pListen };
    Clearing() : Module ({ ParamSpec::number ("reduction", "Reduction", "dB", 0.0f, 30.0f, 12.0f),
                           ParamSpec::number ("sensitivity", "Sensitivity", "%", 0.0f, 100.0f, 50.0f, false, 0),
                           ParamSpec::number ("smoothing", "Smoothing", "%", 0.0f, 100.0f, 50.0f, false, 0),
                           ParamSpec::choice ("source", "Noise source", { "Auto", "Learned" }, 0),
                           ParamSpec::toggle ("listen", "Listen to what's removed", false).notSaved() }), fft (N)
    {
        const double pi = juce::MathConstants<double>::pi;
        for (int i = 0; i < N; ++i) wn[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / N);
        for (auto* v : { &nf, &pn, &bmin, &m1, &m2, &prof, &acc, &ps, &gsm, &gtm, &pm }) v->assign (bins, 0.0);
    }
    juce::String getModuleId() const override { return "clearing"; }
    juce::String getDisplayName() const override { return "Clearing"; }
    juce::String getSubtitle() const override { return "Noise reduction"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return N; }
    juce::String getModuleMeterLabel() const override { return "Average reduction"; }
    float readModuleMeter() override { return maxRed.exchange (0.0f); }

    void learnStart() { command.store (1); }
    void learnStop() { command.store (2); }
    bool isLearning() const { return learning.load(); }
    bool hasProfile() const { return have.load(); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; chans.assign ((size_t) channels, {});
        for (auto& c : chans) { c.in.assign (ring, 0.0); c.out.assign (ring, 0.0); c.f.assign ((size_t) N * 2, 0.0); }
        reset();
    }
    void reset() override   // audio state only: the noise estimate and any learned profile are kept
    {
        for (auto& c : chans) { std::fill (c.in.begin(), c.in.end(), 0.0); std::fill (c.out.begin(), c.out.end(), 0.0); }
        std::fill (ps.begin(), ps.end(), 0.0); std::fill (gsm.begin(), gsm.end(), 1.0); std::fill (acc.begin(), acc.end(), 0.0);
        first = true; live = 0; zrun = 0; lacc = 0; lcount = 0; wp = 0; hc = 0; learning.store (false);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), (int) chans.size());
        if (nc == 0) return;
        for (int i = 0; i < n; ++i)
        {
            double mag = 0.0;
            for (int c = 0; c < nc; ++c) { const double x = b.getSample (c, i); chans[(size_t) c].in[(size_t) wp] = x; mag += std::abs (x); }
            if (mag < 1.0e-8) { if (++zrun >= 64) live = 0; } else zrun = 0;
            ++live;
            wp = (wp + 1) & mask;
            if (++hc >= hop) { hc = 0; frame (nc); }
            const int rp = (wp - 1 - N) & mask;
            for (int c = 0; c < nc; ++c) { auto& s = chans[(size_t) c]; b.setSample (c, i, (float) s.out[(size_t) rp]); s.out[(size_t) rp] = 0.0; }
        }
    }

    /** Factory presets from the JSFX: reduction dB, sensitivity %, smoothing %. */
    struct Preset { const char* name; float reduction, sensitivity, smoothing; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", 12, 50, 50 }, { "Light Cleanup", 8, 40, 60 }, { "Room Tone Removal", 14, 55, 55 },
            { "Remote Guest Cleanup", 16, 60, 65 }, { "Heavy Noise Rescue", 22, 70, 65 }, { "Quiet Room Polish", 5, 30, 70 } } };
        return p;
    }

    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        setParam (pReduction, p.reduction); setParam (pSensitivity, p.sensitivity); setParam (pSmoothing, p.smoothing);
    }

private:
    static constexpr int N = 2048, hop = 512, ring = 4096, mask = ring - 1;
    static constexpr size_t bins = 1025;
    struct Chan { std::vector<double> in, out, f; };

    void frame (int nc)
    {
        const double ln10 = std::log (10.0);
        std::fill (pm.begin(), pm.end(), 0.0);
        for (int c = 0; c < nc; ++c)
        {
            auto& s = chans[(size_t) c];
            for (int j = 0; j < N; ++j) { const int idx = (wp - N + j) & mask; s.f[(size_t) (2 * j)] = s.in[(size_t) idx] * wn[(size_t) j]; s.f[(size_t) (2 * j + 1)] = 0.0; }
            fft.forward (s.f.data());
            for (size_t k = 0; k < bins; ++k) pm[k] += s.f[2 * k] * s.f[2 * k] + s.f[2 * k + 1] * s.f[2 * k + 1];
        }
        for (size_t k = 0; k < bins; ++k) pm[k] /= nc;

        int cmd = command.exchange (0);
        if (cmd == 1) { learning.store (true); lcount = 0; lacc = 0; std::fill (acc.begin(), acc.end(), 0.0); cmd = 0; }
        if (learning.load())
        {
            if (live >= N + 64) { for (size_t k = 0; k < bins; ++k) acc[k] += pm[k]; ++lacc; }
            ++lcount;
            if (cmd == 2 || lcount * 512.0 / sampleRate > 8.0)
            {
                learning.store (false);
                if (lacc >= 8) { for (size_t k = 0; k < bins; ++k) prof[k] = acc[k] / lacc; have.store (true); }
            }
        }
        const bool usep = getParam (pSource) > 0.5f && have.load();
        const bool listen = getParam (pListen) > 0.5f;
        const double sm = (double) getParam (pSmoothing) / 100.0, sn = (double) getParam (pSensitivity) / 100.0;
        const double al = 1.0 + 3.0 * sn, gmin = std::exp (-ln10 * (double) getParam (pReduction) / 20.0);
        const double co = 0.5, cc = 0.6 * (1.0 - sm) + 0.05 * sm;
        const bool full = live >= N + 64;
        if (full && wuc < 60) ++wuc;
        const double wu = juce::jmin (1.0, juce::jmax (0.0, (wuc - 15) / 45.0));
        for (size_t k = 0; k < bins; ++k)
        {
            const double P = pm[k];
            if (first) ps[k] = P; else ps[k] += (P - ps[k]) * 0.5;
            if (full)
            {
                if (estOk) { pn[k] += (P - pn[k]) * 0.1; bmin[k] = juce::jmin (bmin[k], pn[k]); }
                else { pn[k] = P; bmin[k] = P; m1[k] = P; m2[k] = P; }
            }
            nf[k] = juce::jmin (bmin[k], juce::jmin (m1[k], m2[k]));
            const double nfv = usep ? prof[k] : nf[k] * 2.0;
            double G = 1.0 - al * nfv / juce::jmax (ps[k], 1.0e-20);
            G = juce::jmin (1.0, juce::jmax (gmin, G));
            if (! usep) G = 1.0 - (1.0 - G) * wu;
            gsm[k] += (G - gsm[k]) * (G > gsm[k] ? co : cc);
        }
        first = false;
        if (full)
        {
            if (estOk) { if (++bcnt >= 80) { bcnt = 0; for (size_t k = 0; k < bins; ++k) { m2[k] = m1[k]; m1[k] = bmin[k]; bmin[k] = pn[k]; } } }
            else { estOk = true; bcnt = 0; }
        }
        gtm[0] = gsm[0]; gtm[1024] = gsm[1024];
        for (size_t k = 1; k < 1024; ++k) gtm[k] = (gsm[k - 1] + 2.0 * gsm[k] + gsm[k + 1]) * 0.25;
        const double wgt = juce::jmin (1.0, sm * 1.5);
        double redSum = 0.0; int redN = 0;
        const int kLo = (int) (100.0 * N / sampleRate), kHi = juce::jmin (1024, (int) (8000.0 * N / sampleRate));
        for (size_t k = 0; k < bins; ++k)
        {
            const double gl = gsm[k] + (gtm[k] - gsm[k]) * wgt;
            gtm[k] = listen ? 1.0 - gl : gl;
            if ((int) k >= kLo && (int) k <= kHi) { redSum += -20.0 * std::log10 (juce::jmax (1.0e-6, gl)); ++redN; }
        }
        for (int c = 0; c < nc; ++c)
        {
            auto& s = chans[(size_t) c];
            for (size_t k = 0; k < bins; ++k)
            {
                const double go = gtm[k];
                s.f[2 * k] *= go; s.f[2 * k + 1] *= go;
                if (k > 0 && k < 1024) { const size_t j = (size_t) N - k; s.f[2 * j] *= go; s.f[2 * j + 1] *= go; }
            }
            fft.inverse (s.f.data());
            for (int j = 0; j < N; ++j) { const int idx = (wp - N + j) & mask; s.out[(size_t) idx] += s.f[(size_t) (2 * j)] * wn[(size_t) j] * (1.0 / 3072.0); }
        }
        const float red = redN > 0 ? (float) (redSum / redN) : 0.0f;
        float old = maxRed.load (std::memory_order_relaxed);
        while (red > old && ! maxRed.compare_exchange_weak (old, red)) {}
    }
    Fft fft;
    std::array<double, N> wn {};
    std::vector<Chan> chans;
    std::vector<double> nf, pn, bmin, m1, m2, prof, acc, ps, gsm, gtm, pm;
    double sampleRate = 48000.0;
    int wp = 0, hc = 0, live = 0, zrun = 0, lacc = 0, lcount = 0, bcnt = 0, wuc = 0;
    bool first = true, estOk = false;
    std::atomic<int> command { 0 };
    std::atomic<bool> learning { false }, have { false };
    std::atomic<float> maxRed { 0.0f };
};
} // namespace ps
