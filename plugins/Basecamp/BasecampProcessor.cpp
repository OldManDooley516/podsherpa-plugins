#include "BasecampProcessor.h"

namespace ps
{
static juce::String two (int n) { return juce::String (n).paddedLeft ('0', 2); }

BasecampProcessor::BasecampProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    const juce::NormalisableRange<float> gainRange (-18.0f, 18.0f, 0.1f);
    addParameter (inGain = new juce::AudioParameterFloat (juce::ParameterID { "in", 1 }, "Input gain", gainRange, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    addParameter (outGain = new juce::AudioParameterFloat (juce::ParameterID { "out", 1 }, "Output gain", gainRange, 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Fixed host parameter list. These IDs are permanent: slot01.bypass, slot01.solo, slot01.p01 ...
    for (int s = 0; s < numSlots; ++s)
    {
        auto& hp = hostParams[(size_t) s];
        const auto sid = "slot" + two (s + 1), label = "S" + two (s + 1);
        addParameter (hp.bypass = new juce::AudioParameterBool (juce::ParameterID { sid + ".bypass", 1 }, label + " Bypass", false));
        addParameter (hp.solo = new juce::AudioParameterBool (juce::ParameterID { sid + ".solo", 1 }, label + " Solo", false));
        for (int k = 0; k < poolSize; ++k)
            addParameter (hp.pool[(size_t) k] = new HostParam (sid + ".p" + two (k + 1), label + " unused " + two (k + 1)));
    }

    // Default chain. The full default is Dropoff, Clearing, Contour, Contour Dynamic, Altitude,
    // Summit; only the modules ported so far are loaded.
    setSlotModule (0, "dropoff");
    setSlotModule (1, "summit");
}

bool BasecampProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == l.getMainInputChannelSet() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

//==============================================================================
juce::String BasecampProcessor::getSlotModuleId (int s) const
{
    const juce::ScopedLock l (editLock);
    auto* m = slots[(size_t) s].getModule(); return m ? m->getModuleId() : juce::String();
}
juce::String BasecampProcessor::getSlotName (int s) const
{
    const juce::ScopedLock l (editLock);
    auto* m = slots[(size_t) s].getModule(); return m ? m->getDisplayName() : juce::String();
}
bool BasecampProcessor::isSlotPlaceholder (int s) const
{
    const juce::ScopedLock l (editLock);
    return dynamic_cast<PlaceholderModule*> (slots[(size_t) s].getModule()) != nullptr;
}
std::vector<HostParam*> BasecampProcessor::getSlotParams (int s) const
{
    std::vector<HostParam*> v;
    for (auto* p : hostParams[(size_t) s].pool) if (p->isActive()) v.push_back (p);
    return v;
}

void BasecampProcessor::setSlotModule (int s, const juce::String& id)
{
    juce::ValueTree t ("SLOT");
    t.setProperty ("module", id, nullptr);
    if (auto m = createModule (id)) t.setProperty ("moduleVersion", m->getVersion(), nullptr);
    loadSlot (s, t, true);
}

void BasecampProcessor::moveSlot (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= numSlots || to >= numSlots) return;
    const auto a = saveSlot (from), b = saveSlot (to);
    loadSlot (from, b, false); loadSlot (to, a, true);
}

juce::ValueTree BasecampProcessor::saveSlot (int s) const
{
    const juce::ScopedLock l (editLock);
    const auto& hp = hostParams[(size_t) s];
    auto* m = slots[(size_t) s].getModule();
    juce::ValueTree t ("SLOT");
    if (auto* ph = dynamic_cast<PlaceholderModule*> (m)) t = ph->saved.createCopy();   // unknown module: keep its data untouched
    else if (m != nullptr)
    {
        t.setProperty ("module", m->getModuleId(), nullptr);
        t.setProperty ("moduleVersion", m->getVersion(), nullptr);
        const auto& specs = m->getParamSpecs();
        for (size_t k = 0; k < specs.size(); ++k)
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", specs[k].id, nullptr);
            c.setProperty ("value", specs[k].fromNorm (hp.pool[k]->getNorm()), nullptr);
            t.appendChild (c, nullptr);
        }
    }
    t.setProperty ("index", s, nullptr);
    t.setProperty ("bypass", hp.bypass->get(), nullptr);
    t.setProperty ("solo", hp.solo->get(), nullptr);
    return t;
}

void BasecampProcessor::loadSlot (int s, juce::ValueTree saved, bool notify)
{
    std::unique_ptr<Module> old;
    {
        const juce::ScopedLock el (editLock);
        auto& hp = hostParams[(size_t) s];
        const auto id = saved.getProperty ("module").toString();
        std::unique_ptr<Module> m;
        if (id.isNotEmpty())
        {
            m = createModule (id);
            if (m) saved = m->migrate (saved, (int) saved.getProperty ("moduleVersion", 0));
            else   m = std::make_unique<PlaceholderModule> (saved);
        }
        auto* mod = m.get();
        if (mod != nullptr)   // settings go in before the module is prepared, so it starts on them
            for (int k = 0; k < mod->getNumParams(); ++k)
            {
                const auto& spec = mod->getParamSpecs()[(size_t) k];
                const auto c = saved.getChildWithProperty ("id", spec.id);
                mod->setParam (k, spec.snap (c.isValid() ? (float) c.getProperty ("value", spec.def) : spec.def));
            }
        {
            const juce::ScopedLock cl (chainLock);   // the audio thread skips at most one block here
            old = slots[(size_t) s].setModule (std::move (m));
            const auto prefix = "S" + two (s + 1) + " " + (mod ? mod->getDisplayName() : juce::String()) + ": ";
            const int n = mod ? mod->getNumParams() : 0;
            jassert (n <= poolSize);
            for (int k = 0; k < poolSize; ++k)
            {
                if (k < n)
                {
                    const auto& spec = mod->getParamSpecs()[(size_t) k];
                    hp.pool[(size_t) k]->bind (spec, prefix, mod->getParam (k));
                }
                else hp.pool[(size_t) k]->unbind();
            }
        }
        hp.bypass->setValueNotifyingHost ((bool) saved.getProperty ("bypass", false) ? 1.0f : 0.0f);
        hp.solo->setValueNotifyingHost ((bool) saved.getProperty ("solo", false) ? 1.0f : 0.0f);
    }
    old.reset();
    if (notify)
    {
        refreshLatency();
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true).withLatencyChanged (true));
        sendChangeMessage();
    }
}

void BasecampProcessor::refreshLatency()
{
    int total = 0;
    { const juce::ScopedLock l (editLock); for (auto& s : slots) total += s.getLatency(); }
    totalLatency.store (total);
    setLatencySamples (total);
}

juce::ValueTree BasecampProcessor::saveRack() const
{
    juce::ValueTree t ("BASECAMP");
    t.setProperty ("rackVersion", rackVersion, nullptr);
    t.setProperty ("inGain", inGain->get(), nullptr);
    t.setProperty ("outGain", outGain->get(), nullptr);
    for (int s = 0; s < numSlots; ++s)
        if (! slots[(size_t) s].isEmpty()) t.appendChild (saveSlot (s), nullptr);
    return t;
}

void BasecampProcessor::loadRack (const juce::ValueTree& t)
{
    if (! t.hasType ("BASECAMP")) return;
    *inGain = (float) t.getProperty ("inGain", 0.0f);
    *outGain = (float) t.getProperty ("outGain", 0.0f);
    for (int s = 0; s < numSlots; ++s)
    {
        auto child = t.getChildWithProperty ("index", s);
        loadSlot (s, child.isValid() ? child.createCopy() : juce::ValueTree ("SLOT"), s == numSlots - 1);
    }
}

void BasecampProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = saveRack().createXml()) copyXmlToBinary (*xml, dest);
}
void BasecampProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) loadRack (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
void BasecampProcessor::prepareToPlay (double sr, int block)
{
    {
        const juce::ScopedLock el (editLock);
        const juce::ScopedLock cl (chainLock);
        sampleRate = sr; blockSize = block;
        const int ch = getTotalNumOutputChannels();
        for (auto& s : slots) s.prepare (sr, block, ch);
        masterTp.prepare (ch); loudness.prepare (sr, ch);
        lastIn = lastOut = -1000.0f;
    }
    refreshLatency();
}

static void ramp (juce::AudioBuffer<float>& b, float& last, float target)
{
    if (last < -100.0f) last = target;
    b.applyGainRamp (0, b.getNumSamples(), last, target); last = target;
}

void BasecampProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    ramp (b, lastIn, dbToGain (inGain->get()));
    inMeter.push (b);

    {
        const juce::ScopedTryLock lock (chainLock);
        if (lock.isLocked())
        {
            bool anySolo = false;
            for (int s = 0; s < numSlots; ++s)
                anySolo = anySolo || (! slots[(size_t) s].isEmpty() && hostParams[(size_t) s].solo->get());

            for (int s = 0; s < numSlots; ++s)
            {
                auto& slot = slots[(size_t) s]; auto* m = slot.getModule();
                if (m == nullptr) continue;
                const auto& hp = hostParams[(size_t) s]; const auto& specs = m->getParamSpecs();
                for (size_t k = 0; k < specs.size(); ++k) m->setParam ((int) k, specs[k].fromNorm (hp.pool[k]->getNorm()));
                slot.process (b, hp.bypass->get() || (anySolo && ! hp.solo->get()));
            }
        }
    }

    ramp (b, lastOut, dbToGain (outGain->get()));
    outMeter.push (b);
    loudness.process (b);

    float tp = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float x = b.getSample (c, i);
            tp = juce::jmax (tp, std::abs (x), (float) masterTp.push (c, x, true));
        }
    float old = truePeakMax.load();
    while (tp > old && ! truePeakMax.compare_exchange_weak (old, tp)) {}
}
} // namespace ps

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ps::BasecampProcessor(); }
