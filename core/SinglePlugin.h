#pragma once
#include "ModuleSlot.h"
#include "HostParam.h"
#include "Look.h"

namespace ps
{
inline const char* suiteVersion() { return "0.1"; }

/** Wraps one module as its own VST3/AU plugin. Parameter IDs are "<module>.<param>". */
class SinglePluginProcessor : public juce::AudioProcessor
{
public:
    explicit SinglePluginProcessor (std::unique_ptr<Module> m)
        : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        module = m.get();
        const auto id = module->getModuleId();
        addParameter (bypass = new juce::AudioParameterBool (juce::ParameterID { "bypass", 1 }, "Bypass", false));
        addParameter (inGain = new juce::AudioParameterFloat (juce::ParameterID { "in", 1 }, "Input gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        addParameter (outGain = new juce::AudioParameterFloat (juce::ParameterID { "out", 1 }, "Output gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        for (auto& s : module->getParamSpecs())
        {
            auto* p = new HostParam (id + "." + s.id, s.name);
            p->bind (s, {}, s.def);
            params.push_back (p); addParameter (p);
        }
        slot.setModule (std::move (m));
    }
    Module& getModule() { return *module; }
    std::vector<HostParam*> getModuleParams() const { return params; }

    const juce::String getName() const override { return "PodSherpa " + module->getDisplayName(); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return out == l.getMainInputChannelSet() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }
    void prepareToPlay (double sr, int block) override
    {
        slot.prepare (sr, block, getTotalNumOutputChannels());
        setLatencySamples (slot.getLatency());
        lastIn = lastOut = -1000.0f;
    }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const auto& specs = module->getParamSpecs();
        for (size_t i = 0; i < params.size(); ++i) module->setParam ((int) i, specs[i].fromNorm (params[i]->getNorm()));
        ramp (b, lastIn, dbToGain (inGain->get()));
        inMeter.push (b);
        slot.process (b, bypass->get());
        ramp (b, lastOut, dbToGain (outGain->get()));
        outMeter.push (b);
    }
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::ValueTree t ("MODULE");
        t.setProperty ("module", module->getModuleId(), nullptr);
        t.setProperty ("moduleVersion", module->getVersion(), nullptr);
        t.setProperty ("bypass", bypass->get(), nullptr);
        t.setProperty ("inGain", inGain->get(), nullptr);
        t.setProperty ("outGain", outGain->get(), nullptr);
        for (auto* p : params)
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", p->getSpec().id, nullptr); c.setProperty ("value", p->getReal(), nullptr);
            t.appendChild (c, nullptr);
        }
        if (auto xml = t.createXml()) copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        auto xml = getXmlFromBinary (data, size);
        if (! xml) return;
        auto t = juce::ValueTree::fromXml (*xml);
        if (! t.hasType ("MODULE") || t.getProperty ("module").toString() != module->getModuleId()) return;
        t = module->migrate (t, (int) t.getProperty ("moduleVersion", 0));
        *bypass = (bool) t.getProperty ("bypass", false);
        *inGain = (float) t.getProperty ("inGain", 0.0f);
        *outGain = (float) t.getProperty ("outGain", 0.0f);
        for (auto* p : params)
        {
            const auto s = p->getSpec();
            const auto c = t.getChildWithProperty ("id", s.id);
            p->setValue (s.toNorm (c.isValid() ? (float) c.getProperty ("value", s.def) : s.def));
        }
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    LevelMeter inMeter, outMeter;
    juce::AudioParameterBool* bypass = nullptr;
    juce::AudioParameterFloat *inGain = nullptr, *outGain = nullptr;

private:
    static void ramp (juce::AudioBuffer<float>& b, float& last, float target)
    {
        if (last < -100.0f) last = target;
        b.applyGainRamp (0, b.getNumSamples(), last, target); last = target;
    }
    Module* module = nullptr;
    ModuleSlot slot;
    std::vector<HostParam*> params;
    float lastIn = -1000.0f, lastOut = -1000.0f;
};

/** Plain first-pass editor: side rails (In left, Out right), power button top right, footer. */
class SinglePluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SinglePluginEditor (SinglePluginProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p), panel (p.getModuleParams()), inBar (p.inMeter), outBar (p.outMeter),
          inAtt (*p.inGain, inSlider), outAtt (*p.outGain, outSlider), powerAtt (*p.bypass, power)
    {
        setLookAndFeel (&lnf);
        for (auto* s : { &inSlider, &outSlider })
        {
            s->setSliderStyle (juce::Slider::LinearVertical);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 24);
            s->setTextValueSuffix (" dB");
            addAndMakeVisible (*s);
        }
        power.setClickingTogglesState (true); power.setButtonText ("Bypass");
        addAndMakeVisible (power); addAndMakeVisible (panel); addAndMakeVisible (inBar); addAndMakeVisible (outBar);
        setSize (1120, juce::jmax (420, 190 + panel.idealHeight()));
        startTimerHz (30);
    }
    ~SinglePluginEditor() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (look::bg);
        auto r = getLocalBounds();
        auto head = r.removeFromTop (70).reduced (24, 0);
        g.setColour (look::text); g.setFont (look::font (28.0f, true));
        const auto title = "PodSherpa " + proc.getModule().getDisplayName();
        g.drawText (title, head, juce::Justification::centredLeft);
        const int tw = juce::GlyphArrangement::getStringWidthInt (look::font (28.0f, true), title);
        g.setColour (look::dim); g.setFont (look::font (20.0f, true));
        g.drawText (proc.getModule().getSubtitle() + "   v" + suiteVersion(), head.withTrimmedLeft (tw + 18), juce::Justification::centredLeft);
        look::footer (g, r.removeFromBottom (34));
        g.setColour (look::panel); g.fillRoundedRectangle (panelArea.toFloat(), 10.0f);
        g.setColour (look::dim); g.setFont (look::font (15.0f));
        g.drawText ("In", inLabel, juce::Justification::centred); g.drawText ("Out", outLabel, juce::Justification::centred);
        if (proc.getModule().getModuleMeterLabel().isNotEmpty())
        {
            g.setColour (look::blue); g.setFont (look::font (22.0f, true));
            g.drawText (proc.getModule().getModuleMeterLabel() + ": " + juce::String (moduleMeter, 1) + " dB",
                        panelArea.reduced (24, 12).removeFromBottom (34), juce::Justification::centredLeft);
        }
        if (proc.bypass->get()) { g.setColour (look::bg.withAlpha (0.55f)); g.fillRect (getLocalBounds().withTrimmedTop (70)); }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        power.setBounds (r.removeFromTop (70).removeFromRight (140).reduced (20, 18));
        r.removeFromBottom (34);
        auto left = r.removeFromLeft (130).reduced (14, 8), right = r.removeFromRight (130).reduced (14, 8);
        inLabel = left.removeFromTop (22); outLabel = right.removeFromTop (22);
        inBar.setBounds (left.removeFromLeft (26)); inSlider.setBounds (left);
        outBar.setBounds (right.removeFromRight (26)); outSlider.setBounds (right);
        panelArea = r.reduced (4, 8);
        panel.setBounds (panelArea.reduced (24, 16).withTrimmedBottom (40));
    }
private:
    void timerCallback() override
    {
        inBar.tick(); outBar.tick();
        moduleMeter = juce::jmax (proc.getModule().readModuleMeter(), moduleMeter - 0.5f);
        repaint();
    }
    SinglePluginProcessor& proc;
    PsLookAndFeel lnf;
    ModulePanel panel;
    MeterBar inBar, outBar;
    juce::Slider inSlider, outSlider;
    juce::TextButton power;
    juce::SliderParameterAttachment inAtt, outAtt;
    juce::ButtonParameterAttachment powerAtt;
    juce::Rectangle<int> panelArea, inLabel, outLabel;
    float moduleMeter = 0.0f;
};

inline juce::AudioProcessorEditor* SinglePluginProcessor::createEditor() { return new SinglePluginEditor (*this); }
} // namespace ps
