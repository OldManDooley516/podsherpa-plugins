#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Traverse: speech compressor. Audio path ported from the JSFX Traverse Compressor v0.4.1:
    10 ms RMS level detector on the louder channel, soft knee, one gain for all channels,
    attack and release on the gain reduction in dB, linear dry/wet mix, 20 ms parameter smoothing.

    Interface (Nov 21): the editor draws the transfer curve with a live dot where the voice is
    right now, lets the threshold dot be dragged (sideways threshold, up and down ratio), and
    shows input and output level behind the 12-second reduction history. Delta plays only what
    the squeeze takes off the voice: the input minus the compressed signal before Makeup, so it
    is the part that is being pushed down, not the level boost. It fades in and out over 10 ms,
    is never saved, and leaves the audio bit-identical when off. Input and output gain and the
    bypass crossfade are handled by the shared core. */
class Traverse : public Module
{
public:
    enum { pThreshold, pRatio, pAttack, pRelease, pKnee, pMakeup, pMix, pDelta };
    Traverse() : Module ({ ParamSpec::number ("threshold", "Threshold", "dB", -48.0f, 0.0f, -18.0f).says ("Where it starts squeezing. Lower means more of the voice gets worked on."),
                           ParamSpec::number ("ratio", "Ratio", ":1", 1.0f, 8.0f, 2.0f).says ("How hard it squeezes once it starts."),
                           ParamSpec::number ("attack", "Attack", "ms", 2.0f, 150.0f, 25.0f, false, 0).says ("How quickly it clamps down. Fast tames hard consonants, slow lets them through."),
                           ParamSpec::number ("release", "Release", "ms", 40.0f, 1000.0f, 180.0f, false, 0).says ("How quickly it eases off again once the voice drops back."),
                           ParamSpec::number ("knee", "Knee", "dB", 0.0f, 18.0f, 6.0f).says ("How gently it eases in around the threshold. Softer is less noticeable."),
                           ParamSpec::number ("makeup", "Makeup", "dB", -18.0f, 18.0f, 0.0f).says ("Puts the level back up after the squeezing has taken it down."),
                           ParamSpec::number ("mix", "Mix", "%", 0.0f, 100.0f, 100.0f, false, 0) .says ("Blends the squeezed voice back with the untouched one. Lower keeps more of the original."),
                           ParamSpec::toggle ("delta", "Delta", false).notSaved()
                               .says ("Plays only what the squeezing takes off the voice.") }) {}
    juce::String getModuleId() const override { return "traverse"; }
    juce::String getDisplayName() const override { return "Traverse"; }
    juce::String getSubtitle() const override { return "Speech compressor"; }
    int getVersion() const override { return 1; }
    bool hasTransferCurve() const override { return true; }
    float getCurveThresholdDb() const override { return getParam (pThreshold); }
    /** The same maths the audio thread uses, so the drawn curve is the curve, not a sketch
        of it. Reads parameters directly rather than the smoothed values, so the picture
        follows the control as it is dragged instead of lagging behind it. */
    float curveOutputDb (float inputDb) const override
    {
        return (float) (inputDb - reduction (inputDb, getParam (pThreshold), getParam (pRatio), getParam (pKnee)));
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
        return getParam (pDelta) >= 0.5f ? "Delta: you are hearing only what the squeezing takes off" : juce::String();
    }

    juce::String getModuleMeterLabel() const override { return "Gain reduction"; }
    float readModuleMeter() override { return maxGr.exchange (0.0f); }

    void prepare (double sr, int, int) override
    {
        sampleRate = sr;
        paramC = std::exp (-1.0 / (0.02 * sr)); rmsC = std::exp (-1.0 / (0.01 * sr));
        reset();
    }
    void reset() override
    {
        power = gr = outPower = deltaAmt = 0.0;
        th = getParam (pThreshold); rat = getParam (pRatio); kn = getParam (pKnee); makeup = getParam (pMakeup);
        wet = getParam (pMix) / 100.0; attackMs = getParam (pAttack); releaseMs = getParam (pRelease);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = b.getNumChannels();
        const double tTh = getParam (pThreshold), tRat = getParam (pRatio), tKn = getParam (pKnee), tMk = getParam (pMakeup),
                     tWet = getParam (pMix) / 100.0, tAtt = getParam (pAttack), tRel = getParam (pRelease);
        const double ln10 = std::log (10.0), q = 1.0 - paramC;
        float blockMax = 0.0f, inMax = 0.0f, outMax = 0.0f;
        const double deltaTarget = getParam (pDelta) >= 0.5f ? 1.0 : 0.0, deltaStep = 1.0 / (0.01 * sampleRate);
        for (int i = 0; i < n; ++i)
        {
            th = paramC * th + q * tTh; rat = paramC * rat + q * tRat; kn = paramC * kn + q * tKn;
            makeup = paramC * makeup + q * tMk; wet = paramC * wet + q * tWet;
            attackMs = paramC * attackMs + q * tAtt; releaseMs = paramC * releaseMs + q * tRel;

            double peakSq = 0.0;
            for (int c = 0; c < nc; ++c) { const double x = b.getSample (c, i); peakSq = juce::jmax (peakSq, x * x); }
            power = rmsC * power + (1.0 - rmsC) * peakSq;
            const double level = 10.0 * std::log (juce::jmax (1.0e-12, power)) / ln10;
            const double wanted = reduction (level, th, rat, kn);
            const double coef = std::exp (-1.0 / (0.001 * sampleRate * (wanted > gr ? attackMs : releaseMs)));
            gr = coef * gr + (1.0 - coef) * wanted;
            // One identical gain for every channel. Linear dry/wet avoids a unity bump.
            const double compressed = std::exp (ln10 * (makeup - gr) / 20.0);
            const double gain = 1.0 + wet * (compressed - 1.0);
            // Delta: what the squeeze removes, before Makeup and following Mix. Only touched while
            // it is on or fading, so with Delta off this is the same multiply as before.
            if (deltaAmt != deltaTarget) deltaAmt = deltaTarget > deltaAmt ? juce::jmin (1.0, deltaAmt + deltaStep) : juce::jmax (0.0, deltaAmt - deltaStep);
            const double squeezed = 1.0 + wet * (std::exp (-ln10 * gr / 20.0) - 1.0);
            double outSq = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                const double x = b.getSample (c, i);
                double y = x * gain;
                outSq = juce::jmax (outSq, y * y);   // the meter reads the compressor, not the delta
                if (deltaAmt > 0.0) y += deltaAmt * (x * (1.0 - squeezed) - y);
                b.setSample (c, i, (float) y);
            }
            outPower = rmsC * outPower + (1.0 - rmsC) * outSq;
            inMax = juce::jmax (inMax, (float) power);
            outMax = juce::jmax (outMax, (float) outPower);
            // Reduction shown excludes makeup and follows the mix.
            const double shown = -20.0 * std::log (juce::jmax (1.0e-12, 1.0 + wet * (std::exp (-ln10 * gr / 20.0) - 1.0))) / ln10;
            blockMax = juce::jmax (blockMax, (float) shown);
        }
        float old = maxGr.load (std::memory_order_relaxed);
        while (blockMax > old && ! maxGr.compare_exchange_weak (old, blockMax)) {}
        storeMax (maxInPow, inMax);
        storeMax (maxOutPow, outMax);
    }

    /** Factory presets from the JSFX: threshold, ratio, attack, release, makeup, mix. Knee is not touched. */
    struct Preset { const char* name; float threshold, ratio, attack, release, makeup, mix; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", -18, 2, 25, 180, 0, 100 }, { "Natural Voice", -20, 1.6f, 30, 220, 1.5f, 100 },
            { "Radio Voice", -24, 3.5f, 10, 120, 4, 100 }, { "Interview Guest", -24, 2.5f, 15, 250, 3, 100 },
            { "Heavy Squeeze", -30, 5, 8, 150, 6, 100 }, { "Gentle Glue", -30, 4, 20, 200, 3, 50 } } };
        return p;
    }

    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        setParam (pThreshold, p.threshold); setParam (pRatio, p.ratio); setParam (pAttack, p.attack);
        setParam (pRelease, p.release); setParam (pMakeup, p.makeup); setParam (pMix, p.mix);
    }

private:
    static double reduction (double level, double threshold, double ratio, double knee)
    {
        const double x = level - threshold, slope = 1.0 - 1.0 / ratio;
        if (knee <= 0.0) return juce::jmax (0.0, x) * slope;
        if (x <= -knee / 2.0) return 0.0;
        if (x >= knee / 2.0) return x * slope;
        return slope * (x + knee / 2.0) * (x + knee / 2.0) / (2.0 * knee);
    }
    static void storeMax (std::atomic<float>& a, float v)
    {
        float old = a.load (std::memory_order_relaxed);
        while (v > old && ! a.compare_exchange_weak (old, v)) {}
    }
    static float toDb (float pow) { return 10.0f * std::log10 (juce::jmax (1.0e-12f, pow)); }
    double sampleRate = 48000.0, paramC = 0.0, rmsC = 0.0, power = 0.0, gr = 0.0, outPower = 0.0, deltaAmt = 0.0;
    double th = -18.0, rat = 2.0, kn = 6.0, makeup = 0.0, wet = 1.0, attackMs = 25.0, releaseMs = 180.0;
    std::atomic<float> maxGr { 0.0f }, maxInPow { 0.0f }, maxOutPow { 0.0f };
};
} // namespace ps
