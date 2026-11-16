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
