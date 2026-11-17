#pragma once
#include "../core/Module.h"
#include "../core/Meters.h"
#include "../core/Fft.h"

namespace ps
{
/** Serac: de-esser. A Linkwitz-Riley crossover splits the voice at Frequency; the part above it
    is turned down while the part below is left alone, and the two add back together.

    What decides when to act is the share of the energy sitting above the crossover, not how loud
    the voice is. An "s" puts most of its energy up there while a vowel puts very little, so the
    same Sensitivity setting means the same thing on a quiet recording and a loud one. The only
    absolute figure in the detector is a gate at -60 dBFS, which stops it chasing silence.

    Range caps how far it will ever pull the sibilance down, which is what keeps an s from turning
    into a th: the band is never removed, only leaned on.

    Learn listens for up to eight seconds, picks out the frames that are mostly high-frequency,
    and sets Frequency to the bottom edge of where their energy actually sits. Sibilance ranges
    from about 5 kHz on a deep voice to 9 kHz on a bright one, so this saves hunting for it.

    Listen plays only what is being taken out, so the setting can be judged by ear.

    Not built yet: a spectral mode that would turn down only the frequencies that are sibilant
    rather than the whole band above the crossover, and the suite interface. */
class Serac : public Module
{
public:
    enum { pSensitivity, pFreq, pRange, pLearn, pListen };
    Serac() : Module ({ ParamSpec::number ("sensitivity", "Sensitivity", "%", 0.0f, 100.0f, 50.0f, false, 0).says ("How much of an 's' it takes to set this off. Higher acts on more words."),
                        ParamSpec::number ("freq", "Frequency", "Hz", 2000.0f, 12000.0f, 6500.0f, true, 0).says ("Where the sibilance sits. Lower for a deep voice, higher for a bright one. Learn can find it for you."),
                        ParamSpec::number ("range", "Range", "dB", 0.0f, 18.0f, 6.0f).says ("The most it will ever turn the sibilance down. This is what stops it lisping."),
                        ParamSpec::toggle ("learn", "Learn the voice", false).notSaved().says ("Hold this on over a few seconds of the voice and it sets Frequency to where that person's sibilance actually is."),
                        ParamSpec::toggle ("listen", "Listen to what's removed", false).notSaved().says ("Play only what is being taken out. You should hear esses, not whole words.") }),
              fft (fftSize) {}
    juce::String getModuleId() const override { return "serac"; }
    juce::String getDisplayName() const override { return "Serac"; }
    juce::String getSubtitle() const override { return "De-esser"; }
    int getVersion() const override { return 1; }
    juce::String getModuleMeterLabel() const override { return "Sibilance reduction"; }
    float readModuleMeter() override { return maxCut.exchange (0.0f); }
    bool isLearning() const { return learning.load(); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr;
        numChannels = juce::jmin (channels, (int) maxChannels);
        envC = 1.0 - std::exp (-1.0 / (0.002 * sr));          // 2 ms on the band energies
        atkC = 1.0 - std::exp (-1.0 / (0.001 * sr));          // 1 ms onto the cut
        relC = 1.0 - std::exp (-1.0 / (0.050 * sr));          // 50 ms off it
        const double pi = juce::MathConstants<double>::pi;
        for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize);
        reset();
    }
    void reset() override
    {
        for (auto& z : state) z.fill (0.0);
        eLow = eHigh = cut = 0.0; lastFreq = -1.0;
        learning.store (false); learnHeld = false; learned = 0.0f;
        std::fill (frame.begin(), frame.end(), 0.0); std::fill (sum.begin(), sum.end(), 0.0);
        framePos = 0; frames = 0; blocksLearnt = 0;
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        if (nc == 0) return;
        const double freq = juce::jlimit (2000.0, juce::jmin (12000.0, sampleRate * 0.45), (double) getParam (pFreq));
        const double range = getParam (pRange);
        const bool listen = getParam (pListen) > 0.5f;

        // A learn re-asserts its result until the host has taken it, then stops.
        if (learned > 0.0f)
        {
            if (std::abs (getParam (pFreq) - learned) > 1.0f) { setParam (pFreq, learned); noteSelfChanged (pFreq); }
            else learned = 0.0f;
        }
        if (const bool wantLearn = getParam (pLearn) > 0.5f; wantLearn != learnHeld)
        {
            learnHeld = wantLearn;
            if (wantLearn) { learning.store (true); frames = 0; blocksLearnt = 0; framePos = 0; std::fill (sum.begin(), sum.end(), 0.0); }
            else if (learning.load()) finishLearn();   // switched off early: settle on what was heard
        }

        if (freq != lastFreq)
        {
            lastFreq = freq;
            const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70711), ia = 1.0 / (1.0 + alpha);
            lp = { (1.0 - c) * 0.5 * ia, (1.0 - c) * ia, (1.0 - c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
            hp = { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
        }

        // Sensitivity 0 asks for almost pure sibilance before it acts, 100 acts on a little.
        const double trigger = 0.90 - 0.80 * (double) getParam (pSensitivity) / 100.0;
        const double gate = 1.0e-6;   // -60 dBFS on the summed energy, so silence is left alone
        const double ln10over20 = std::log (10.0) / 20.0;
        float blockMax = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            double low[maxChannels], high[maxChannels], sqLow = 0.0, sqHigh = 0.0, mono = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                double* z = state[(size_t) c].data();
                const double x = b.getSample (c, i);
                low[c] = f4 (lp, z, x); high[c] = f4 (hp, z + 4, x);
                sqLow += low[c] * low[c]; sqHigh += high[c] * high[c]; mono += x;
            }
            sqLow /= nc; sqHigh /= nc;
            eLow += (sqLow - eLow) * envC; eHigh += (sqHigh - eHigh) * envC;

            const double total = eLow + eHigh;
            const double share = total > gate ? eHigh / total : 0.0;
            // How far past the trigger, over a fixed span rather than the whole way to 1.0: a
            // real "s" never puts quite all of its energy above the crossover, so measuring
            // against 1.0 would mean the full Range was never reached and the figure on the
            // control would be a promise the plugin does not keep.
            const double over = juce::jlimit (0.0, 1.0, (share - trigger) / 0.20);
            const double wanted = range * over;
            cut += (wanted - cut) * (wanted > cut ? atkC : relC);
            const double g = std::exp (-cut * ln10over20);
            blockMax = juce::jmax (blockMax, (float) cut);

            for (int c = 0; c < nc; ++c)
                b.setSample (c, i, (float) (listen ? high[c] * (1.0 - g) : low[c] + high[c] * g));

            if (learning.load()) feedLearn (mono / nc);
        }
        float old = maxCut.load();
        while (blockMax > old && ! maxCut.compare_exchange_weak (old, blockMax)) {}
    }

    /** Factory presets. Starting points only; they have not been set by ear. */
    struct Preset { const char* name; float sensitivity, freq, range; };
    static const std::array<Preset, 6>& factoryPresets()
    {
        static const std::array<Preset, 6> p { { { "Default", 50, 6500, 6 }, { "Light Touch", 35, 7000, 4 },
            { "Male Voice", 50, 5500, 6 }, { "Female Voice", 50, 7500, 6 },
            { "Bright Mic", 60, 7000, 9 }, { "Heavy Sibilance", 75, 6000, 12 } } };
        return p;
    }
    int getNumFactoryPresets() const override { return (int) factoryPresets().size(); }
    juce::String getFactoryPresetName (int i) const override { return factoryPresets()[(size_t) i].name; }
    void applyFactoryPreset (int i) override
    {
        const auto& p = factoryPresets()[(size_t) i];
        setParam (pSensitivity, p.sensitivity); setParam (pFreq, p.freq); setParam (pRange, p.range);
    }

private:
    static constexpr int maxChannels = 8, fftSize = 1024, hop = 512, bins = fftSize / 2 + 1;

    static double f4 (const std::array<double, 5>& c, double* z, double x)
    {
        double t = c[0] * x + z[0]; z[0] = c[1] * x - c[3] * t + z[1]; z[1] = c[2] * x - c[4] * t; x = t;
        t = c[0] * x + z[2]; z[2] = c[1] * x - c[3] * t + z[3]; z[3] = c[2] * x - c[4] * t; return t;
    }

    /** Collects one analysis frame at a time while Learn is on, and settles on a frequency
        either when eight seconds are up or when Learn is switched off. */
    void feedLearn (double x)
    {
        frame[(size_t) framePos++] = x;
        if (framePos < fftSize) return;
        for (int i = 0; i < fftSize; ++i) { buf[(size_t) (2 * i)] = frame[(size_t) i] * window[(size_t) i]; buf[(size_t) (2 * i + 1)] = 0.0; }
        for (int i = 0; i < fftSize - hop; ++i) frame[(size_t) i] = frame[(size_t) (i + hop)];
        framePos = fftSize - hop;
        fft.forward (buf.data());

        double below = 0.0, above = 0.0;
        const double perBin = sampleRate / fftSize;
        for (int k = 0; k < bins; ++k)
        {
            mag[(size_t) k] = buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
            (k * perBin < 2000.0 ? below : above) += mag[(size_t) k];
        }
        // Keep the frames that are mostly high: those are the ones carrying sibilance.
        if (above + below > 1.0e-9 && above / (above + below) > 0.5)
        {
            for (int k = 0; k < bins; ++k) sum[(size_t) k] += mag[(size_t) k];
            ++frames;
        }
        if (++blocksLearnt * hop < (int) (sampleRate * 8.0)) return;
        finishLearn();
    }

    /** Settles on a frequency from the frames collected, and stops learning. */
    void finishLearn()
    {
        learning.store (false);
        if (frames >= 8)
        {
            // The bottom edge of where that energy sits: the first 15 % of it, between 2 and 12 kHz.
            const double perBin = sampleRate / fftSize;
            double band = 0.0;
            const int lo = (int) std::ceil (2000.0 / perBin), hi = juce::jmin (bins - 1, (int) (12000.0 / perBin));
            for (int k = lo; k <= hi; ++k) band += sum[(size_t) k];
            double run = 0.0;
            for (int k = lo; k <= hi; ++k)
            {
                run += sum[(size_t) k];
                if (run >= 0.15 * band) { learned = (float) juce::jlimit (2000.0, 12000.0, k * perBin); break; }
            }
        }
    }

    Fft fft;
    std::array<double, 5> lp {}, hp {};
    std::array<double, fftSize * 2> buf {};
    std::array<double, bins> mag {};
    std::array<std::array<double, 8>, maxChannels> state {};
    std::array<double, fftSize> window {}, frame {};
    std::array<double, bins> sum {};
    double sampleRate = 48000.0, envC = 0.0, atkC = 0.0, relC = 0.0;
    double eLow = 0.0, eHigh = 0.0, cut = 0.0, lastFreq = -1.0;
    int numChannels = 2, framePos = 0, frames = 0, blocksLearnt = 0;
    bool learnHeld = false;
    float learned = 0.0f;
    std::atomic<bool> learning { false };
    std::atomic<float> maxCut { 0.0f };
};
} // namespace ps
