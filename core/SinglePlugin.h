#pragma once
#include "ModuleSlot.h"
#include "HostParam.h"
#include "Look.h"

namespace ps
{
/** The version in the chip, from CMakeLists.txt so the chip and what the host reports can
    never disagree. */
#ifndef PS_VERSION
 #error "PS_VERSION comes from CMakeLists.txt"
#endif
inline const char* suiteVersion() { return PS_VERSION; }

/** Wraps one module as its own VST3/AU plugin. Parameter IDs are "<module>.<param>". */
class SinglePluginProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
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
        slot.tapSpectrum = true;
        slot.setModule (std::move (m));
    }
    Module& getModule() { return *module; }
    ModuleSlot& getSlot() { return slot; }

    /** What the spectrum display shows: 0 off, 1 output only, 2 input and output. Not a host
        parameter (nothing to automate) but saved with the project, as the JSFX saves its
        Spectrum switch. */
    std::atomic<int> spectrumView { 2 };
    std::vector<HostParam*> getModuleParams() const { return params; }

    const juce::String getName() const override { return "PodSherpa " + module->getDisplayName(); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    // The module's factory presets, offered to the host as programs: REAPER lists them in the
    // FX window's preset menu, beside any the user saves there.
    int getNumPrograms() override { return juce::jmax (1, module->getNumFactoryPresets()); }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int index) override
    {
        if (index < 0 || index >= module->getNumFactoryPresets()) return;
        program = index;
        module->applyFactoryPreset (index);
        // The values have to reach the host parameters, or the next block overwrites them again.
        const auto& specs = module->getParamSpecs();
        for (size_t i = 0; i < params.size(); ++i) params[i]->setValueNotifyingHost (specs[i].toNorm (module->getParam ((int) i)));
    }
    const juce::String getProgramName (int index) override
    {
        return index >= 0 && index < module->getNumFactoryPresets() ? module->getFactoryPresetName (index) : juce::String ("Default");
    }
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
        reported.store (slot.getLatency(), std::memory_order_relaxed);
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
        ramp (b, lastOut, dbToGain (juce::jmin (outGain->get(), module->getMaxOutputGainDb())));
        outMeter.push (b);
        // A control that changes the module's latency (Phase, Altitude's look-ahead) is answered
        // on the message thread: telling the host from here is not safe in every host.
        if (slot.getLatency() != reported.load (std::memory_order_relaxed) || module->peekSelfChanged() != 0) triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override
    {
        const int lat = slot.getLatency();
        if (lat != reported.load (std::memory_order_relaxed)) { reported.store (lat, std::memory_order_relaxed); setLatencySamples (lat); }
        if (const auto moved = module->takeSelfChanged(); moved != 0)
        {
            const auto& specs = module->getParamSpecs();
            for (size_t i = 0; i < params.size() && i < 32; ++i)
                if (moved & (1u << i)) params[i]->setValueNotifyingHost (specs[i].toNorm (module->getParam ((int) i)));
        }
    }
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::ValueTree t ("MODULE");
        t.setProperty ("module", module->getModuleId(), nullptr);
        t.setProperty ("moduleVersion", module->getVersion(), nullptr);
        t.setProperty ("bypass", bypass->get(), nullptr);
        t.setProperty ("inGain", inGain->get(), nullptr);
        t.setProperty ("outGain", outGain->get(), nullptr);
        if (module->hasSpectrum()) t.setProperty ("spectrum", spectrumView.load(), nullptr);
        for (auto* p : params)
        {
            juce::ValueTree c ("PARAM");
            const auto s = p->getSpec();
            c.setProperty ("id", s.id, nullptr); c.setProperty ("value", s.transient ? s.def : p->getReal(), nullptr);
            t.appendChild (c, nullptr);
        }
        if (auto extra = module->saveExtraState(); extra.isValid()) t.appendChild (extra, nullptr);
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
        spectrumView.store (juce::jlimit (0, 2, (int) t.getProperty ("spectrum", 2)));
        for (auto* p : params)
        {
            const auto s = p->getSpec();
            const auto c = t.getChildWithProperty ("id", s.id);
            p->setValue (s.toNorm (s.transient || ! c.isValid() ? s.def : (float) c.getProperty ("value", s.def)));
        }
        for (int i = 0; i < t.getNumChildren(); ++i)
            if (! t.getChild (i).hasType ("PARAM")) module->loadExtraState (t.getChild (i));
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
    std::atomic<int> reported { 0 };
    int program = 0;
};

/** The suite editor: header layout A, In and Out rails, footer.

    Header layout A is one control row: the plugin's name on the left, the preset bar centred,
    then the Look pill, the mode switch and the power button on the right.

    The Look arrives from SuiteSettings, which is shared by every plugin and every instance, so
    switching it in one window moves the others. Vintage is the exception: it is held here, for
    the life of this window, because the standard says it is never saved and never opens. Picking
    any Look swatch leaves it.

    Bypass greys the whole plugin rather than just dimming the controls, so a bypassed plugin in
    a rack of twelve is obvious at a glance. */
class SinglePluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SinglePluginEditor (SinglePluginProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p),
          theme (themes::byId (SuiteSettings::get().look())),
          mode (SuiteSettings::get().mode()),
          lnf (theme),
          panel (p.getModuleParams(), theme, mode),
          presets (theme, p),
          lookPill (theme),
          modeSwitch (theme, { "Beginner", "Normal", "Minimal" }),
          power (theme),
          footer (theme),
          inMeterBar (theme, p.inMeter), outMeterBar (theme, p.outMeter),
          inAtt (*p.inGain, inSlider), outAtt (*p.outGain, outSlider)
    {
        applyTheme();
        setLookAndFeel (&lnf);

        for (auto* s : { &inSlider, &outSlider })
        {
            s->setSliderStyle (juce::Slider::LinearVertical);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 26);
            s->setTextValueSuffix (" dB");
            addAndMakeVisible (*s);
        }

        lookPill.setLook (SuiteSettings::get().look());
        lookPill.onChange = [this] (Theme::Id id)
        {
            SuiteSettings::get().setLook (id);   // leaves Vintage, as the standard requires
            vintage = false;
            customAccent = -1;
            pullSuiteLook();
        };

        modeSwitch.setIndex ((int) mode);
        modeSwitch.onChange = [this] (int i)
        {
            SuiteSettings::get().setMode ((Mode) i);
            mode = (Mode) i;
            panel.setMode (mode);
            setSize (getWidth(), preferredHeight());   // Beginner adds a caption under every row
            resized();
            repaint();
        };

        power.setBypassed (proc.bypass->get());
        power.onToggle = [this] (bool b)
        {
            proc.bypass->beginChangeGesture();
            proc.bypass->setValueNotifyingHost (b ? 1.0f : 0.0f);
            proc.bypass->endChangeGesture();
            repaint();
        };

        footer.onVintageToggle = [this]
        {
            vintage = ! vintage;
            customAccent = -1;
            pullSuiteLook();
        };
        footer.onAccentWheel = [this]
        {
            customAccent = (customAccent + 1) % numVintageAccents;
            applyTheme();
            repaint();
        };

        addAndMakeVisible (panel);
        addAndMakeVisible (presets);
        addAndMakeVisible (lookPill);
        addAndMakeVisible (modeSwitch);
        addAndMakeVisible (power);
        addAndMakeVisible (footer);
        addAndMakeVisible (inMeterBar);
        addAndMakeVisible (outMeterBar);
        if (p.getModule().hasFrequencyResponse())
        {
            response = std::make_unique<ResponseCurve> (theme, p.getModule());
            response->setHandleParams (p.getModuleParams());
            if (p.getModule().hasSpectrum())
            {
                response->setSpectrum (&specIn.getBands(), &specOut.getBands());
                response->setView (p.spectrumView.load());
                response->onViewChange = [this] (int v) { proc.spectrumView.store (v); };
            }
            addAndMakeVisible (*response);
        }
        if (p.getModule().hasTransferCurve())
        {
            curve = std::make_unique<TransferCurve> (theme, p.getModule());
            curve->setParams (p.getModuleParams());
            history = std::make_unique<ReductionHistory> (theme, &p.getModule());
            addAndMakeVisible (*curve);
            addAndMakeVisible (*history);
        }
        if (const auto lbl = p.getModule().getModuleMeterLabel(); lbl.isNotEmpty())
        {
            reduction = std::make_unique<ReductionMeter> (theme, lbl);
            // Altitude rides gain in both directions and can boost well past a limiter's range.
            reduction->setRange (p.getModule().getMaxOutputGainDb() > 0.0f ? 18.0f : 12.0f);
            addAndMakeVisible (*reduction);
        }
        extraReset.setButtonText ("Restart");
        extraReset.setTooltip ("Start the loudness measurement again from here.");
        extraReset.onClick = [this] { proc.getModule().resetExtraReadout(); };
        addChildComponent (extraReset);
        tips.setMillisecondsBeforeTipAppears (700);
        addAndMakeVisible (tips);

        setSize (1120, preferredHeight());
        startTimerHz (timerRate);
    }

    ~SinglePluginEditor() override { setLookAndFeel (nullptr); }

    /** Used by ps_shot, which has no host and no window, to force a Look for a screenshot. */
    void setLookForShot (Theme::Id id, Mode m)
    {
        vintage = id == Theme::Id::Vintage;
        lookPill.setLook (id);
        mode = m;
        modeSwitch.setIndex ((int) m);
        panel.setMode (m);
        theme = vintage ? themes::vintage() : themes::byId (id);
        if (mode == Mode::Minimal) theme = themes::withMinimal (theme);
        lnf.setTheme (theme);
        panel.setTheme (theme);
        // Dropdowns copy their text colour when the look-and-feel is attached and keep it, so
        // without this a switch into Vintage left pale text on its silver dropdowns.
        sendLookAndFeelChange();
        setSize (getWidth(), preferredHeight());   // the mode may have changed the row height
        resized();
        repaint();
    }
    void tickForShot() { timerCallback(); }
    ResponseCurve* responseForShot() { return response.get(); }
    TransferCurve* curveForShot() { return curve.get(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.bg);

        // Header: name, type and version on the left. The version sits in a knocked-out box.
        g.setColour (theme.text);
        g.setFont (type::titleFont (type::title));
        const auto title = "PodSherpa " + proc.getModule().getDisplayName();
        g.drawText (title, titleArea, juce::Justification::centredLeft);
        const int tw = juce::GlyphArrangement::getStringWidthInt (type::titleFont (type::title), title);

        auto after = titleArea.withTrimmedLeft (tw + 16);
        g.setColour (theme.dim);
        g.setFont (type::font (type::subtitle, true));
        const auto sub = proc.getModule().getSubtitle();
        g.drawText (sub, after, juce::Justification::centredLeft);
        const int sw = juce::GlyphArrangement::getStringWidthInt (type::font (type::subtitle, true), sub);

        auto chip = after.withTrimmedLeft (sw + 12).withWidth (54).withSizeKeepingCentre (54, 22);
        g.setColour (theme.line);
        g.fillRoundedRectangle (chip.toFloat(), 4.0f);
        g.setColour (theme.text);
        g.setFont (type::font (type::footer, true));
        g.drawText ("v" + juce::String (suiteVersion()), chip, juce::Justification::centred);

        // Main panel, and a rail behind each meter so they do not float against the window edge.
        g.setColour (theme.panel);
        g.fillRoundedRectangle (panelArea.toFloat(), theme.corner);
        g.setColour (theme.panel.withAlpha (0.6f));
        g.fillRoundedRectangle (railLeft.toFloat(), theme.corner);
        g.fillRoundedRectangle (railRight.toFloat(), theme.corner);

        // Rail captions. "Out max" appears when the module holds the output below the setting,
        // which otherwise looks like a broken control.
        g.setColour (theme.dim);
        g.setFont (type::font (type::support));
        g.drawText ("In", inLabel, juce::Justification::centred);
        const float outCap = proc.getModule().getMaxOutputGainDb();
        g.drawText (proc.outGain->get() > outCap + 0.001f ? "Out max " + juce::String (outCap, 1) : juce::String ("Out"),
                    outLabel, juce::Justification::centred);

        // The module's own meter, if it has one. Gain reduction is accent2 (blue in Default) and
        // always carries its label and value, so the colour is never the only signal.

        // The module's second readout (Summit's integrated loudness). Coloured by whether it is
        // on target, but the verdict is written out too, so colour is never the only signal.
        if (const auto extra = proc.getModule().getExtraReadout(); extra.isNotEmpty())
        {
            g.setColour (proc.getModule().extraReadoutIsGood() ? theme.accent : theme.warn);
            g.setFont (type::font (type::label, true));
            g.drawText (extra, extraArea, juce::Justification::centredRight);
        }

        if (mode == Mode::Beginner) paintPip (g);

        if (proc.bypass->get())
        {
            g.setColour (theme.bg.withAlpha (0.62f));
            g.fillRect (getLocalBounds());
            g.setColour (theme.warn);
            g.setFont (type::font (type::value, true));
            g.drawText ("Bypassed", getLocalBounds().removeFromTop (headerH), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();

        measuredTitleWidth = headerLeftWidth();
        auto head = r.removeFromTop (headerH).reduced (24, 0);
        power.setBounds (head.removeFromRight (44).withSizeKeepingCentre (40, 40));
        head.removeFromRight (14);
        modeSwitch.setBounds (head.removeFromRight (modeSwitch.idealWidth()).withSizeKeepingCentre (modeSwitch.idealWidth(), 32));
        head.removeFromRight (10);
        lookPill.setBounds (head.removeFromRight (lookPill.idealWidth()).withSizeKeepingCentre (lookPill.idealWidth(), 32));
        titleArea = head;
        // The preset bar is centred on the window rather than on what is left of the row, so it
        // stays put as the title and the right-hand controls change width. It is clamped to the
        // gap between them, because at the default width a centred bar and a long plugin name
        // overlap - which is exactly what the first render showed.
        const int gapL = titleArea.getX() + measuredTitleWidth + 24;
        const int gapR = head.getRight();
        int pw = juce::jmin (presets.idealWidth(), juce::jmax (120, gapR - gapL - 16));
        int cx = juce::jlimit (gapL + pw / 2, juce::jmax (gapL + pw / 2, gapR - pw / 2), getWidth() / 2);
        presets.setBounds (juce::Rectangle<int> (pw, 34).withCentre ({ cx, headerH / 2 }));

        footer.setBounds (r.removeFromBottom (34));

        railLeft  = r.removeFromLeft (136).reduced (10, 8);
        railRight = r.removeFromRight (136).reduced (10, 8);
        auto left  = railLeft.reduced (10, 10);
        auto right = railRight.reduced (10, 10);
        inLabel  = left.removeFromTop (22);
        outLabel = right.removeFromTop (22);
        left.removeFromTop (4); right.removeFromTop (4);
        // Meter beside its own fader, both inside the rail, so the pair reads as one control.
        inMeterBar.setBounds (left.removeFromLeft (24));
        left.removeFromLeft (8);
        inSlider.setBounds (left);
        outMeterBar.setBounds (right.removeFromRight (24));
        right.removeFromRight (8);
        outSlider.setBounds (right);

        panelArea = r.reduced (4, 8);
        auto inner = panelArea.reduced (24, 16);
        const bool hasExtra = proc.getModule().getExtraReadout().isNotEmpty();
        // Each readout gets its own row. Sharing one left the reduction bar about 130 px wide,
        // which is too short to judge depth on.
        auto strip = inner.removeFromBottom (stripHeight());
        if (reduction != nullptr) reduction->setBounds (strip.removeFromTop (42));
        if (history != nullptr) history->setBounds (historyArea);
        if (hasExtra)
        {
            auto row = strip.removeFromTop (32);
            extraReset.setVisible (proc.getModule().hasExtraReadoutReset());
            if (extraReset.isVisible())
            {
                extraReset.setBounds (row.removeFromRight (84).withSizeKeepingCentre (84, 26));
                row.removeFromRight (12);
            }
            extraArea = row;
        }
        else { extraArea = {}; extraReset.setVisible (false); }
        moduleMeterArea = strip;
        if (mode == Mode::Beginner) pipArea = inner.removeFromTop (54);
        else                        pipArea = {};

        // A module with a curve gets the JSFX arrangement: the curve card on the left, the
        // controls beside it, and the reduction history across the bottom.
        if (curve != nullptr)
        {
            historyArea = inner.removeFromBottom (126);
            inner.removeFromBottom (10);
            curve->setBounds (inner.removeFromLeft (juce::jmin (400, inner.getWidth() / 2)));
            inner.removeFromLeft (16);
        }
        else historyArea = {};

        // An EQ reads better with the curve across the top and the bands under it, which is how
        // the JSFX lays it out: the shape is the thing you steer by, the numbers follow.
        if (response != nullptr)
        {
            response->setBounds (inner.removeFromTop (juce::jmin (230, inner.getHeight() / 2)));
            inner.removeFromTop (10);
        }

        panel.setBounds (inner);
    }

private:
    static constexpr int headerH = 74;
    static constexpr int timerRate = 30;
    static constexpr int numVintageAccents = 5;

    /** Tall enough for the controls at the current mode, plus the header, the footer, the
        module's own meter strip and, in Beginner, Pip's badge. Worked out rather than fixed,
        because the three modes differ by about 150 px on a five-control plugin. */
    int preferredHeight() const
    {
        // header, footer, panel margins, then whatever the readout strip actually needs: the
        // reduction meter and the extra readout each take their own row.
        const int stripH = stripHeight();
        const int chrome = headerH + 34 + 16 + stripH + 32;
        const int pip = mode == Mode::Beginner ? 58 : 0;
        if (curve != nullptr)
        {
            // Tall enough for the curve card and the history, or for the controls beside the
            // curve, whichever needs more.
            const int body = juce::jmax (300, panel.idealHeight());
            return chrome + pip + body + 136;
        }
        if (response != nullptr) return chrome + pip + 240 + panel.idealHeight();
        return juce::jmax (440, chrome + pip + panel.idealHeight());
    }

    /** The readout strip under the controls. A plain module with nothing to read out keeps a
        little breathing room there; an EQ with a graph does not need it, and on Contour it was a
        34 px empty band at the bottom of the panel. */
    int stripHeight() const
    {
        const bool hasExtra = proc.getModule().getExtraReadout().isNotEmpty();
        return (reduction != nullptr ? 42 : 0) + (hasExtra ? 32 : 0)
             + (reduction == nullptr && ! hasExtra && response == nullptr ? 34 : 0);
    }

    /** Width of the name, processor type and version chip together, so resized() knows where the
        left block ends and the preset bar can be kept clear of it. */
    int headerLeftWidth() const
    {
        const auto title = "PodSherpa " + proc.getModule().getDisplayName();
        int w = juce::GlyphArrangement::getStringWidthInt (type::titleFont (type::title), title) + 16;
        w += juce::GlyphArrangement::getStringWidthInt (type::font (type::subtitle, true), proc.getModule().getSubtitle());
        return w + 12 + 54;   // gap plus the version chip
    }

    /** Pip, the mountain-guide mascot, with one plain-language line about this plugin. Beginner
        mode only. This is a placeholder drawn in vector: real artwork for Pip is still to come,
        and the badge is sized so swapping in an image does not move the layout. */
    void paintPip (juce::Graphics& g) const
    {
        if (pipArea.isEmpty()) return;
        auto r = pipArea;
        auto badge = r.removeFromLeft (46).withSizeKeepingCentre (42, 42);

        g.setColour (theme.panelHi);
        g.fillEllipse (badge.toFloat());
        g.setColour (theme.accent);
        g.drawEllipse (badge.toFloat().reduced (0.5f), 1.4f);

        // A mountain and a sun: the guide, in the smallest marks that still read.
        juce::Path m;
        auto b = badge.toFloat().reduced (9.0f);
        m.startNewSubPath (b.getX(), b.getBottom());
        m.lineTo (b.getX() + b.getWidth() * 0.38f, b.getY() + b.getHeight() * 0.28f);
        m.lineTo (b.getX() + b.getWidth() * 0.58f, b.getY() + b.getHeight() * 0.62f);
        m.lineTo (b.getX() + b.getWidth() * 0.72f, b.getY() + b.getHeight() * 0.40f);
        m.lineTo (b.getRight(), b.getBottom());
        m.closeSubPath();
        g.setColour (theme.accent);
        g.fillPath (m);

        r.removeFromLeft (10);
        g.setColour (theme.text);
        g.setFont (type::font (type::label));
        g.drawFittedText (proc.getModule().getSubtitle().isNotEmpty()
                              ? "Pip says: " + plainIntro()
                              : juce::String ("Pip says: start with a preset, then nudge one control at a time."),
                          r, juce::Justification::centredLeft, 2);
    }

    juce::String plainIntro() const
    {
        const auto n = proc.getModule().getDisplayName();
        return "this is your " + proc.getModule().getSubtitle().toLowerCase()
             + ". Start with a preset, then nudge one control at a time and listen.";
    }

    /** Re-reads the suite Look (or keeps Vintage) and pushes it everywhere. */
    void pullSuiteLook()
    {
        theme = vintage ? themes::vintage() : themes::byId (SuiteSettings::get().look());
        applyTheme();
        resized();
        repaint();
    }

    void applyTheme()
    {
        if (mode == Mode::Minimal) theme = themes::withMinimal (theme);
        if (vintage && customAccent >= 0)
        {
            static const juce::Colour wheel[numVintageAccents] {
                juce::Colour (0xffe8a33d), juce::Colour (0xff8fb0a0), juce::Colour (0xffd2644f),
                juce::Colour (0xff9ab0d8), juce::Colour (0xffc9a86a)
            };
            theme.accent = wheel[customAccent];
            theme.accentIsCustom = true;
        }
        lnf.setTheme (theme);
        panel.setTheme (theme);
        // Dropdowns copy their text colour when the look-and-feel is attached and keep it, so
        // without this a switch into Vintage left pale text on its silver dropdowns.
        sendLookAndFeelChange();
    }

    void timerCallback() override
    {
        // Another plugin, possibly in another process, may have changed the Look.
        SuiteSettings::get().reloadIfChangedExternally();
        const int rev = SuiteSettings::get().getRevision();
        if (rev != seenRevision)
        {
            seenRevision = rev;
            const auto m = SuiteSettings::get().mode();
            if (m != mode) { mode = m; modeSwitch.setIndex ((int) m); panel.setMode (m); }
            if (! vintage) lookPill.setLook (SuiteSettings::get().look());
            pullSuiteLook();
        }
        power.setBypassed (proc.bypass->get());
        inMeterBar.tick();
        outMeterBar.tick();
        moduleMeter = proc.getModule().readModuleMeter();
        if (reduction != nullptr) reduction->push (moduleMeter);
        float levelIn = -200.0f, levelOut = -200.0f;
        if (curve != nullptr && proc.getModule().readCurveLevels (levelIn, levelOut)) curve->pushLevel (levelIn);
        if (history != nullptr) history->push (moduleMeter, levelIn, levelOut);
        if (curve != nullptr) curve->repaint();   // the curve follows the controls as they move
        if (response != nullptr)
        {
            if (proc.getModule().hasSpectrum())
            {
                auto& slot = proc.getSlot();
                const int view = proc.spectrumView.load();
                if (view != response->getView()) response->setView (view);   // a project was loaded
                if (view == 2) specIn.update (slot.preTap, slot.getSampleRate(), timerRate);
                if (view >= 1) specOut.update (slot.postTap, slot.getSampleRate(), timerRate);
            }
            response->repaint();
        }
        repaint();
    }

    SinglePluginProcessor& proc;
    Theme theme;
    Mode mode;
    PsLookAndFeel lnf;
    ModulePanel panel;
    PresetBar presets;
    LookPill lookPill;
    Segmented modeSwitch;
    PowerButton power;
    Footer footer;
    ThemedMeter inMeterBar, outMeterBar;
    juce::Slider inSlider, outSlider;
    juce::SliderParameterAttachment inAtt, outAtt;
    juce::TooltipWindow tips { this };
    std::unique_ptr<ResponseCurve> response;
    SpectrumAnalyser specIn, specOut;
    std::unique_ptr<TransferCurve> curve;
    std::unique_ptr<ReductionHistory> history;
    std::unique_ptr<ReductionMeter> reduction;
    juce::TextButton extraReset;
    juce::Rectangle<int> panelArea, inLabel, outLabel, titleArea, moduleMeterArea, extraArea, pipArea, railLeft, railRight, historyArea;
    float moduleMeter = 0.0f;
    int measuredTitleWidth = 0;
    bool vintage = false;
    int customAccent = -1;
    int seenRevision = SuiteSettings::get().getRevision();
};

inline juce::AudioProcessorEditor* SinglePluginProcessor::createEditor() { return new SinglePluginEditor (*this); }
} // namespace ps
