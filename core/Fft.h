#pragma once
#include <vector>
#include <cmath>

namespace ps
{
/** Small in-place complex FFT (radix 2) on interleaved re/im doubles. Forward and inverse are
    both unscaled, like the JSFX fft()/ifft() pair, so a round trip multiplies by the size. */
class Fft
{
public:
    explicit Fft (int sizeIn) : size (sizeIn), rev ((size_t) sizeIn), cosT ((size_t) sizeIn / 2), sinT ((size_t) sizeIn / 2)
    {
        int bits = 0; while ((1 << bits) < size) ++bits;
        for (int i = 0; i < size; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < size / 2; ++i) { cosT[(size_t) i] = std::cos (2.0 * pi * i / size); sinT[(size_t) i] = std::sin (2.0 * pi * i / size); }
    }
    void forward (double* d) const { run (d, -1.0); }
    void inverse (double* d) const { run (d, 1.0); }
    int getSize() const { return size; }
private:
    void run (double* d, double sign) const
    {
        for (int i = 0; i < size; ++i) { const int j = rev[(size_t) i]; if (j > i) { std::swap (d[2 * i], d[2 * j]); std::swap (d[2 * i + 1], d[2 * j + 1]); } }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int s = 0; s < size; s += len)
                for (int k = 0; k < half; ++k)
                {
                    const double wr = cosT[(size_t) (k * step)], wi = sign * sinT[(size_t) (k * step)];
                    double* a = d + 2 * (s + k); double* b = d + 2 * (s + k + half);
                    const double tr = b[0] * wr - b[1] * wi, ti = b[0] * wi + b[1] * wr;
                    b[0] = a[0] - tr; b[1] = a[1] - ti; a[0] += tr; a[1] += ti;
                }
        }
    }
    int size; std::vector<int> rev; std::vector<double> cosT, sinT;
};
} // namespace ps
