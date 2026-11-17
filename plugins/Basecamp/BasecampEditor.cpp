#include "BasecampProcessor.h"
#include "../../core/Look.h"
#include "../../core/SinglePlugin.h"

namespace ps
{
/** Basecamp: slot list on the left, the selected module's controls on the right, In/Out rails,
    and the master readouts under the header.

    It carries the same furniture as the single plugins - the Look pill, the mode switch and the
    footer that hides Vintage - so that switching the Look anywhere moves the whole suite, this
    rack included. The readouts (latency, short-term loudness, true peak) are Basecamp's own and
    sit on their own line, because header layout A's control row is already full. */
class BasecampEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit BasecampEditor (BasecampProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p),
          theme (themes::byId (SuiteSettings::get().look())),
          mode (SuiteSettings::get().mode()),
          lnf (theme),
          lookPill (theme), modeSwitch (theme, { "Beginner", "Normal", "Minimal" }),
          footer (theme),
          inBar (theme, p.inMeter), outBar (theme, p.outMeter),
          inAtt (*p.inGain, inSlider), outAtt (*p.outGain, outSlider)
    {
        if (mode == Mode::Minimal) theme = themes::withMinimal (theme);
        lnf.setTheme (theme);
        setLookAndFeel (&lnf);

        lookPill.setLook (SuiteSettings::get().look());
        lookPill.onChange = [this] (Theme::Id id)
        {
            SuiteSettings::get().setLook (id);
            vintage = false;
            pullSuiteLook();
        };
        modeSwitch.setIndex ((int) mode);
        modeSwitch.onChange = [this] (int i)
        {
            SuiteSettings::get().setMode ((Mode) i);
            mode = (Mode) i;
            rebuildPanel();
            pullSuiteLook();
        };
        footer.onVintageToggle = [this] { vintage = ! vintage; pullSuiteLook(); };
        addAndMakeVisible (lookPill);
        addAndMakeVisible (modeSwitch);
        addAndMakeVisible (footer);
        for (auto* s : { &inSlider, &outSlider })
        {
            s->setSliderStyle (juce::Slider::LinearVertical);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 24);
            s->setTextValueSuffix (" dB");
            addAndMakeVisible (*s);
        }
        addAndMakeVisible (inBar); addAndMakeVisible (outBar);

        for (int s = 0; s < BasecampProcessor::numSlots; ++s)
        {
            auto row = std::make_unique<Row>();
            const auto& hp = proc.getSlotHostParams (s);
            row->select.setButtonText (juce::String (s + 1));
            row->select.setClickingTogglesState (false);
            row->select.onClick = [this, s] { selected = s; rebuildPanel(); refreshRows(); };
            row->bypass.setButtonText ("Bypass"); row->bypass.setClickingTogglesState (true);
            row->solo.setButtonText ("Solo"); row->solo.setClickingTogglesState (true);
            row->bypass.setColour (juce::TextButton::buttonOnColourId, theme.warn);
            row->up.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb2")); row->down.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbc"));
            row->up.onClick = [this, s] { if (s > 0) { proc.moveSlot (s, s - 1); if (selected == s) selected = s - 1; else if (selected == s - 1) selected = s; rebuildPanel(); refreshRows(); } };
            row->down.onClick = [this, s] { if (s < BasecampProcessor::numSlots - 1) { proc.moveSlot (s, s + 1); if (selected == s) selected = s + 1; else if (selected == s + 1) selected = s; rebuildPanel(); refreshRows(); } };
            row->module.onChange = [this, s]
            {
                const int id = rows[(size_t) s]->module.getSelectedId();
                if (id == 1) { if (proc.getSlotModuleId (s).isNotEmpty()) proc.setSlotModule (s, {}); }
                else if (id >= 2 && id < 100)
                {
                    const auto& info = moduleRegistry()[(size_t) (id - 2)];
                    if (proc.getSlotModuleId (s) != info.id) { selected = s; proc.setSlotModule (s, info.id); }
                }
            };
            row->bypassAtt = std::make_unique<juce::ButtonParameterAttachment> (*hp.bypass, row->bypass);
            row->soloAtt = std::make_unique<juce::ButtonParameterAttachment> (*hp.solo, row->solo);
            for (auto* c : std::initializer_list<juce::Component*> { &row->select, &row->module, &row->bypass, &row->solo, &row->up, &row->down })
                addAndMakeVisible (c);
            rows.push_back (std::move (row));
        }
        proc.addChangeListener (this);
        refreshRows(); rebuildPanel();
        setSize (1120, 740);
        startTimerHz (30);
    }
    ~BasecampEditor() override { proc.removeChangeListener (this); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.bg);
        auto r = getLocalBounds();
        auto head = r.removeFromTop (headerH).reduced (24, 0);
        g.setColour (theme.text); g.setFont (type::titleFont (type::title));
        g.drawText ("PodSherpa Basecamp", head, juce::Justification::centredLeft);
        const int tw = juce::GlyphArrangement::getStringWidthInt (type::titleFont (type::title), "PodSherpa Basecamp");
        g.setColour (theme.dim); g.setFont (type::font (type::subtitle, true));
        g.drawText ("Master rack", head.withTrimmedLeft (tw + 16), juce::Justification::centredLeft);

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const float lufs = proc.loudness.get();
        g.setColour (theme.accent); g.setFont (type::font (type::label, true));
        g.drawText ("Latency " + juce::String (proc.getTotalLatency() * 1000.0 / sr, 1) + " ms      "
                    + "Short-term " + (lufs < -100.0f ? juce::String ("-") : juce::String (lufs, 1)) + " LUFS      "
                    + "True peak " + (tpHold < 1.0e-5f ? juce::String ("-") : juce::String (gainToDb (tpHold), 1)) + " dBTP",
                    readouts, juce::Justification::centredRight);

        r.removeFromBottom (34);
        g.setColour (theme.panel);
        g.fillRoundedRectangle (listArea.toFloat(), theme.corner); g.fillRoundedRectangle (panelArea.toFloat(), theme.corner);
        g.setColour (theme.panel.withAlpha (0.6f));
        g.fillRoundedRectangle (railLeft.toFloat(), theme.corner); g.fillRoundedRectangle (railRight.toFloat(), theme.corner);
        g.setColour (theme.dim); g.setFont (type::font (type::support));
        g.drawText ("In", inLabel, juce::Justification::centred); g.drawText ("Out", outLabel, juce::Justification::centred);

        auto pa = panelArea.reduced (24, 14);
        g.setColour (theme.text); g.setFont (type::font (type::value, true));
        const auto name = proc.getSlotName (selected);
        g.drawText ("SLOT " + juce::String (selected + 1) + (name.isNotEmpty() ? "   " + name : juce::String ("   Empty")), pa.removeFromTop (38), juce::Justification::centredLeft);
        if (name.isEmpty())
        {
            g.setColour (theme.dim); g.setFont (type::font (type::label));
            g.drawText ("Pick a module for this slot from the list on the left.", pa.removeFromTop (40), juce::Justification::centredLeft);
        }
        else if (proc.isSlotPlaceholder (selected))
        {
            g.setColour (theme.warn); g.setFont (type::font (type::label));
            g.drawFittedText ("This module is not in this build. Audio passes through unchanged and its settings are kept in the project.", pa.removeFromTop (60), juce::Justification::centredLeft, 2);
        }
        else if (auto* m = proc.getSlot (selected).getModule(); m != nullptr && m->getModuleMeterLabel().isNotEmpty())
        {
            g.setColour (theme.accent2); g.setFont (type::font (type::value, true));
            g.drawText (m->getModuleMeterLabel() + ": " + juce::String (moduleMeter, 1) + " dB", pa.removeFromBottom (34), juce::Justification::centredLeft);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (headerH).reduced (24, 0);
        modeSwitch.setBounds (head.removeFromRight (modeSwitch.idealWidth()).withSizeKeepingCentre (modeSwitch.idealWidth(), 32));
        head.removeFromRight (10);
        lookPill.setBounds (head.removeFromRight (lookPill.idealWidth()).withSizeKeepingCentre (lookPill.idealWidth(), 32));
        readouts = r.removeFromTop (28).reduced (24, 0);
        footer.setBounds (r.removeFromBottom (34));
        railLeft = r.removeFromLeft (124).reduced (8, 8); railRight = r.removeFromRight (124).reduced (8, 8);
        auto left = railLeft.reduced (10, 10), right = railRight.reduced (10, 10);
        inLabel = left.removeFromTop (22); outLabel = right.removeFromTop (22);
        left.removeFromTop (4); right.removeFromTop (4);
        inBar.setBounds (left.removeFromLeft (24)); left.removeFromLeft (8); inSlider.setBounds (left);
        outBar.setBounds (right.removeFromRight (24)); right.removeFromRight (8); outSlider.setBounds (right);
        listArea = r.removeFromLeft (452).reduced (4, 8);
        panelArea = r.reduced (4, 8);
        auto rr = listArea.reduced (10, 10);
        const int h = rr.getHeight() / BasecampProcessor::numSlots;
        for (auto& row : rows)
        {
            auto line = rr.removeFromTop (h).reduced (0, 4);
            row->select.setBounds (line.removeFromLeft (40)); line.removeFromLeft (6);
            row->down.setBounds (line.removeFromRight (36)); line.removeFromRight (4);
            row->up.setBounds (line.removeFromRight (36)); line.removeFromRight (6);
            row->solo.setBounds (line.removeFromRight (62)); line.removeFromRight (4);
            row->bypass.setBounds (line.removeFromRight (78)); line.removeFromRight (6);
            row->module.setBounds (line);
        }
        if (panel) panel->setBounds (panelArea.reduced (24, 14).withTrimmedTop (52).withTrimmedBottom (40));
    }

private:
    struct Row
    {
        juce::TextButton select, bypass, solo, up, down; juce::ComboBox module;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAtt, soloAtt;
    };
    void refreshRows()
    {
        for (int s = 0; s < (int) rows.size(); ++s)
        {
            auto& row = *rows[(size_t) s];
            const auto id = proc.getSlotModuleId (s);
            row.module.clear (juce::dontSendNotification);
            row.module.addItem ("Empty", 1);
            int sel = 1, i = 2;
            for (auto& info : moduleRegistry()) { row.module.addItem (info.name, i); if (info.id == id) sel = i; ++i; }
            if (id.isNotEmpty() && sel == 1) { row.module.addItem (proc.getSlotName (s), 100); sel = 100; }
            row.module.setSelectedId (sel, juce::dontSendNotification);
            row.select.setToggleState (s == selected, juce::dontSendNotification);
            row.bypass.setEnabled (id.isNotEmpty()); row.solo.setEnabled (id.isNotEmpty());
        }
    }
    void rebuildPanel()
    {
        panel.reset();
        const auto params = proc.getSlotParams (selected);
        if (! params.empty()) { panel = std::make_unique<ModulePanel> (params, theme, mode); addAndMakeVisible (*panel); resized(); }
        moduleMeter = 0.0f; repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshRows(); rebuildPanel(); }
    void pullSuiteLook()
    {
        theme = vintage ? themes::vintage() : themes::byId (SuiteSettings::get().look());
        if (mode == Mode::Minimal) theme = themes::withMinimal (theme);
        lnf.setTheme (theme);
        if (panel) panel->setTheme (theme);
        for (auto& row : rows) row->bypass.setColour (juce::TextButton::buttonOnColourId, theme.warn);
        resized();
        repaint();
    }
    void timerCallback() override
    {
        SuiteSettings::get().reloadIfChangedExternally();
        const int rev = SuiteSettings::get().getRevision();
        if (rev != seenRevision)
        {
            seenRevision = rev;
            const auto m = SuiteSettings::get().mode();
            if (m != mode) { mode = m; modeSwitch.setIndex ((int) m); rebuildPanel(); }
            if (! vintage) lookPill.setLook (SuiteSettings::get().look());
            pullSuiteLook();
        }
        inBar.tick(); outBar.tick();
        tpHold = juce::jmax (tpHold * 0.995f, proc.truePeakMax.exchange (0.0f));
        if (auto* m = proc.getSlot (selected).getModule()) moduleMeter = juce::jmax (m->readModuleMeter(), moduleMeter - 0.5f);
        repaint();
    }

    static constexpr int headerH = 74;

    BasecampProcessor& proc;
    Theme theme;
    Mode mode;
    PsLookAndFeel lnf;
    LookPill lookPill;
    Segmented modeSwitch;
    Footer footer;
    ThemedMeter inBar, outBar;
    juce::Slider inSlider, outSlider;
    juce::SliderParameterAttachment inAtt, outAtt;
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<ModulePanel> panel;
    juce::Rectangle<int> readouts, listArea, panelArea, inLabel, outLabel, railLeft, railRight;
    int selected = 0; float moduleMeter = 0.0f, tpHold = 0.0f;
    bool vintage = false;
    int seenRevision = SuiteSettings::get().getRevision();
};

juce::AudioProcessorEditor* BasecampProcessor::createEditor() { return new BasecampEditor (*this); }
} // namespace ps
