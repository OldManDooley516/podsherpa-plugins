#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace ps
{
/** The suite's Looks and modes, as runtime values rather than compile-time constants.

    The first-pass editor hard-coded one navy/mint palette as a set of global consts. Looks have
    to be switchable while a plugin is open, and the hidden Vintage look can change its own accent
    from a colour wheel, so the palette is a value that gets passed around instead.

    Three listed Looks - Default, Vaporwave, Graphite - and Vintage, which is not listed. Default
    is the out-of-box look and every plugin opens in it. Vintage is reached by triple-clicking the
    SOUTHERN DIGITAL footer, is never saved and never opens.

    Modes are separate from Looks: Beginner, Normal, Minimal. A mode changes density, captions and
    how much is on screen. Minimal also pulls the accent into the purple/blue family, which is the
    one place a mode touches colour.

    Two rules hold across every Look, from the interface standard:
      - amber warnings stay visible in all of them, so amber is never themed down to something
        that disappears into its own background;
      - gain reduction is blue and controls are mint in Default, and the pairing of colour with a
        word, a value or a position is kept everywhere, so colour is never the only signal. */
struct Theme
{
    enum class Id { Default_, Vaporwave, Graphite, Vintage };

    Id id = Id::Default_;
    juce::String name { "Default" };

    juce::Colour bg, panel, panelHi, line, text, dim, accent, accent2, warn, bad;

    /** Vintage draws its controls differently rather than just recolouring them: silver buttons
        with a lit amber edge, ribbed faders, and no indicator dot on knobs. */
    bool silverButtons = false;
    bool ribbedFaders  = false;
    bool knobDots      = true;

    /** Vintage only: the secret colour wheel sets this, and accent follows it. */
    bool accentIsCustom = false;

    float corner = 10.0f;

    juce::Colour accentOn (bool on) const { return on ? accent : panelHi; }
};

namespace themes
{
    inline Theme defaultLook()
    {
        Theme t;
        t.id = Theme::Id::Default_; t.name = "Default";
        t.bg      = juce::Colour (0xff0e1a30);
        t.panel   = juce::Colour (0xff172742);
        t.panelHi = juce::Colour (0xff20355a);
        t.line    = juce::Colour (0xff2c4470);
        t.text    = juce::Colour (0xffeaf0fb);
        t.dim     = juce::Colour (0xffa9b8d4);
        t.accent  = juce::Colour (0xff5eead4);   // mint: controls
        t.accent2 = juce::Colour (0xff60a5fa);   // blue: gain reduction
        t.warn    = juce::Colour (0xfffbbf24);
        t.bad     = juce::Colour (0xfff87171);
        return t;
    }

    inline Theme vaporwave()
    {
        Theme t = defaultLook();
        t.id = Theme::Id::Vaporwave; t.name = "Vaporwave";
        t.bg      = juce::Colour (0xff1a1033);
        t.panel   = juce::Colour (0xff2a1a4d);
        t.panelHi = juce::Colour (0xff3b2566);
        t.line    = juce::Colour (0xff53328c);
        t.text    = juce::Colour (0xfff6ecff);
        t.dim     = juce::Colour (0xffc3a8e8);
        t.accent  = juce::Colour (0xffff77c8);
        t.accent2 = juce::Colour (0xff57e0f0);
        t.warn    = juce::Colour (0xffffc24d);   // kept bright: warnings must read here too
        t.bad     = juce::Colour (0xffff6b8a);
        t.corner  = 12.0f;
        return t;
    }

    inline Theme graphite()
    {
        Theme t = defaultLook();
        t.id = Theme::Id::Graphite; t.name = "Graphite";
        t.bg      = juce::Colour (0xff17191c);
        t.panel   = juce::Colour (0xff22262b);
        t.panelHi = juce::Colour (0xff2e3339);
        t.line    = juce::Colour (0xff3d444c);
        t.text    = juce::Colour (0xfff0f2f5);
        t.dim     = juce::Colour (0xffa6adb8);
        t.accent  = juce::Colour (0xff7fd1b9);
        t.accent2 = juce::Colour (0xff6fa8dc);
        t.warn    = juce::Colour (0xfff2b73d);
        t.bad     = juce::Colour (0xffe8705f);
        t.corner  = 6.0f;
        return t;
    }

    inline Theme vintage()
    {
        Theme t;
        t.id = Theme::Id::Vintage; t.name = "Vintage";
        t.bg      = juce::Colour (0xff20211d);
        t.panel   = juce::Colour (0xff2b2c26);
        t.panelHi = juce::Colour (0xffb9b7ad);   // silver face
        t.line    = juce::Colour (0xff4a4a40);
        t.text    = juce::Colour (0xfff2eddc);
        t.dim     = juce::Colour (0xffb0a98f);
        t.accent  = juce::Colour (0xffe8a33d);   // amber lit edge
        t.accent2 = juce::Colour (0xff8fb0a0);
        t.warn    = juce::Colour (0xffffc24d);
        t.bad     = juce::Colour (0xffd2644f);
        t.silverButtons = true;
        t.ribbedFaders  = true;
        t.knobDots      = false;
        t.corner = 4.0f;
        return t;
    }

    /** The three listed Looks, in the order they appear in the Look pill. Vintage is deliberately
        absent: it is a bonus look, not a choice. */
    inline std::vector<Theme> listed() { return { defaultLook(), vaporwave(), graphite() }; }

    inline Theme byId (Theme::Id id)
    {
        switch (id)
        {
            case Theme::Id::Vaporwave: return vaporwave();
            case Theme::Id::Graphite:  return graphite();
            case Theme::Id::Vintage:   return vintage();
            default:                   return defaultLook();
        }
    }

    /** Minimal mode pulls the accent into the purple/blue family. Applied on top of any Look,
        and left alone in Vintage, whose whole point is the amber edge. */
    inline Theme withMinimal (Theme t)
    {
        if (t.id == Theme::Id::Vintage) return t;
        t.accent  = juce::Colour (0xff8b9dff);
        t.accent2 = juce::Colour (0xff6fa8dc);
        return t;
    }
}

enum class Mode { Beginner, Normal, Minimal };

inline juce::String modeName (Mode m)
{
    return m == Mode::Beginner ? "Beginner" : m == Mode::Minimal ? "Minimal" : "Normal";
}

/** Type scale. The standard puts sizes 20% above the earlier set and gives floors at the default
    window size: at least 16 px for main labels, 22-26 px for key values, 14 px for supporting
    text. Captions get shortened before type gets smaller, so these do not scale down with mode. */
namespace type
{
    constexpr float title    = 28.0f;
    constexpr float subtitle = 20.0f;
    constexpr float value    = 24.0f;
    constexpr float label    = 17.0f;
    constexpr float support  = 14.0f;
    constexpr float footer   = 13.0f;

    /** The suite's faces, compiled into the binary rather than looked up by name, so a machine
        without Montserrat and Manrope installed still draws the interface correctly. That is the
        standard's requirement; assets/fonts/README.md says why these are embedded outlines here
        and pre-rendered atlases in the JSFX versions, which cannot carry a font at all.

        Loaded once on first use and held for the life of the process. If a face somehow fails to
        load, the Font falls back to the host's default rather than drawing nothing. */
    namespace detail
    {
        /** JUCE already caches by the data pointer, so this does not keep a cache of its own.
            An earlier version did, as a static std::map of Typeface::Ptr, and it crashed every
            process on exit: the map outlived JUCE's own typeface cache and released its
            references after that cache had been destroyed. Inside a DAW that is a crash on quit,
            which is both alarming and hard to trace back to a font. */
        inline juce::Typeface::Ptr load (const void* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        inline juce::Font make (juce::Typeface::Ptr tf, float size)
        {
            if (tf == nullptr) return juce::Font (juce::FontOptions (size, juce::Font::plain));
            return juce::Font (juce::FontOptions (tf).withHeight (size));
        }
    }

    /** Manrope: Medium for labels and supporting text, SemiBold for values and anything bold. */
    inline juce::Font font (float size, bool bold = false)
    {
        return detail::make (detail::load (bold ? BinaryData::ManropeSemiBold_ttf : BinaryData::ManropeMedium_ttf,
                                           bold ? BinaryData::ManropeSemiBold_ttfSize : BinaryData::ManropeMedium_ttfSize),
                             size);
    }

    /** Montserrat SemiBold: the plugin title only. */
    inline juce::Font titleFont (float size)
    {
        return detail::make (detail::load (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize), size);
    }
}
} // namespace ps
