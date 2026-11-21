#pragma once
#include "Module.h"
#include "Meters.h"
#include "Spectrum.h"

namespace ps
{
/** Holds one module and gives it the shared behaviour: a click-free bypass (10 ms crossfade)
    against a dry path delayed by the module's latency, plus input and output meters. */
class ModuleSlot
{
public:
    Module* getModule() const { return module.get(); }
    bool isEmpty() const { return module == nullptr; }
    int getLatency() const { return module ? module->getLatencySamples() : 0; }

    /** Returns the previous module so the caller can delete it outside the audio lock. */
    std::unique_ptr<Module> setModule (std::unique_ptr<Module> m)
    {
        std::swap (module, m);
        if (module && sampleRate > 0.0) prepareModule();
        return m;
    }
    void prepare (double sr, int maxBlock, int channels)
    {
        sampleRate = sr; blockSize = maxBlock; numChannels = channels;
        if (module) prepareModule();
    }
    void process (juce::AudioBuffer<float>& buffer, bool bypassed)
    {
        if (! module) return;
        const int n = buffer.getNumSamples(), nc = juce::jmin (buffer.getNumChannels(), numChannels);
        inMeter.push (buffer);
        const bool spectrum = tapSpectrum && module->hasSpectrum();
        if (spectrum) preTap.push (buffer, nc);

        // A module may change its latency between blocks: a phase switch, a look-ahead time.
        // The delay line is already as long as the module can ever ask for, so nothing allocates
        // here; what is thrown away is the audio in flight, which the module loses too.
        const int want = juce::jlimit (0, delayLine.getNumSamples(), module->getLatencySamples());
        if (want != latency) { delayLine.clear(); delayPos = 0; latency = want; }

        // Dry path, delayed by the module's latency so bypass never shifts the audio.
        dry.setSize (numChannels, n, false, false, true);
        for (int c = 0; c < nc; ++c)
        {
            const float* in = buffer.getReadPointer (c);
            float* d = dry.getWritePointer (c);
            if (latency == 0) { juce::FloatVectorOperations::copy (d, in, n); continue; }
            float* line = delayLine.getWritePointer (c);
            int p = delayPos;
            for (int i = 0; i < n; ++i) { d[i] = line[p]; line[p] = in[i]; if (++p >= latency) p = 0; }
        }
        if (latency > 0) delayPos = (delayPos + n) % latency;

        wet.setTargetValue (bypassed ? 0.0f : 1.0f);
        const bool fullyDry = bypassed && ! wet.isSmoothing();
        if (! fullyDry) module->process (buffer);   // a fully bypassed module costs no CPU

        if (fullyDry)
            for (int c = 0; c < nc; ++c) buffer.copyFrom (c, 0, dry, c, 0, n);
        else if (wet.isSmoothing())
            for (int i = 0; i < n; ++i)
            {
                const float g = wet.getNextValue();
                for (int c = 0; c < nc; ++c)
                {
                    const float d = dry.getSample (c, i);
                    buffer.setSample (c, i, d + (buffer.getSample (c, i) - d) * g);
                }
            }
        outMeter.push (buffer);
        if (spectrum) postTap.push (buffer, nc);
    }

    LevelMeter inMeter, outMeter;
    /** What goes into the module and what comes out of it, for a spectrum display. The
        output side includes bypass, so a bypassed EQ shows the two lying on top of each other. */
    SpectrumTap preTap, postTap;
    bool tapSpectrum = false;   // set by a host that draws one; Basecamp's rack does not yet
    double getSampleRate() const { return sampleRate; }

private:
    void prepareModule()
    {
        module->prepare (sampleRate, blockSize, numChannels);
        module->reset();
        delayLine.setSize (numChannels, juce::jmax (1, module->getMaxLatencySamples()));
        delayLine.clear(); delayPos = 0;
        latency = juce::jlimit (0, delayLine.getNumSamples(), module->getLatencySamples());
        dry.setSize (numChannels, blockSize);
        wet.reset (sampleRate, 0.010);
        wet.setCurrentAndTargetValue (1.0f);
    }
    std::unique_ptr<Module> module;
    juce::AudioBuffer<float> delayLine, dry;
    juce::SmoothedValue<float> wet { 1.0f };
    int delayPos = 0, latency = 0, blockSize = 512, numChannels = 2;
    double sampleRate = 0.0;
};
} // namespace ps
