#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Traverse: speech compressor. Audio path ported from the JSFX Traverse Compressor v0.4.1:
    10 ms RMS level detector on the louder channel, soft knee, one gain for all channels,
    attack and release on the gain reduction in dB, linear dry/wet mix, 20 ms parameter smoothing.

    Not carried over yet: the factory presets as a menu, the curve and level displays and the
    clip lamps. Input and output gain and the bypass crossfade are handled by the shared core. */
class Traverse : public Module
{
public:
    enum { pThreshold, pRatio, pAttack, pRelease, pKnee, pMakeup, pMix };
    Traverse() : Module ({ ParamSpec::number ("threshold", "Threshold", "dB", -48.0f, 0.0f, -18.0f),
                           ParamSpec::number ("ratio", "Ratio", ":1", 1.0f, 8.0f, 2.0f),
                           ParamSpec::number ("attack", "Attack", "ms", 2.0f, 150.0f, 25.0f, false, 0),
                           ParamSpec::number ("release", "Release", "ms", 40.0f, 1000.0f, 180.0f, false, 0),
                           ParamSpec::number ("knee", "Knee", "dB", 0.0f, 18.0f, 6.0f),
                           ParamSpec::number ("makeup", "Makeup", "dB", -18.0f, 18.0f, 0.0f),
                           ParamSpec::number ("mix", "Mix", "%", 0.0f, 100.0f, 100.0f, false, 0) }) {}
    juce::String getModuleId() const override { return "traverse"; }
    juce::String getDisplayName() const override { return "Traverse"; }
    juce::String getSubtitle() const override { return "Speech compressor"; }
    int getVersion() const override { return 1; }
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
        power = gr = 0.0;
        th = getParam (pThreshold); rat = getParam (pRatio); kn = getParam (pKnee); makeup = getParam (pMakeup);
        wet = getParam (pMix) / 100.0; attackMs = getParam (pAttack); releaseMs = getParam (pRelease);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = b.getNumChannels();
        const double tTh = getParam (pThreshold), tRat = getParam (pRatio), tKn = getParam (pKnee), tMk = getParam (pMakeup),
                     tWet = getParam (pMix) / 100.0, tAtt = getParam (pAttack), tRel = getParam (pRelease);
        const double ln10 = std::log (10.0), q = 1.0 - paramC;
        float blockMax = 0.0f;
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
            for (int c = 0; c < nc; ++c) b.setSample (c, i, (float) (b.getSample (c, i) * gain));
            // Reduction shown excludes makeup and follows the mix.
            const double shown = -20.0 * std::log (juce::jmax (1.0e-12, 1.0 + wet * (std::exp (-ln10 * gr / 20.0) - 1.0))) / ln10;
            blockMax = juce::jmax (blockMax, (float) shown);
        }
        float old = maxGr.load (std::memory_order_relaxed);
        while (blockMax > old && ! maxGr.compare_exchange_weak (old, blockMax)) {}
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
    double sampleRate = 48000.0, paramC = 0.0, rmsC = 0.0, power = 0.0, gr = 0.0;
    double th = -18.0, rat = 2.0, kn = 6.0, makeup = 0.0, wet = 1.0, attackMs = 25.0, releaseMs = 180.0;
    std::atomic<float> maxGr { 0.0f };
};
} // namespace ps
