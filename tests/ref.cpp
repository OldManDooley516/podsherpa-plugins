// ps_ref: runs one module over a raw float32 stereo file, for comparing a port against the
// original JSFX running in a JSFX engine.
//   ps_ref <module id> <in.f32> <out.f32> <block size> [sr=48000] [param=value ...]
// Parameters are set before prepare(), like sliders that were already set when the JSFX loaded.
#include "../core/ModuleFactory.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 5) { std::cerr << "usage: ps_ref <module> <in.f32> <out.f32> <block> [sr=N] [param=value ...]" << std::endl; return 2; }
    auto m = ps::createModule (argv[1]);
    if (! m) { std::cerr << "unknown module " << argv[1] << std::endl; return 2; }
    double sr = 48000.0; const int block = juce::String (argv[4]).getIntValue();
    for (int a = 5; a < argc; ++a)
    {
        const auto kv = juce::StringArray::fromTokens (argv[a], "=", "");
        if (kv.size() != 2) { std::cerr << "bad argument " << argv[a] << std::endl; return 2; }
        if (kv[0] == "sr") { sr = kv[1].getDoubleValue(); continue; }
        bool found = false; const auto& specs = m->getParamSpecs();
        for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == kv[0]) { m->setParam ((int) i, kv[1].getFloatValue()); found = true; }
        if (! found) { std::cerr << "unknown parameter " << kv[0] << std::endl; return 2; }
    }
    juce::MemoryBlock data; juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).loadFileAsData (data);
    const int n = (int) (data.getSize() / (2 * sizeof (float)));
    const float* src = static_cast<const float*> (data.getData());
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i) { b.setSample (0, i, src[2 * i]); b.setSample (1, i, src[2 * i + 1]); }
    m->prepare (sr, block, 2);
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, len);
        m->process (view);
    }
    std::vector<float> out ((size_t) n * 2);
    for (int i = 0; i < n; ++i) { out[(size_t) (2 * i)] = b.getSample (0, i); out[(size_t) (2 * i + 1)] = b.getSample (1, i); }
    juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]).replaceWithData (out.data(), out.size() * sizeof (float));
    std::cout << argv[1] << ": " << n << " samples, latency " << m->getLatencySamples() << std::endl;
    // Scree's repair count is the figure the JSFX shows, so a port can be checked against it.
    if (auto* s = dynamic_cast<ps::Scree*> (m.get())) std::cout << "  clicks fixed: " << s->getClicksFixed() << std::endl;
    if (auto* s = dynamic_cast<ps::Summit*> (m.get())) std::cout << "  integrated loudness: " << s->getIntegratedLufs() << " LUFS" << std::endl;
    return 0;
}
