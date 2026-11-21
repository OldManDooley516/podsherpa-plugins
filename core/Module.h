#pragma once
#include "ParamSpec.h"
#include <atomic>
#include <vector>

namespace ps
{
/** The contract every PodSherpa module implements. One module class is used standalone,
    inside Basecamp and inside Overlook. Bypass, dry/wet alignment and in/out meters are
    handled by ModuleSlot, not by the module. */
class Module
{
public:
    explicit Module (std::vector<ParamSpec> s) : specs (std::move (s)), values (specs.size())
    {
        for (size_t i = 0; i < specs.size(); ++i) values[i].store (specs[i].def);
    }
    virtual ~Module() = default;

    virtual juce::String getModuleId() const = 0;      // stable, e.g. "summit"
    virtual juce::String getDisplayName() const = 0;   // e.g. "Summit"
    virtual juce::String getSubtitle() const { return {}; }
    virtual int getVersion() const = 0;                // state version, bump when params change meaning

    virtual void prepare (double sampleRate, int maxBlock, int numChannels) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;
    virtual int getLatencySamples() const { return 0; }
    /** The largest latency this module can ask for at the prepared sample rate, whatever its
        parameters are set to later. The slot sizes its bypass delay for this once, so a module
        may change its latency between blocks without anything having to allocate. */
    virtual int getMaxLatencySamples() const { return getLatencySamples(); }

    /** Module-specific meter (gain reduction in dB for Summit, and so on). Reading clears it. */
    virtual juce::String getModuleMeterLabel() const { return {}; }
    virtual float readModuleMeter() { return 0.0f; }

    /** A second, slower readout drawn beside the meter: a whole phrase, already formatted,
        because what it says differs by module. Summit uses it for integrated loudness, which it
        has measured accurately since Nov 16 and had no way to show. Called from the editor's
        timer, so it must not block; return an empty string for no readout.

        Modules that offer one can also say whether it is on target, which the editor uses to
        colour it - with the words still present, so the colour is never the only signal. */
    /** A compressor-shaped module can describe its own transfer curve, which the editor draws
        as a card: input level in, output level out, both in dBFS. The JSFX versions have shown
        this since 0.4 and the port had nothing, which is most of why the ports are harder to
        work with than the originals. Generic on purpose - Ridgeline and Summit can offer it
        without the editor knowing anything about compression.

        getCurveThresholdDb returns where to draw the threshold guide; the curve is sampled
        across -60 to 0 dBFS. */
    /** A filter-shaped module can describe its own frequency response, which the editor draws
        as a curve across 20 Hz to 20 kHz. Like the transfer curve, the editor knows nothing
        about filters: it asks for a level in dB at a frequency. Contour implements it from the
        same biquad coefficients the audio path runs, so the drawn curve is the real one. */
    virtual bool hasFrequencyResponse() const { return false; }
    virtual float responseDbAt (float) const { return 0.0f; }

    /** Dots on the response curve that can be grabbed: each names the parameters it moves.
        Sideways moves freq, up and down moves gain, the wheel moves q; -1 where a band has no
        such control (the high-pass has no gain). Dragging a band that is off turns it on. */
    struct CurveHandle { juce::String name, shortName; int freq = -1, gain = -1, q = -1, on = -1; };
    virtual std::vector<CurveHandle> getCurveHandles() const { return {}; }
    /** A choice parameter whose value is 0 for off, or handle index + 1 to hear only that
        handle's band (Solo). The curve sets it while a dot is Alt/Option-dragged. -1: none. */
    virtual int getSoloParam() const { return -1; }
    /** When the module is playing something other than its normal output (a solo, a delta),
        a sentence saying so. The editor shows it in amber; empty when nothing is auditioned. */
    virtual juce::String getAuditionWarning() const { return {}; }

    /** A module that wants a spectrum display behind its curve. The slot then keeps the latest
        8192 samples going in and coming out (SpectrumTap) and the editor draws both. Only the
        copy happens on the audio thread; the FFT runs in the editor. */
    virtual bool hasSpectrum() const { return false; }

    virtual bool hasTransferCurve() const { return false; }
    /** The curve card's heading: an expander (Glade) is not a compressor. */
    virtual juce::String getCurveTitle() const { return "Compression curve"; }
    virtual float curveOutputDb (float inputDb) const { return inputDb; }
    virtual float getCurveThresholdDb() const { return -60.0f; }
    /** The level the module's own detector saw, and the level that came out, both in dBFS and
        both the loudest since the last read (reading clears them). The editor puts a dot on the
        curve at the input level and draws both as traces behind the reduction history, so you
        can see whether the voice is reaching the threshold at all. False: nothing to show. */
    virtual bool readCurveLevels (float& /*inDb*/, float& /*outDb*/) { return false; }
    /** Parameters the curve's threshold dot moves: sideways sets the first (threshold, dB),
        up and down the second (ratio). -1 where a module has no such control. */
    virtual int getCurveThresholdParam() const { return -1; }
    virtual int getCurveRatioParam() const { return -1; }

    /** Sidechain (Nov 21). A module that can react to another track says so, and the standalone
        plugin then offers a second, optional stereo input. Before each block the wrapper points
        the module at that input (or at nothing, when the host gives none or inside Basecamp,
        which is single-track) and clears the pointer afterwards. Same length as the block. */
    virtual bool wantsSidechain() const { return false; }
    void setSidechain (const juce::AudioBuffer<float>* sc) { sidechain = sc; }
    /** True while the module is reacting to the sidechain rather than its own input, so the
        displays call the level they show "Sidechain" instead of "Voice". */
    virtual bool isKeyedFromSidechain() const { return false; }

    /** Pip's walkthrough for a factory preset: one short step per entry, shown one at a time in
        Beginner mode when that preset is picked. Empty: no walkthrough, Pip's usual line. */
    virtual juce::StringArray getPipTutorial (int /*presetIndex*/) const { return {}; }

    virtual juce::String getExtraReadout() const { return {}; }
    virtual bool extraReadoutIsGood() const { return true; }

    /** Restarts whatever the extra readout accumulates. Shown as a button when the string is
        non-empty; an integrated measurement is meaningless without a way to start it again. */
    virtual void resetExtraReadout() {}
    virtual bool hasExtraReadoutReset() const { return false; }

    /** Highest output gain in dB the shared wrapper may apply after this module, read every
        block. A module that promises a peak level (Summit's ceiling) lowers this so the gain
        after it cannot break that promise. Default: the full range of the output gain control. */
    virtual float getMaxOutputGainDb() const { return 18.0f; }

    /** Factory presets, the ones the JSFX offers. Applying one sets the parameters that preset
        covers and leaves the rest (Phase, Listen, the band on/off switches) where they are, the
        way picking a preset in the JSFX does. */
    virtual int getNumFactoryPresets() const { return 0; }
    virtual juce::String getFactoryPresetName (int) const { return {}; }
    virtual void applyFactoryPreset (int) {}

    /** A module that moves one of its own controls (Serac setting Frequency from what it just
        learned) records it here; the wrapper passes it to the host and clears it. Only the first
        32 parameters can be reported this way, which is every module in the suite. */
    void noteSelfChanged (int i) { if (i < 32) selfChanged.fetch_or (1u << i, std::memory_order_relaxed); }
    juce::uint32 peekSelfChanged() const { return selfChanged.load (std::memory_order_relaxed); }
    juce::uint32 takeSelfChanged() { return selfChanged.exchange (0, std::memory_order_relaxed); }

    /** State a module keeps that is not a parameter: Clearing's learned noise profile. Saved
        with the project and handed back on load, the way the JSFX keeps it in @serialize. */
    virtual juce::ValueTree saveExtraState() const { return {}; }
    virtual void loadExtraState (const juce::ValueTree&) {}

    /** Upgrade a saved SLOT/MODULE tree written by an older version of this module. */
    virtual juce::ValueTree migrate (juce::ValueTree saved, int /*fromVersion*/) { return saved; }

    const std::vector<ParamSpec>& getParamSpecs() const { return specs; }
    int getNumParams() const { return (int) specs.size(); }
    void setParam (int i, float v) { values[(size_t) i].store (v, std::memory_order_relaxed); }
    float getParam (int i) const { return values[(size_t) i].load (std::memory_order_relaxed); }

protected:
    const juce::AudioBuffer<float>* sidechain = nullptr;

private:
    std::vector<ParamSpec> specs;
    std::vector<std::atomic<float>> values;
    std::atomic<juce::uint32> selfChanged { 0 };
};

/** Stands in for a module this build does not know. Passes audio through and keeps the
    saved data so the project is not lost. */
class PlaceholderModule : public Module
{
public:
    explicit PlaceholderModule (juce::ValueTree savedTree)
        : Module ({}), saved (savedTree.createCopy()) {}
    juce::String getModuleId() const override { return saved.getProperty ("module").toString(); }
    juce::String getDisplayName() const override { return "Missing: " + getModuleId(); }
    int getVersion() const override { return (int) saved.getProperty ("moduleVersion", 0); }
    void prepare (double, int, int) override {}
    void reset() override {}
    void process (juce::AudioBuffer<float>&) override {}
    juce::ValueTree saved;
};
} // namespace ps
