#pragma once
#include "../core/Module.h"

namespace ps
{
/** Gust: de-plosive. UNFINISHED - see "Where it stands" before trusting it on a deep voice.

     A "p" or "b" fires a puff of air at the microphone and lands as a short,
    heavy thump low down, usually under 200 Hz and lasting 10 to 80 ms. Scree cannot help with
    these by design: it patches at most 3 ms and hunts for sharp high-frequency spikes, while a
    pop is the opposite, slow and low. Of the repairs Sean had made by hand across two episodes,
    30 of 156 were exactly this.

    A Linkwitz-Riley crossover splits the voice at Frequency. The part below it is turned down
    while the thump lasts and the part above is untouched, so the words keep their edges; the two
    halves add back together.

    What marks a pop out is where its energy sits, not that it is sudden. Measured against
    plosives placed into real speech at known moments, how far the low end jumps past its own
    recent average tells you almost nothing: ordinary speech jumps further than a pop does, which
    is why an early version of this acted on a sixth of every recording. What does separate them
    is how much louder the low end is than everything above it at that instant. A voice's lowest
    note arrives with harmonics stacked above it; a pop is pressure under the voice with little
    on top.

    Where to split matters more than anything else here, and it was measured rather than guessed.
    The pops Sean had repaired by hand carry about half their energy between 120 and 200 Hz, a
    third between 80 and 120, and very little below 50; a first version of this was tuned against
    an invented pop that was all sub-90 Hz, and it found none of his. Splitting at 120 Hz catches
    40 of 40 pops shaped like his while acting on 0.4% of the speech around them. Going higher
    lets the voice's own fundamental into the low band and the test falls apart: at 250 Hz the
    same catch rate costs 34%.

    What the ratio is measured against is the voice's own usual balance over the last second or
    so, not a fixed figure, because how bottom-heavy a voice is varies enormously. On a deep,
    closely miked guest whose fundamental sits under the split, a fixed test reads every syllable
    as a pop: one of Sean's recordings fired on 21% of itself. Judged against that speaker's own
    normal balance the same recording settles to 3%, with no change on the others. It also means
    a steady rumble simply becomes that voice's normal and is left alone, which is Dropoff's job
    rather than this one's.

    One guard on top: the low end must also be loud against the programme as a whole. In a pause
    there is no voice above the room tone, so the ratio alone would read every silence as a pop;
    measured, pauses sit some 40 dB below a pop on this count.

    Both are ratios, so the setting means the same thing on a quiet recording and a loud one.

    Listen plays only what is being taken out.

    Where it stands (Nov 17, evening). UNFINISHED: do not use on a deep voice. On three of
    Sean's four test recordings it behaves - about 10 pops a minute on BG Pepper, 1 on Tony
    Mormino, 1.4 on the sibilant voice - and it finds 39 of 40 pops planted into real speech.
    On Joe Sinicrope it fires 128 times a minute and is ducking his voice, not thumps.

    This was written up three times in one day and the detour is worth recording, because the
    mistake in the middle is an easy one to repeat.

    Nov 16: called a fault, reasoning that no recording has two pops a second and that
    Sinicrope's deep, closely miked voice - 32 dB more energy under 50 Hz than the others, with
    its fundamental under the split - must be reading its own syllables as pops.

    Nov 17 morning: reversed, because Sean listened to the removed track and heard "bumps and
    clicks, not voice", and because no removed event was longer than 80 ms while a syllable is
    100-300 ms. Both observations were real. Neither meant what was concluded from them.

    Nov 17 evening: reversed back, with the arithmetic that settles it. Sinicrope's pitch is
    98 Hz, so his vocal folds produce one sharp pulse every 10.2 ms. The gap between the events
    Gust acts on has a median of 9.8 ms. The detector is locked to his pitch: it is firing once
    per glottal pulse, which is why no event reaches 80 ms - a pitch period is 10 ms, so events
    cannot be longer whether they are pops or voice. The 80 ms test could never have separated
    the two, and the pitch period is the test that does.

    An isolated glottal pulse sounds like a click, and a train of them sounds like a buzz, so
    "bumps and clicks" was an honest description of a voice with its vocal tract stripped away.
    Played back at a fair level the removed track is recognisably Sinicrope speaking, with the
    high end missing: Gust is taking the bottom of his voice.

    The lesson, which cost most of a day: when a measurement and a listening test disagree,
    neither wins by default. Find the third test that explains both. Here it was one division.

    What it needs is what was already written on Nov 16 and then abandoned: the split has to sit
    under the speaker's fundamental, which means tracking their pitch rather than trusting a
    fixed 120 Hz. Serac's Learn solves the same shape of problem for sibilance. Until then this
    is a draft, safe on ordinary voices and wrong on deep ones.

    The test for any fix: the median gap between events must stop matching the speaker's pitch
    period. 9.8 ms against a 10.2 ms period is a failure; no relationship is a pass.

    Not built yet: the suite interface. */
class Gust : public Module
{
public:
    enum { pSensitivity, pFreq, pRange, pListen };
    Gust() : Module ({ ParamSpec::number ("sensitivity", "Sensitivity", "%", 0.0f, 100.0f, 50.0f, false, 0).says ("How big a thump has to be before the low end is ducked."),
                       ParamSpec::number ("freq", "Frequency", "Hz", 40.0f, 250.0f, 120.0f, true, 0).says ("The line a thump lives below. Raise it for a deep voice, lower it for a light one."),
                       ParamSpec::number ("range", "Range", "dB", 0.0f, 24.0f, 12.0f).says ("The most it will ever duck the low end during a thump."),
                       ParamSpec::toggle ("listen", "Listen to what's removed", false).notSaved() .says ("Play only what is being taken out. You should hear thumps, not the bottom of the voice.") }) {}
    juce::String getModuleId() const override { return "gust"; }
    juce::String getDisplayName() const override { return "Gust"; }
    juce::String getSubtitle() const override { return "De-plosive"; }
    int getVersion() const override { return 1; }
    juce::String getModuleMeterLabel() const override { return "Pop reduction"; }
    float readModuleMeter() override { return maxCut.exchange (0.0f); }
    /** How many pops have been turned down since the last reset. */
    int getPopCount() const { return pops.load(); }

    void prepare (double sr, int, int channels) override
    {
        sampleRate = sr;
        numChannels = juce::jmin (channels, (int) maxChannels);
        fastC = 1.0 - std::exp (-1.0 / (0.003 * sr));    // 3 ms, what the low end is doing now
        slowC = 1.0 - std::exp (-1.0 / (0.300 * sr));    // 300 ms, unused for now
        progC = 1.0 - std::exp (-1.0 / (1.500 * sr));    // 1.5 s, how loud the recording runs
        atkC  = 1.0 - std::exp (-1.0 / (0.0005 * sr));   // 0.5 ms onto the cut: a pop starts abruptly
        relC  = 1.0 - std::exp (-1.0 / (0.040 * sr));    // 40 ms off it, about the length of a pop
        reset();
    }
    void reset() override
    {
        for (auto& z : state) z.fill (0.0);
        fastLow = fastHigh = cut = 0.0; programme = 1.0e-9; usual = 0.0; lastFreq = -1.0; wasCutting = false;
        pops.store (0);
    }
    void process (juce::AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), nc = juce::jmin (b.getNumChannels(), numChannels);
        if (nc == 0) return;
        const double freq = juce::jlimit (40.0, 250.0, (double) getParam (pFreq));
        const double range = getParam (pRange);
        const bool listen = getParam (pListen) > 0.5f;

        if (freq != lastFreq)
        {
            lastFreq = freq;
            const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70711), ia = 1.0 / (1.0 + alpha);
            lp = { (1.0 - c) * 0.5 * ia, (1.0 - c) * ia, (1.0 - c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
            hp = { (1.0 + c) * 0.5 * ia, -(1.0 + c) * ia, (1.0 + c) * 0.5 * ia, -2.0 * c * ia, (1.0 - alpha) * ia };
        }

        // How far the low end must stand above the rest before this counts as a pop. Sensitivity
        // 0 waits until it is louder outright, 100 acts while it is still 14 dB under.
        const double needed = 2.0 - 16.0 * (double) getParam (pSensitivity) / 100.0;
        const double gate = 1.0e-8;                 // -80 dBFS: below this there is nothing to fix
        const double ln10over20 = std::log (10.0) / 20.0;
        float blockMax = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            double low[maxChannels], high[maxChannels], sqLow = 0.0, sqHigh = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                double* z = state[(size_t) c].data();
                const double x = b.getSample (c, i);
                low[c] = f4 (lp, z, x); high[c] = f4 (hp, z + 4, x);
                sqLow += low[c] * low[c]; sqHigh += high[c] * high[c];
            }
            sqLow /= nc; sqHigh /= nc;
            fastLow  += (sqLow  - fastLow)  * fastC;
            fastHigh += (sqHigh - fastHigh) * fastC;
            programme += (sqLow + sqHigh - programme) * progC;

            double wanted = 0.0;
            if (fastLow > gate)
            {
                // 15 dB under the programme still counts as loud; a pause runs 40 dB under.
                const double prominence = 10.0 * std::log10 (fastLow / juce::jmax (programme, 1.0e-12));
                const double dominance = 10.0 * std::log10 (fastLow / juce::jmax (fastHigh, 1.0e-12));
                if (dominance > needed && prominence > -15.0)
                    wanted = range * juce::jlimit (0.0, 1.0, (dominance - needed) / 10.0);
            }
            cut += (wanted - cut) * (wanted > cut ? atkC : relC);
            const double g = std::exp (-cut * ln10over20);
            blockMax = juce::jmax (blockMax, (float) cut);

            // One pop, one count: the cut has to fall well back before another can be counted,
            // or the dip in the middle of a single pop is counted as two.
            if (! wasCutting && cut > 1.0) { pops.fetch_add (1, std::memory_order_relaxed); wasCutting = true; }
            else if (wasCutting && cut < 0.3) wasCutting = false;

            for (int c = 0; c < nc; ++c)
                b.setSample (c, i, (float) (listen ? low[c] * (1.0 - g) : low[c] * g + high[c]));
        }
        float old = maxCut.load();
        while (blockMax > old && ! maxCut.compare_exchange_weak (old, blockMax)) {}
    }

    /** Factory presets. Starting points only; they have not been set by ear. */
    struct Preset { const char* name; float sensitivity, freq, range; };
    static const std::array<Preset, 5>& factoryPresets()
    {
        static const std::array<Preset, 5> p { { { "Default", 50, 120, 12 }, { "Light Touch", 35, 120, 8 },
            { "Close Mic", 65, 140, 14 }, { "Deep Voice", 50, 90, 12 }, { "Heavy Pops", 75, 150, 18 } } };
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
    static constexpr int maxChannels = 8;
    static double f4 (const std::array<double, 5>& c, double* z, double x)
    {
        double t = c[0] * x + z[0]; z[0] = c[1] * x - c[3] * t + z[1]; z[1] = c[2] * x - c[4] * t; x = t;
        t = c[0] * x + z[2]; z[2] = c[1] * x - c[3] * t + z[3]; z[3] = c[2] * x - c[4] * t; return t;
    }
    std::array<double, 5> lp {}, hp {};
    std::array<std::array<double, 8>, maxChannels> state {};
    double sampleRate = 48000.0, fastC = 0.0, slowC = 0.0, progC = 0.0, atkC = 0.0, relC = 0.0;
    double fastLow = 0.0, fastHigh = 0.0, programme = 1.0e-9, usual = 0.0, cut = 0.0, lastFreq = -1.0;
    int numChannels = 2;
    bool wasCutting = false;
    std::atomic<int> pops { 0 };
    std::atomic<float> maxCut { 0.0f };
};
} // namespace ps
