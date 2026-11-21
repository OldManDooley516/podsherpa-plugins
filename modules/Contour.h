#pragma once
#include "../core/Module.h"
#include "../core/LinearPhase.h"

namespace ps
{
/** Contour: five-band voice EQ. Audio path ported from the JSFX Contour v0.4.1: a high-pass
    (6 to 24 dB/oct), three bells and a high shelf, with 15 ms parameter smoothing once per
    block, and a linear-phase mode that applies the same curve with a 2048-sample delay.

    Difference from the JSFX: the Phase switch is read when playback is prepared, so changing it
    takes effect at the next start (the JSFX switches live). Listen (Nov 21) plays only the
    part of the input one band works on, as the JSFX does: a band-pass at a bell's frequency and
    width, the low-pass twin of the high-pass (exactly what it removes, as Dropoff 0.4.1 does it),
    or a high-pass at the shelf's corner. Band gain is ignored, so a band can be listened to
    before it is set. Switching crossfades through the EQ in 10 ms, and in Linear mode the
    listened signal is delayed to match, so nothing jumps in time. Input and output gain
    and the bypass crossfade are handled by the shared core. */
class Contour : public Module
{
public:
    enum { pHpFreq, pHpSlope, pHpOn, pB2Freq, pB2Gain, pB2Q, pB2On, pB3Freq, pB3Gain, pB3Q, pB3On,
           pB4Freq, pB4Gain, pB4Q, pB4On, pShelfFreq, pShelfGain, pShelfOn, pPhase, pListen };
    Contour() : Module ({
        ParamSpec::number ("hp.freq", "High-pass", "Hz", 20.0f, 500.0f, 80.0f, true, 0).says ("Everything below this is rolled off. Rumble, footsteps and handling noise live down here."),
        ParamSpec::choice ("hp.slope", "High-pass slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" }, 1).says ("How sharply the high-pass cuts. Steeper removes more but can thin a deep voice."),
        ParamSpec::toggle ("hp.on", "High-pass on", true).says ("Turn the high-pass on or off."),
        ParamSpec::number ("b2.freq", "Band 2", "Hz", 20.0f, 20000.0f, 200.0f, true, 0).says ("Which frequency this band works on. Around 200 Hz is the warmth and body of a voice."),
        ParamSpec::number ("b2.gain", "Band 2 gain", "dB", -18.0f, 18.0f, 0.0f).says ("Boost or cut this band. Cutting what you dislike usually beats boosting what you want."),
        ParamSpec::number ("b2.q", "Band 2 Q", "", 0.3f, 8.0f, 1.2f, true).says ("How wide a range around the frequency this band touches. Higher is narrower."),
        ParamSpec::toggle ("b2.on", "Band 2 on", true).says ("Turn this band on or off."),
        ParamSpec::number ("b3.freq", "Band 3", "Hz", 20.0f, 20000.0f, 3200.0f, true, 0).says ("Which frequency this band works on. Around 3 kHz is where clarity and presence sit."),
        ParamSpec::number ("b3.gain", "Band 3 gain", "dB", -18.0f, 18.0f, 0.0f).says ("Boost or cut this band. A small cut here takes the edge off a harsh voice."),
        ParamSpec::number ("b3.q", "Band 3 Q", "", 0.3f, 8.0f, 1.0f, true).says ("How wide a range around the frequency this band touches. Higher is narrower."),
        ParamSpec::toggle ("b3.on", "Band 3 on", true).says ("Turn this band on or off."),
        ParamSpec::number ("b4.freq", "Band 4", "Hz", 20.0f, 20000.0f, 6500.0f, true, 0).says ("Which frequency this band works on. Around 6 kHz is where sibilance and sparkle live."),
        ParamSpec::number ("b4.gain", "Band 4 gain", "dB", -18.0f, 18.0f, 0.0f).says ("Boost or cut this band. Cut to calm hissy esses, boost for more detail."),
        ParamSpec::number ("b4.q", "Band 4 Q", "", 0.3f, 8.0f, 3.0f, true).says ("How wide a range around the frequency this band touches. Higher is narrower."),
        ParamSpec::toggle ("b4.on", "Band 4 on", true).says ("Turn this band on or off."),
        ParamSpec::number ("shelf.freq", "High shelf", "Hz", 20.0f, 20000.0f, 10000.0f, true, 0).says ("Where the high shelf starts. Everything above it is lifted or dropped together."),
        ParamSpec::number ("shelf.gain", "High shelf gain", "dB", -18.0f, 18.0f, 0.0f).says ("Boost for air and openness, cut to take the top off a bright recording."),
        ParamSpec::toggle ("shelf.on", "High shelf on", true).says ("Turn the high shelf on or off."),
        ParamSpec::choice ("phase", "Phase", { "Natural (0 ms)", "Linear (43 ms)" }, 0).says ("Linear keeps the timing of the voice exact but adds delay. Natural adds no delay at all."),
        ParamSpec::choice ("listen", "Listen", { "Off", "High-pass", "Band 2", "Band 3", "Band 4", "High shelf" }, 0).notSaved()
            .says ("Play only the part of the voice one band works on, to find what to change. Always off when a project opens.") }) {}
    juce::String getModuleId() const override { return "contour"; }
    juce::String getDisplayName() const override { return "Contour"; }
    juce::String getSubtitle() const override { return "Voice EQ"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return linear ? LinearPhase::latency : 0; }
    int getMaxLatencySamples() const override { return LinearPhase::latency; }

    void prepare (double sr, int maxBlock, int channels) override
    {
        sampleRate = sr; numChannels = juce::jmin (channels, (int) maxChannels);
        dryBuf.setSize (maxChannels, juce::jmax (1, maxBlock));
        listenDelay.setSize (maxChannels, LinearPhase::latency);
        linear = getParam (pPhase) >= 0.5f;
        fir.prepare (numChannels);
        reset();
    }
    void reset() override
    {
        for (auto& z : state) z.fill (0.0);
        for (int i = 0; i < 18; ++i) sm[i] = getParam (i);
        build(); fir.reset(); lastCheck = -1.0;
        resetListen();
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        // Listen needs the input as it was; keep a copy only while Listen is on or fading.
        const int wantListen = (int) std::floor (getParam (pListen) + 0.5f);
        const bool listening = wantListen != 0 || listenBand != 0;
        if (listening)
        {
            if (dryBuf.getNumSamples() < n) dryBuf.setSize (maxChannels, n, false, false, true);   // a host that broke its promise
            for (int c = 0; c < nc; ++c) dryBuf.copyFrom (c, 0, b, c, 0, n);
        }
        processEq (b, n, nc);
        if (listening) processListen (b, n, nc, wantListen);
    }

private:
    void processEq (juce::AudioBuffer<float>& b, int n, int nc)
    {
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

public:
    /** Factory presets from the JSFX. Values in parameter order, without Phase. */
    struct Preset { const char* name; float v[18]; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { {
            { "Default",      { 80, 1, 1, 200, 0, 1.2f, 1, 3200, 0, 1, 1, 6500, 0, 3, 1, 10000, 0, 1 } },
            { "Warm Male Voice",     { 70, 1, 1, 150, 2, 0.9f, 1, 3200, -1.5f, 1.2f, 1, 6500, -1.5f, 2, 1, 10000, 0, 1 } },
            { "Bright Female Voice",     { 100, 1, 1, 250, -1.5f, 1, 1, 3200, 1.5f, 1, 1, 6500, 0, 3, 1, 10000, 2.5f, 1 } },
            { "Remove Mud",     { 80, 1, 1, 250, -3, 1, 1, 400, -1.5f, 1.5f, 1, 6500, 0, 3, 1, 10000, 0, 1 } },
            { "Presence Lift",     { 80, 1, 1, 200, -1, 1.2f, 1, 3000, 2, 1, 1, 6500, 0, 3, 1, 10000, 1, 1 } },
            { "Rumble Cut",     { 100, 3, 1, 200, 0, 1.2f, 1, 3200, 0, 1, 1, 6500, 0, 3, 1, 10000, 0, 1 } } } };
        return p;
    }

    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        for (int k = 0; k < 18; ++k) setParam (k, p.v[k]);
    }

    bool hasFrequencyResponse() const override { return true; }
    bool hasSpectrum() const override { return true; }
    std::vector<CurveHandle> getCurveHandles() const override
    {
        return { { "High-pass", "HP", pHpFreq, -1, -1, pHpOn },
                 { "Band 2", "2", pB2Freq, pB2Gain, pB2Q, pB2On },
                 { "Band 3", "3", pB3Freq, pB3Gain, pB3Q, pB3On },
                 { "Band 4", "4", pB4Freq, pB4Gain, pB4Q, pB4On },
                 { "High shelf", "HS", pShelfFreq, pShelfGain, -1, pShelfOn } };
    }
    int getListenParam() const override { return pListen; }

    /** |H(e^jw)| of every active stage, multiplied, in dB. Built from the controls with the same
        filter design the audio path uses, so the curve is the real response, and it follows a
        knob or a dragged dot even when the host is not running audio through the plugin. */
    float responseDbAt (float hz) const override
    {
        if (sampleRate <= 0.0) return 0.0f;
        const double w = 2.0 * juce::MathConstants<double>::pi * (double) hz / sampleRate;
        if (w >= juce::MathConstants<double>::pi) return 0.0f;
        double v[18];
        for (int i = 0; i < 18; ++i) v[i] = getParam (i);
        std::array<Coef, 6> c6; std::array<bool, 6> on6;
        design (v, sampleRate, c6, on6);
        const double cw = std::cos (w), sw = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        double total = 0.0;
        for (size_t i = 0; i < c6.size(); ++i)
        {
            if (! on6[i]) continue;
            const auto& c = c6[i];
            const double nr = c[0] + c[1] * cw + c[2] * c2, ni = -(c[1] * sw + c[2] * s2);
            const double dr = 1.0 + c[3] * cw + c[4] * c2, di = -(c[3] * sw + c[4] * s2);
            const double den = dr * dr + di * di;
            if (den < 1.0e-30) continue;
            total += 10.0 * std::log10 (juce::jmax (1.0e-30, (nr * nr + ni * ni) / den));
        }
        return (float) juce::jlimit (-30.0, 30.0, total);
    }

private:
    static constexpr int maxChannels = 8;
    using Coef = std::array<double, 5>;
    using Coefs = std::array<Coef, 6>; using Active = std::array<bool, 6>;
    template <typename C, typename A>
    static void set (C& cf, A& on, int i, double b0, double b1, double b2, double a0, double a1, double a2)
    {
        const double ia = 1.0 / a0; cf[(size_t) i] = { b0 * ia, b1 * ia, b2 * ia, a1 * ia, a2 * ia }; on[(size_t) i] = true;
    }
    static void ident (Coefs& cf, Active& on, int i) { cf[(size_t) i] = { 1.0, 0.0, 0.0, 0.0, 0.0 }; on[(size_t) i] = false; }

    /** The filter design, shared by the audio path (smoothed values) and the drawn curve (the
        controls as set). v holds the 18 parameters in order, without Phase. */
    static void design (const double* v, double sr, Coefs& cf, Active& on)
    {
        const double pi = juce::MathConstants<double>::pi;
        auto omega = [&] (double f) { return 2.0 * pi * juce::jmin (f, sr * 0.45) / sr; };
        auto hp1 = [&] (int i, double f) { const double K = std::tan (pi * juce::jmin (f, sr * 0.45) / sr); set (cf, on, i, 1.0, -1.0, 0.0, 1.0 + K, K - 1.0, 0.0); };
        auto hp2 = [&] (int i, double f, double Q) { const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q); set (cf, on, i, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al); };
        auto bell = [&] (int i, double f, double g, double Q)
        {
            const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q), A = std::exp (std::log (10.0) * g / 40.0);
            set (cf, on, i, 1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
        };
        auto hshelf = [&] (int i, double f, double g)
        {
            const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / 1.41421, A = std::exp (std::log (10.0) * g / 40.0), sA = 2.0 * std::sqrt (A) * al;
            set (cf, on, i, A * ((A + 1.0) + (A - 1.0) * c + sA), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sA),
                 (A + 1.0) - (A - 1.0) * c + sA, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sA);
        };
        ident (cf, on, 0); ident (cf, on, 1);
        if (v[2] >= 0.5)
        {
            const int sl = (int) std::floor (v[1] + 0.5);
            if (sl == 0) hp1 (0, v[0]);
            else if (sl == 1) hp2 (0, v[0], 0.70711);
            else if (sl == 2) { hp1 (0, v[0]); hp2 (1, v[0], 1.0); }
            else { hp2 (0, v[0], 0.5412); hp2 (1, v[0], 1.30656); }
        }
        for (int k = 0; k < 3; ++k)
        {
            const double ge = v[4 + 4 * k] * v[6 + 4 * k];
            if (std::abs (ge) > 0.01) bell (2 + k, v[3 + 4 * k], ge, v[5 + 4 * k]); else ident (cf, on, 2 + k);
        }
        const double ge = v[16] * v[17];
        if (std::abs (ge) > 0.01) hshelf (5, v[15], ge); else ident (cf, on, 5);
    }
    void build() { design (sm, sampleRate, cf, active); }
    //==========================================================================
    void resetListen()
    {
        for (auto& z : lz) z.fill (0.0);
        listenDelay.clear(); listenPos = 0; listenBand = 0; listenGain = 0.0;
    }

    /** The filter Listen plays for a band, from the smoothed controls so a band being swept
        is heard moving. Stages are reused from the EQ's own design helpers. */
    void designListen (int band)
    {
        lactive.fill (false);
        const double pi = juce::MathConstants<double>::pi, sr = sampleRate;
        auto omega = [&] (double f) { return 2.0 * pi * juce::jmin (f, sr * 0.45) / sr; };
        auto lp1 = [&] (int i, double f) { const double K = std::tan (pi * juce::jmin (f, sr * 0.45) / sr); set (lc, lactive, i, K, K, 0.0, 1.0 + K, K - 1.0, 0.0); };
        auto lp2 = [&] (int i, double f, double Q) { const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q); set (lc, lactive, i, (1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al); };
        auto hp2 = [&] (int i, double f, double Q) { const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q); set (lc, lactive, i, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al); };
        auto bp = [&] (int i, double f, double Q) { const double w0 = omega (f), c = std::cos (w0), al = std::sin (w0) / (2.0 * Q); set (lc, lactive, i, al, 0.0, -al, 1.0 + al, -2.0 * c, 1.0 - al); };
        if (band == 1)
        {   // the low-pass twin of the high-pass, same corner and slope
            const int sl = (int) std::floor (sm[1] + 0.5);
            if (sl == 0) lp1 (0, sm[0]);
            else if (sl == 1) lp2 (0, sm[0], 0.70711);
            else if (sl == 2) { lp1 (0, sm[0]); lp2 (1, sm[0], 1.0); }
            else { lp2 (0, sm[0], 0.5412); lp2 (1, sm[0], 1.30656); }
        }
        else if (band >= 2 && band <= 4) bp (0, sm[3 + 4 * (band - 2)], sm[5 + 4 * (band - 2)]);
        else if (band == 5) hp2 (0, sm[15], 0.70711);
    }

    /** Replaces the EQ's output with the listened band. Changing band fades to the EQ and back
        (10 ms each way) so the filter can start clean; turning Listen off fades to the EQ. */
    void processListen (juce::AudioBuffer<float>& b, int n, int nc, int want)
    {
        const double step = 1.0 / (0.010 * sampleRate);
        const int lat = linear ? LinearPhase::latency : 0;
        if (listenBand != 0) designListen (listenBand);
        for (int i = 0; i < n; ++i)
        {
            if (want != listenBand)
            {
                listenGain -= step;
                if (listenGain <= 0.0)
                {
                    listenGain = 0.0; listenBand = want;
                    for (auto& z : lz) z.fill (0.0);
                    listenDelay.clear(); listenPos = 0;
                    if (listenBand != 0) designListen (listenBand);
                }
            }
            else if (listenBand != 0) listenGain = juce::jmin (1.0, listenGain + step);
            if (listenBand == 0) continue;
            for (int c = 0; c < nc; ++c)
            {
                double y = dryBuf.getSample (c, i);
                auto& z = lz[(size_t) c];
                for (int k = 0; k < 2; ++k)
                    if (lactive[(size_t) k])
                    {
                        const auto& q = lc[(size_t) k];
                        const double t = q[0] * y + z[(size_t) (2 * k)];
                        z[(size_t) (2 * k)] = q[1] * y - q[3] * t + z[(size_t) (2 * k + 1)];
                        z[(size_t) (2 * k + 1)] = q[2] * y - q[4] * t;
                        y = t;
                    }
                if (lat > 0)
                {   // line the listened signal up with the linear-phase EQ's delay
                    float* d = listenDelay.getWritePointer (c);
                    const double out = d[listenPos]; d[listenPos] = (float) y; y = out;
                }
                const double eq = b.getSample (c, i);
                b.setSample (c, i, (float) (eq + (y - eq) * listenGain));
            }
            if (lat > 0 && ++listenPos >= lat) listenPos = 0;
        }
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
    // Listen
    juce::AudioBuffer<float> dryBuf, listenDelay;
    std::array<Coef, 2> lc {}; std::array<bool, 2> lactive {};
    std::array<std::array<double, 4>, maxChannels> lz {};
    int listenBand = 0, listenPos = 0; double listenGain = 0.0;
};
} // namespace ps
