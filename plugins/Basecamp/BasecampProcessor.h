#pragma once
#include "../../core/ModuleSlot.h"
#include "../../core/ModuleFactory.h"
#include "../../core/HostParam.h"

namespace ps
{
/** Basecamp: the master rack. A serial chain of up to 12 module slots (v1: no parallel lanes).
    Any module can sit in any slot, and the same module can appear more than once.

    Host parameters are fixed: each slot owns bypass, solo and a pool of `poolSize` parameters.
    Whatever module sits in a slot has its parameters mapped onto that slot's pool, so automation
    belongs to the slot and the host's parameter list never changes. */
class BasecampProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    static constexpr int numSlots = 12, poolSize = 48, rackVersion = 1;

    BasecampProcessor();

    // Chain editing. Call from the message thread.
    void setSlotModule (int slot, const juce::String& moduleId);   // empty id clears the slot
    void moveSlot (int from, int to);                              // swaps the two slots' contents
    juce::String getSlotModuleId (int slot) const;
    juce::String getSlotName (int slot) const;
    bool isSlotPlaceholder (int slot) const;
    std::vector<HostParam*> getSlotParams (int slot) const;        // only the mapped ones
    ModuleSlot& getSlot (int slot) { return slots[(size_t) slot]; }
    int getTotalLatency() const { return totalLatency.load(); }

    juce::ValueTree saveRack() const;
    void loadRack (const juce::ValueTree&);

    // juce::AudioProcessor
    const juce::String getName() const override { return "PodSherpa Basecamp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    struct SlotParams
    {
        juce::AudioParameterBool *bypass = nullptr, *solo = nullptr;
        std::array<HostParam*, poolSize> pool {};
    };
    const SlotParams& getSlotHostParams (int slot) const { return hostParams[(size_t) slot]; }

    juce::AudioParameterFloat *inGain = nullptr, *outGain = nullptr;
    LevelMeter inMeter, outMeter;
    ShortTermLoudness loudness;
    std::atomic<float> truePeakMax { 0.0f };   // linear, held until the UI resets it

private:
    juce::ValueTree saveSlot (int slot) const;
    void loadSlot (int slot, juce::ValueTree saved, bool notify);
    void refreshLatency();

    std::array<ModuleSlot, numSlots> slots;
    std::array<SlotParams, numSlots> hostParams;
    juce::CriticalSection chainLock;   // held by the audio thread (try-lock) and around a module swap
    juce::CriticalSection editLock;    // serialises every non-audio access to the chain
    TruePeak masterTp;
    std::atomic<int> totalLatency { 0 };
    double sampleRate = 0.0; int blockSize = 512;
    float lastIn = -1000.0f, lastOut = -1000.0f;
};
} // namespace ps
