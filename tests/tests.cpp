// Headless checks: limiter ceiling, latency, bypass alignment, rack state and loudness.
#include "../plugins/Basecamp/BasecampProcessor.h"
#include "../core/SinglePlugin.h"
#include <iostream>

using namespace ps;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}
static juce::AudioBuffer<float> sine (double sr, int n, double freq, float amp, int ch = 2)
{
    juce::AudioBuffer<float> b (ch, n);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i)
        b.setSample (c, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
    return b;
}
template <typename Fn> static void runBlocks (juce::AudioBuffer<float>& b, int block, Fn&& fn)
{
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, b.getNumSamples() - pos);
        juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), b.getNumChannels(), pos, n);
        fn (view);
    }
}
static int firstNonZero (const juce::AudioBuffer<float>& b)
{
    for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1.0e-6f) return i;
    return -1;
}
static float truePeakOf (const juce::AudioBuffer<float>& b)
{
    TruePeak tp; tp.prepare (b.getNumChannels()); float m = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < b.getNumChannels(); ++c)
            m = juce::jmax (m, std::abs (b.getSample (c, i)), (float) tp.push (c, b.getSample (c, i), true));
    return m;
}
static void writeRaw (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    juce::FileOutputStream os (f); os.setPosition (0); os.truncate();
    for (int i = 0; i < b.getNumSamples(); ++i) for (int c = 0; c < b.getNumChannels(); ++c) os.writeFloat (b.getSample (c, i));
}
// Writes a test signal and each module's output as raw float32, for comparing against a
// reference transcription of the JSFX code (tests/jsfx_reference.py).
static void dump (const juce::File& dir)
{
    const double sr = 48000.0; const int n = 48000, block = 256;
    juce::Random rng (1234); juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float env = ((i / 4000) % 3 == 0) ? 1.0f : 0.2f;
        in.setSample (0, i, env * (0.6f * (float) std::sin (0.031 * i) + 0.5f * (float) std::sin (0.41 * i) + 0.3f * (rng.nextFloat() - 0.5f)));
        in.setSample (1, i, env * (0.6f * (float) std::sin (0.027 * i + 1.0) + 0.4f * (float) std::sin (0.83 * i) + 0.3f * (rng.nextFloat() - 0.5f)));
    }
    writeRaw (dir.getChildFile ("in.f32"), in);
    for (int mode = 0; mode < 3; ++mode) for (int tpOn = 0; tpOn < 2; ++tpOn)
    {
        Summit s; s.setParam (Summit::pThreshold, -9.0f); s.setParam (Summit::pCeiling, -1.0f); s.setParam (Summit::pSpeed, 6.0f);
        s.setParam (Summit::pMode, (float) mode); s.setParam (Summit::pTruePeak, (float) tpOn);
        s.prepare (sr, block, 2); auto b = in;
        runBlocks (b, block, [&] (auto& v) { s.process (v); });
        writeRaw (dir.getChildFile ("summit_m" + juce::String (mode) + "_tp" + juce::String (tpOn) + ".f32"), b);
    }
    for (int slope = 0; slope < 6; ++slope)
    {
        Dropoff d; d.setParam (Dropoff::pFreq, 90.0f); d.setParam (Dropoff::pSlope, (float) slope);
        d.prepare (sr, block, 2); auto b = in;
        runBlocks (b, block, [&] (auto& v) { d.process (v); });
        writeRaw (dir.getChildFile ("dropoff_s" + juce::String (slope) + ".f32"), b);
    }
    // Contour: every factory preset, Natural and Linear, with a knob moving mid-file so the
    // smoothing is exercised. Used to prove interface changes leave the audio bit-identical.
    for (int pre = 0; pre < (int) Contour::factoryPresets().size(); ++pre) for (int lin = 0; lin < 2; ++lin)
    {
        Contour c; c.applyFactoryPreset (pre); c.setParam (Contour::pPhase, (float) lin);
        c.prepare (sr, block, 2); auto b = in; int pos = 0;
        runBlocks (b, block, [&] (auto& v) { if ((pos += block) == 24064) c.setParam (Contour::pB3Gain, 4.0f); c.process (v); });
        writeRaw (dir.getChildFile ("contour_p" + juce::String (pre) + "_l" + juce::String (lin) + ".f32"), b);
    }
    // Traverse: every factory preset, with Threshold moved mid-file. Proves the Nov 21 interface
    // work (level readout, Delta) leaves the audio bit-identical with Delta off.
    for (int pre = 0; pre < (int) Traverse::factoryPresets().size(); ++pre)
    {
        Traverse t; t.applyFactoryPreset (pre);
        t.prepare (sr, block, 2); auto b = in; int pos = 0;
        runBlocks (b, block, [&] (auto& v) { if ((pos += block) == 24064) t.setParam (Traverse::pThreshold, -36.0f); t.process (v); });
        writeRaw (dir.getChildFile ("traverse_p" + juce::String (pre) + ".f32"), b);
    }
}
static void snapshot (juce::AudioProcessor& p, const juce::File& f)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorIfNeeded());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::FileOutputStream os (f); os.setPosition (0); os.truncate();
    juce::PNGImageFormat().writeImageToStream (img, os);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
    {   // draws the Basecamp editor to a PNG, for checking layout without a DAW
        BasecampProcessor p; p.setPlayConfigDetails (2, 2, 48000.0, 256); p.prepareToPlay (48000.0, 256);
        snapshot (p, juce::File (argv[2]));
        return 0;
    }
    if (argc == 3 && juce::String (argv[1]) == "--dump") { dump (juce::File (argv[2])); return 0; }
    const double sr = 48000.0; const int block = 256;

    std::cout << "Summit" << std::endl;
    {
        Summit s; s.prepare (sr, block, 2);
        juce::AudioBuffer<float> imp (2, 4096); imp.clear(); imp.setSample (0, 100, 0.25f); imp.setSample (1, 100, 0.25f);
        s.setParam (Summit::pThreshold, 0.0f); s.setParam (Summit::pCeiling, 0.0f);
        runBlocks (imp, block, [&] (auto& v) { s.process (v); });
        check (firstNonZero (imp) - 100 == s.getLatencySamples(), "reported latency matches the measured delay (" + juce::String (s.getLatencySamples()) + " samples)");

        for (int mode = 0; mode < 3; ++mode)
        {
            s.setParam (Summit::pMode, (float) mode);
            s.setParam (Summit::pThreshold, -12.0f); s.setParam (Summit::pCeiling, -1.0f); s.setParam (Summit::pTruePeak, 1.0f);
            s.reset();
            // loud speech-like test: 220 Hz + 3.1 kHz bursts well over the threshold
            auto b = sine (sr, 48000, 220.0, 0.9f); auto hi = sine (sr, 48000, 3100.0, 0.5f);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i)
                b.setSample (c, i, (b.getSample (c, i) + hi.getSample (c, i)) * (((i / 6000) % 2) ? 1.0f : 0.15f));
            runBlocks (b, block, [&] (auto& v) { s.process (v); });
            const float peak = b.getMagnitude (0, 48000), tp = truePeakOf (b);
            check (peak <= dbToGain (-1.0f) * 1.0001f, "mode " + juce::String (mode) + ": sample peak " + juce::String (gainToDb (peak), 2) + " dB stays under the -1 dB ceiling");
            check (gainToDb (tp) <= -1.0f + 0.1f, "mode " + juce::String (mode) + ": true peak " + juce::String (gainToDb (tp), 2) + " dBTP within 0.1 dB of the ceiling");
            check (s.readModuleMeter() > 3.0f, "mode " + juce::String (mode) + ": gain reduction is reported");
        }
    }

    std::cout << "Summit output gain" << std::endl;
    {
        SinglePluginProcessor p (std::make_unique<Summit>());
        auto mp = p.getModuleParams();
        auto set = [&] (int i, float v) { mp[(size_t) i]->setValue (mp[(size_t) i]->getSpec().toNorm (v)); };
        set (Summit::pThreshold, -6.0f); set (Summit::pCeiling, -1.0f); set (Summit::pTruePeak, 1.0f);
        p.prepareToPlay (sr, block);
        juce::MidiBuffer mb;
        auto peakWith = [&] (float outDb)
        {
            *p.outGain = outDb;
            auto b = sine (sr, 48000, 220.0, 0.9f);
            runBlocks (b, block, [&] (auto& v) { p.processBlock (v, mb); });
            return gainToDb (b.getMagnitude (24000, 24000));
        };
        const float up = peakWith (6.0f), down = peakWith (-6.0f);
        check (up <= 0.05f, "output gain +6 over a -1 dB ceiling is held to 0 dBFS (peak " + juce::String (up, 2) + " dB)");
        check (down < -6.5f && down > -7.5f, "trimming down is not capped (peak " + juce::String (down, 2) + " dB)");
    }

    std::cout << "Dropoff" << std::endl;
    {
        Dropoff d; d.prepare (sr, block, 2); d.setParam (Dropoff::pFreq, 80.0f); d.setParam (Dropoff::pSlope, 3.0f);
        auto lo = sine (sr, 48000, 20.0, 0.5f), mid = sine (sr, 48000, 1000.0, 0.5f);
        runBlocks (lo, block, [&] (auto& v) { d.process (v); }); d.reset();
        runBlocks (mid, block, [&] (auto& v) { d.process (v); });
        const float loDb = gainToDb (lo.getMagnitude (0, 24000, 24000) / 0.5f), midDb = gainToDb (mid.getMagnitude (0, 24000, 24000) / 0.5f);
        check (loDb < -40.0f, "20 Hz is cut by " + juce::String (-loDb, 1) + " dB at 80 Hz, 24 dB/oct");
        check (std::abs (midDb) < 0.1f, "1 kHz passes unchanged (" + juce::String (midDb, 2) + " dB)");
    }

    std::cout << "Every module" << std::endl;
    for (auto& info : moduleRegistry())
    {
        auto m = info.create(); m->prepare (sr, block, 2);
        auto b = sine (sr, 48000, 220.0, 0.5f); auto hi = sine (sr, 48000, 3100.0, 0.2f);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i)
            b.setSample (c, i, (b.getSample (c, i) + hi.getSample (c, i)) * (((i / 6000) % 2) ? 1.0f : 0.1f));
        runBlocks (b, block, [&] (auto& v) { m->process (v); });
        bool finite = true; for (int i = 0; i < 48000; ++i) if (! std::isfinite (b.getSample (0, i)) || ! std::isfinite (b.getSample (1, i))) finite = false;
        const float pk = b.getMagnitude (0, 24000, 24000);
        check (finite && pk > 0.01f && pk < 4.0f, info.name + ": runs at default settings (peak " + juce::String (gainToDb (pk), 1) + " dB, "
               + juce::String (m->getNumParams()) + " parameters, latency " + juce::String (m->getLatencySamples()) + ")");
        check (m->getNumParams() <= 50, info.name + ": fits a Basecamp slot (50 parameters)");
    }

    std::cout << "Factory presets" << std::endl;
    for (auto& info : moduleRegistry())
    {
        auto m = info.create();
        const int n = m->getNumFactoryPresets(), np = m->getNumParams();
        const auto& specs = m->getParamSpecs();
        check (n > 0 && m->getFactoryPresetName (0) == "Default", info.name + ": offers " + juce::String (n) + " presets, the first called Default");

        // Put every parameter somewhere it is not, then ask for Default. Whatever the preset
        // covers must land exactly on that parameter's default; anything else must be untouched.
        // A field wired to the wrong parameter shows up as neither.
        std::vector<float> odd ((size_t) np);
        for (int k = 0; k < np; ++k) { odd[(size_t) k] = specs[(size_t) k].fromNorm (0.37f); m->setParam (k, odd[(size_t) k]); }
        m->applyFactoryPreset (0);
        juce::StringArray wrong, untouched;
        for (int k = 0; k < np; ++k)
        {
            const auto& s = specs[(size_t) k]; const float v = m->getParam (k);
            if (std::abs (v - s.def) <= 1.0e-4f) continue;
            (std::abs (v - odd[(size_t) k]) <= 1.0e-4f ? untouched : wrong).add (s.id);
        }
        check (wrong.isEmpty(), info.name + ": Default restores the parameters it covers"
               + (wrong.isEmpty() ? (untouched.isEmpty() ? juce::String()
                                                         : " (leaves " + untouched.joinIntoString (", ") + " alone)")
                                  : "; wrong: " + wrong.joinIntoString (", ")));

        // Every preset must leave the module making sane audio.
        bool allFine = true;
        for (int i = 0; i < n; ++i)
        {
            m->applyFactoryPreset (i); m->prepare (sr, block, 2); m->reset();
            auto b = sine (sr, 12000, 220.0, 0.5f);
            runBlocks (b, block, [&] (auto& v) { m->process (v); });
            const float pk = b.getMagnitude (0, 12000);
            for (int s2 = 0; s2 < 12000; ++s2) if (! std::isfinite (b.getSample (0, s2))) allFine = false;
            if (! (pk > 0.0001f && pk < 4.0f)) allFine = false;
        }
        check (allFine, info.name + ": all " + juce::String (n) + " presets pass audio");
    }

    std::cout << "Slot bypass" << std::endl;
    {
        ModuleSlot slot; slot.setModule (std::make_unique<Summit>()); slot.prepare (sr, block, 2);
        auto in = sine (sr, 9600, 440.0, 0.5f); auto out = in;
        runBlocks (out, block, [&] (auto& v) { slot.process (v, true); });
        const int lat = slot.getLatency(); float err = 0.0f;
        for (int i = 4800; i < 9600; ++i) err = juce::jmax (err, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
        check (err < 1.0e-6f, "bypassed slot outputs the input delayed by exactly its latency");
    }

    std::cout << "Latency follows the controls" << std::endl;
    {
        Dropoff d; d.prepare (sr, block, 2);
        check (d.getLatencySamples() == 0 && d.getMaxLatencySamples() == LinearPhase::latency,
               "Dropoff starts at no latency but reserves room for the linear-phase delay");
        auto tone = sine (sr, block, 1000.0, 0.5f);
        d.setParam (Dropoff::pPhase, 1.0f); d.process (tone);
        check (d.getLatencySamples() == LinearPhase::latency, "switching it to Linear while playing raises its latency to "
               + juce::String (LinearPhase::latency) + " samples");
        d.setParam (Dropoff::pPhase, 0.0f); d.process (tone);
        check (d.getLatencySamples() == 0, "switching back to Natural drops it again");

        Altitude a; a.prepare (sr, block, 2);
        const int at15 = a.getLatencySamples();
        auto voice = sine (sr, block, 300.0, 0.3f);
        a.setParam (Altitude::pLookahead, 50.0f); a.process (voice);
        check (at15 == (int) std::floor (sr * 0.015 + 0.5) && a.getLatencySamples() == (int) std::floor (sr * 0.05 + 0.5),
               "Altitude's look-ahead follows its control, 15 ms to 50 ms, without re-preparing");
    }

    std::cout << "Slot follows a latency change" << std::endl;
    {
        ModuleSlot slot; slot.setModule (std::make_unique<Dropoff>()); slot.prepare (sr, block, 2);
        slot.getModule()->setParam (Dropoff::pPhase, 1.0f);
        auto warm = sine (sr, 24000, 440.0, 0.5f);
        runBlocks (warm, block, [&] (auto& v) { slot.process (v, false); });
        check (slot.getLatency() == LinearPhase::latency, "the slot picks the new latency up from the module");

        auto in = sine (sr, 48000, 440.0, 0.5f); auto out = in;
        runBlocks (out, block, [&] (auto& v) { slot.process (v, true); });
        const int lat = slot.getLatency(); float err = 0.0f;
        for (int i = 24000; i < 48000; ++i) err = juce::jmax (err, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
        check (err < 1.0e-6f, "and bypass still lines up exactly, at the new latency");
    }

    std::cout << "Loudness" << std::endl;
    {
        ShortTermLoudness l; l.prepare (sr, 2);
        auto b = sine (sr, 48000 * 4, 997.0, 0.1f, 1);
        juce::AudioBuffer<float> st (2, b.getNumSamples()); st.clear(); st.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
        l.process (st);
        check (std::abs (l.get() - (-23.0f)) < 0.2f, "-20 dBFS 997 Hz tone in one channel reads " + juce::String (l.get(), 2) + " LUFS (expect -23.0)");
    }

    std::cout << "Listen does not come back on" << std::endl;
    {
        SinglePluginProcessor p (std::make_unique<Scree>());
        auto mp = p.getModuleParams();
        mp[(size_t) Scree::pListen]->setValue (1.0f);
        mp[(size_t) Scree::pSensitivity]->setValue (mp[(size_t) Scree::pSensitivity]->getSpec().toNorm (80.0f));
        juce::MemoryBlock state; p.getStateInformation (state);

        SinglePluginProcessor q (std::make_unique<Scree>());
        q.setStateInformation (state.getData(), (int) state.getSize());
        auto mq = q.getModuleParams();
        check (mq[(size_t) Scree::pListen]->getReal() < 0.5f, "a project saved with Listen on reopens with it off");
        check (std::abs (mq[(size_t) Scree::pSensitivity]->getReal() - 80.0f) < 0.01f, "the settings beside it still come back (Sensitivity 80)");
    }

    std::cout << "Gust (draft)" << std::endl;
    {
        Gust g; g.prepare (sr, block, 2);
        // A steady voice-like tone well above the split must be left completely alone.
        auto tone = sine (sr, 48000, 400.0, 0.3f);
        auto before = tone;
        runBlocks (tone, block, [&] (auto& v) { g.process (v); });
        check (g.getPopCount() == 0, "a steady 400 Hz tone is not mistaken for a pop");

        // A low thump dropped into that tone is.
        g.reset();
        auto mixed = sine (sr, 48000, 400.0, 0.3f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 2000; ++i)
            {
                const double t = i / sr, e = std::exp (-t / 0.015);
                mixed.setSample (c, 24000 + i, mixed.getSample (c, 24000 + i) + (float) (0.5 * e * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t)));
            }
        auto popIn = mixed;
        runBlocks (mixed, block, [&] (auto& v) { g.process (v); });
        float wasThere = 0.0f, isThere = 0.0f;
        for (int i = 24000; i < 26000; ++i) { wasThere = juce::jmax (wasThere, std::abs (popIn.getSample (0, i))); isThere = juce::jmax (isThere, std::abs (mixed.getSample (0, i))); }
        check (g.getPopCount() >= 1 && isThere < wasThere * 0.8f, "a 60 Hz thump in the middle of it is found and turned down");

        // Range 0 leaves the sound alone: the crossover adds back up flat.
        Gust q; q.prepare (sr, block, 2); q.setParam (Gust::pRange, 0.0f);
        auto flatIn = sine (sr, 24000, 500.0, 0.4f);
        auto flatOut = flatIn;
        runBlocks (flatOut, block, [&] (auto& v) { q.process (v); });
        double ea = 0.0, eb = 0.0;
        for (int i = 12000; i < 24000; ++i) { ea += flatIn.getSample (0, i) * flatIn.getSample (0, i); eb += flatOut.getSample (0, i) * flatOut.getSample (0, i); }
        check (std::abs (10.0 * std::log10 (eb / ea)) < 0.05, "with Range at 0 the crossover adds back up flat");
    }

    std::cout << "Glade" << std::endl;
    {
        auto rmsDb = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            double e = 0.0; for (int i = from; i < to; ++i) e += b.getSample (0, i) * b.getSample (0, i);
            return 10.0 * std::log10 (juce::jmax (1.0e-20, e / juce::jmax (1, to - from)));
        };
        Glade g; g.prepare (sr, block, 2);
        const int la = g.getLatencySamples();
        juce::AudioBuffer<float> imp (2, 4000); imp.clear(); imp.setSample (0, 100, 0.5f); imp.setSample (1, 100, 0.5f);
        runBlocks (imp, block, [&] (auto& v) { g.process (v); });
        check (la == (int) std::round (0.020 * sr) && firstNonZero (imp) - 100 == la, "look-ahead is 20 ms and the reported latency matches the delay (" + juce::String (la) + " samples)");

        // A voice-level tone well over the threshold comes out untouched, just delayed.
        g.reset();
        auto loud = sine (sr, 24000, 300.0, 0.14f), loudIn = loud;   // about -20 dBFS RMS
        runBlocks (loud, block, [&] (auto& v) { g.process (v); });
        float worst = 0.0f;
        for (int i = 4000; i < 24000; ++i) worst = juce::jmax (worst, std::abs (loud.getSample (0, i) - loudIn.getSample (0, i - la)));
        check (worst < 1.0e-6f, "speech-level audio passes unchanged (largest difference " + juce::String (worst, 9) + ")");

        // Room tone far under it is turned down by Range and no more.
        for (float range : { 6.0f, 12.0f })
        {
            Glade q; q.prepare (sr, block, 2); q.setParam (Glade::pRange, range);
            auto quiet = sine (sr, 96000, 300.0, 0.0014f), quietIn = quiet;   // about -60 dBFS, 2 s so Close has settled
            runBlocks (quiet, block, [&] (auto& v) { q.process (v); });
            const double cut = rmsDb (quietIn, 60000, 95000) - rmsDb (quiet, 60000 + la, 95000 + la);
            check (std::abs (cut - range) < 0.1, "a -60 dB pause is turned down by Range " + juce::String (range, 0) + " (measured " + juce::String (cut, 2) + " dB)");
        }

        // Look-ahead: a word starting out of silence is already open when it begins.
        {
            Glade q; q.prepare (sr, block, 2);
            juce::AudioBuffer<float> b (2, 48000); b.clear();
            auto word = sine (sr, 24000, 300.0, 0.05f);   // a soft onset, about -29 dBFS
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 24000, word, c, 0, 24000);
            auto in = b;
            runBlocks (b, block, [&] (auto& v) { q.process (v); });
            const double first2ms = rmsDb (b, 24000 + la, 24000 + la + 96) - rmsDb (in, 24000, 24096);
            check (first2ms > -1.0, "the first 2 ms of a word after silence lose under 1 dB (" + juce::String (first2ms, 2) + " dB)");
        }

        // Hold: a quiet word ending just after the voice drops is left alone, then the pause settles.
        {
            Glade q; q.prepare (sr, block, 2);
            auto b = sine (sr, 72000, 300.0, 0.14f);
            for (int c = 0; c < 2; ++c) b.applyGain (c, 24000, 48000, 0.025f);   // drops 32 dB, under the threshold
            auto in = b;
            runBlocks (b, block, [&] (auto& v) { q.process (v); });
            const double tail = rmsDb (b, 24000 + la + 480, 24000 + la + 3840) - rmsDb (in, 24480, 27840);
            const double later = rmsDb (b, 60000 + la, 71000 + la) - rmsDb (in, 60000, 71000);
            check (tail > -0.5, "the 70 ms after the voice drops are held open (" + juce::String (tail, 2) + " dB)");
            check (later < -5.5, "after Hold and Close the pause has settled near Range (" + juce::String (later, 2) + " dB)");
        }

        // Delta plays what is taken off: nothing on speech, and the curve agrees with the audio.
        {
            Glade q; q.prepare (sr, block, 2); q.setParam (Glade::pDelta, 1.0f);
            auto b = sine (sr, 24000, 300.0, 0.14f);
            runBlocks (b, block, [&] (auto& v) { q.process (v); });
            check (rmsDb (b, 6000, 24000) < -100.0, "Delta on speech-level audio is silent (nothing is taken off)");
            check (std::abs (q.curveOutputDb (-20.0f) + 20.0f) < 1.0e-4f && std::abs (q.curveOutputDb (-60.0f) + 66.0f) < 1.0e-3f,
                   "the drawn curve: -20 dB in stays -20, -60 dB in comes out at -66 (Range 6)");
            check (q.getCurveTitle() == "Expander curve", "the curve card is titled as an expander");
        }
    }

    std::cout << "Serac" << std::endl;
    {
        auto reduction = [&] (Serac& s, double freq, float amp, int samples = 24000)
        {
            s.reset();
            auto b = sine (sr, samples, freq, amp);
            runBlocks (b, block, [&] (auto& v) { s.process (v); });
            return s.readModuleMeter();
        };
        Serac s; s.prepare (sr, block, 2);

        const float onVowel = reduction (s, 300.0, 0.3f), onEss = reduction (s, 8000.0, 0.3f);
        check (onVowel < 0.5f, "a 300 Hz vowel is left alone (" + juce::String (onVowel, 2) + " dB)");
        check (onEss > 5.5f && onEss <= 6.01f, "8 kHz sibilance is pulled down to the 6 dB Range (" + juce::String (onEss, 2) + " dB)");

        // The point of the design: the same setting does the same thing at any level.
        const float loud = reduction (s, 8000.0, 0.5f), quiet = reduction (s, 8000.0, 0.005f);
        check (std::abs (loud - quiet) < 0.2f, "and by the same amount 40 dB quieter (" + juce::String (loud, 2)
               + " dB against " + juce::String (quiet, 2) + " dB)");

        s.setParam (Serac::pRange, 12.0f);
        const float wide = reduction (s, 8000.0, 0.3f);
        s.setParam (Serac::pRange, 0.0f);
        const float none = reduction (s, 8000.0, 0.3f);
        check (wide > 11.0f && wide <= 12.01f && none < 0.01f, "Range is the ceiling on it: 12 gives "
               + juce::String (wide, 2) + " dB, 0 gives none");

        // Range 0 must leave the sound alone: the two halves of the crossover add back up flat.
        // Measured as RMS, not peak: at 9 kHz there are only five samples per cycle, so the
        // largest sample can sit 0.1 dB under the real crest and a peak reading would lie.
        auto rmsDb = [] (const juce::AudioBuffer<float>& b, int start, int num)
        {
            double a = 0.0;
            for (int i = 0; i < num; ++i) { const double v = b.getSample (0, start + i); a += v * v; }
            return 20.0 * std::log10 (std::sqrt (a / num) + 1.0e-30);
        };
        s.setParam (Serac::pRange, 0.0f);
        float worst = 0.0f;
        for (double f : { 200.0, 1000.0, 5000.0, 9000.0 })
        {
            s.reset();
            auto b = sine (sr, 24000, f, 0.4f);
            const float before = (float) rmsDb (b, 12000, 12000);
            runBlocks (b, block, [&] (auto& v) { s.process (v); });
            worst = juce::jmax (worst, std::abs ((float) rmsDb (b, 12000, 12000) - before));
        }
        check (worst < 0.05f, "with Range at 0 the crossover adds back up flat from 200 Hz to 9 kHz (worst "
               + juce::String (worst, 3) + " dB)");

        s.setParam (Serac::pRange, 6.0f); s.setParam (Serac::pListen, 1.0f);
        s.reset();
        auto v = sine (sr, 24000, 300.0, 0.3f);
        runBlocks (v, block, [&] (auto& w) { s.process (w); });
        check (gainToDb (v.getMagnitude (12000, 12000)) < -60.0f, "Listen on a vowel is near silence: nothing is being taken out");
        s.setParam (Serac::pListen, 0.0f);

        // Learn: a voice-like tone with sibilance sitting between 7 and 10 kHz.
        Serac l; l.prepare (sr, block, 2);
        l.setParam (Serac::pLearn, 1.0f);
        auto mix = sine (sr, (int) sr * 2, 200.0, 0.2f);
        for (double f : { 7000.0, 8000.0, 9000.0, 10000.0 })
        {
            auto h = sine (sr, (int) sr * 2, f, 0.2f);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < mix.getNumSamples(); ++i)
                mix.setSample (c, i, mix.getSample (c, i) + h.getSample (c, i));
        }
        runBlocks (mix, block, [&] (auto& w) { l.process (w); });
        l.setParam (Serac::pLearn, 0.0f);
        for (int k = 0; k < 3; ++k) { juce::AudioBuffer<float> q (2, block); q.clear(); l.process (q); }
        const float found = l.getParam (Serac::pFreq);
        check (found > 6000.0f && found < 8000.0f && ! l.isLearning(),
               "Learn puts Frequency at the bottom of the sibilance it heard: " + juce::String (found, 0) + " Hz");
    }

    std::cout << "Clearing learns a noise profile and keeps it" << std::endl;
    {
        SinglePluginProcessor p (std::make_unique<Clearing>());
        auto& c = static_cast<Clearing&> (p.getModule());
        p.prepareToPlay (sr, block);
        juce::MidiBuffer mb; juce::Random rng (1);
        p.getModuleParams()[(size_t) Clearing::pLearn]->setValue (1.0f);
        for (int k = 0; k < (int) (sr * 10.0) / block; ++k)   // ten seconds of steady noise
        {
            juce::AudioBuffer<float> n (2, block);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) n.setSample (ch, i, 0.01f * (rng.nextFloat() * 2.0f - 1.0f));
            p.processBlock (n, mb);
        }
        check (c.hasProfile() && ! c.isLearning(), "holding Learn over steady noise captures a profile and stops on its own after eight seconds");

        juce::MemoryBlock state; p.getStateInformation (state);
        SinglePluginProcessor q (std::make_unique<Clearing>());
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (static_cast<Clearing&> (q.getModule()).hasProfile(), "the profile comes back when the project is reopened");
        check (q.getModuleParams()[(size_t) Clearing::pLearn]->getReal() < 0.5f, "while Learn itself does not come back on");
    }

    std::cout << "Integrated loudness" << std::endl;
    {
        auto oneChannel = [&] (float amp, int seconds)
        {
            auto t = sine (sr, (int) sr * seconds, 997.0, amp, 1);
            juce::AudioBuffer<float> b (2, t.getNumSamples()); b.clear();
            b.copyFrom (0, 0, t, 0, 0, t.getNumSamples());
            return b;
        };
        IntegratedLoudness l; l.prepare (sr, 2);
        auto tone = oneChannel (0.1f, 10);
        runBlocks (tone, block, [&] (auto& v) { l.process (v); });
        const float steady = l.get();
        check (std::abs (steady - (-23.0f)) < 0.2f, "-20 dBFS 997 Hz tone reads " + juce::String (steady, 2) + " LUFS (expect -23.0)");

        // The blocks straddling the end of the tone are above the relative gate and do count,
        // so the figure dips slightly; the silence itself must then add nothing at all.
        juce::AudioBuffer<float> quietRoom (2, (int) sr * 10); quietRoom.clear();
        runBlocks (quietRoom, block, [&] (auto& v) { l.process (v); });
        const float afterSilence = l.get();
        runBlocks (quietRoom, block, [&] (auto& v) { l.process (v); });
        check (l.get() == afterSilence && std::abs (afterSilence - steady) < 0.2f,
               "silence is gated out: " + juce::String (afterSilence, 2) + " LUFS, and ten seconds more changes nothing");

        IntegratedLoudness r; r.prepare (sr, 2);
        auto loud = oneChannel (0.1f, 10), faint = oneChannel (0.01f, 10);
        runBlocks (loud, block, [&] (auto& v) { r.process (v); });
        runBlocks (faint, block, [&] (auto& v) { r.process (v); });
        check (std::abs (r.get() - (-23.0f)) < 0.2f, "a passage 20 dB quieter falls under the relative gate ("
               + juce::String (r.get(), 2) + " LUFS; without gating it would read about -26)");
    }

    std::cout << "Basecamp" << std::endl;
    {
        BasecampProcessor p; p.setPlayConfigDetails (2, 2, sr, block); p.prepareToPlay (sr, block);
        juce::MidiBuffer midi;
        check (p.getSlotModuleId (0) == "dropoff" && p.getSlotModuleId (1) == "summit", "default chain is Dropoff then Summit");
        check (p.getParameters().size() == 2 + 12 * 50, "host sees a fixed list of " + juce::String (p.getParameters().size()) + " parameters");
        check (p.getTotalLatency() == p.getLatencySamples() && p.getTotalLatency() > 0, "rack reports total latency to the host (" + juce::String (p.getTotalLatency()) + " samples)");

        // same module twice, parameters independent
        p.setSlotModule (2, "summit");
        auto s1 = p.getSlotParams (1), s2 = p.getSlotParams (2);
        s1[0]->setRealNotifyingHost (-10.0f); s2[0]->setRealNotifyingHost (-2.0f);
        check (std::abs (s1[0]->getReal() + 10.0f) < 0.01f && std::abs (s2[0]->getReal() + 2.0f) < 0.01f, "two Summits keep separate settings");
        check (s2[0]->getName (64) == "S03 Summit: Threshold", "slot parameter is named for the host: " + s2[0]->getName (64));
        const int twoLat = p.getTotalLatency();

        // all bypassed = input delayed by total latency
        for (int s = 0; s < 3; ++s) *p.getSlotHostParams (s).bypass = true;
        auto in = sine (sr, 19200, 440.0, 0.5f); auto out = in;
        runBlocks (out, block, [&] (auto& v) { p.processBlock (v, midi); });
        float err = 0.0f;
        for (int i = 9600; i < 19200; ++i) err = juce::jmax (err, std::abs (out.getSample (0, i) - in.getSample (0, i - twoLat)));
        check (err < 1.0e-6f, "all slots bypassed: output is the input delayed by the reported latency");
        for (int s = 0; s < 3; ++s) *p.getSlotHostParams (s).bypass = false;

        // reorder
        p.moveSlot (2, 0);
        check (p.getSlotModuleId (0) == "summit" && p.getSlotModuleId (2) == "dropoff", "moving a slot swaps the modules");
        check (std::abs (p.getSlotParams (0)[0]->getReal() + 2.0f) < 0.01f, "settings travel with the module when it moves");

        // state round trip, plus an unknown module
        auto tree = p.saveRack();
        juce::ValueTree ghost ("SLOT"); ghost.setProperty ("index", 5, nullptr); ghost.setProperty ("module", "notbuiltyet", nullptr);
        ghost.setProperty ("moduleVersion", 3, nullptr);
        juce::ValueTree gp ("PARAM"); gp.setProperty ("id", "target", nullptr); gp.setProperty ("value", -18.0f, nullptr); ghost.appendChild (gp, nullptr);
        tree.appendChild (ghost, nullptr);
        juce::MemoryBlock mb; { auto xml = tree.createXml(); juce::AudioProcessor::copyXmlToBinary (*xml, mb); }

        BasecampProcessor q; q.setPlayConfigDetails (2, 2, sr, block); q.prepareToPlay (sr, block);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getSlotModuleId (0) == "summit" && q.getSlotModuleId (1) == "summit" && q.getSlotModuleId (2) == "dropoff", "saved chain reloads in the same order");
        check (std::abs (q.getSlotParams (0)[0]->getReal() + 2.0f) < 0.01f && std::abs (q.getSlotParams (1)[0]->getReal() + 10.0f) < 0.01f, "saved settings reload");
        check (q.isSlotPlaceholder (5) && q.getSlotName (5) == "Missing: notbuiltyet", "unknown module loads as a placeholder");
        auto again = q.saveRack().getChildWithProperty ("index", 5);
        check (again.getProperty ("module").toString() == "notbuiltyet" && (float) again.getChildWithProperty ("id", "target").getProperty ("value") == -18.0f,
               "placeholder keeps the unknown module's data when saved again");
        auto probe = sine (sr, 4800, 440.0, 0.2f);
        runBlocks (probe, block, [&] (auto& v) { q.processBlock (v, midi); });
        check (probe.getMagnitude (0, 2400, 2400) > 0.1f, "audio still passes with a placeholder in the chain");

        // solo and clear
        q.setSlotModule (0, {}); q.setSlotModule (1, {});
        check (q.getSlotModuleId (0).isEmpty() && q.getTotalLatency() == 0, "clearing the Summits removes their latency");
        check (q.getSlotParams (0).empty() && q.getSlotHostParams (0).pool[0]->getName (64) == "S01 unused 01", "cleared slot unmaps its parameters");
    }


    // ---- The suite interface -------------------------------------------------------------
    {
        // Beginner mode shows a plain-language line under every control. A module whose
        // parameters carry no caption renders an empty row, which is how the first Beginner
        // render came out, so this is checked for the whole suite rather than per plugin.
        juce::StringArray missing;
        for (auto& info : moduleRegistry())
        {
            auto m = info.create();
            for (auto& spec : m->getParamSpecs())
                if (spec.hint.isEmpty()) missing.add (info.name + "." + spec.id);
        }
        check (missing.isEmpty(), "every control has a plain-language caption"
                                  + (missing.isEmpty() ? juce::String() : " (missing: " + missing.joinIntoString (", ") + ")"));

        // Captions are for someone who has not used the tool: they should say what happens to
        // the sound, not name the mechanism, and they have to fit the row.
        int tooLong = 0;
        for (auto& info : moduleRegistry())
        {
            auto m = info.create();
            for (auto& spec : m->getParamSpecs())
                if (spec.hint.length() > 130) ++tooLong;
        }
        check (tooLong == 0, "captions are short enough to fit one line");

        // The three faces must come from the binary, not from whatever is installed. If a load
        // silently failed, type::font would fall back to the host default and the interface
        // would still draw - just in the wrong face, on machines Sean cannot see.
        {
            struct { juce::Typeface::Ptr tf; const char* want; } faces[] {
                { juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, (size_t) BinaryData::MontserratSemiBold_ttfSize), "Montserrat" },
                { juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeMedium_ttf,      (size_t) BinaryData::ManropeMedium_ttfSize),      "Manrope" },
                { juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeSemiBold_ttf,    (size_t) BinaryData::ManropeSemiBold_ttfSize),    "Manrope" },
            };
            bool allLoaded = true;
            for (auto& f : faces)
                allLoaded = allLoaded && f.tf != nullptr && f.tf->getName().contains (f.want);
            check (allLoaded, "all three faces load from the binary, not from installed fonts");
            check (type::titleFont (28.0f).getTypefacePtr() != nullptr
                       && type::titleFont (28.0f).getTypefacePtr()->getName().contains ("Montserrat"),
                   "the plugin title is Montserrat");
            check (type::font (17.0f).getTypefacePtr() != nullptr
                       && type::font (17.0f).getTypefacePtr()->getName().contains ("Manrope"),
                   "labels are Manrope");
        }

        // Vintage is a bonus look: never saved, never the opening look.
        auto& ss = SuiteSettings::get();
        const auto before = ss.look();
        ss.setLook (Theme::Id::Vintage);
        check (ss.look() != Theme::Id::Vintage, "Vintage is never saved as the suite Look");
        ss.setLook (Theme::Id::Graphite);
        check (ss.look() == Theme::Id::Graphite, "a listed Look is saved");
        ss.setLook (before);

        // Amber warnings have to stay visible in every Look, so the warning colour must keep
        // real contrast against the surface it is drawn on. 3:1 is the floor for large text.
        auto luminance = [] (juce::Colour c)
        {
            auto ch = [] (double v) { v /= 255.0; return v <= 0.03928 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
        };
        auto contrast = [&] (juce::Colour a, juce::Colour b)
        {
            const double la = luminance (a), lb = luminance (b);
            return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
        };
        double worstWarn = 1.0e9, worstText = 1.0e9;
        for (auto id : { Theme::Id::Default_, Theme::Id::Vaporwave, Theme::Id::Graphite, Theme::Id::Vintage })
        {
            const auto t = themes::byId (id);
            worstWarn = juce::jmin (worstWarn, contrast (t.warn, t.panel));
            worstText = juce::jmin (worstText, contrast (t.text, t.panel));
            // Vintage fills its controls with silver, so its face text is dark; the others are
            // light on a dark face. Either way the pair has to be readable.
            worstText = juce::jmin (worstText, contrast (faceText (t), t.panelHi));
        }
        check (worstWarn >= 3.0, "amber warnings stay readable in every Look (worst " + juce::String (worstWarn, 1) + ":1)");
        check (worstText >= 4.5, "text stays readable on every Look's surfaces (worst " + juce::String (worstText, 1) + ":1)");
    }



    std::cout << "Contour curve dots" << std::endl;
    {
        // The curve follows the controls even when no audio has run since they moved, which is
        // what happens when a dot is dragged with the transport stopped.
        Contour c; c.prepare (sr, block, 2);
        c.setParam (Contour::pB3Freq, 3200.0f); c.setParam (Contour::pB3Gain, 6.0f);
        const float at = c.responseDbAt (3200.0f);
        check (std::abs (at - 6.0f) < 0.05f, "a +6 dB band at 3.2 kHz draws +" + juce::String (at, 2) + " dB with no audio processed");

        // Every dot points at the right kind of control.
        bool ok = true;
        const auto& sp = c.getParamSpecs();
        for (const auto& h : c.getCurveHandles())
        {
            ok &= h.freq >= 0 && sp[(size_t) h.freq].unit == "Hz";
            ok &= h.gain < 0 || sp[(size_t) h.gain].unit == "dB";
            ok &= h.q < 0 || sp[(size_t) h.q].name.endsWith ("Q");
            ok &= h.on >= 0 && sp[(size_t) h.on].type == ParamSpec::Type::Bool;
        }
        check (ok && c.getCurveHandles().size() == 5, "five dots, each moving its band's frequency, gain, width and on switch");
    }


    std::cout << "Contour Solo and Delta" << std::endl;
    {
        auto levelThrough = [&] (int listen, double hz, std::function<void (Contour&)> setup = {})
        {
            Contour c; c.prepare (sr, block, 2); if (setup) setup (c);
            c.setParam (Contour::pSolo, (float) listen);
            auto t = sine (sr, 24000, hz, 0.5f);
            runBlocks (t, block, [&] (auto& v) { c.process (v); });
            return gainToDb (t.getMagnitude (0, 12000, 12000) / 0.5f);
        };
        // Band 3 at 3.2 kHz, width 1, with its gain cut: Listen ignores the gain.
        auto cut3 = [] (Contour& c) { c.setParam (Contour::pB3Gain, -12.0f); };
        const float on = levelThrough (3, 3200.0, cut3), off = levelThrough (3, 200.0, cut3);
        check (std::abs (on) < 0.2f && off < -20.0f, "Solo Band 3: 3.2 kHz plays at " + juce::String (on, 2)
               + " dB (its -12 dB cut ignored), 200 Hz at " + juce::String (off, 1) + " dB");
        auto hp = [] (Contour& c) { c.setParam (Contour::pHpSlope, 3.0f); };
        const float lo = levelThrough (1, 20.0, hp), hi = levelThrough (1, 1000.0, hp);
        check (std::abs (lo) < 0.5f && hi < -40.0f, "Solo the high-pass plays what it removes: 20 Hz at "
               + juce::String (lo, 2) + " dB, 1 kHz at " + juce::String (hi, 1) + " dB");
        const float air = levelThrough (5, 16000.0), body = levelThrough (5, 1000.0);
        check (air > -1.0f && body < -30.0f, "Solo the shelf: 16 kHz at " + juce::String (air, 2) + " dB, 1 kHz at " + juce::String (body, 1) + " dB");

        // Linear mode: what is heard is the same signal, delayed to match the EQ's latency.
        auto run = [&] (bool linear)
        {
            Contour c; c.setParam (Contour::pPhase, linear ? 1.0f : 0.0f); c.prepare (sr, block, 2);
            c.setParam (Contour::pSolo, 2.0f);
            juce::AudioBuffer<float> b (2, 24000); juce::Random r (7);
            for (int i = 0; i < 24000; ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, 0.3f * (r.nextFloat() - 0.5f));
            runBlocks (b, block, [&] (auto& v) { c.process (v); });
            return b;
        };
        const auto nat = run (false), lin = run (true);
        float worst = 0.0f;
        for (int i = 4000; i < 24000 - LinearPhase::latency; ++i)
            worst = juce::jmax (worst, std::abs (nat.getSample (0, i) - lin.getSample (0, i + LinearPhase::latency)));
        check (worst < 1.0e-6f, "in Linear mode Solo is the same sound " + juce::String (LinearPhase::latency) + " samples later, lined up with the EQ");

        // Switching band, and switching off, never jumps.
        {
            Contour c; c.prepare (sr, block, 2); c.setParam (Contour::pSolo, 3.0f);
            Contour ref; ref.prepare (sr, block, 2);
            auto t = sine (sr, 48000, 1000.0, 0.5f), u = t; int pos = 0;
            float jump = 0.0f, prev = 0.0f;
            runBlocks (t, block, [&] (auto& v)
            {
                pos += block;
                if (pos == 12032) c.setParam (Contour::pSolo, 2.0f);
                if (pos == 24064) c.setParam (Contour::pSolo, 0.0f);
                c.process (v);
                for (int i = 0; i < v.getNumSamples(); ++i) { const float x = v.getSample (0, i); if (pos > 2000) jump = juce::jmax (jump, std::abs (x - prev)); prev = x; }
            });
            runBlocks (u, block, [&] (auto& v) { ref.process (v); });
            float after = 0.0f;
            for (int i = 26000; i < 48000; ++i) after = juce::jmax (after, std::abs (t.getSample (0, i) - u.getSample (0, i)));
            check (jump < 0.075f, "switching Band 3 to Band 2 to off steps no more than the tone itself does (" + juce::String (jump, 3) + ", tone 0.065)");
            check (after == 0.0f, "once Solo is off the output is exactly the EQ's again");
        }
        // Delta: output minus input. Input + delta must give back exactly what the EQ outputs,
        // in both phase modes, and a flat EQ must have nothing to say.
        auto noise = [&]
        {
            juce::AudioBuffer<float> b (2, 24000); juce::Random r (11);
            for (int i = 0; i < 24000; ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, 0.3f * (r.nextFloat() - 0.5f));
            return b;
        };
        for (int lin = 0; lin < 2; ++lin)
        {
            auto runWith = [&] (bool delta, bool flat)
            {
                Contour c; c.setParam (Contour::pPhase, (float) lin);
                if (! flat) c.applyFactoryPreset (3);   // Remove Mud: -3 dB at 250 Hz, -1.5 dB at 400 Hz, high-pass 80 Hz
                else c.setParam (Contour::pHpOn, 0.0f);
                c.setParam (Contour::pDelta, delta ? 1.0f : 0.0f);
                c.prepare (sr, block, 2);
                auto b = noise();
                runBlocks (b, block, [&] (auto& v) { c.process (v); });
                return b;
            };
            const auto in = noise(), eq = runWith (false, false), delta = runWith (true, false), flat = runWith (true, true);
            const int lat = lin ? LinearPhase::latency : 0;
            float worst = 0.0f, flatPeak = 0.0f;
            for (int i = 4000 + lat; i < 24000; ++i)
            {
                worst = juce::jmax (worst, std::abs (in.getSample (0, i - lat) + delta.getSample (0, i) - eq.getSample (0, i)));
                flatPeak = juce::jmax (flatPeak, std::abs (flat.getSample (0, i)));
            }
            const juce::String m = lin ? "Linear" : "Natural";
            check (worst < 1.0e-6f, m + ": input plus Delta equals the EQ's output (worst " + juce::String (worst, 9) + ")");
            check (flatPeak < 1.0e-6f, m + ": with every band flat, Delta is silent");
        }
        {
            Contour c; c.prepare (sr, block, 2); c.setParam (Contour::pDelta, 1.0f); c.setParam (Contour::pSolo, 3.0f);
            auto t = sine (sr, 24000, 3200.0, 0.5f);
            runBlocks (t, block, [&] (auto& v) { c.process (v); });
            check (std::abs (gainToDb (t.getMagnitude (0, 12000, 12000) / 0.5f)) < 0.2f && c.getAuditionWarning().startsWith ("Soloing"),
                   "with Solo and Delta both on, Solo wins (and the warning says so)");
        }

        Contour c;
        check (c.getParamSpecs()[(size_t) Contour::pSolo].transient && c.getParamSpecs()[(size_t) Contour::pDelta].transient, "Solo and Delta are always off when a project opens");
    }

    std::cout << "Traverse level readout, curve dot and Delta" << std::endl;
    {
        // The detector level the editor shows: a sine's RMS is 3 dB under its peak.
        {
            Traverse t; t.prepare (sr, block, 2);
            auto b = sine (sr, 24000, 1000.0, juce::Decibels::decibelsToGain (-10.0f));
            float inDb = 0.0f, outDb = 0.0f;
            int pos = 0;   // read once the compressor has settled; the editor reads every 33 ms
            runBlocks (b, block, [&] (auto& v) { if ((pos += block) == 12032) t.readCurveLevels (inDb, outDb); t.process (v); });
            const bool got = t.readCurveLevels (inDb, outDb);
            check (got && std::abs (inDb - -13.0f) < 0.3f, "level readout: a -10 dBFS sine reads " + juce::String (inDb, 2) + " dB RMS (expected -13)");
            // Threshold -18, ratio 2, knee 6: 5 dB over, so about 2.5 dB off the output.
            check (std::abs ((inDb - outDb) - 2.5f) < 0.4f, "output readout sits " + juce::String (inDb - outDb, 2) + " dB under the input (curve says 2.5)");
            t.readCurveLevels (inDb, outDb);
            check (inDb < -100.0f, "reading the levels clears them");
        }
        check (Traverse().getCurveThresholdParam() == Traverse::pThreshold && Traverse().getCurveRatioParam() == Traverse::pRatio,
               "the curve's dot moves Threshold and Ratio");

        // Delta: the input minus the squeezed signal (before Makeup). So the compressor's own
        // output with Makeup at 0 plus Delta gives back the input, and with nothing over the
        // threshold Delta is silent.
        auto voice = [&] (float gain)
        {
            juce::AudioBuffer<float> b (2, 48000); juce::Random r (5);
            for (int i = 0; i < 48000; ++i)
            {
                const float env = ((i / 6000) % 2 == 0) ? 1.0f : 0.15f;
                for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, gain * env * (0.5f * (float) std::sin (0.05 * i + ch) + 0.2f * (r.nextFloat() - 0.5f)));
            }
            return b;
        };
        auto runT = [&] (juce::AudioBuffer<float> b, bool delta, float makeup, float mix)
        {
            Traverse t; t.applyFactoryPreset (2);   // Radio Voice
            t.setParam (Traverse::pMakeup, makeup); t.setParam (Traverse::pMix, mix);
            t.setParam (Traverse::pDelta, delta ? 1.0f : 0.0f);
            t.prepare (sr, block, 2);
            runBlocks (b, block, [&] (auto& v) { t.process (v); });
            return b;
        };
        for (float mix : { 100.0f, 50.0f })
        {
            const auto in = voice (1.0f), comp = runT (in, false, 0.0f, mix), del = runT (in, true, 0.0f, mix);
            float worst = 0.0f, delPeak = 0.0f;
            for (int i = 2000; i < 48000; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    worst = juce::jmax (worst, std::abs (comp.getSample (ch, i) + del.getSample (ch, i) - in.getSample (ch, i)));
                    delPeak = juce::jmax (delPeak, std::abs (del.getSample (ch, i)));
                }
            check (worst < 1.0e-6f && delPeak > 0.05f, "Mix " + juce::String ((int) mix) + "%: compressed plus Delta gives back the input (worst "
                   + juce::String (worst, 9) + "), and Delta is not empty");
        }
        {
            // Makeup does not leak into Delta: same Delta at +6 dB Makeup as at 0.
            const auto in = voice (1.0f), a = runT (in, true, 0.0f, 100.0f), b2 = runT (in, true, 6.0f, 100.0f);
            float worst = 0.0f;
            for (int i = 2000; i < 48000; ++i) worst = juce::jmax (worst, std::abs (a.getSample (0, i) - b2.getSample (0, i)));
            check (worst < 1.0e-6f, "Delta ignores Makeup: it is what the squeeze takes off, not the level boost");
        }
        {
            const auto quiet = runT (voice (0.005f), true, 0.0f, 100.0f);
            check (quiet.getMagnitude (2000, 46000) < 1.0e-6f, "with the voice under the threshold, Delta is silent");
        }
        {
            // Switching Delta on and off mid-take fades, and afterwards the output is exactly normal.
            Traverse t; t.applyFactoryPreset (2); t.prepare (sr, block, 2);
            Traverse ref; ref.applyFactoryPreset (2); ref.prepare (sr, block, 2);
            auto b = sine (sr, 48000, 200.0, 0.5f), u = b; int pos = 0;
            float jump = 0.0f, prev = 0.0f;
            runBlocks (b, block, [&] (auto& v)
            {
                pos += block;
                if (pos == 12032) t.setParam (Traverse::pDelta, 1.0f);
                if (pos == 24064) t.setParam (Traverse::pDelta, 0.0f);
                t.process (v);
                for (int i = 0; i < v.getNumSamples(); ++i) { const float x = v.getSample (0, i); if (pos > 4000) jump = juce::jmax (jump, std::abs (x - prev)); prev = x; }
            });
            runBlocks (u, block, [&] (auto& v) { ref.process (v); });
            float after = 0.0f;
            for (int i = 26000; i < 48000; ++i) after = juce::jmax (after, std::abs (b.getSample (0, i) - u.getSample (0, i)));
            check (jump < 0.03f, "switching Delta on and off steps no more than the tone does (" + juce::String (jump, 4) + ")");
            check (after == 0.0f, "once Delta is off the output is exactly the compressor's again");
        }
        Traverse t;
        check (t.getParamSpecs()[(size_t) Traverse::pDelta].transient, "Delta is always off when a project opens");
        t.setParam (Traverse::pDelta, 1.0f);
        check (t.getAuditionWarning().startsWith ("Delta"), "with Delta on, the amber warning says so");
    }

    std::cout << "Spectrum display" << std::endl;
    {
        auto bandOf = [] (double hz)
        {
            for (int k = 0; k < SpectrumAnalyser::numBands; ++k)
                if (hz < SpectrumAnalyser::edgeHz (k + 1)) return k;
            return SpectrumAnalyser::numBands - 1;
        };
        auto read = [&] (const juce::AudioBuffer<float>& b)
        {
            SpectrumTap tap; tap.push (b, b.getNumChannels());
            SpectrumAnalyser a;
            for (int i = 0; i < 7; ++i) a.update (tap, sr, 30.0);   // settles within the hold time
            return a.getBands();
        };

        // Calibration: a full-scale sine reads about 0 dB in the band that holds it. A Hann
        // window between FFT steps can read up to 1.4 dB low, so allow that.
        const auto full = read (sine (sr, 8192, 1000.0, 1.0f));
        const float at1k = full[(size_t) bandOf (1000.0)];
        check (at1k > -1.6f && at1k < 0.2f, "a full-scale 1 kHz sine reads " + juce::String (at1k, 2) + " dB in its band");

        // The Nov 15 bug: loud 200 Hz energy smeared into everything below 100 Hz at 2048
        // points. At 8192 the bottom of the graph stays clear.
        const auto loud200 = read (sine (sr, 8192, 200.0, 0.5f));
        const float at40 = loud200[(size_t) bandOf (40.0)] - loud200[(size_t) bandOf (200.0)];
        check (at40 < -60.0f, "a loud 200 Hz tone leaves 40 Hz " + juce::String (-at40, 1) + " dB below it");

        // Through Contour's slot: a 40 Hz tone under an 80 Hz, 24 dB/oct high-pass shows the
        // input at full height and the output pulled down, the way the high-pass sounds.
        Contour c; c.setParam (Contour::pHpSlope, 3.0f);
        ModuleSlot slot; slot.tapSpectrum = true; slot.prepare (sr, block, 2);
        slot.setModule (std::make_unique<Contour> ());
        slot.getModule()->setParam (Contour::pHpSlope, 3.0f);
        auto tone = sine (sr, 48000, 40.0, 0.5f);
        runBlocks (tone, block, [&] (auto& v) { slot.process (v, false); });
        SpectrumAnalyser ai, ao;
        for (int i = 0; i < 7; ++i) { ai.update (slot.preTap, sr, 30.0); ao.update (slot.postTap, sr, 30.0); }
        const float drop = ao.getBands()[(size_t) bandOf (40.0)] - ai.getBands()[(size_t) bandOf (40.0)];
        check (drop < -20.0f && drop > -30.0f, "40 Hz under the 80 Hz, 24 dB/oct high-pass shows " + juce::String (-drop, 1)
               + " dB lower going out than coming in (curve says " + juce::String (-slot.getModule()->responseDbAt (40.0f), 1) + ")");

        // Stopped transport: nothing new arrives and the display sinks rather than freezing.
        for (int i = 0; i < 300; ++i) ao.update (slot.postTap, sr, 30.0);
        check (ao.getBands()[(size_t) bandOf (1000.0)] <= SpectrumAnalyser::floorDb + 0.1f, "with playback stopped the display falls to the floor");

        // Basecamp's slots do not tap (no spectrum drawn there yet), so they do no extra work.
        ModuleSlot rack; rack.prepare (sr, block, 2); rack.setModule (std::make_unique<Contour> ());
        auto t2 = sine (sr, 4096, 1000.0, 0.5f);
        runBlocks (t2, block, [&] (auto& v) { rack.process (v, false); });
        check (rack.postTap.samplesWritten() == 0, "a slot that is not asked to tap copies nothing");
    }

    check (juce::String (suiteVersion()) == "0.5.1", "the suite version is 0.5.1 (Sean, Nov 21)");

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
