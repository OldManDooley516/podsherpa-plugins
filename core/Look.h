#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HostParam.h"
#include "Meters.h"
#include "Theme.h"
#include "SuiteSettings.h"
#include "Chrome.h"
#include <map>

namespace ps
{
/** The suite look-and-feel, driven by a Theme that can change while a plugin is open.

    The first pass had one palette as a set of global consts, which cannot support three Looks and
    a hidden fourth. The palette now arrives as a Theme and the colours are re-pushed on change. */
class PsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit PsLookAndFeel (const Theme& t) { setTheme (t); }

    void setTheme (const Theme& t)
    {
        setColour (juce::ResizableWindow::backgroundColourId, t.bg);
        setColour (juce::Slider::trackColourId, t.accent);
        setColour (juce::Slider::thumbColourId, t.accent);
        setColour (juce::Slider::backgroundColourId, t.line);
        setColour (juce::Slider::textBoxTextColourId, t.text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, t.text);
        setColour (juce::ComboBox::backgroundColourId, t.panelHi);
        setColour (juce::ComboBox::outlineColourId, t.line);
        setColour (juce::ComboBox::textColourId, t.silverButtons ? t.bg : t.text);
        setColour (juce::ComboBox::arrowColourId, t.silverButtons ? t.bg : t.dim);
        setColour (juce::PopupMenu::backgroundColourId, t.panel);
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, t.bg);
        setColour (juce::TextButton::buttonColourId, t.panelHi);
        setColour (juce::TextButton::buttonOnColourId, t.accent);
        setColour (juce::TextButton::textColourOffId, t.silverButtons ? t.bg : t.text);
        setColour (juce::TextButton::textColourOnId, t.bg);
        setColour (juce::ToggleButton::textColourId, t.text);
        setColour (juce::ToggleButton::tickColourId, t.accent);
        setColour (juce::TooltipWindow::backgroundColourId, t.panel);
        setColour (juce::TooltipWindow::textColourId, t.text);
        setColour (juce::TooltipWindow::outlineColourId, t.line);
        ribbed = t.ribbedFaders;
        silver = t.silverButtons;
        edge   = t.accent;
        faceHi = t.panelHi;
        lineCol = t.line;
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return type::font (type::label); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return type::font (type::label); }
    juce::Font getPopupMenuFont() override { return type::font (type::label); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    /** Vintage draws its faders ribbed, with a silver cap and a lit edge, rather than JUCE's
        rounded thumb. The other Looks fall through to the stock drawing. */
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float min, float max,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (! ribbed)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, min, max, style, s);
            return;
        }
        const bool vertical = style == juce::Slider::LinearVertical;
        auto track = vertical ? juce::Rectangle<float> ((float) x + (float) w * 0.5f - 3.0f, (float) y, 6.0f, (float) h)
                              : juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
        g.setColour (lineCol);
        g.fillRoundedRectangle (track, 3.0f);

        auto cap = vertical ? juce::Rectangle<float> ((float) w - 8.0f, 20.0f).withCentre ({ (float) x + (float) w * 0.5f, pos })
                            : juce::Rectangle<float> (20.0f, (float) h - 8.0f).withCentre ({ pos, (float) y + (float) h * 0.5f });
        g.setColour (faceHi);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.4f);
        g.setColour (lineCol.withAlpha (0.8f));
        for (int i = 1; i <= 3; ++i)
        {
            const float f = (float) i / 4.0f;
            if (vertical) g.drawHorizontalLine ((int) (cap.getY() + cap.getHeight() * f), cap.getX() + 3.0f, cap.getRight() - 3.0f);
            else          g.drawVerticalLine   ((int) (cap.getX() + cap.getWidth()  * f), cap.getY() + 3.0f, cap.getBottom() - 3.0f);
        }
    }

private:
    bool ribbed = false, silver = false;
    juce::Colour edge, faceHi, lineCol;
};

//==============================================================================
/** Builds controls for a list of host parameters: slider, menu or switch per parameter.

    Used by the standalone plugins and by Basecamp's expanded view. The mode decides density:
    Beginner gives every control its plain-language line underneath, Normal shows the name alone
    with the line as a tooltip, Minimal tightens the rows and drops the hints. */
class ModulePanel : public juce::Component, private juce::Timer
{
public:
    ModulePanel (std::vector<HostParam*> params, const Theme& t, Mode m)
        : theme (t), mode (m)
    {
        for (auto* p : params)
        {
            auto row = std::make_unique<Row>();
            row->param = p; row->spec = p->getSpec();
            auto* r = row.get();

            r->label.setText (r->spec.name, juce::dontSendNotification);
            r->label.setFont (type::font (type::label, true));
            r->label.setTooltip (r->spec.hint);
            addAndMakeVisible (r->label);

            r->hint.setText (r->spec.hint, juce::dontSendNotification);
            r->hint.setFont (type::font (type::support));
            r->hint.setColour (juce::Label::textColourId, theme.dim);
            addChildComponent (r->hint);

            if (r->spec.type == ParamSpec::Type::Choice)
            {
                r->combo.addItemList (r->spec.choices, 1);
                r->combo.setTooltip (r->spec.hint);
                r->combo.onChange = [r] { r->set ((float) r->combo.getSelectedItemIndex()); };
                addAndMakeVisible (r->combo);
            }
            else if (r->spec.type == ParamSpec::Type::Bool)
            {
                r->toggle.setClickingTogglesState (true);
                r->toggle.setTooltip (r->spec.hint);
                r->toggle.onClick = [r] { r->set (r->toggle.getToggleState() ? 1.0f : 0.0f);
                                          r->toggle.setButtonText (r->toggle.getToggleState() ? "On" : "Off"); };
                addAndMakeVisible (r->toggle);
            }
            else
            {
                r->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                r->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 112, 32);
                r->slider.setRange (0.0, 1.0);
                r->slider.setTooltip (r->spec.hint);
                r->slider.textFromValueFunction = [r] (double v) { return r->spec.text (r->spec.fromNorm ((float) v)); };
                r->slider.valueFromTextFunction = [r] (const juce::String& t) { return (double) r->spec.toNorm (r->spec.parse (t)); };
                r->slider.setDoubleClickReturnValue (true, r->spec.toNorm (r->spec.def));
                r->slider.onDragStart = [r] { r->dragging = true; r->param->beginChangeGesture(); };
                r->slider.onDragEnd   = [r] { r->dragging = false; r->param->endChangeGesture(); };
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
        buildGroups();
        applyMode();
        refresh();
        startTimerHz (20);
    }

    void setTheme (const Theme&) { for (auto& r : rows) r->hint.setColour (juce::Label::textColourId, theme.dim); repaint(); }

    void setMode (Mode m) { mode = m; applyMode(); resized(); repaint(); }

    int rowHeight() const { return mode == Mode::Beginner ? 66 : mode == Mode::Minimal ? 40 : 48; }

    /** Parameters are named "low.threshold", "b2.freq", "hp.slope": the part before the dot is
        the band or section they belong to. A flat list ignores that, which is why Ridgeline
        arrived as 27 stacked sliders and Contour Dynamic as 25 - unreadable, and slow to work
        with on a real edit. Grouped parameters are laid out as columns of knobs, one column per
        section; ungrouped ones keep the full-width rows, which read better for a short list. */
    static juce::String groupOf (const juce::String& id)
    {
        const int dot = id.indexOfChar ('.');
        return dot > 0 ? id.substring (0, dot) : juce::String();
    }

    int knobCell() const { return mode == Mode::Minimal ? 78 : 92; }
    int toggleCell() const { return 44; }   // an On button needs a button's height, not a knob's
    int headerH() const { return 26; }

    /** Height of the band of columns. On/off switches go to the bottom of every column, on one
        shared line, so they read as a row (Contour's sat at three different heights because the
        high-pass and shelf have fewer knobs than the bells). */
    int columnsHeight() const
    {
        int knobs = 0, toggles = 0;
        for (auto& g : groups)
        {
            int k = 0, t = 0;
            for (auto* r : g->rows) (r->spec.type == ParamSpec::Type::Bool ? t : k)++;
            knobs = juce::jmax (knobs, k); toggles = juce::jmax (toggles, t);
        }
        return headerH() + knobs * knobCell() + toggles * toggleCell() + 14;
    }

    /** Under a band of columns, two or three short switch rows (Contour's Phase, Solo and
        Delta) share one line rather than taking one each, outside Beginner, where each needs
        its sentence underneath. */
    bool plainTwoUp() const
    {
        if (groups.empty() || mode == Mode::Beginner || plain.size() < 2 || plain.size() > 3 || (getWidth() > 0 && getWidth() < 700)) return false;
        for (auto* r : plain) if (r->spec.type != ParamSpec::Type::Choice && r->spec.type != ParamSpec::Type::Bool) return false;
        return true;
    }

    int idealHeight() const
    {
        const int plainRows = plainTwoUp() ? 1 : (int) plain.size();
        int h = plainRows * rowHeight() + 8;
        if (! groups.empty()) h += columnsHeight();
        return h;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int rh = rowHeight();
        // The name column and the value box are fixed widths, which is fine across the whole
        // window and ruinous beside a curve card: at half width the slider itself was left with
        // about 60 px. Both shrink when there is not room, so the slider keeps the remainder.
        const bool tight = r.getWidth() < 640;
        const int nameW = tight ? 118 : (mode == Mode::Minimal ? 180 : 210);
        const int boxW = tight ? 84 : 112;
        for (auto& row : plain)
            if (row->spec.type != ParamSpec::Type::Choice && row->spec.type != ParamSpec::Type::Bool
                && row->slider.getSliderStyle() == juce::Slider::LinearHorizontal)
                row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, boxW, tight ? 28 : 32);

        // Grouped sections first, as columns across the top.
        if (! groups.empty())
        {
            auto band = r.removeFromTop (columnsHeight());
            const int colW = band.getWidth() / (int) groups.size();
            for (size_t gi = 0; gi < groups.size(); ++gi)
            {
                auto col = band.removeFromLeft (gi + 1 == groups.size() ? band.getWidth() : colW).reduced (4, 0);
                groups[gi]->header.setBounds (col.removeFromTop (headerH()));
                col.removeFromBottom (14);
                // Switches from the bottom up, in reverse so the first stays on top.
                for (auto it = groups[gi]->rows.rbegin(); it != groups[gi]->rows.rend(); ++it)
                    if ((*it)->spec.type == ParamSpec::Type::Bool)
                    {
                        auto cell = col.removeFromBottom (toggleCell());
                        (*it)->label.setBounds ({});
                        (*it)->toggle.setBounds (cell.withSizeKeepingCentre (juce::jmin (120, cell.getWidth() - 20), 32));
                    }
                for (auto* row : groups[gi]->rows)
                {
                    if (row->spec.type == ParamSpec::Type::Bool) continue;
                    auto cell = col.removeFromTop (knobCell());
                    if (row->spec.type == ParamSpec::Type::Choice)
                    {
                        row->label.setBounds (cell.removeFromTop (18));
                        row->combo.setBounds (cell.reduced (6, 6).withHeight (30));
                    }
                    else
                    {
                        row->label.setBounds (cell.removeFromTop (18));
                        row->slider.setBounds (cell.reduced (2, 0));
                    }
                }
            }
            r.removeFromTop (6);
        }

        const bool twoUp = plainTwoUp();
        juce::Rectangle<int> pairLine;
        for (size_t pi = 0; pi < plain.size(); ++pi)
        {
            auto* row = plain[pi];
            juce::Rectangle<int> line;
            if (twoUp)
            {
                const int n = (int) plain.size();
                if (pi == 0) pairLine = r.removeFromTop (rh).reduced (0, mode == Mode::Minimal ? 4 : 6);
                const int share = (pairLine.getWidth() + 24) / (n - (int) pi);
                line = (pi + 1 == plain.size() ? pairLine : pairLine.removeFromLeft (share - 24));
                if (pi + 1 < plain.size()) pairLine.removeFromLeft (24);
                const int nw = juce::jmin (row->spec.type == ParamSpec::Type::Bool ? 70 : 90, line.getWidth() / 3);
                row->label.setBounds (line.removeFromLeft (nw));
                if (row->spec.type == ParamSpec::Type::Bool)
                    row->toggle.setBounds (line.removeFromLeft (96).withSizeKeepingCentre (96, 32));
                else
                {
                    const int cw = juce::jmin (260, line.getWidth());
                    row->combo.setBounds (line.removeFromLeft (cw).withSizeKeepingCentre (cw, 34));
                }
                continue;
            }
            line = r.removeFromTop (rh).reduced (0, mode == Mode::Minimal ? 4 : 6);
            auto names = line.removeFromLeft (nameW);
            if (mode == Mode::Beginner && row->spec.hint.isNotEmpty())
            {
                row->label.setBounds (names.removeFromTop (names.getHeight() / 2));
                row->hint.setBounds (line.removeFromBottom (line.getHeight() / 2 - 2));
            }
            else
            {
                row->label.setBounds (names);
            }
            if (row->spec.type == ParamSpec::Type::Choice)    row->combo.setBounds (line.removeFromLeft (230).withSizeKeepingCentre (230, 34));
            else if (row->spec.type == ParamSpec::Type::Bool) row->toggle.setBounds (line.removeFromLeft (96).withSizeKeepingCentre (96, 32));
            else                                              row->slider.setBounds (line);
        }
    }

private:
    struct Row
    {
        HostParam* param = nullptr; ParamSpec spec; bool dragging = false;
        juce::Label label, hint; juce::Slider slider; juce::ComboBox combo; juce::TextButton toggle;
        void set (float real) { param->beginChangeGesture(); param->setValueNotifyingHost (spec.toNorm (real)); param->endChangeGesture(); }
    };

    void applyMode()
    {
        for (auto& r : rows)
            r->hint.setVisible (mode == Mode::Beginner && r->spec.hint.isNotEmpty());
    }

    void refresh()
    {
        for (auto& row : rows)
        {
            const float n = row->param->getNorm(), real = row->spec.fromNorm (n);
            if (row->spec.type == ParamSpec::Type::Choice)
                row->combo.setSelectedItemIndex ((int) real, juce::dontSendNotification);
            else if (row->spec.type == ParamSpec::Type::Bool)
            {
                row->toggle.setToggleState (real >= 0.5f, juce::dontSendNotification);
                row->toggle.setButtonText (real >= 0.5f ? "On" : "Off");
            }
            else if (! row->dragging)
                row->slider.setValue (n, juce::dontSendNotification);
        }
    }

    void timerCallback() override { refresh(); }

    const Theme& theme;
    Mode mode;
    struct Group { juce::String id; juce::Label header; std::vector<Row*> rows; };

    /** Splits the rows into sections by the part of the id before the dot, and takes each
        section's heading from the words its parameter names share. "Low threshold", "Low ratio"
        and "Low attack" become a "Low" heading over "threshold", "ratio" and "attack", so the
        band name is written once instead of five times. */
    void buildGroups()
    {
        std::vector<juce::String> order;
        std::map<juce::String, std::vector<Row*>> byId;
        for (auto& r : rows)
        {
            const auto g = groupOf (r->spec.id);
            if (g.isEmpty()) { plain.push_back (r.get()); continue; }
            if (byId.find (g) == byId.end()) order.push_back (g);
            byId[g].push_back (r.get());
        }
        // A section of one is not a section; it reads better as an ordinary row.
        for (auto& g : order)
        {
            auto& v = byId[g];
            if (v.size() < 2) { for (auto* r : v) plain.push_back (r); continue; }
            groups.push_back (std::make_unique<Group>());
            auto& grp = *groups.back();
            grp.id = g;
            grp.rows = v;
        }
        for (auto& grpPtr : groups)
        {
            auto& grp = *grpPtr;
            // Longest leading run of whole words common to every name in the section.
            juce::StringArray first;
            first.addTokens (grp.rows[0]->spec.name, " ", "");
            int shared = first.size();
            for (auto* r : grp.rows)
            {
                juce::StringArray w;
                w.addTokens (r->spec.name, " ", "");
                int k = 0;
                while (k < shared && k < w.size() && w[k].equalsIgnoreCase (first[k])) ++k;
                shared = juce::jmin (shared, k);
            }
            juce::String head;
            for (int i = 0; i < shared; ++i) head += (i ? " " : "") + first[i];
            if (head.isEmpty()) head = grp.id;
            grp.header.setText (head.substring (0, 1).toUpperCase() + head.substring (1), juce::dontSendNotification);
            grp.header.setFont (type::font (type::label, true));
            grp.header.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (grp.header);

            for (auto* r : grp.rows)
            {
                // The label is the part of the id after the dot - "low.threshold" gives
                // "Threshold". That is always the control's role, where diffing the display
                // names is not: "Band 1" and "Band 1 Q" share every word of the shorter one,
                // which left the frequency knob labelled "1".
                auto rest = r->spec.id.fromFirstOccurrenceOf (".", false, false);
                if (rest.isEmpty()) rest = r->spec.name;
                if (rest.equalsIgnoreCase ("q")) rest = "Q";
                else if (rest.equalsIgnoreCase ("freq")) rest = "Frequency";
                else if (rest.equalsIgnoreCase ("on")) rest = "";
                r->label.setText (rest.substring (0, 1).toUpperCase() + rest.substring (1), juce::dontSendNotification);
                r->label.setFont (type::font (type::support));
                r->label.setJustificationType (juce::Justification::centred);
                if (r->spec.type != ParamSpec::Type::Choice && r->spec.type != ParamSpec::Type::Bool)
                {
                    // Knobs in a column read far faster than a stack of wide sliders, and fit
                    // four bands across the default window.
                    r->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    r->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 20);
                }
                if (r->spec.type == ParamSpec::Type::Bool) r->toggle.setButtonText (r->spec.name);
            }
        }
    }

    std::vector<std::unique_ptr<Row>> rows;
    std::vector<Row*> plain;
    std::vector<std::unique_ptr<Group>> groups;
};
} // namespace ps
