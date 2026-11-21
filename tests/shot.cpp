/** ps_shot: renders a plugin editor to a PNG without a window or a host.

    The interface cannot be judged from code, and the build machine has no screen, so this walks
    the editor's own paint path into an offscreen image. It is a development tool, not part of the
    product: it proves layout, contrast and that nothing overlaps, and it is no substitute for
    Sean loading the plugin in REAPER, where resizing, mouse behaviour and the host's own chrome
    are what actually matter.

    Usage:  ps_shot <module-id> <look> <mode> <out.png> [width] [height]
            look: default | vaporwave | graphite | vintage
            mode: beginner | normal | minimal
*/
#include "../core/SinglePlugin.h"
#include "../core/ModuleFactory.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <tuple>

using namespace ps;

static Theme::Id lookFromName (const juce::String& s)
{
    if (s.equalsIgnoreCase ("vaporwave")) return Theme::Id::Vaporwave;
    if (s.equalsIgnoreCase ("graphite"))  return Theme::Id::Graphite;
    if (s.equalsIgnoreCase ("vintage"))   return Theme::Id::Vintage;
    return Theme::Id::Default_;
}

static Mode modeFromName (const juce::String& s)
{
    if (s.equalsIgnoreCase ("beginner")) return Mode::Beginner;
    if (s.equalsIgnoreCase ("minimal"))  return Mode::Minimal;
    return Mode::Normal;
}

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage: ps_shot <module-id> <look> <mode> <out.png> [w] [h]\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String moduleId { argv[1] };
    const auto look = lookFromName (argv[2]);
    const auto mode = modeFromName (argv[3]);
    const juce::File out { juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]) };

    auto module = createModule (moduleId);
    if (module == nullptr)
    {
        std::printf ("unknown module '%s'\n", moduleId.toRawUTF8());
        return 1;
    }

    SinglePluginProcessor proc (std::move (module));
    proc.prepareToPlay (48000.0, 512);
    // PS_SHOT_PRESET=<n> renders with factory preset n, to see a curve that is not flat.
    if (const auto pre = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_PRESET", {}); pre.isNotEmpty())
        proc.setCurrentProgram (pre.getIntValue());
    // PS_SHOT_SOLO=<n> sets the module's Solo choice (Contour: 3 = Band 3).
    if (const auto li = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_SOLO", {}); li.isNotEmpty())
        if (const int lp = proc.getModule().getSoloParam(); lp >= 0)
            proc.getModuleParams()[(size_t) lp]->setRealNotifyingHost ((float) li.getIntValue());

    // PS_SHOT_DELTA=1 turns on Delta (Contour, Traverse), to see its amber warning.
    if (juce::SystemStats::getEnvironmentVariable ("PS_SHOT_DELTA", {}).isNotEmpty())
        for (size_t i = 0; i < proc.getModule().getParamSpecs().size(); ++i)
            if (proc.getModule().getParamSpecs()[i].id == "delta")
                proc.getModuleParams()[i]->setRealNotifyingHost (1.0f);

    // Push a little audio through so the meters are not sitting at the floor in the shot.
    {
        juce::AudioBuffer<float> b { 2, 512 };
        juce::Random rnd { 1 };
        for (int block = 0; block < 20; ++block)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    b.setSample (c, i, 0.25f * (rnd.nextFloat() * 2.0f - 1.0f));
            juce::MidiBuffer m;
            proc.processBlock (b, m);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed { proc.createEditor() };
    if (ed == nullptr) { std::printf ("no editor\n"); return 1; }

    SuiteSettings::get().setMode (mode);
    if (look != Theme::Id::Vintage) SuiteSettings::get().setLook (look);
    if (auto* se = dynamic_cast<SinglePluginEditor*> (ed.get()))
        se->setLookForShot (look, mode);

    // PS_SHOT_PIPSTEP=<n> (with PS_SHOT_PRESET) shows step n of that preset's Pip walkthrough.
    if (const auto ps = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_PIPSTEP", {}); ps.isNotEmpty())
        if (auto* se = dynamic_cast<SinglePluginEditor*> (ed.get()))
            se->pipStepForShot (ps.getIntValue());

    const int w = argc > 5 ? juce::String (argv[5]).getIntValue() : ed->getWidth();
    const int h = argc > 6 ? juce::String (argv[6]).getIntValue() : ed->getHeight();
    ed->setSize (w, h);

    // The editor's timer never fires here, so pull the meters forward by hand, with audio
    // between ticks as a host would have. A voice-like test signal: a 130 Hz buzz with three
    // formants, a little hiss, and 50 Hz hum for a high-pass to visibly remove.
    if (auto* se = dynamic_cast<SinglePluginEditor*> (ed.get()))
    {
        juce::AudioBuffer<float> b { 2, 512 };
        juce::Random rnd { 2 };
        double ph = 0.0, hum = 0.0;
        const double pi2 = 2.0 * juce::MathConstants<double>::pi;
        auto formant = [] (double hz)
        {
            double g = 0.0;
            for (auto [f, bw, a] : { std::tuple { 500.0, 120.0, 1.0 }, { 1500.0, 200.0, 0.5 }, { 2600.0, 300.0, 0.3 } })
                g += a / (1.0 + std::pow ((hz - f) / bw, 2.0));
            return 0.15 + g;
        };
        // PS_SHOT_SECONDS=<n> runs that much audio instead of half a second, with the voice
        // speaking in syllables and phrases at uneven levels, so a history display fills up.
        const double secs = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_SECONDS", "0").getDoubleValue();
        const int ticks = secs > 0.0 ? (int) (secs * 48000.0 / 2048.0) : 12;
        // PS_SHOT_GAIN=<dB> scales the test voice (default 0: peaks around -10 dBFS RMS).
        const double voiceGain = juce::Decibels::decibelsToGain (juce::SystemStats::getEnvironmentVariable ("PS_SHOT_GAIN", "0").getDoubleValue());
        long n = 0;
        for (int tick = 0; tick < ticks; ++tick)
        {
            for (int blk = 0; blk < 4; ++blk)
            {
                for (int i = 0; i < 512; ++i, ++n)
                {
                    double env = 1.0;
                    if (secs > 0.0)
                    {   // 4 syllables a second, phrases of 2.5 s with a 0.5 s pause, louder and quieter phrases
                        const double t = (double) n / 48000.0, inPhrase = std::fmod (t, 3.0);
                        const double phraseDb = std::array<double, 4> { 0.0, -9.0, 4.0, -4.0 }[(size_t) ((int) (t / 3.0) % 4)];
                        const double syl = 0.55 + 0.45 * std::sin (t * 2.0 * juce::MathConstants<double>::pi * 4.0);
                        env = inPhrase < 2.5 ? syl * juce::Decibels::decibelsToGain (phraseDb) : 0.0;
                    }
                    double v = 0.0;
                    for (int k = 1; k <= 120; ++k) v += std::sin (ph * k) * formant (130.0 * k) / k;
                    ph += pi2 * 130.0 / 48000.0; hum += pi2 * 50.0 / 48000.0;
                    v = voiceGain * (0.12 * env * v + 0.03 * std::sin (hum) + 0.002 * (rnd.nextFloat() * 2.0f - 1.0f));
                    for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) v);
                }
                juce::MidiBuffer m;
                proc.processBlock (b, m);
            }
            se->tickForShot();
        }
    }

    // PS_SHOT_DRAG=<dot>,<dx>,<dy>[,shift] drags a curve dot with real mouse events, then
    // leaves the mouse over it, so the drag code and the hover readout can be checked headless.
    if (const auto drag = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_DRAG", {}); drag.isNotEmpty())
        if (auto* se = dynamic_cast<SinglePluginEditor*> (ed.get()))
            if (auto* rc = se->responseForShot())
            {
                const auto parts = juce::StringArray::fromTokens (drag, ",", {});
                const int dot = parts[0].getIntValue();
                const float dx = parts[1].getFloatValue(), dy = parts[2].getFloatValue();
                const auto mods = parts.size() > 3 ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)
                                                   : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                const auto t0 = juce::Time::getCurrentTime();
                auto ev = [&] (juce::Point<float> p, juce::ModifierKeys m)
                {
                    return juce::MouseEvent (src, p, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, rc, rc, t0, rc->handlePositionForShot (dot), t0, 1, false);
                };
                const auto start = rc->handlePositionForShot (dot);
                rc->mouseDown (ev (start, mods));
                for (int k = 1; k <= 10; ++k) rc->mouseDrag (ev (start + juce::Point<float> (dx, dy) * ((float) k / 10.0f), mods));
                rc->mouseUp (ev (start + juce::Point<float> (dx, dy), juce::ModifierKeys()));
                rc->mouseMove (ev (rc->handlePositionForShot (dot), juce::ModifierKeys()));
                juce::MidiBuffer m; juce::AudioBuffer<float> b { 2, 512 }; b.clear();
                proc.processBlock (b, m);   // the host parameters reach the module on the next block
                const auto& sp = proc.getModule().getParamSpecs();
                const auto hd = proc.getModule().getCurveHandles()[(size_t) dot];
                std::printf ("dragged %s: %s", hd.name.toRawUTF8(), sp[(size_t) hd.freq].text (proc.getModule().getParam (hd.freq)).toRawUTF8());
                if (hd.gain >= 0) std::printf (", %s", sp[(size_t) hd.gain].text (proc.getModule().getParam (hd.gain)).toRawUTF8());
                std::printf ("\n");
            }

    // PS_SHOT_CURVEDRAG=<dx>,<dy>[,shift] drags a transfer curve's threshold dot, then leaves
    // the mouse over it so the hover readout shows.
    if (const auto drag = juce::SystemStats::getEnvironmentVariable ("PS_SHOT_CURVEDRAG", {}); drag.isNotEmpty())
        if (auto* se = dynamic_cast<SinglePluginEditor*> (ed.get()))
            if (auto* tc = se->curveForShot())
            {
                const auto parts = juce::StringArray::fromTokens (drag, ",", {});
                const float dx = parts[0].getFloatValue(), dy = parts[1].getFloatValue();
                const auto mods = parts.size() > 2 ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)
                                                   : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                const auto t0 = juce::Time::getCurrentTime();
                const auto start = tc->dotPositionForShot();
                auto ev = [&] (juce::Point<float> p, juce::ModifierKeys m)
                {
                    return juce::MouseEvent (src, p, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, tc, tc, t0, start, t0, 1, false);
                };
                tc->mouseDown (ev (start, mods));
                for (int k = 1; k <= 10; ++k) tc->mouseDrag (ev (start + juce::Point<float> (dx, dy) * ((float) k / 10.0f), mods));
                tc->mouseUp (ev (start + juce::Point<float> (dx, dy), juce::ModifierKeys()));
                tc->mouseMove (ev (tc->dotPositionForShot(), juce::ModifierKeys()));
                juce::MidiBuffer m; juce::AudioBuffer<float> b { 2, 512 }; b.clear();
                proc.processBlock (b, m);
                const auto& sp = proc.getModule().getParamSpecs();
                const int ti = proc.getModule().getCurveThresholdParam(), ri = proc.getModule().getCurveRatioParam();
                std::printf ("dragged curve: threshold %s, ratio %s\n", sp[(size_t) ti].text (proc.getModule().getParam (ti)).toRawUTF8(),
                             sp[(size_t) ri].text (proc.getModule().getParam (ri)).toRawUTF8());
            }

    juce::Image img { juce::Image::ARGB, w, h, true };
    {
        juce::Graphics g { img };
        ed->paintEntireComponent (g, true);
    }

    out.deleteFile();
    juce::FileOutputStream os { out };
    if (! os.openedOk()) { std::printf ("cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (img, os)) { std::printf ("png write failed\n"); return 1; }
    std::printf ("%s  %dx%d  %s / %s\n", out.getFileName().toRawUTF8(), w, h, argv[2], argv[3]);
    return 0;
}
