#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Altitude: voice leveler. Audio path ported from the JSFX Altitude v0.4.1: 80 Hz high-passed
    level detectors (400 ms and 30 ms) on the louder channel, a slew-limited ride gain toward
    the target, a 200 ms hold after the voice drops below the silence level, and a look-ahead
    delay on the audio.

    Not carried over yet: "Learn my voice", the gain history display, the factory presets as a menu and the clip
    lamps. The JSFX's "Output trim" slider is not ported because the JSFX never applies it.
    Input and output gain and the bypass crossfade are handled by the shared core. */
class Altitude : public Module
{
public:
    enum { pTarget, pSilence, pMaxBoost, pMaxCut, pRise, pFall, pSoft, pLoud, pLookahead };
    Altitude() : Module ({ ParamSpec::number ("target", "Target", "dB", -36.0f, -12.0f, -22.0f),
                           ParamSpec::number ("silence", "Silence", "dB", -80.0f, -25.0f, -50.0f, false, 0),
                           ParamSpec::number ("maxboost", "Max boost", "dB", 0.0f, 12.0f, 6.0f),
                           ParamSpec::number ("maxcut", "Max cut", "dB", 0.0f, 18.0f, 6.0f),
                           ParamSpec::number ("rise", "Gain rise", "dB/s", 0.2f, 6.0f, 1.5f),
                           ParamSpec::number ("fall", "Gain fall", "dB/s", 1.0f, 18.0f, 6.0f),
                           ParamSpec::number ("soft", "Soft correction", "%", 0.0f, 100.0f, 100.0f, false, 0),
                           ParamSpec::number ("loud", "Loud correction", "%", 0.0f, 100.0f, 100.0f, false, 0),
                           ParamSpec::number ("lookahead", "Latency", "ms", 0.0f, 100.0f, 15.0f, false, 0) }) {}
    juce::String getModuleId() const override { return "altitude"; }
    juce::String getDisplayName() const override { return "Altitude"; }
    juce::String getSubtitle() const override { return "Voice leveler"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return delaySamples; }
    int getMaxLatencySamples() const override { return juce::jmax (0, bufLen - 1); }
    juce::String getModuleMeterLabel() const override { return "Ride gain"; }
    float readModuleMeter() override { return shownGain.load (std::memory_order_relaxed); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr;
        bufLen = juce::jmax (1, (int) std::floor (sr * 0.1 + 0.5)) + 1;
        delaySamples = juce::jmin (bufLen - 1, (int) std::floor (sr * (double) getParam (pLookahead) * 0.001 + 0.5));
        chans.assign ((size_t) channels, {});
        for (auto& c : chans) c.buf.assign ((size_t) bufLen, 0.0f);
        slowC = std::exp (-1.0 / (0.4 * sr)); fastC = std::exp (-1.0 / (0.03 * sr));
        hpC = std::exp (-2.0 * juce::MathConstants<double>::pi * 80.0 / sr);
        reset();
    }
    void reset() override
    {
        for (auto& c : chans) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.hp = c.prev = 0.0; }
        pos = 0; power = fastPower = gain = rideMix = 0.0; hold = 0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), (int) chans.size());
        const double target = getParam (pTarget), silence = getParam (pSilence), maxBoost = getParam (pMaxBoost), maxCut = getParam (pMaxCut),
                     soft = getParam (pSoft), loud = getParam (pLoud);
        const double riseStep = (double) getParam (pRise) / sampleRate, fallStep = (double) getParam (pFall) / sampleRate;
        // The look-ahead follows the control while playing, as it does in the JSFX. The buffer is
        // already as long as the longest look-ahead, so only the read position moves.
        delaySamples = juce::jmin (bufLen - 1, (int) std::floor (sampleRate * (double) getParam (pLookahead) * 0.001 + 0.5));
        const double ln10 = std::log (10.0), mixStep = 1.0 / (0.05 * sampleRate);
        double actual = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double p = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                auto& s = chans[(size_t) c]; const double x = b.getSample (c, i);
                s.hp = hpC * (s.hp + x - s.prev); s.prev = x;
                p = juce::jmax (p, s.hp * s.hp);
            }
            power = slowC * power + (1.0 - slowC) * p;
            fastPower = fastC * fastPower + (1.0 - fastC) * p;
            const double fastDb = 20.0 * std::log (juce::jmax (1.0e-6, std::sqrt (fastPower))) / ln10;
            if (fastDb > silence) hold = (int) std::floor (sampleRate * 0.2); else hold = juce::jmax (0, hold - 1);
            const double levelDb = 20.0 * std::log (juce::jmax (1.0e-6, std::sqrt (power))) / ln10;
            const double err = target - levelDb;
            const double correction = err * (err >= 0.0 ? soft : loud) / 100.0;
            const double desired = hold > 0 ? juce::jmin (maxBoost, juce::jmax (-maxCut, correction)) : 0.0;
            const double step = desired > gain ? riseStep : fallStep;
            gain += juce::jmin (step, juce::jmax (-step, desired - gain));
            rideMix += juce::jmin (mixStep, juce::jmax (-mixStep, 1.0 - rideMix));   // 50 ms fade-in after a start
            actual = gain * rideMix;
            const double mult = std::exp (ln10 * actual / 20.0);
            int rd = pos - delaySamples; if (rd < 0) rd += bufLen;
            for (int c = 0; c < nc; ++c)
            {
                auto& s = chans[(size_t) c];
                s.buf[(size_t) pos] = b.getSample (c, i);
                b.setSample (c, i, (float) ((double) s.buf[(size_t) rd] * mult));
            }
            if (++pos >= bufLen) pos = 0;
        }
        shownGain.store ((float) actual, std::memory_order_relaxed);
    }

    /** Factory presets from the JSFX. */
    struct Preset { const char* name; float target, silence, maxBoost, maxCut, rise, fall, soft, loud, lookahead; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", -22, -50, 6, 6, 1.5f, 6, 100, 100, 15 }, { "Gentle Ride", -22, -50, 4, 4, 1, 4, 100, 100, 15 },
            { "Interview Balance", -21, -52, 8, 9, 2.5f, 8, 100, 100, 15 }, { "Distant Speaker Lift", -22, -58, 12, 4, 2, 5, 100, 100, 15 },
            { "Quick Catch", -22, -50, 6, 8, 4, 12, 100, 100, 5 }, { "Loud Talker Tamer", -23, -50, 3, 12, 2, 10, 100, 100, 15 } } };
        return p;
    }

private:
    struct Chan { std::vector<float> buf; double hp = 0.0, prev = 0.0; };
    std::vector<Chan> chans;
    double sampleRate = 48000.0, slowC = 0.0, fastC = 0.0, hpC = 0.0;
    double power = 0.0, fastPower = 0.0, gain = 0.0, rideMix = 0.0;
    int bufLen = 4801, delaySamples = 720, pos = 0, hold = 0;
    std::atomic<float> shownGain { 0.0f };
};
} // namespace ps
