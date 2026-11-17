#pragma once
#include "Theme.h"
#include "SuiteSettings.h"
#include "Meters.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ps
{
/** The suite's shared furniture: the header row, the input and output rails, and the footer.

    Everything here takes the Theme by reference and reads it at paint time, so a Look change is
    a repaint rather than a rebuild. Each piece is its own component so that Basecamp's rack can
    reuse the rails and footer without the single-plugin header. */

//==============================================================================
/** Text colours for a control face. Vintage fills its controls with silver, so the light text
    the other Looks use disappears on it: both the preset name and the unselected mode labels
    came out nearly invisible in the first Vintage render. These pick the readable pair. */
inline juce::Colour faceText (const Theme& t)     { return t.silverButtons ? t.bg : t.text; }
inline juce::Colour faceTextDim (const Theme& t)  { return t.silverButtons ? t.bg.withAlpha (0.55f) : t.dim; }

/** A segmented switch, used for the Look pill and the mode switch.

    Colour is never the only signal in this suite, so each segment carries its word as well as
    its fill, and the selected one is also raised. That is what lets the Look pill work in
    Graphite, where the three accents are close in value. */
class Segmented : public juce::Component
{
public:
    std::function<void (int)> onChange;

    Segmented (const Theme& t, juce::StringArray items) : theme (t), labels (std::move (items)) {}

    void setIndex (int i, bool notify = false)
    {
        i = juce::jlimit (0, labels.size() - 1, i);
        if (i == index) return;
        index = i;
        repaint();
        if (notify && onChange) onChange (index);
    }
    int getIndex() const { return index; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.panelHi.withAlpha (theme.silverButtons ? 1.0f : 0.55f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (theme.line);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

        const float w = r.getWidth() / (float) labels.size();
        for (int i = 0; i < labels.size(); ++i)
        {
            auto seg = r.withX (r.getX() + w * (float) i).withWidth (w).reduced (3.0f, 3.0f);
            const bool on = i == index;
            if (on)
            {
                g.setColour (theme.silverButtons ? theme.panelHi.brighter (0.25f) : theme.accent);
                g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
                if (theme.silverButtons)   // Vintage: lit amber edge rather than a filled pill
                {
                    g.setColour (theme.accent);
                    g.drawRoundedRectangle (seg.reduced (0.5f), seg.getHeight() * 0.5f, 1.6f);
                }
            }
            g.setColour (on ? theme.bg : faceTextDim (theme));
            g.setFont (type::font (type::support, on));
            g.drawText (labels[i], seg.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = (int) ((float) e.x / ((float) getWidth() / (float) labels.size()));
        setIndex (i, true);
    }

    int idealWidth() const
    {
        int w = 0;
        for (const auto& s : labels)
            w += juce::GlyphArrangement::getStringWidthInt (type::font (type::support, true), s) + 26;
        return w + 6;
    }

private:
    const Theme& theme;
    juce::StringArray labels;
    int index = 0;
};

//==============================================================================
/** The Look pill: a compact dropdown rather than a segmented control.

    Three Looks spelled out in full ate about 230 px of the header, which pushed the centred
    preset bar into them at the default width. A pill carries the current Look's name with its
    accent as a swatch beside it, and opens the list on click, which is a third of the width.
    The swatch never stands alone - the name is always next to it. */
class LookPill : public juce::Component
{
public:
    std::function<void (Theme::Id)> onChange;

    explicit LookPill (const Theme& t) : theme (t) {}

    void setLook (Theme::Id id) { current = id; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.panelHi.withAlpha (theme.silverButtons ? 1.0f : 0.55f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (theme.line);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

        auto body = getLocalBounds().reduced (12, 0);
        auto dot = body.removeFromLeft (12).withSizeKeepingCentre (10, 10);
        g.setColour (themes::byId (current).accent);
        g.fillEllipse (dot.toFloat());
        body.removeFromLeft (8);

        auto chev = body.removeFromRight (14);
        g.setColour (faceTextDim (theme));
        juce::Path p;
        p.startNewSubPath ((float) chev.getX(), (float) chev.getCentreY() - 2.0f);
        p.lineTo ((float) chev.getCentreX(), (float) chev.getCentreY() + 3.0f);
        p.lineTo ((float) chev.getRight(), (float) chev.getCentreY() - 2.0f);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (faceText (theme));
        g.setFont (type::font (type::support, true));
        g.drawText (name (current), body, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        for (int i = 0; i < 3; ++i)
            m.addItem (i + 1, name ((Theme::Id) i), true, (Theme::Id) i == current);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this] (int r) { if (r > 0 && onChange) onChange ((Theme::Id) (r - 1)); });
    }

    int idealWidth() const
    {
        int w = 0;
        for (int i = 0; i < 3; ++i)
            w = juce::jmax (w, juce::GlyphArrangement::getStringWidthInt (type::font (type::support, true), name ((Theme::Id) i)));
        return w + 64;
    }

private:
    static juce::String name (Theme::Id id)
    {
        return id == Theme::Id::Vaporwave ? "Vaporwave" : id == Theme::Id::Graphite ? "Graphite"
             : id == Theme::Id::Vintage ? "Vintage" : "Default";
    }
    const Theme& theme;
    Theme::Id current = Theme::Id::Default_;
};

//==============================================================================
/** The preset bar: previous, the name, next, and a menu on the name.

    Factory presets reach the host as programs, so this drives the processor's program interface
    rather than keeping a list of its own. That is deliberate: REAPER shows the same names in its
    own FX preset menu, and anything the user saves there sits beside them. */
class PresetBar : public juce::Component
{
public:
    PresetBar (const Theme& t, juce::AudioProcessor& p) : theme (t), proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.panelHi.withAlpha (theme.silverButtons ? 1.0f : 0.55f));
        g.fillRoundedRectangle (r, theme.corner);
        g.setColour (theme.line);
        g.drawRoundedRectangle (r.reduced (0.5f), theme.corner, 1.0f);

        auto body = getLocalBounds();
        auto l = body.removeFromLeft (arrowW), rr = body.removeFromRight (arrowW);
        // Drawn rather than typed: the angle brackets in Manrope sit high and read as
        // punctuation beside the preset name, not as controls.
        auto arrow = [&g] (juce::Rectangle<int> box, int dir, juce::Colour c)
        {
            auto b = box.toFloat().withSizeKeepingCentre (9.0f, 14.0f);
            juce::Path p;
            p.startNewSubPath (dir > 0 ? b.getX() : b.getRight(), b.getY());
            p.lineTo (dir > 0 ? b.getRight() : b.getX(), b.getCentreY());
            p.lineTo (dir > 0 ? b.getX() : b.getRight(), b.getBottom());
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arrow (l,  -1, canStep (-1) ? faceTextDim (theme) : theme.line);
        arrow (rr,  1, canStep (1)  ? faceTextDim (theme) : theme.line);

        g.setColour (faceText (theme));
        g.setFont (type::font (type::label));
        g.drawText (proc.getProgramName (proc.getCurrentProgram()), body, juce::Justification::centred, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < arrowW)                 { step (-1); return; }
        if (e.x > getWidth() - arrowW)    { step (1);  return; }
        showMenu();
    }

    int idealWidth() const { return 300; }

private:
    bool canStep (int d) const
    {
        const int n = proc.getNumPrograms(), i = proc.getCurrentProgram() + d;
        return n > 1 && i >= 0 && i < n;
    }
    void step (int d)
    {
        if (! canStep (d)) return;
        proc.setCurrentProgram (proc.getCurrentProgram() + d);
        repaint();
    }
    void showMenu()
    {
        juce::PopupMenu m;
        const int n = proc.getNumPrograms(), cur = proc.getCurrentProgram();
        for (int i = 0; i < n; ++i)
            m.addItem (i + 1, proc.getProgramName (i), true, i == cur);
        m.addSeparator();
        m.addItem (10000, "Save and load your own in the host's preset menu", false, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this] (int r) { if (r > 0 && r < 10000) { proc.setCurrentProgram (r - 1); repaint(); } });
    }

    static constexpr int arrowW = 34;
    const Theme& theme;
    juce::AudioProcessor& proc;
};

//==============================================================================
/** The power button. One per plugin, top right, no outer glow and no "On" text. */
class PowerButton : public juce::Component
{
public:
    std::function<void (bool)> onToggle;

    PowerButton (const Theme& t) : theme (t) {}

    void setBypassed (bool b) { bypassed = b; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto d = juce::jmin (r.getWidth(), r.getHeight());
        auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());

        g.setColour (theme.silverButtons ? theme.panelHi : theme.panelHi.withAlpha (0.7f));
        g.fillEllipse (c);
        g.setColour (bypassed ? theme.line : theme.accent);
        g.drawEllipse (c.reduced (1.0f), 1.8f);

        // The standard glyph: a ring broken at the top with a stem through the gap.
        auto ring = c.reduced (d * 0.3f);
        juce::Path p;
        p.addCentredArc (ring.getCentreX(), ring.getCentreY(), ring.getWidth() * 0.5f, ring.getHeight() * 0.5f,
                         0.0f, juce::degreesToRadians (28.0f), juce::degreesToRadians (332.0f), true);
        p.startNewSubPath (ring.getCentreX(), ring.getY() - ring.getHeight() * 0.28f);
        p.lineTo (ring.getCentreX(), ring.getCentreY() - ring.getHeight() * 0.1f);
        g.setColour (bypassed ? theme.dim : theme.accent);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        bypassed = ! bypassed;
        repaint();
        if (onToggle) onToggle (bypassed);
    }

private:
    const Theme& theme;
    bool bypassed = false;
};

//==============================================================================
/** Vertical level meter with a latching clip light. The light stays on until it is clicked,
    which is the point: a clip that happened while you were looking elsewhere is the one worth
    knowing about. Amber and red both stay bright in every Look. */
class ThemedMeter : public juce::Component
{
public:
    ThemedMeter (const Theme& t, LevelMeter& m) : theme (t), meter (m) {}

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
        const bool clipped = meter.isClipped();
        g.setColour (clipped ? theme.bad : theme.line);
        g.fillRoundedRectangle (lamp.toFloat(), 3.0f);
        if (clipped)
        {
            g.setColour (theme.bg);
            g.setFont (type::font (10.0f, true));
            g.drawText ("CLIP", lamp, juce::Justification::centred);
        }
        r.removeFromTop (4);
        g.setColour (theme.panelHi);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (shown + 60.0f) / 60.0f);
        auto fill = r.withTop (r.getBottom() - (int) (frac * (float) r.getHeight()));
        g.setColour (shown > -1.0f ? theme.warn : theme.accent);
        g.fillRoundedRectangle (fill.toFloat(), 3.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { meter.clearClip(); repaint(); }

private:
    const Theme& theme;
    LevelMeter& meter;
    float shown = -120.0f;
};

//==============================================================================
/** The footer. Carries the only letter-spaced text in the suite, and hides Vintage behind a
    triple-click on it. In Vintage an unlabeled silver colour-wheel button appears bottom right
    and cycles the accent; it is deliberately unmarked. */
class Footer : public juce::Component
{
public:
    std::function<void()> onVintageToggle;
    std::function<void()> onAccentWheel;

    Footer (const Theme& t) : theme (t) {}

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme.dim);
        g.setFont (type::font (type::footer).withExtraKerningFactor (0.25f));
        g.drawText ("SOUTHERN DIGITAL", getLocalBounds(), juce::Justification::centred);

        if (theme.id == Theme::Id::Vintage)
        {
            auto w = wheelBounds().toFloat();
            g.setColour (theme.panelHi);
            g.fillEllipse (w);
            g.setColour (theme.accent);
            g.drawEllipse (w.reduced (0.5f), 1.2f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (theme.id == Theme::Id::Vintage && wheelBounds().contains (e.getPosition()))
        {
            if (onAccentWheel) onAccentWheel();
            return;
        }
        // Triple-click on the wordmark toggles Vintage. JUCE reports the count, so there is no
        // timer to keep: a third click inside the system double-click interval counts.
        if (e.getNumberOfClicks() >= 3 && onVintageToggle) onVintageToggle();
    }

private:
    juce::Rectangle<int> wheelBounds() const
    {
        return juce::Rectangle<int> (16, 16).withCentre ({ getWidth() - 20, getHeight() / 2 });
    }
    const Theme& theme;
};
} // namespace ps
