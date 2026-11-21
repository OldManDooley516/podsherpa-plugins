#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Glade: voice expander. Turns room tone and loud breaths down in the pauses, gently, while
    leaving the speaker's quiet words, soft consonants and word endings alone. Built VST3-first
    (Nov 21) from Codex's handoff; there is no JSFX version.

    How it decides: a 5 ms RMS detector, on the loudest channel after an 80 Hz high-pass (so
    rumble and handling noise cannot hold it open), compares the level with Threshold. Below it,
    the level is turned down by Ratio for every dB under, through a 6 dB soft knee, and never by
    more than Range. One gain is used for every channel.

    What protects the speech:
    - Look-ahead. The audio is delayed 20 ms and the detector hears it first, so the expander is
      already open when a soft H, F or S begins instead of opening during it. On the test clips
      20 ms protected soft fricative onsets clearly better than 5 or 10 ms (worst clip -0.9 dB
      at 5 ms, -0.3 dB at 20 ms) at no cost to pause reduction. This is the plugin's only
      latency and the host is told about it; the suite is for editing, so 20 ms is no problem.
    - Hold. Once the voice drops below the threshold the gain stays put for Hold before it starts
      to close, so a word's quiet ending is not faded away.
    - Range. It never takes off more than Range (6 dB to start, from the handoff), so a pause is
      lowered, not gated: room tone stays, just quieter.

    Not built: a dedicated breath detector. A loud breath is turned down only if it is quieter
    than Threshold. If taming a breath means setting Threshold where soft speech gets hurt too,
    that is the case the handoff asks to record so Sean can decide on breath detection.

    Delta plays only what is taken off, so a damaged consonant can be heard on its own. It fades
    over 10 ms, is never saved, and leaves the audio unchanged when off. */
class Glade : public Module
{
public:
    enum { pThreshold, pRange, pRatio, pOpen, pHold, pClose, pDelta };
    Glade() : Module ({ ParamSpec::number ("threshold", "Threshold", "dB", -60.0f, -15.0f, -45.0f)
                            .says ("Below this it starts turning down. Set it just under the quietest words."),
                        ParamSpec::number ("range", "Range", "dB", 0.0f, 24.0f, 6.0f)
                            .says ("The most it will ever turn a pause down. Room tone stays, just quieter."),
                        ParamSpec::number ("ratio", "Ratio", ":1", 1.0f, 4.0f, 2.0f)
                            .says ("How steeply it turns down once the sound drops under the threshold."),
                        ParamSpec::number ("open", "Open", "ms", 0.5f, 20.0f, 2.0f, true)
                            .says ("How fast it opens for the voice. It hears ahead, so fast is safe."),
                        ParamSpec::number ("hold", "Hold", "ms", 0.0f, 300.0f, 100.0f, false, 0)
                            .says ("How long it waits after the voice stops. Saves the ends of words."),
                        ParamSpec::number ("close", "Close", "ms", 20.0f, 600.0f, 180.0f, true, 0)
                            .says ("How slowly it settles down into the pause. Slower sounds more natural."),
                        ParamSpec::toggle ("delta", "Delta", false).notSaved()
                            .says ("Plays only what is being turned down. You should hear pauses, not words.") }) {}

    juce::String getModuleId() const override { return "glade"; }
    juce::String getDisplayName() const override { return "Glade"; }
    juce::String getSubtitle() const override { return "Voice expander"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return lookahead; }

    juce::String getModuleMeterLabel() const override { return "Pause reduction"; }
    float readModuleMeter() override { return maxGr.exchange (0.0f); }

    bool hasTransferCurve() const override { return true; }
    juce::String getCurveTitle() const override { return "Expander curve"; }
    float getCurveThresholdDb() const override { return getParam (pThreshold); }
    float curveOutputDb (float inputDb) const override
    {
        return (float) (inputDb - reduction (inputDb, getParam (pThreshold), getParam (pRatio), getParam (pRange)));
    }
    bool readCurveLevels (float& inDb, float& outDb) override
    {
        inDb = toDb (maxInPow.exchange (0.0f));
        outDb = toDb (maxOutPow.exchange (0.0f));
        return true;
    }
    int getCurveThresholdParam() const override { return pThreshold; }
    int getCurveRatioParam() const override { return pRatio; }
    juce::String getAuditionWarning() const override
    {
        return getParam (pDelta) >= 0.5f ? "Delta: you are hearing only what Glade turns down" : juce::String();
    }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr;
        numChannels = juce::jlimit (1, (int) maxChannels, channels);
        lookahead = juce::jmax (1, (int) std::round (0.020 * sr));
        for (auto& d : delay) d.assign ((size_t) lookahead, 0.0f);
        rmsC = std::exp (-1.0 / (0.005 * sr));
        const double w0 = 2.0 * juce::MathConstants<double>::pi * 80.0 / sr;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70711), ia = 1.0 / (1.0 + alpha);
        hp = { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
        reset();
    }
    void reset() override
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& z : hpState) z.fill (0.0);
        pos = 0; power = outPower = gr = held = 0.0; holdLeft = 0; deltaAmt = 0.0;
    }

    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        if (nc == 0) return;
        const double th = getParam (pThreshold), range = getParam (pRange), ratio = getParam (pRatio);
        const double openC = std::exp (-1.0 / (0.001 * sampleRate * getParam (pOpen)));
        const double closeC = std::exp (-1.0 / (0.001 * sampleRate * getParam (pClose)));
        const int holdN = (int) (0.001 * sampleRate * getParam (pHold));
        const double deltaTarget = getParam (pDelta) >= 0.5f ? 1.0 : 0.0, deltaStep = 1.0 / (0.01 * sampleRate);
        const double ln10 = std::log (10.0);
        float blockMax = 0.0f, inMax = 0.0f, outMax = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Detector on the incoming (undelayed) audio: it is 20 ms ahead of what is heard.
            double sq = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                const double s = hpf (hpState[(size_t) c].data(), b.getSample (c, i));
                sq = juce::jmax (sq, s * s);
            }
            power = rmsC * power + (1.0 - rmsC) * sq;
            const double level = 10.0 * std::log (juce::jmax (1.0e-12, power)) / ln10;
            const double wanted = reduction (level, th, ratio, range);

            // Hold: anything that opens it resets the clock; it may close only once that runs out.
            if (wanted <= held) { held = wanted; holdLeft = holdN; }
            else if (holdLeft > 0) --holdLeft;
            else held = wanted;

            const double coef = held < gr ? openC : closeC;
            gr = coef * gr + (1.0 - coef) * held;
            const double g = std::exp (-ln10 * gr / 20.0);

            if (deltaAmt != deltaTarget)
                deltaAmt = deltaTarget > deltaAmt ? juce::jmin (1.0, deltaAmt + deltaStep) : juce::jmax (0.0, deltaAmt - deltaStep);

            double outSq = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                auto& d = delay[(size_t) c];
                const double x = d[(size_t) pos];
                d[(size_t) pos] = b.getSample (c, i);
                double y = x * g;
                outSq = juce::jmax (outSq, y * y);
                if (deltaAmt > 0.0) y += deltaAmt * (x * (1.0 - g) - y);
                b.setSample (c, i, (float) y);
            }
            for (int c = nc; c < b.getNumChannels(); ++c) b.setSample (c, i, 0.0f);
            if (++pos >= lookahead) pos = 0;

            outPower = rmsC * outPower + (1.0 - rmsC) * outSq;
            inMax = juce::jmax (inMax, (float) power);
            outMax = juce::jmax (outMax, (float) outPower);
            blockMax = juce::jmax (blockMax, (float) gr);
        }
        storeMax (maxGr, blockMax);
        storeMax (maxInPow, inMax);
        storeMax (maxOutPow, outMax);
    }

    /** Factory presets. Starting points chosen by measurement on the test clips, not by ear. */
    struct Preset { const char* name; float threshold, range, ratio, open, hold, close; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { {
            { "Default",          -45, 6,  2.0f, 2, 100, 180 },
            { "Light Touch",      -48, 4,  1.5f, 2, 150, 250 },
            { "Room Tone",        -50, 8,  2.0f, 2, 120, 220 },
            { "Breath Tamer",     -40, 8,  2.0f, 1, 80,  140 },
            { "Quiet Voice",      -55, 6,  2.0f, 2, 120, 200 },
            { "Mic Bleed",        -38, 12, 3.0f, 1, 80,  120 } } };
        return p;
    }
    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        setParam (pThreshold, p.threshold); setParam (pRange, p.range); setParam (pRatio, p.ratio);
        setParam (pOpen, p.open); setParam (pHold, p.hold); setParam (pClose, p.close);
    }

    /** The reduction in dB for a detector level: Ratio - 1 dB per dB under the threshold, eased
        in over a 6 dB knee, capped at Range. Shared by the audio path and the drawn curve. */
    static double reduction (double level, double threshold, double ratio, double range)
    {
        constexpr double knee = 6.0;
        const double x = threshold - level, slope = ratio - 1.0;
        double r;
        if (x <= -knee / 2.0) r = 0.0;
        else if (x >= knee / 2.0) r = x * slope;
        else r = slope * (x + knee / 2.0) * (x + knee / 2.0) / (2.0 * knee);
        return juce::jlimit (0.0, juce::jmax (0.0, range), r);
    }

private:
    static constexpr int maxChannels = 8;
    double hpf (double* z, double x) const
    {
        const double y = hp[0] * x + z[0];
        z[0] = hp[1] * x - hp[3] * y + z[1];
        z[1] = hp[2] * x - hp[4] * y;
        return y;
    }
    static void storeMax (std::atomic<float>& a, float v)
    {
        float old = a.load (std::memory_order_relaxed);
        while (v > old && ! a.compare_exchange_weak (old, v)) {}
    }
    static float toDb (float pow) { return 10.0f * std::log10 (juce::jmax (1.0e-12f, pow)); }

    std::array<std::vector<float>, maxChannels> delay;
    std::array<std::array<double, 2>, maxChannels> hpState {};
    std::array<double, 5> hp {};
    double sampleRate = 48000.0, rmsC = 0.0, power = 0.0, outPower = 0.0, gr = 0.0, held = 0.0, deltaAmt = 0.0;
    int numChannels = 2, lookahead = 960, pos = 0, holdLeft = 0;
    std::atomic<float> maxGr { 0.0f }, maxInPow { 0.0f }, maxOutPow { 0.0f };
};
} // namespace ps
