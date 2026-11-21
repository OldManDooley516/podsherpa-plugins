#pragma once
#include "Fft.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>

namespace ps
{
/** The audio-thread half of a spectrum display: a ring holding the most recent 8192 samples,
    mixed to mono. Writing a few floats per sample is all the audio thread does; the FFT runs in
    the editor (SpectrumAnalyser), so a closed editor costs nothing beyond this copy, and an
    open one never adds work to the audio callback.

    8192 points, as Dropoff's JSFX has always used and as Contour, Contour Dynamic and Ridgeline
    were moved to on Nov 15: at 2048 points loud energy around 200 Hz smeared into everything
    below 100 Hz and the display showed low end that a high-pass had already removed. */
class SpectrumTap
{
public:
    static constexpr int size = 8192;

    void push (const juce::AudioBuffer<float>& b, int numChannels)
    {
        const int n = b.getNumSamples(), nc = juce::jmin (numChannels, b.getNumChannels());
        if (nc <= 0) return;
        const float scale = 1.0f / (float) nc;
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < nc; ++c) s += b.getSample (c, i);
            ring[(size_t) w] = s * scale;
            w = (w + 1) & (size - 1);
        }
        writePos.store (w, std::memory_order_release);
        written.fetch_add ((juce::uint32) n, std::memory_order_release);
    }

    /** Copies the latest 8192 samples, oldest first. A block written during the copy can tear
        the oldest few samples; for a display averaged over 171 ms that is invisible. */
    void snapshot (std::array<float, size>& out) const
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < size; ++i) out[(size_t) i] = ring[(size_t) ((w + i) & (size - 1))];
    }
    juce::uint32 samplesWritten() const { return written.load (std::memory_order_acquire); }
    void clear() { ring.fill (0.0f); }

private:
    std::array<float, size> ring {};
    std::atomic<int> writePos { 0 };
    std::atomic<juce::uint32> written { 0 };
};

/** The editor half: windows the tap's latest 8192 samples, runs the FFT and folds it into
    96 bands spaced evenly in octaves from 20 Hz to 20 kHz, the same layout as the JSFX.

    Levels are calibrated so a full-scale sine reads 0 dB in the band that holds it. A band
    wider than one FFT step takes the loudest step inside it (so a single harmonic is not
    averaged away); a band narrower than one step, at the bottom, reads the step it sits in.

    Rises follow the audio quickly and falls are slowed, so speech reads as a shape rather than
    a flicker. When playback stops nothing new arrives, and the display sinks to the floor
    instead of freezing on the last syllable. */
class SpectrumAnalyser
{
public:
    static constexpr int numBands = 96;
    static constexpr float floorDb = -120.0f;

    SpectrumAnalyser() : fft (SpectrumTap::size), window ((size_t) SpectrumTap::size), buf ((size_t) SpectrumTap::size * 2)
    {
        double sum = 0.0;
        for (int i = 0; i < SpectrumTap::size; ++i)
        {
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (SpectrumTap::size - 1));
            sum += window[(size_t) i];
        }
        gainNorm = 2.0 / sum;
        bands.fill (floorDb);
    }

    /** Call from the editor timer. tickRateHz sets how fast the display falls. */
    void update (const SpectrumTap& tap, double sampleRate, double tickRateHz)
    {
        if (sampleRate <= 0.0) return;
        const auto written = tap.samplesWritten();
        const bool fresh = written != lastWritten;
        lastWritten = written;

        // A host with large blocks can leave a tick or two with nothing new; hold the last
        // reading through those, and only sink once a quarter second has passed with no audio.
        std::array<float, numBands> now;
        if (fresh) { measure (tap, sampleRate, last); staleTicks = 0; now = last; }
        else if (++staleTicks <= (int) (tickRateHz * 0.25)) now = last;
        else now.fill (floorDb);

        const float fall = (float) (fallDbPerSecond / juce::jmax (1.0, tickRateHz));
        for (int k = 0; k < numBands; ++k)
        {
            float& b = bands[(size_t) k];
            const float v = now[(size_t) k];
            if (v > b) b += (v - b) * 0.7f;            // rise: most of the way each tick
            else b = juce::jmax (v, b - fall);         // fall: no faster than the set rate
        }
    }

    const std::array<float, numBands>& getBands() const { return bands; }
    void reset() { bands.fill (floorDb); }

    /** Centre frequency of a band, for drawing. */
    static double bandHz (int k) { return 20.0 * std::pow (1000.0, (k + 0.5) / numBands); }
    static double edgeHz (int k) { return 20.0 * std::pow (1000.0, (double) k / numBands); }

private:
    void measure (const SpectrumTap& tap, double sampleRate, std::array<float, numBands>& out)
    {
        tap.snapshot (samples);
        for (int i = 0; i < SpectrumTap::size; ++i)
        {
            buf[(size_t) (2 * i)] = samples[(size_t) i] * window[(size_t) i];
            buf[(size_t) (2 * i + 1)] = 0.0;
        }
        fft.forward (buf.data());

        const double binHz = sampleRate / SpectrumTap::size;
        const int nyq = SpectrumTap::size / 2;
        auto magAt = [&] (int bin)
        {
            const double re = buf[(size_t) (2 * bin)], im = buf[(size_t) (2 * bin + 1)];
            return std::sqrt (re * re + im * im) * gainNorm;
        };
        for (int k = 0; k < numBands; ++k)
        {
            const double lo = edgeHz (k), hi = edgeHz (k + 1);
            int b0 = (int) std::ceil (lo / binHz), b1 = (int) std::floor (hi / binHz);
            double m = 0.0;
            if (b1 < b0) m = magAt (juce::jlimit (1, nyq - 1, (int) std::floor (bandHz (k) / binHz + 0.5)));
            else for (int b = juce::jmax (1, b0); b <= juce::jmin (nyq - 1, b1); ++b) m = juce::jmax (m, magAt (b));
            out[(size_t) k] = m > 1.0e-6 ? (float) (20.0 * std::log10 (m)) : floorDb;
            if (hi > sampleRate * 0.5) out[(size_t) k] = floorDb;
        }
    }

    static constexpr double fallDbPerSecond = 36.0;
    Fft fft;
    std::vector<double> window, buf;
    std::array<float, SpectrumTap::size> samples {};
    std::array<float, numBands> bands {}, last {};
    int staleTicks = 0;
    double gainNorm = 1.0;
    juce::uint32 lastWritten = 0;
};
} // namespace ps
