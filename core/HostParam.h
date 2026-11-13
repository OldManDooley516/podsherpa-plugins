#pragma once
#include "ParamSpec.h"
#include <atomic>

namespace ps
{
/** A host-visible parameter that can be pointed at any module parameter.
    Standalone plugins bind it once. Basecamp keeps a fixed pool per rack slot and repoints
    them when the module in the slot changes, so the host's parameter list never changes. */
class HostParam : public juce::AudioProcessorParameterWithID
{
public:
    HostParam (const juce::String& paramId, const juce::String& idleName)
        : juce::AudioProcessorParameterWithID (juce::ParameterID { paramId, 1 }, idleName), idle (idleName) {}

    void bind (const ParamSpec& s, const juce::String& namePrefix, float realValue)
    {
        { const juce::SpinLock::ScopedLockType l (lock); spec = s; prefix = namePrefix; active = true; }
        norm.store (s.toNorm (realValue));
    }
    void unbind()
    {
        { const juce::SpinLock::ScopedLockType l (lock); spec = {}; prefix = {}; active = false; }
        norm.store (0.0f);
    }
    bool isActive() const { const juce::SpinLock::ScopedLockType l (lock); return active; }
    ParamSpec getSpec() const { const juce::SpinLock::ScopedLockType l (lock); return spec; }
    float getNorm() const { return norm.load (std::memory_order_relaxed); }
    float getReal() const { return getSpec().fromNorm (getNorm()); }
    void setRealNotifyingHost (float v) { setValueNotifyingHost (getSpec().toNorm (v)); }

    // juce::AudioProcessorParameter
    float getValue() const override { return getNorm(); }
    void setValue (float v) override { norm.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed); }
    float getDefaultValue() const override { const auto s = getSpec(); return isActive() ? s.toNorm (s.def) : 0.0f; }
    juce::String getName (int maxLen) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return (active ? prefix + spec.name : idle).substring (0, maxLen);
    }
    juce::String getLabel() const override { const juce::SpinLock::ScopedLockType l (lock); return active ? spec.unit : juce::String(); }
    juce::String getText (float n, int maxLen) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return (active ? spec.text (spec.fromNorm (n)) : juce::String ("-")).substring (0, maxLen > 0 ? maxLen : 64);
    }
    float getValueForText (const juce::String& t) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return active ? spec.toNorm (spec.parse (t)) : 0.0f;
    }

private:
    juce::String idle, prefix;
    ParamSpec spec;
    bool active = false;
    std::atomic<float> norm { 0.0f };
    mutable juce::SpinLock lock;
};
} // namespace ps
