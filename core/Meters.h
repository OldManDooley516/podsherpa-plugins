#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>

namespace ps
{
inline float gainToDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

/** Block peak meter with a latching clip flag (stays on until cleared). */
struct LevelMeter
{
    void push (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples()));
        float old = peak.load (std::memory_order_relaxed);
        while (p > old && ! peak.compare_exchange_weak (old, p)) {}
        if (p >= 1.0f) clipped.store (true, std::memory_order_relaxed);
    }
    float readPeak() { return peak.exchange (0.0f); }       // linear; reading clears it
    bool isClipped() const { return clipped.load(); }
    void clearClip() { clipped.store (false); }
    std::atomic<float> peak { 0.0f };
    std::atomic<bool> clipped { false };
};

/** Inter-sample peak estimate, same design as the JSFX suite: 8x polyphase interpolator,
    128 taps, Kaiser-windowed sinc (beta 7), centred so phase 0 is the input delayed by 8 samples.
    Only phases 1..7 are computed; they lie between x[n-8] and x[n-7]. */
class TruePeak
{
public:
    static constexpr int delay = 8, taps = 16, phases = 7;

    TruePeak()
    {
        auto i0 = [] (double x) { double sum = 1.0, t = 1.0; for (int k = 1; k <= 30; ++k) { t *= x / 2.0 / k; sum += t * t; } return sum; };
        const double beta = 7.0, i0b = i0 (beta), pi = juce::MathConstants<double>::pi;
        for (int r = 1; r <= phases; ++r)
        {
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const double jj = 8.0 * k + r - 64.0;
                const double v = (jj == 0.0 ? 1.0 : std::sin (pi * jj / 8.0) / (pi * jj / 8.0)) * i0 (beta * std::sqrt (1.0 - (jj / 64.0) * (jj / 64.0))) / i0b;
                coef[(size_t) (r - 1)][(size_t) k] = v; sum += v;
            }
            for (auto& v : coef[(size_t) (r - 1)]) v /= sum;   // unity gain at DC for every phase
        }
    }
    void prepare (int numChannels) { state.assign ((size_t) numChannels, {}); }
    void reset() { for (auto& s : state) s = {}; }

    /** Push one sample for a channel. Returns the largest inter-sample magnitude (0 when off). */
    double push (int ch, double x, bool on)
    {
        auto& s = state[(size_t) ch];
        s.pos = (s.pos + 15) % 16; s.buf[(size_t) s.pos] = x; s.buf[(size_t) (s.pos + 16)] = x;   // doubled ring, newest first
        if (! on) return 0.0;
        double m = 0.0;
        for (int r = 0; r < phases; ++r)
        {
            double acc = 0.0;
            for (int k = 0; k < taps; ++k) acc += s.buf[(size_t) (s.pos + k)] * coef[(size_t) r][(size_t) k];
            m = juce::jmax (m, std::abs (acc));
        }
        return m;
    }

private:
    struct State { std::array<double, 32> buf {}; int pos = 0; };
    std::array<std::array<double, taps>, phases> coef {};
    std::vector<State> state;
};

/** Short-term loudness (3 s window, ITU-R BS.1770 K-weighting, no gating). */
class ShortTermLoudness
{
public:
    void prepare (double sr, int numChannels)
    {
        const double pi = juce::MathConstants<double>::pi;
        { // high shelf
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        { // high pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / sr), a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        state.assign ((size_t) numChannels, {});
        hop = juce::jmax (1, (int) (sr * 0.1));
        blocks.fill (0.0); blockPos = 0; count = 0; acc = 0.0; filled = 0;
        lufs.store (-120.0f);
    }
    void process (const juce::AudioBuffer<float>& b)
    {
        const int nc = juce::jmin (b.getNumChannels(), (int) state.size());
        for (int n = 0; n < b.getNumSamples(); ++n)
        {
            for (int c = 0; c < nc; ++c)
            {
                auto& z = state[(size_t) c];
                double x = b.getSample (c, n);
                double y = s1[0] * x + z[0]; z[0] = s1[1] * x - s1[3] * y + z[1]; z[1] = s1[2] * x - s1[4] * y;
                x = y;
                y = s2[0] * x + z[2]; z[2] = s2[1] * x - s2[3] * y + z[3]; z[3] = s2[2] * x - s2[4] * y;
                acc += y * y;
            }
            if (++count >= hop)
            {
                blocks[(size_t) blockPos] = acc / hop; blockPos = (blockPos + 1) % (int) blocks.size();
                filled = juce::jmin (filled + 1, (int) blocks.size());
                acc = 0.0; count = 0;
                double sum = 0.0; for (double v : blocks) sum += v;
                const double ms = sum / filled;
                lufs.store (ms > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -120.0f);
            }
        }
    }
    float get() const { return lufs.load(); }

private:
    std::array<double, 5> s1 {}, s2 {};
    std::vector<std::array<double, 4>> state;
    std::array<double, 30> blocks {};
    int hop = 4800, blockPos = 0, count = 0, filled = 0;
    double acc = 0.0;
    std::atomic<float> lufs { -120.0f };
};
} // namespace ps
