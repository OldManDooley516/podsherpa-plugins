// Headless checks: limiter ceiling, latency, bypass alignment, rack state and loudness.
#include "../plugins/Basecamp/BasecampProcessor.h"
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
}
static void snapshot (juce::AudioProcessor& p, const juce::File& f)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
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

    std::cout << "Slot bypass" << std::endl;
    {
        ModuleSlot slot; slot.setModule (std::make_unique<Summit>()); slot.prepare (sr, block, 2);
        auto in = sine (sr, 9600, 440.0, 0.5f); auto out = in;
        runBlocks (out, block, [&] (auto& v) { slot.process (v, true); });
        const int lat = slot.getLatency(); float err = 0.0f;
        for (int i = 4800; i < 9600; ++i) err = juce::jmax (err, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
        check (err < 1.0e-6f, "bypassed slot outputs the input delayed by exactly its latency");
    }

    std::cout << "Loudness" << std::endl;
    {
        ShortTermLoudness l; l.prepare (sr, 2);
        auto b = sine (sr, 48000 * 4, 997.0, 0.1f, 1);
        juce::AudioBuffer<float> st (2, b.getNumSamples()); st.clear(); st.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
        l.process (st);
        check (std::abs (l.get() - (-23.0f)) < 0.2f, "-20 dBFS 997 Hz tone in one channel reads " + juce::String (l.get(), 2) + " LUFS (expect -23.0)");
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
        juce::ValueTree ghost ("SLOT"); ghost.setProperty ("index", 5, nullptr); ghost.setProperty ("module", "altitude", nullptr);
        ghost.setProperty ("moduleVersion", 3, nullptr);
        juce::ValueTree gp ("PARAM"); gp.setProperty ("id", "target", nullptr); gp.setProperty ("value", -18.0f, nullptr); ghost.appendChild (gp, nullptr);
        tree.appendChild (ghost, nullptr);
        juce::MemoryBlock mb; { auto xml = tree.createXml(); juce::AudioProcessor::copyXmlToBinary (*xml, mb); }

        BasecampProcessor q; q.setPlayConfigDetails (2, 2, sr, block); q.prepareToPlay (sr, block);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getSlotModuleId (0) == "summit" && q.getSlotModuleId (1) == "summit" && q.getSlotModuleId (2) == "dropoff", "saved chain reloads in the same order");
        check (std::abs (q.getSlotParams (0)[0]->getReal() + 2.0f) < 0.01f && std::abs (q.getSlotParams (1)[0]->getReal() + 10.0f) < 0.01f, "saved settings reload");
        check (q.isSlotPlaceholder (5) && q.getSlotName (5) == "Missing: altitude", "unknown module loads as a placeholder");
        auto again = q.saveRack().getChildWithProperty ("index", 5);
        check (again.getProperty ("module").toString() == "altitude" && (float) again.getChildWithProperty ("id", "target").getProperty ("value") == -18.0f,
               "placeholder keeps the unknown module's data when saved again");
        auto probe = sine (sr, 4800, 440.0, 0.2f);
        runBlocks (probe, block, [&] (auto& v) { q.processBlock (v, midi); });
        check (probe.getMagnitude (0, 2400, 2400) > 0.1f, "audio still passes with a placeholder in the chain");

        // solo and clear
        q.setSlotModule (0, {}); q.setSlotModule (1, {});
        check (q.getSlotModuleId (0).isEmpty() && q.getTotalLatency() == 0, "clearing the Summits removes their latency");
        check (q.getSlotParams (0).empty() && q.getSlotHostParams (0).pool[0]->getName (64) == "S01 unused 01", "cleared slot unmaps its parameters");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
