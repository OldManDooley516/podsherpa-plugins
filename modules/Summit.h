#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"

namespace ps
{
/** Summit: look-ahead true-peak limiter. Audio path ported from the JSFX Summit Limiter v0.3.1.
    Peaks at the threshold are scaled to the ceiling; a 3 ms look-ahead moving average of the
    gain guarantees the ceiling without a hard clipper.

    Not carried over yet: the loudness target selector and LUFS meter (display only), the
    factory presets, and the JSFX rule that caps output gain at minus the ceiling. Input and
    output gain and the bypass crossfade are handled by the shared core. */
class Summit : public Module
{
public:
    enum { pThreshold, pCeiling, pSpeed, pMode, pTruePeak };
    Summit() : Module ({ ParamSpec::number ("threshold", "Threshold", "dB", -24.0f, 0.0f, -3.0f),
                         ParamSpec::number ("ceiling", "Ceiling", "dB", -12.0f, 0.0f, -1.0f),
                         ParamSpec::number ("speed", "Speed", "", 0.0f, 10.0f, 6.0f),          // 0 = slow, 10 = fast
                         ParamSpec::choice ("mode", "Mode", { "Clean", "Punch", "Warm" }, 0),
                         ParamSpec::toggle ("truepeak", "True peak", true) }) {}
    juce::String getModuleId() const override { return "summit"; }
    juce::String getDisplayName() const override { return "Summit"; }
    juce::String getSubtitle() const override { return "Limiter"; }
    int getVersion() const override { return 1; }
    int getLatencySamples() const override { return lookback; }
    juce::String getModuleMeterLabel() const override { return "Gain reduction"; }
    float readModuleMeter() override { return -gainToDb (minGain.exchange (1.0f)); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr; numChannels = juce::jmin (channels, maxChannels);
        look = juce::jmax (1, (int) std::floor (sr * 0.003 + 0.5));   // 3 ms look-ahead
        window = look + 1;                                            // gain-averaging window
        lookback = look + TruePeak::delay;                            // peak-hold span = audio delay
        abuf = lookback + 1; capacity = lookback + 2;
        audio.assign ((size_t) numChannels, std::vector<double> ((size_t) abuf, 0.0));
        gainBuf.assign ((size_t) window, 1.0);
        qVal.assign ((size_t) capacity, 0.0); qTime.assign ((size_t) capacity, 0);
        tp.prepare (numChannels);
        paramC = std::exp (-1.0 / (0.02 * sr));
        reset();
    }
    void reset() override
    {
        for (auto& a : audio) std::fill (a.begin(), a.end(), 0.0);
        std::fill (gainBuf.begin(), gainBuf.end(), 1.0);
        tp.reset();
        gpos = apos = head = tail = 0; clock = 0; gainSum = (double) window; gf = gs = 1.0;
        thrDb = getParam (pThreshold); ceilDb = getParam (pCeiling); speed = 10.0 - getParam (pSpeed);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        static const double ln10 = std::log (10.0);
        const double pThr = getParam (pThreshold), pCeil = getParam (pCeiling), pSpd = 10.0 - getParam (pSpeed);
        const int mode = (int) std::floor (getParam (pMode) + 0.5f); const bool tpOn = getParam (pTruePeak) > 0.5f;
        const double fk = mode == 1 ? 0.2 : 0.4, sk = mode == 1 ? 1.0 : 2.0, mixf = mode == 1 ? 0.8 : 0.6;
        const int nc = juce::jmin (b.getNumChannels(), numChannels);
        double blockMin = 1.0, xs[maxChannels] = {};

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            thrDb = paramC * thrDb + (1.0 - paramC) * pThr;
            ceilDb = paramC * ceilDb + (1.0 - paramC) * pCeil;
            speed = paramC * speed + (1.0 - paramC) * pSpd;
            const double ceiling = std::exp (ln10 * ceilDb / 20.0), invThr = std::exp (-ln10 * thrDb / 20.0);

            // Peaks at the threshold are scaled to 1.0 (= the ceiling after the limiter).
            double peak = 0.0, tpx = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                double x = (double) b.getSample (c, i) * invThr;
                if (mode == 2) x = sat (x);
                xs[c] = x; peak = juce::jmax (peak, std::abs (x)); tpx = juce::jmax (tpx, tp.push (c, x, tpOn));
            }
            if (tpOn) peak = juce::jmax (peak, tpx);

            // Monotonic deque: maximum over the most recent lookback + 1 detector values.
            while (head != tail && qTime[(size_t) head] < clock - lookback) head = (head + 1) % capacity;
            while (head != tail && qVal[(size_t) ((tail + capacity - 1) % capacity)] <= peak) tail = (tail + capacity - 1) % capacity;
            qVal[(size_t) tail] = peak; qTime[(size_t) tail] = clock; tail = (tail + 1) % capacity;
            const double safe = juce::jmin (1.0, 1.0 / juce::jmax (1.0e-12, qVal[(size_t) head]));

            // Speed 0..10 -> 40 ms .. 900 ms. Fast and slow release envelopes are blended; each
            // is capped by the safe gain, so the blend never exceeds it.
            const double relMs = 40.0 * std::exp (3.1135 * speed / 10.0);
            const double cf = std::exp (-1.0 / (0.001 * relMs * fk * sampleRate)), cs = std::exp (-1.0 / (0.001 * relMs * sk * sampleRate));
            gf = juce::jmin (safe, 1.0 - cf * (1.0 - gf)); gs = juce::jmin (safe, 1.0 - cs * (1.0 - gs));
            const double releaseGain = mixf * gf + (1.0 - mixf) * gs;

            // Every gain in this moving average protects the delayed sample.
            gainSum += releaseGain - gainBuf[(size_t) gpos]; gainBuf[(size_t) gpos] = releaseGain; gpos = (gpos + 1) % window;
            const double applied = juce::jmin (1.0, juce::jmax (0.0, gainSum / window));

            const int readPos = (apos + 1) % abuf;
            for (int c = 0; c < nc; ++c)
            {
                auto& a = audio[(size_t) c];
                const double out = a[(size_t) readPos]; a[(size_t) apos] = xs[c];
                b.setSample (c, i, (float) (out * applied * ceiling));
            }
            apos = readPos; ++clock;
            blockMin = juce::jmin (blockMin, applied);
        }
        float old = minGain.load(); const float bm = (float) blockMin;
        while (bm < old && ! minGain.compare_exchange_weak (old, bm)) {}
    }

    /** Factory presets from the JSFX: threshold dB, ceiling dB, speed, true peak, mode. */
    struct Preset { const char* name; float threshold, ceiling, speed; bool truePeak; int mode; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", -3, -1, 6, true, 0 }, { "Podcast Standard", -6, -1, 5, true, 0 },
            { "Gentle Safety", -2, -1, 4, true, 0 }, { "Loud and Punchy", -9, -1, 7, true, 1 },
            { "Warm Broadcast", -7, -1, 5, true, 2 }, { "Streaming Safe", -5, -2, 5, true, 0 } } };
        return p;
    }

private:
    static constexpr int maxChannels = 8;
    // Warm: unity gain for small signals, rounds off peaks. Sits ahead of the limiter.
    static double sat (double v)
    {
        const double u = 0.7 * v;
        return std::abs (u) >= 3.0 ? (u < 0.0 ? -1.0 : 1.0) / 0.7 : (u * (27.0 + u * u) / (27.0 + 9.0 * u * u)) / 0.7;
    }
    double sampleRate = 48000.0, paramC = 0.0, thrDb = -3.0, ceilDb = -1.0, speed = 4.0, gainSum = 1.0, gf = 1.0, gs = 1.0;
    int numChannels = 2, look = 144, window = 145, lookback = 152, abuf = 153, capacity = 154;
    int gpos = 0, apos = 0, head = 0, tail = 0; juce::int64 clock = 0;
    TruePeak tp;
    std::vector<std::vector<double>> audio;
    std::vector<double> gainBuf, qVal; std::vector<juce::int64> qTime;
    std::atomic<float> minGain { 1.0f };
};
} // namespace ps
