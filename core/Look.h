#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostParam.h"
#include "Meters.h"

namespace ps
{
/** Default look (navy / mint). Plain first pass: the full Look system, modes, pre-rendered
    type and header layout A come after the plumbing is proven in a DAW. */
namespace look
{
    const juce::Colour bg { 0xff0e1a30 }, panel { 0xff172742 }, panelHi { 0xff20355a }, line { 0xff2c4470 },
                       text { 0xffeaf0fb }, dim { 0xffa9b8d4 }, mint { 0xff5eead4 }, blue { 0xff60a5fa },
                       amber { 0xfffbbf24 }, red { 0xfff87171 };
    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
    inline void footer (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (dim);
        auto f = font (13.0f).withExtraKerningFactor (0.25f);
        g.setFont (f);
        g.drawText ("SOUTHERN DIGITAL", r, juce::Justification::centred);
    }
}

class PsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PsLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, look::bg);
        setColour (juce::Slider::trackColourId, look::mint);
        setColour (juce::Slider::thumbColourId, look::mint);
        setColour (juce::Slider::backgroundColourId, look::line);
        setColour (juce::Slider::textBoxTextColourId, look::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, look::text);
        setColour (juce::ComboBox::backgroundColourId, look::panelHi);
        setColour (juce::ComboBox::outlineColourId, look::line);
        setColour (juce::ComboBox::textColourId, look::text);
        setColour (juce::PopupMenu::backgroundColourId, look::panel);
        setColour (juce::PopupMenu::textColourId, look::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look::mint);
        setColour (juce::PopupMenu::highlightedTextColourId, look::bg);
        setColour (juce::TextButton::buttonColourId, look::panelHi);
        setColour (juce::TextButton::buttonOnColourId, look::mint);
        setColour (juce::TextButton::textColourOffId, look::text);
        setColour (juce::TextButton::textColourOnId, look::bg);
        setColour (juce::ToggleButton::textColourId, look::text);
        setColour (juce::ToggleButton::tickColourId, look::mint);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return look::font (16.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return look::font (16.0f); }
    juce::Font getPopupMenuFont() override { return look::font (16.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
};

/** Vertical level meter with a latching clip light. Click the light to clear it. */
class MeterBar : public juce::Component
{
public:
    explicit MeterBar (LevelMeter& m) : meter (m) {}
    void tick()
    {
        const float db = gainToDb (meter.readPeak());
        shown = juce::jmax (db, shown - 1.5f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto lamp = r.removeFromTop (16).reduced (1);
        g.setColour (meter.isClipped() ? look::red : look::line);
        g.fillRoundedRectangle (lamp.toFloat(), 3.0f);
        r.removeFromTop (4);
        g.setColour (look::panelHi); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (shown + 60.0f) / 60.0f);
        auto fill = r.withTop (r.getBottom() - (int) (frac * (float) r.getHeight()));
        g.setColour (shown > -1.0f ? look::amber : look::mint); g.fillRoundedRectangle (fill.toFloat(), 3.0f);
    }
    void mouseDown (const juce::MouseEvent&) override { meter.clearClip(); repaint(); }
private:
    LevelMeter& meter; float shown = -120.0f;
};

/** Builds controls for a list of host parameters: slider, menu or switch per parameter.
    Used by the standalone plugins and by Basecamp's expanded view. */
class ModulePanel : public juce::Component, private juce::Timer
{
public:
    explicit ModulePanel (std::vector<HostParam*> params)
    {
        for (auto* p : params)
        {
            auto row = std::make_unique<Row>(); row->param = p; row->spec = p->getSpec();
            row->label.setText (row->spec.name, juce::dontSendNotification);
            row->label.setFont (look::font (17.0f));
            addAndMakeVisible (row->label);
            auto* r = row.get();
            if (r->spec.type == ParamSpec::Type::Choice)
            {
                r->combo.addItemList (r->spec.choices, 1);
                r->combo.onChange = [r] { r->set ((float) r->combo.getSelectedItemIndex()); };
                addAndMakeVisible (r->combo);
            }
            else if (r->spec.type == ParamSpec::Type::Bool)
            {
                r->toggle.setClickingTogglesState (true);
                r->toggle.onClick = [r] { r->set (r->toggle.getToggleState() ? 1.0f : 0.0f); r->toggle.setButtonText (r->toggle.getToggleState() ? "On" : "Off"); };
                addAndMakeVisible (r->toggle);
            }
            else
            {
                r->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                r->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 104, 30);
                r->slider.setRange (0.0, 1.0);
                r->slider.textFromValueFunction = [r] (double v) { return r->spec.text (r->spec.fromNorm ((float) v)); };
                r->slider.valueFromTextFunction = [r] (const juce::String& t) { return (double) r->spec.toNorm (r->spec.parse (t)); };
                r->slider.setDoubleClickReturnValue (true, r->spec.toNorm (r->spec.def));
                r->slider.onDragStart = [r] { r->dragging = true; r->param->beginChangeGesture(); };
                r->slider.onDragEnd = [r] { r->dragging = false; r->param->endChangeGesture(); };
                r->slider.onValueChange = [r]
                {
                    if (r->dragging) r->param->setValueNotifyingHost ((float) r->slider.getValue());
                    else { r->param->beginChangeGesture(); r->param->setValueNotifyingHost ((float) r->slider.getValue()); r->param->endChangeGesture(); }
                };
                r->slider.setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
                addAndMakeVisible (r->slider);
            }
            rows.push_back (std::move (row));
        }
        refresh();
        startTimerHz (20);
    }
    int idealHeight() const { return (int) rows.size() * rowH + 8; }
    void resized() override
    {
        auto r = getLocalBounds();
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (rowH).reduced (0, 6);
            row->label.setBounds (line.removeFromLeft (124));
            if (row->spec.type == ParamSpec::Type::Choice)    row->combo.setBounds (line.removeFromLeft (220));
            else if (row->spec.type == ParamSpec::Type::Bool) row->toggle.setBounds (line.removeFromLeft (90));
            else row->slider.setBounds (line);
        }
    }
private:
    static constexpr int rowH = 48;
    struct Row
    {
        HostParam* param = nullptr; ParamSpec spec; bool dragging = false;
        juce::Label label; juce::Slider slider; juce::ComboBox combo; juce::TextButton toggle;
        void set (float real) { param->beginChangeGesture(); param->setValueNotifyingHost (spec.toNorm (real)); param->endChangeGesture(); }
    };
    void refresh()
    {
        for (auto& row : rows)
        {
            const float n = row->param->getNorm(), real = row->spec.fromNorm (n);
            if (row->spec.type == ParamSpec::Type::Choice) row->combo.setSelectedItemIndex ((int) real, juce::dontSendNotification);
            else if (row->spec.type == ParamSpec::Type::Bool) { row->toggle.setToggleState (real >= 0.5f, juce::dontSendNotification); row->toggle.setButtonText (real >= 0.5f ? "On" : "Off"); }
            else if (! row->dragging) row->slider.setValue (n, juce::dontSendNotification);
        }
    }
    void timerCallback() override { refresh(); }
    std::vector<std::unique_ptr<Row>> rows;
};
} // namespace ps
