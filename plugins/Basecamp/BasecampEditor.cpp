#include "BasecampProcessor.h"
#include "../../core/Look.h"
#include "../../core/SinglePlugin.h"

namespace ps
{
/** Plain first-pass Basecamp editor: slot list on the left, the selected module's controls on
    the right, In/Out rails, and the master readouts in the header. */
class BasecampEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit BasecampEditor (BasecampProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p), inBar (p.inMeter), outBar (p.outMeter),
          inAtt (*p.inGain, inSlider), outAtt (*p.outGain, outSlider)
    {
        setLookAndFeel (&lnf);
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
            row->bypass.setColour (juce::TextButton::buttonOnColourId, look::amber);
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
        g.fillAll (look::bg);
        auto r = getLocalBounds();
        auto head = r.removeFromTop (70).reduced (24, 0);
        g.setColour (look::text); g.setFont (look::font (28.0f, true));
        g.drawText ("PodSherpa Basecamp", head, juce::Justification::centredLeft);
        g.setColour (look::dim); g.setFont (look::font (20.0f, true));
        g.drawText (juce::String ("Master rack   v") + suiteVersion(), head.withTrimmedLeft (300), juce::Justification::centredLeft);

        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const float lufs = proc.loudness.get();
        g.setColour (look::mint); g.setFont (look::font (18.0f, true));
        g.drawText ("Latency " + juce::String (proc.getTotalLatency() * 1000.0 / sr, 1) + " ms      "
                    + "Short-term " + (lufs < -100.0f ? juce::String ("-") : juce::String (lufs, 1)) + " LUFS      "
                    + "True peak " + (tpHold < 1.0e-5f ? juce::String ("-") : juce::String (gainToDb (tpHold), 1)) + " dBTP",
                    readouts, juce::Justification::centredRight);

        look::footer (g, r.removeFromBottom (34));
        g.setColour (look::panel);
        g.fillRoundedRectangle (listArea.toFloat(), 10.0f); g.fillRoundedRectangle (panelArea.toFloat(), 10.0f);
        g.setColour (look::dim); g.setFont (look::font (15.0f));
        g.drawText ("In", inLabel, juce::Justification::centred); g.drawText ("Out", outLabel, juce::Justification::centred);

        auto pa = panelArea.reduced (24, 14);
        g.setColour (look::text); g.setFont (look::font (24.0f, true));
        const auto name = proc.getSlotName (selected);
        g.drawText ("SLOT " + juce::String (selected + 1) + (name.isNotEmpty() ? "   " + name : juce::String ("   Empty")), pa.removeFromTop (38), juce::Justification::centredLeft);
        if (name.isEmpty())
        {
            g.setColour (look::dim); g.setFont (look::font (17.0f));
            g.drawText ("Pick a module for this slot from the list on the left.", pa.removeFromTop (40), juce::Justification::centredLeft);
        }
        else if (proc.isSlotPlaceholder (selected))
        {
            g.setColour (look::amber); g.setFont (look::font (17.0f));
            g.drawFittedText ("This module is not in this build. Audio passes through unchanged and its settings are kept in the project.", pa.removeFromTop (60), juce::Justification::centredLeft, 2);
        }
        else if (auto* m = proc.getSlot (selected).getModule(); m != nullptr && m->getModuleMeterLabel().isNotEmpty())
        {
            g.setColour (look::blue); g.setFont (look::font (22.0f, true));
            g.drawText (m->getModuleMeterLabel() + ": " + juce::String (moduleMeter, 1) + " dB", pa.removeFromBottom (34), juce::Justification::centredLeft);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        readouts = r.removeFromTop (70).reduced (24, 0);
        r.removeFromBottom (34);
        auto left = r.removeFromLeft (120).reduced (12, 8), right = r.removeFromRight (120).reduced (12, 8);
        inLabel = left.removeFromTop (22); outLabel = right.removeFromTop (22);
        inBar.setBounds (left.removeFromLeft (26)); inSlider.setBounds (left);
        outBar.setBounds (right.removeFromRight (26)); outSlider.setBounds (right);
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
        if (! params.empty()) { panel = std::make_unique<ModulePanel> (params); addAndMakeVisible (*panel); resized(); }
        moduleMeter = 0.0f; repaint();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshRows(); rebuildPanel(); }
    void timerCallback() override
    {
        inBar.tick(); outBar.tick();
        tpHold = juce::jmax (tpHold * 0.995f, proc.truePeakMax.exchange (0.0f));
        if (auto* m = proc.getSlot (selected).getModule()) moduleMeter = juce::jmax (m->readModuleMeter(), moduleMeter - 0.5f);
        repaint();
    }

    BasecampProcessor& proc;
    PsLookAndFeel lnf;
    MeterBar inBar, outBar;
    juce::Slider inSlider, outSlider;
    juce::SliderParameterAttachment inAtt, outAtt;
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<ModulePanel> panel;
    juce::Rectangle<int> readouts, listArea, panelArea, inLabel, outLabel;
    int selected = 0; float moduleMeter = 0.0f, tpHold = 0.0f;
};

juce::AudioProcessorEditor* BasecampProcessor::createEditor() { return new BasecampEditor (*this); }
} // namespace ps
