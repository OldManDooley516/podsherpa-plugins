#pragma once
#include "Fft.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace ps
{
/** Linear-phase filter shared by Contour, Contour Dynamic and Dropoff, same design as the JSFX
    suite: the wanted magnitude response is sampled on a 4096-point grid, turned into a 3841-tap
    symmetric impulse response (Blackman window) and applied by partitioned convolution in
    128-sample blocks. Delay is 2048 samples at any sample rate (43 ms at 48 kHz).
    Channels are processed in pairs, packed as the real and imaginary parts of one signal. */
class LinearPhase
{
public:
    static constexpr int latency = 2048, grid = 4096, taps = 3841, part = 128, parts = 31;

    LinearPhase() : fftBig (grid), fftSmall (2 * part)
    {
        const double pi = juce::MathConstants<double>::pi;
        for (int i = 0; i < taps; ++i) win[(size_t) i] = 0.42 - 0.5 * std::cos (2.0 * pi * i / 3840.0) + 0.08 * std::cos (4.0 * pi * i / 3840.0);
        fb.assign ((size_t) grid * 2, 0.0); fh.assign ((size_t) parts * 512, 0.0);
    }
    void prepare (int channels) { pairs.assign ((size_t) ((channels + 1) / 2), {}); reset(); }
    void reset() { for (auto& p : pairs) { p.cin.fill (0.0); p.ob.fill (0.0); std::fill (p.fdl.begin(), p.fdl.end(), 0.0); } cp = 0; head = 0; }

    /** mag(k) returns the wanted linear gain at frequency k * sampleRate / 4096, k = 0..2048. */
    template <typename MagFn> void design (MagFn&& mag)
    {
        for (int k = 0; k <= grid / 2; ++k)
        {
            const double m = mag (k);
            fb[(size_t) (2 * k)] = m; fb[(size_t) (2 * k + 1)] = 0.0;
            if (k > 0 && k < grid / 2) { fb[(size_t) (2 * (grid - k))] = m; fb[(size_t) (2 * (grid - k) + 1)] = 0.0; }
        }
        fftBig.inverse (fb.data());
        for (int p = 0; p < parts; ++p)
        {
            double* v = fh.data() + p * 512; std::fill (v, v + 512, 0.0);
            for (int j = 0; j < part; ++j)
            {
                const int m = p * part + j;
                if (m < taps) { int n = m - 1920; if (n < 0) n += grid; v[2 * j] = fb[(size_t) (2 * n)] * (1.0 / grid) * win[(size_t) m]; }
            }
            fftSmall.forward (v);
        }
    }
    /** Filters one sample frame in place: in[c] -> out[c], delayed by `latency`. */
    void processFrame (const double* in, double* out, int numChannels)
    {
        for (size_t p = 0; p < pairs.size(); ++p)
        {
            auto& s = pairs[p]; const int a = (int) p * 2, b = a + 1;
            s.cin[(size_t) (256 + 2 * cp)] = in[a]; s.cin[(size_t) (257 + 2 * cp)] = b < numChannels ? in[b] : 0.0;
            out[a] = s.ob[(size_t) (2 * cp)]; if (b < numChannels) out[b] = s.ob[(size_t) (2 * cp + 1)];
        }
        if (++cp >= part)
        {
            for (auto& s : pairs)
            {
                std::array<double, 512> tb = s.cin, acc {};
                fftSmall.forward (tb.data());
                std::copy (tb.begin(), tb.end(), s.fdl.begin() + head * 512);
                for (int p = 0; p < parts; ++p)
                {
                    int idx = head - p; if (idx < 0) idx += parts;
                    const double* bb = s.fdl.data() + idx * 512; const double* cc = fh.data() + p * 512;
                    for (int i = 0; i < 512; i += 2)
                    {
                        acc[(size_t) i] += bb[i] * cc[i] - bb[i + 1] * cc[i + 1];
                        acc[(size_t) (i + 1)] += bb[i] * cc[i + 1] + bb[i + 1] * cc[i];
                    }
                }
                fftSmall.inverse (acc.data());
                for (int j = 0; j < part; ++j) { s.ob[(size_t) (2 * j)] = acc[(size_t) (256 + 2 * j)] * 0.00390625; s.ob[(size_t) (2 * j + 1)] = acc[(size_t) (257 + 2 * j)] * 0.00390625; }
                std::copy (s.cin.begin() + 256, s.cin.end(), s.cin.begin());
            }
            head = (head + 1) % parts; cp = 0;
        }
    }
private:
    struct Pair { std::array<double, 512> cin {}; std::array<double, 256> ob {}; std::vector<double> fdl = std::vector<double> ((size_t) parts * 512, 0.0); };
    Fft fftBig, fftSmall;
    std::array<double, taps> win {};
    std::vector<double> fb, fh;
    std::vector<Pair> pairs;
    int cp = 0, head = 0;
};
} // namespace ps
