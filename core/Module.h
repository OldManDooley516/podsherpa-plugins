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

    /** Module-specific meter (gain reduction in dB for Summit, and so on). Reading clears it. */
    virtual juce::String getModuleMeterLabel() const { return {}; }
    virtual float readModuleMeter() { return 0.0f; }

    /** Upgrade a saved SLOT/MODULE tree written by an older version of this module. */
    virtual juce::ValueTree migrate (juce::ValueTree saved, int /*fromVersion*/) { return saved; }

    const std::vector<ParamSpec>& getParamSpecs() const { return specs; }
    int getNumParams() const { return (int) specs.size(); }
    void setParam (int i, float v) { values[(size_t) i].store (v, std::memory_order_relaxed); }
    float getParam (int i) const { return values[(size_t) i].load (std::memory_order_relaxed); }

private:
    std::vector<ParamSpec> specs;
    std::vector<std::atomic<float>> values;
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
