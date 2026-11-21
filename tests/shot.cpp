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
        for (int tick = 0; tick < 12; ++tick)
        {
            for (int blk = 0; blk < 4; ++blk)
            {
                for (int i = 0; i < 512; ++i)
                {
                    double v = 0.0;
                    for (int k = 1; k <= 120; ++k) v += std::sin (ph * k) * formant (130.0 * k) / k;
                    ph += pi2 * 130.0 / 48000.0; hum += pi2 * 50.0 / 48000.0;
                    v = 0.12 * v + 0.03 * std::sin (hum) + 0.002 * (rnd.nextFloat() * 2.0f - 1.0f);
                    for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) v);
                }
                juce::MidiBuffer m;
                proc.processBlock (b, m);
            }
            se->tickForShot();
        }
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
