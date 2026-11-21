#pragma once
#include "Theme.h"
#include "SuiteSettings.h"
#include "Meters.h"
#include "Module.h"
#include "Spectrum.h"
#include "HostParam.h"
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
class Segmented : public juce::Component, public juce::SettableTooltipClient
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
/** Horizontal gain-reduction meter with a peak hold.

    Every module that reduces gain already measured it and the editor only printed the number.
    A number updating 30 times a second cannot be read, which is why Sean could not tell whether
    a de-esser was leaning on one syllable or flattening every word. The bar can be watched; the
    number is kept beside it, so the reading never depends on seeing the bar alone.

    Reduction grows leftward from the right edge, which is the convention everywhere else, so a
    busy plugin reads as a bar pushing back against the signal. Altitude rides gain both ways,
    so a negative value (a boost) grows rightward from zero in the second accent instead. */
class ReductionMeter : public juce::Component
{
public:
    ReductionMeter (const Theme& t, juce::String labelText) : theme (t), label (std::move (labelText)) {}

    void setRange (float maxDb) { fullScale = juce::jmax (1.0f, maxDb); }

    /** Called from the editor's timer with the module's latest reading, in dB. */
    void push (float db)
    {
        shown = db > shown ? db : shown + (db - shown) * 0.25f;     // snap up, ease down
        if (std::abs (shown) >= std::abs (hold) || ++holdAge > 45) { hold = shown; holdAge = 0; }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto text = r.removeFromLeft (juce::jmin (150, r.getWidth() / 4));
        auto num = r.removeFromRight (74);
        auto bar = r.reduced (8, 7);

        g.setColour (theme.dim);
        g.setFont (type::font (type::support));
        g.drawText (label, text, juce::Justification::centredLeft);

        g.setColour (theme.panelHi.withAlpha (0.65f));
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);

        // Tick every 6 dB, so the depth can be judged without reading the number.
        g.setColour (theme.line);
        for (float d = 6.0f; d < fullScale; d += 6.0f)
        {
            const int x = bar.getRight() - (int) (bar.getWidth() * (d / fullScale));
            g.drawVerticalLine (x, (float) bar.getY() + 2.0f, (float) bar.getBottom() - 2.0f);
        }

        const float f = juce::jlimit (0.0f, 1.0f, std::abs (shown) / fullScale);
        if (f > 0.001f)
        {
            const bool boost = shown < 0.0f;
            auto fill = boost ? bar.withWidth ((int) (bar.getWidth() * f))
                              : bar.withLeft (bar.getRight() - (int) (bar.getWidth() * f));
            g.setColour (boost ? theme.accent : theme.accent2);
            g.fillRoundedRectangle (fill.toFloat(), 3.0f);
        }
        const float hf = juce::jlimit (0.0f, 1.0f, std::abs (hold) / fullScale);
        if (hf > 0.001f)
        {
            const int x = hold < 0.0f ? bar.getX() + (int) (bar.getWidth() * hf)
                                      : bar.getRight() - (int) (bar.getWidth() * hf);
            g.setColour (theme.text.withAlpha (0.8f));
            g.drawVerticalLine (x, (float) bar.getY(), (float) bar.getBottom());
        }

        g.setColour (theme.accent2);
        g.setFont (type::font (type::value, true));
        g.drawText (juce::String (shown, 1) + " dB", num, juce::Justification::centredRight);
    }

private:
    const Theme& theme;
    juce::String label;
    float fullScale = 18.0f, shown = 0.0f, hold = 0.0f;
    int holdAge = 0;
};

//==============================================================================
/** The compression transfer curve, as the JSFX versions have drawn it since 0.4.

    Input level across, output level down, both -60 to 0 dBFS, with the 1:1 diagonal implied by
    the shaded area between it and the curve. A dotted vertical line marks the threshold and a
    dot sits on the curve where it bends. Nothing here knows what compression is: it asks the
    module for an output level given an input level, so any module with a curve gets one. */
class TransferCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    TransferCurve (const Theme& t, const Module& m) : theme (t), module (m) {}

    /** The host parameters the threshold dot moves, given by the editor. Without them (or for a
        module that names none) the dot is drawn but cannot be dragged. Moves go through the host
        with gestures, so automation records them and the sliders beside the curve follow. */
    void setParams (std::vector<HostParam*> p)
    {
        params = std::move (p);
        setTooltip (draggable() ? "Drag the dot: sideways sets Threshold, up and down sets Ratio. "
                                  "Shift for fine control, double-click to reset both." : juce::String());
    }

    /** The detector level the module saw since the last tick, in dBFS. Rises at once and falls
        at about 30 dB a second, so the dot is readable rather than a flicker. */
    void pushLevel (float db)
    {
        level = db >= level ? db : juce::jmax (db, level - 1.0f);
        hasLevel = true;
    }

    /** For ps_shot: put the live dot somewhere without running audio. */
    void setLevelForShot (float db) { level = db; hasLevel = true; }
    juce::Point<float> dotPositionForShot() const { return dotCentre(); }

    void paint (juce::Graphics& g) override
    {
        auto outer = getLocalBounds();
        {
            // The header carries the values while the dot is held or hovered, and otherwise how
            // to use it, so the hint is on screen rather than only in a tooltip.
            const bool showValues = hover || dragging;
            g.setColour (showValues ? theme.text : theme.dim);
            g.setFont (type::font (type::support, showValues));
            juce::String line = module.getCurveTitle();
            if (showValues) line = valuesLine();
            else if (draggable()) line = module.getCurveTitle() + ". Drag the dot to set it.";
            g.drawFittedText (line, outer.removeFromTop (20), juce::Justification::centredLeft, 1, 0.9f);
        }

        const auto plot = plotBounds();
        g.setColour (theme.bg);
        g.fillRoundedRectangle (plot.toFloat(), 4.0f);

        g.setColour (theme.line.withAlpha (0.55f));
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine (plot.getX() + plot.getWidth() * i / 4, (float) plot.getY(), (float) plot.getBottom());
            g.drawHorizontalLine (plot.getY() + plot.getHeight() * i / 4, (float) plot.getX(), (float) plot.getRight());
        }
        // Grid values: input along the bottom, output up the left. Small, because they are
        // reference marks, not readouts.
        g.setColour (theme.dim.withAlpha (0.8f));
        g.setFont (type::font (11.0f));
        for (int i = 1; i < 4; ++i)
        {
            g.drawText (juce::String (-60 + 15 * i), juce::Rectangle<int> (30, 14).withCentre ({ plot.getX() + plot.getWidth() * i / 4, plot.getBottom() - 9 }),
                        juce::Justification::centred);
            g.drawText (juce::String (-15 * i), juce::Rectangle<int> (28, 14).withPosition (plot.getX() + 4, plot.getY() + plot.getHeight() * i / 4 - 15),
                        juce::Justification::centredLeft);
        }
        g.drawText ("In, dB", plot.reduced (6, 2).removeFromBottom (14), juce::Justification::centredRight);

        // Threshold guide, dotted so it cannot be mistaken for the curve.
        const float th = threshold();
        g.setColour (theme.warn.withAlpha (0.75f));
        for (int y = plot.getY(); y < plot.getBottom(); y += 8)
            g.fillRect (xOf (th), (float) y, 1.0f, 4.0f);

        // Shaded gap between 1:1 and the curve: how much is being taken off, at a glance.
        constexpr int N = 169;
        juce::Path fill, curve;
        for (int i = 0; i < N; ++i)
        {
            const float d = -60.0f + 60.0f * (float) i / (float) (N - 1);
            const float x = xOf (d), y = yOf (module.curveOutputDb (d));
            if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yOf (d)); }
            else        { curve.lineTo (x, y);          fill.lineTo (x, yOf (d)); }
        }
        for (int i = N - 1; i >= 0; --i)
        {
            const float d = -60.0f + 60.0f * (float) i / (float) (N - 1);
            fill.lineTo (xOf (d), yOf (module.curveOutputDb (d)));
        }
        fill.closeSubPath();
        g.setColour (theme.accent2.withAlpha (0.28f));
        g.fillPath (fill);
        g.setColour (theme.accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        paintLevel (g, plot, th);

        // The threshold dot: bigger with a ring when it can be grabbed and the mouse is on it.
        const auto c = dotCentre();
        if (hover || dragging)
        {
            g.setColour (theme.warn.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (c));
        }
        g.setColour (theme.warn);
        const float d = draggable() ? 13.0f : 9.0f;
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
        if (draggable())
        {
            g.setColour (theme.bg);
            g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c), 1.5f);
        }

        g.setColour (theme.line);
        g.drawRoundedRectangle (plot.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (onDot (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { if (! dragging) setHover (false); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! draggable() || ! onDot (e.position)) return;
        dragging = true;
        lastPos = e.position;
        for (auto* p : { param (module.getCurveThresholdParam()), param (module.getCurveRatioParam()) })
            if (p != nullptr) p->beginChangeGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto plot = plotBounds().toFloat();
        // Relative moves, so Shift can be pressed mid-drag without a jump; fine is a fifth.
        const float speed = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float dx = (e.position.x - lastPos.x) * speed, dy = (e.position.y - lastPos.y) * speed;
        lastPos = e.position;
        if (auto* t = param (module.getCurveThresholdParam()))
            t->setRealNotifyingHost (t->getReal() + dx * 60.0f / juce::jmax (1.0f, plot.getWidth()));
        // Pulling the curve down squeezes harder. The whole height covers 1:1 to 8:1, evenly
        // in ratio's own steps (each doubling takes the same distance).
        if (auto* r = param (module.getCurveRatioParam()))
            r->setRealNotifyingHost (r->getReal() * std::pow (8.0f, dy / juce::jmax (1.0f, plot.getHeight())));
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
            for (auto* p : { param (module.getCurveThresholdParam()), param (module.getCurveRatioParam()) })
                if (p != nullptr) p->endChangeGesture();
        dragging = false;
        setHover (onDot (e.position));
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! draggable() || ! onDot (e.position)) return;
        for (auto* p : { param (module.getCurveThresholdParam()), param (module.getCurveRatioParam()) })
            if (p != nullptr) { p->beginChangeGesture(); p->setRealNotifyingHost (p->getSpec().def); p->endChangeGesture(); }
        repaint();
    }

private:
    juce::Rectangle<int> plotBounds() const { return getLocalBounds().withTrimmedTop (20).reduced (2); }
    float threshold() const { return juce::jlimit (-60.0f, 0.0f, module.getCurveThresholdDb()); }
    float xOf (float db) const { const auto p = plotBounds(); return (float) p.getX() + (float) p.getWidth() * (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f; }
    float yOf (float db) const { const auto p = plotBounds(); return (float) p.getY() + (float) p.getHeight() * (-juce::jlimit (-60.0f, 0.0f, db)) / 60.0f; }
    juce::Point<float> dotCentre() const { const float th = threshold(); return { xOf (th), yOf (module.curveOutputDb (th)) }; }
    bool onDot (juce::Point<float> p) const { return draggable() && p.getDistanceFrom (dotCentre()) < 14.0f; }

    HostParam* param (int i) const { return i >= 0 && i < (int) params.size() ? params[(size_t) i] : nullptr; }
    bool draggable() const { return param (module.getCurveThresholdParam()) != nullptr; }

    void setHover (bool h)
    {
        if (h == hover) return;
        hover = h;
        setMouseCursor (h ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    juce::String valuesLine() const
    {
        juce::String s;
        if (auto* t = param (module.getCurveThresholdParam())) s << "Threshold " << t->getSpec().text (t->getReal());
        if (auto* r = param (module.getCurveRatioParam())) s << "    Ratio " << r->getSpec().text (r->getReal());
        return s;
    }

    /** Where the voice is right now: a dot on the curve at the detector's level, a faint line
        down to the input axis, and the level in words in the empty top-left of the plot. When
        the voice is not reaching the threshold the words say so, because a compressor that is
        doing nothing looks exactly like one that is set gently. */
    void paintLevel (juce::Graphics& g, juce::Rectangle<int> plot, float th)
    {
        if (! hasLevel) return;
        auto words = plot.reduced (10, 8).removeFromTop (40);
        g.setFont (type::font (type::support, true));
        if (level < -60.0f)
        {
            g.setColour (theme.dim);
            g.drawText ("No signal", words.removeFromTop (20), juce::Justification::topLeft);
            return;
        }
        const float x = xOf (level), y = yOf (module.curveOutputDb (level));
        g.setColour (theme.text.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, y, (float) plot.getBottom());
        g.setColour (theme.text);
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, y }));
        g.setColour (theme.bg);
        g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, y }), 1.5f);

        g.setColour (theme.text);
        g.drawText ("Voice now " + juce::String (juce::roundToInt (level)) + " dB", words.removeFromTop (20), juce::Justification::topLeft);
        const float under = th - level;
        if (under > 0.5f)
        {
            g.setColour (theme.dim);
            g.setFont (type::font (type::support));
            g.drawText (juce::String (juce::roundToInt (under)) + " dB under the threshold", words, juce::Justification::topLeft);
        }
    }

    const Theme& theme;
    const Module& module;
    std::vector<HostParam*> params;
    float level = -100.0f;
    bool hasLevel = false, hover = false, dragging = false;
    juce::Point<float> lastPos;
};

//==============================================================================
/** The EQ response curve, as Contour's JSFX draws it: 20 Hz to 20 kHz logarithmically, plus
    and minus 18 dB, with the shaded area showing departure from flat.

    Like the transfer curve this is generic - it asks the module for a level in dB at a
    frequency - so Contour Dynamic and Dropoff can offer one by implementing two methods.

    Behind the curve, for a module that asks for one, sits the spectrum of what goes in and what
    comes out, as the JSFX draws it: input as a dim fill, output as a brighter fill with an
    edge. The spectrum has its own scale (0 dBFS at the top, -90 at the bottom) because the EQ's
    plus and minus 18 dB would put a voice off the bottom of the graph. The switch at the top
    right picks Off, Out only, or In + Out, and the legend beside it names both colours. */
class ResponseCurve : public juce::Component
{
public:
    std::function<void (int)> onViewChange;

    ResponseCurve (const Theme& t, const Module& m) : theme (t), module (m), viewSwitch (t, { "Off", "Out", "In + Out" })
    {
        if (module.hasSpectrum())
        {
            viewSwitch.onChange = [this] (int i) { view = i; if (onViewChange) onViewChange (i); repaint(); };
            viewSwitch.setTooltip ("Shows the sound behind the curve: what comes out, or what goes in and what comes out.");
            addAndMakeVisible (viewSwitch);
        }
    }

    /** The editor hands over the two band arrays it updates on its timer. */
    void setSpectrum (const std::array<float, SpectrumAnalyser::numBands>* in,
                      const std::array<float, SpectrumAnalyser::numBands>* out)
    {
        specIn = in; specOut = out;
    }
    void setView (int v) { view = juce::jlimit (0, 2, v); viewSwitch.setIndex (view); repaint(); }
    int getView() const { return view; }

    static constexpr int headerH = 30;

    void resized() override
    {
        if (module.hasSpectrum())
        {
            const int w = juce::jmax (210, viewSwitch.idealWidth());
            viewSwitch.setBounds (getWidth() - w - 2, 0, w, headerH - 4);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto outer = getLocalBounds();
        auto head = outer.removeFromTop (headerH);
        const int heard = listeningTo();
        const auto warning = module.getAuditionWarning();
        g.setFont (type::font (type::support, active >= 0 || warning.isNotEmpty()));
        g.setColour (active >= 0 ? theme.text : warning.isNotEmpty() ? theme.warn : theme.dim);
        {
            // While a dot is hovered or held, its values; otherwise how to use the dots. The hint
            // stays on screen, as the standard asks, rather than living only in a tooltip.
            auto textArea = head.withTrimmedBottom (4).withTrimmedRight (module.hasSpectrum() ? viewSwitch.getWidth() + 130 : 0);
            juce::String line = "Response";
            if (active >= 0) line = readout (active);
            else if (warning.isNotEmpty()) line = warning;
            else if (! handles.empty())
                line = juce::String ("Drag dots to shape. Shift: fine. Scroll: width. ")
                     + (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? "Option" : "Alt") + "-drag: solo the band.";
            g.drawFittedText (line, textArea, juce::Justification::centredLeft, 1, 0.9f);
        }

        // Legend: the colours never stand alone, each has its word beside it.
        if (module.hasSpectrum() && view > 0)
        {
            auto leg = head.withTrimmedBottom (4).withTrimmedRight (viewSwitch.getWidth() + 16);
            auto item = [&] (const juce::String& word, juce::Colour c)
            {
                const int tw = juce::GlyphArrangement::getStringWidthInt (type::font (type::support), word);
                auto r = leg.removeFromRight (tw + 22);
                g.setColour (c);
                g.fillRoundedRectangle (r.removeFromLeft (14).withSizeKeepingCentre (14, 8).toFloat(), 2.0f);
                g.setColour (theme.dim);
                g.drawText (word, r.withTrimmedLeft (5), juce::Justification::centredLeft);
                leg.removeFromRight (10);
            };
            item ("Out", outColour());
            if (view == 2) item ("In", inColour());
        }

        auto plot = outer.reduced (2);
        g.setColour (theme.bg);
        g.fillRoundedRectangle (plot.toFloat(), 4.0f);

        auto xOf = [&] (double hz)
        {
            const double a = std::log10 (juce::jlimit (20.0, 20000.0, hz) / 20.0) / 3.0;   // 20 Hz .. 20 kHz
            return (float) (plot.getX() + plot.getWidth() * a);
        };
        auto yOf = [&] (double db)
        {
            return (float) (plot.getCentreY() - plot.getHeight() * juce::jlimit (-18.0, 18.0, db) / 36.0);
        };

        // The spectrum first, so the grid and the curve sit on top of it.
        if (module.hasSpectrum() && view > 0)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot);
            if (view == 2 && specIn != nullptr) drawSpectrum (g, *specIn, plot, inColour(), false);
            if (specOut != nullptr) drawSpectrum (g, *specOut, plot, outColour(), true);
        }

        // Decade lines, and the 6 dB grid.
        g.setColour (theme.line.withAlpha (0.5f));
        for (double hz : { 100.0, 1000.0, 10000.0 })
            g.drawVerticalLine ((int) xOf (hz), (float) plot.getY(), (float) plot.getBottom());
        for (double db : { -12.0, -6.0, 6.0, 12.0 })
            g.drawHorizontalLine ((int) yOf (db), (float) plot.getX(), (float) plot.getRight());
        g.setColour (theme.line);
        g.drawHorizontalLine ((int) yOf (0.0), (float) plot.getX(), (float) plot.getRight());

        g.setColour (theme.dim.withAlpha (0.8f));
        g.setFont (type::font (12.0f));
        for (auto [hz, label] : { std::pair<double, const char*> { 100.0, "100" },
                                  { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText (label, juce::Rectangle<int> (40, 14).withCentre ({ (int) xOf (hz), plot.getBottom() - 9 }),
                        juce::Justification::centred);
        // The EQ's dB scale, down the left edge, so a boost can be read off the graph.
        for (double db : { 12.0, 6.0, 0.0, -6.0, -12.0 })
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<int> (plot.getX() + 5, (int) yOf (db) - 8, 30, 16), juce::Justification::centredLeft);

        const int N = juce::jmax (2, plot.getWidth());
        juce::Path curve, fill;
        fill.startNewSubPath ((float) plot.getX(), yOf (0.0));
        for (int i = 0; i < N; ++i)
        {
            const double a = (double) i / (double) (N - 1);
            const double hz = 20.0 * std::pow (10.0, a * 3.0);
            const float x = (float) (plot.getX() + plot.getWidth() * a), y = yOf (module.responseDbAt ((float) hz));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo ((float) plot.getRight(), yOf (0.0));
        fill.closeSubPath();
        g.setColour (theme.accent2.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (theme.accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (theme.line);
        g.drawRoundedRectangle (plot.toFloat().reduced (0.5f), 4.0f, 1.0f);

        // The dots. On: filled with the accent, its number knocked out. Off: an outline with a
        // dim number, so "off" is shown by shape and word as well as colour. The one under the
        // mouse grows and gets a ring.
        for (int i = (int) handles.size() - 1; i >= 0; --i)
        {
            const auto c = handleCentre (i);
            const bool on = isOn (i), hot = i == active;
            const float r = hot ? dotR + 2.0f : dotR;
            auto dot = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
            if (hot)
            {
                g.setColour (theme.accent.withAlpha (0.3f));
                g.fillEllipse (dot.expanded (5.0f));
            }
            if (i == heard)
            {   // the band being heard: an amber ring, with the words in the header line
                g.setColour (theme.warn);
                g.drawEllipse (dot.expanded (4.0f), 2.5f);
            }
            if (on)
            {
                g.setColour (theme.accent);
                g.fillEllipse (dot);
                g.setColour (theme.bg);
            }
            else
            {
                g.setColour (theme.bg.withAlpha (0.85f));
                g.fillEllipse (dot);
                g.setColour (theme.dim);
                g.drawEllipse (dot.reduced (0.75f), 1.5f);
            }
            g.setFont (type::font (handles[(size_t) i].shortName.length() > 1 ? 11.0f : 13.0f, true));
            g.drawText (handles[(size_t) i].shortName, dot.toNearestInt(), juce::Justification::centred);
        }
    }

    //==========================================================================
    /** The host parameters the dots move. Given by the editor; without them the dots are not
        drawn (Basecamp's compact cards, for now). Moves go through the host, with gestures, so
        automation records them and the knobs below follow. */
    void setHandleParams (std::vector<HostParam*> p)
    {
        params = std::move (p);
        handles = params.empty() ? std::vector<Module::CurveHandle> {} : module.getCurveHandles();
        repaint();
    }

    /** For ps_shot: where dot i is drawn, so a scripted drag can start on it. */
    juce::Point<float> handlePositionForShot (int i) const { return handleCentre (i); }

    void mouseMove (const juce::MouseEvent& e) override { setActive (hit (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { if (! dragging) setActive (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        setActive (h);
        if (h < 0) return;
        if (e.mods.isPopupMenu())   // right-click turns a band on or off
        {
            if (auto* on = param (handles[(size_t) h].on))
            {
                on->beginChangeGesture(); on->setRealNotifyingHost (on->getReal() >= 0.5f ? 0.0f : 1.0f); on->endChangeGesture();
                repaint();
            }
            return;
        }
        dragging = true;
        lastPos = e.position;
        if (e.mods.isAltDown())
            if (auto* l = param (module.getSoloParam()))
            {   // solo this band while it is held, so it can be swept to find a problem
                listenBefore = l->getReal();
                l->beginChangeGesture(); l->setRealNotifyingHost ((float) (h + 1));
                heldListen = true;
            }
        const auto& hd = handles[(size_t) h];
        for (int id : { hd.freq, hd.gain }) if (auto* p = param (id)) p->beginChangeGesture();
        if (auto* on = param (hd.on); on != nullptr && on->getReal() < 0.5f)
        {   // grabbing a band that is off brings it in, as dragging it would be pointless otherwise
            on->beginChangeGesture(); on->setRealNotifyingHost (1.0f); on->endChangeGesture();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || active < 0) return;
        const auto plot = plotBounds().toFloat();
        // Moves are relative to the last position, so Shift can be pressed mid-drag without
        // the dot jumping: fine control is a fifth of the speed.
        const float speed = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float dx = (e.position.x - lastPos.x) * speed, dy = (e.position.y - lastPos.y) * speed;
        lastPos = e.position;
        const auto& hd = handles[(size_t) active];
        if (auto* f = param (hd.freq))
            f->setRealNotifyingHost (f->getReal() * std::pow (1000.0f, dx / juce::jmax (1.0f, plot.getWidth())));
        if (auto* gp = param (hd.gain))
            gp->setRealNotifyingHost (gp->getReal() - dy * 36.0f / juce::jmax (1.0f, plot.getHeight()));
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging && active >= 0)
        {
            const auto& hd = handles[(size_t) active];
            for (int id : { hd.freq, hd.gain }) if (auto* p = param (id)) p->endChangeGesture();
        }
        if (heldListen)
            if (auto* l = param (module.getSoloParam()))
            {   // put Solo back the way it was before the Alt-drag
                l->setRealNotifyingHost (listenBefore); l->endChangeGesture();
            }
        heldListen = false;
        dragging = false;
        setActive (hit (e.position));
    }

    /** Double-click puts a band back where it started: frequency, gain and width. */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h < 0) return;
        const auto& hd = handles[(size_t) h];
        for (int id : { hd.freq, hd.gain, hd.q })
            if (auto* p = param (id)) { p->beginChangeGesture(); p->setRealNotifyingHost (p->getSpec().def); p->endChangeGesture(); }
        repaint();
    }

    /** The wheel over a dot sets its width (Q). One notch is about 10 percent, Shift a fifth
        of that. Bands without a width ignore it, and the wheel scrolls the window instead. */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int h = hit (e.position);
        auto* q = h >= 0 ? param (handles[(size_t) h].q) : nullptr;
        if (q == nullptr) { Component::mouseWheelMove (e, w); return; }
        const float notches = (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f) / 0.25f;
        const float step = e.mods.isShiftDown() ? 0.02f : 0.1f;
        q->beginChangeGesture();
        q->setRealNotifyingHost (q->getReal() * std::pow (1.0f + step, notches));
        q->endChangeGesture();
        setActive (h);
        repaint();
    }

    /** Spectrum scale: 0 dBFS at the top of the plot, specFloorDb at the bottom. */
    static constexpr float specFloorDb = -90.0f;

private:
    // Input is a neutral grey-blue in every Look so it reads as "before"; output borrows the
    // Look's second accent, kept distinct from the curve's own colour.
    juce::Colour inColour() const  { return theme.dim; }
    juce::Colour outColour() const { return theme.accent2; }

    void drawSpectrum (juce::Graphics& g, const std::array<float, SpectrumAnalyser::numBands>& b,
                       juce::Rectangle<int> plot, juce::Colour c, bool edge)
    {
        auto yOf = [&] (float db)
        {
            const float a = juce::jlimit (0.0f, 1.0f, (db - specFloorDb) / -specFloorDb);
            return (float) plot.getBottom() - a * (float) plot.getHeight();
        };
        auto xOfBand = [&] (int k)
        {
            const double a = (k + 0.5) / SpectrumAnalyser::numBands;   // bands span 20 Hz..20 kHz
            return (float) (plot.getX() + plot.getWidth() * a);
        };
        juce::Path line, area;
        area.startNewSubPath ((float) plot.getX(), (float) plot.getBottom());
        area.lineTo ((float) plot.getX(), yOf (b[0]));
        for (int k = 0; k < SpectrumAnalyser::numBands; ++k)
        {
            const float x = xOfBand (k), y = yOf (b[(size_t) k]);
            if (k == 0) line.startNewSubPath ((float) plot.getX(), y);
            line.lineTo (x, y);
            area.lineTo (x, y);
        }
        const float yEnd = yOf (b[SpectrumAnalyser::numBands - 1]);
        line.lineTo ((float) plot.getRight(), yEnd);
        area.lineTo ((float) plot.getRight(), yEnd);
        area.lineTo ((float) plot.getRight(), (float) plot.getBottom());
        area.closeSubPath();
        g.setColour (c.withAlpha (edge ? 0.22f : 0.18f));
        g.fillPath (area);
        g.setColour (c.withAlpha (edge ? 0.75f : 0.45f));
        g.strokePath (line, juce::PathStrokeType (edge ? 1.4f : 1.0f, juce::PathStrokeType::curved));
    }

    static constexpr float dotR = 11.0f;

    juce::Rectangle<int> plotBounds() const { return getLocalBounds().withTrimmedTop (headerH).reduced (2); }

    HostParam* param (int id) const { return id >= 0 && id < (int) params.size() ? params[(size_t) id] : nullptr; }
    bool isOn (int i) const { auto* p = param (handles[(size_t) i].on); return p == nullptr || p->getReal() >= 0.5f; }

    /** Where a dot sits: its frequency across, its gain up and down. Bands without a gain (the
        high-pass) sit on the 0 dB line. Clamped inside the plot so a band at an extreme stays
        grabbable. */
    juce::Point<float> handleCentre (int i) const
    {
        const auto plot = plotBounds().toFloat();
        const auto& hd = handles[(size_t) i];
        const float hz = param (hd.freq) != nullptr ? param (hd.freq)->getReal() : 1000.0f;
        const float db = param (hd.gain) != nullptr ? param (hd.gain)->getReal() : 0.0f;
        const float a = (float) (std::log10 (juce::jlimit (20.0, 20000.0, (double) hz) / 20.0) / 3.0);
        const float x = plot.getX() + plot.getWidth() * a;
        const float y = plot.getCentreY() - plot.getHeight() * juce::jlimit (-18.0f, 18.0f, db) / 36.0f;
        return { juce::jlimit (plot.getX() + dotR, plot.getRight() - dotR, x),
                 juce::jlimit (plot.getY() + dotR, plot.getBottom() - dotR, y) };
    }

    /** The dot under a point, nearest first; dots are drawn so the lowest number is on top. */
    int hit (juce::Point<float> p) const
    {
        int best = -1; float bestD = (dotR + 5.0f) * (dotR + 5.0f);
        for (int i = 0; i < (int) handles.size(); ++i)
        {
            const float d = handleCentre (i).getDistanceSquaredFrom (p);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void setActive (int i)
    {
        if (dragging && i != active) return;   // keep the held dot while the mouse runs ahead of it
        if (i == active) return;
        active = i;
        setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    juce::String readout (int i) const
    {
        const auto& hd = handles[(size_t) i];
        juce::String t = hd.name;
        if (auto* f = param (hd.freq)) t << "   " << f->getSpec().text (f->getReal());
        if (auto* gp = param (hd.gain)) { const float v = gp->getReal(); t << "   " << (v > 0.05f ? "+" : "") << gp->getSpec().text (v); }
        if (auto* q = param (hd.q)) t << "   Q " << q->getSpec().text (q->getReal());
        if (! isOn (i)) t << "   (off)";
        return t;
    }

    const Theme& theme;
    const Module& module;
    Segmented viewSwitch;
    std::vector<HostParam*> params;
    std::vector<Module::CurveHandle> handles;
    juce::Point<float> lastPos;
    int active = -1;
    bool dragging = false, heldListen = false;
    float listenBefore = 0.0f;

    /** The handle being soloed, or -1. */
    int listeningTo() const
    {
        auto* l = param (module.getSoloParam());
        const int v = l != nullptr ? juce::roundToInt (l->getReal()) : 0;
        return v >= 1 && v <= (int) handles.size() ? v - 1 : -1;
    }
    const std::array<float, SpectrumAnalyser::numBands>* specIn = nullptr;
    const std::array<float, SpectrumAnalyser::numBands>* specOut = nullptr;
    int view = 2;
};

//==============================================================================
/** Twelve seconds of gain reduction, scrolling right to left.

    The number alone says what is happening now; this says whether the plugin has been riding
    the whole take or catching the odd peak, which is the thing you actually need to know when
    setting a compressor. One sample every 50 ms over 240 slots, as the JSFX does it, with the
    scale growing in 6 dB steps so a quiet passage does not look dramatic. */
class ReductionHistory : public juce::Component
{
public:
    explicit ReductionHistory (const Theme& t, const Module* m = nullptr) : theme (t), module (m)
    {
        hist.fill (0.0f); inHist.fill (-100.0f); outHist.fill (-100.0f);
    }

    /** Reduction in dB, plus, for a module that reports them, the input and output levels in
        dBFS (pass anything below -100 for none). The loudest of each since the last slot wins. */
    void push (float db, float inDb = -200.0f, float outDb = -200.0f)
    {
        pending = juce::jmax (pending, db);
        pendingIn = juce::jmax (pendingIn, inDb);
        pendingOut = juce::jmax (pendingOut, outDb);
        if (inDb > -150.0f) showLevels = true;
        if (++sinceWrite < 2) return;          // the editor ticks at 30 Hz; this wants 20
        sinceWrite = 0;
        hist[head] = juce::jmax (0.0f, pending);
        inHist[head] = pendingIn;
        outHist[head] = pendingOut;
        head = (head + 1) % slots;
        pending = 0.0f;
        pendingIn = pendingOut = -200.0f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto outer = getLocalBounds();
        auto top = outer.removeFromTop (20);

        float peak = 0.0f;
        for (auto v : hist) peak = juce::jmax (peak, v);
        const float span = juce::jmax (6.0f, std::ceil (peak / 6.0f) * 6.0f);

        // Right side of the header: the numbers, then a legend naming every colour in the plot.
        g.setColour (theme.accent2);
        g.setFont (type::font (type::support, true));
        const auto nums = "now " + juce::String (hist[(head + slots - 1) % slots], 1) + "    max " + juce::String (peak, 1) + " dB";
        g.drawText (nums, top, juce::Justification::centredRight);
        auto rest = top.withTrimmedRight (juce::GlyphArrangement::getStringWidthInt (type::font (type::support, true), nums) + 22);
        if (showLevels)
        {
            g.setFont (type::font (type::support));
            auto item = [&] (const juce::String& word, juce::Colour c, bool fillSwatch)
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (type::font (type::support), word);
                auto r = rest.removeFromRight (w + 22);
                g.setColour (theme.dim);
                g.drawText (word, r.removeFromRight (w), juce::Justification::centredLeft);
                auto sw = r.removeFromLeft (14).withSizeKeepingCentre (12, fillSwatch ? 10 : 3).toFloat();
                g.setColour (c);
                g.fillRoundedRectangle (sw, 1.5f);
                rest.removeFromRight (8);
            };
            item ("Reduction", theme.accent2, true);
            item ("Out", theme.accent, false);
            item ("In", theme.dim.withAlpha (0.55f), true);
        }

        // Left: what this is, or in amber what you are hearing instead of the normal output.
        const auto warning = module != nullptr ? module->getAuditionWarning() : juce::String();
        g.setColour (warning.isNotEmpty() ? theme.warn : theme.dim);
        g.setFont (type::font (type::support, warning.isNotEmpty()));
        g.drawFittedText (warning.isNotEmpty() ? warning
                          : showLevels ? juce::String ("Last 12 seconds") : juce::String ("Gain reduction, last 12 seconds"),
                          rest, juce::Justification::centredLeft, 1, 0.9f);

        auto plot = outer.reduced (2);
        g.setColour (theme.bg);
        g.fillRoundedRectangle (plot.toFloat(), 4.0f);
        g.setColour (theme.line.withAlpha (0.55f));
        for (int i = 1; i < 3; ++i)
            g.drawHorizontalLine (plot.getY() + plot.getHeight() * i / 3, (float) plot.getX(), (float) plot.getRight());

        auto xOf = [&] (int i) { return plot.getX() + plot.getWidth() * (float) i / (float) (slots - 1); };

        if (showLevels)
        {
            // Levels use their own scale, -60 dBFS at the bottom to 0 at the top, under the
            // reduction, which hangs from the top on its scale as before.
            auto yLev = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
            juce::Path inArea, outLine;
            inArea.startNewSubPath ((float) plot.getX(), (float) plot.getBottom());
            bool started = false;
            for (int i = 0; i < slots; ++i)
            {
                const int k = (head + i) % slots;
                inArea.lineTo (xOf (i), yLev (inHist[k]));
                if (outHist[k] > -60.0f)
                {
                    if (! started) { outLine.startNewSubPath (xOf (i), yLev (outHist[k])); started = true; }
                    else outLine.lineTo (xOf (i), yLev (outHist[k]));
                }
                else started = false;
            }
            inArea.lineTo ((float) plot.getRight(), (float) plot.getBottom());
            inArea.closeSubPath();
            g.setColour (theme.dim.withAlpha (0.22f));
            g.fillPath (inArea);
            g.setColour (theme.accent.withAlpha (0.85f));
            g.strokePath (outLine, juce::PathStrokeType (1.4f));

            if (module != nullptr && module->hasTransferCurve())
            {   // the threshold on the level scale, dotted like the curve's guide
                const float y = yLev (juce::jlimit (-60.0f, 0.0f, module->getCurveThresholdDb()));
                g.setColour (theme.warn.withAlpha (0.7f));
                for (int x = plot.getX(); x < plot.getRight(); x += 8)
                    g.fillRect ((float) x, y, 4.0f, 1.0f);
            }
        }

        juce::Path area, line;
        area.startNewSubPath ((float) plot.getX(), (float) plot.getY());
        for (int i = 0; i < slots; ++i)
        {
            const float v = juce::jmin (span, hist[(head + i) % slots]);
            const float x = xOf (i);
            const float y = plot.getY() + plot.getHeight() * (v / span);
            area.lineTo (x, y);
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        area.lineTo ((float) plot.getRight(), (float) plot.getY());
        area.closeSubPath();
        g.setColour (theme.accent2.withAlpha (0.28f));
        g.fillPath (area);
        g.setColour (theme.accent2);
        g.strokePath (line, juce::PathStrokeType (1.6f));

        g.setColour (theme.dim.withAlpha (0.8f));
        g.setFont (type::font (11.0f));
        g.drawText ("reduction -" + juce::String (span, 0) + " dB", plot.reduced (4, 2), juce::Justification::bottomLeft);
        if (showLevels) g.drawText ("level 0 to -60 dB", plot.reduced (4, 2), juce::Justification::bottomRight);
        g.setColour (theme.line);
        g.drawRoundedRectangle (plot.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

private:
    static constexpr int slots = 240;          // 240 x 50 ms = 12 seconds
    const Theme& theme;
    const Module* module = nullptr;
    std::array<float, slots> hist {}, inHist {}, outHist {};
    int head = 0, sinceWrite = 0;
    float pending = 0.0f, pendingIn = -200.0f, pendingOut = -200.0f;
    bool showLevels = false;
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
