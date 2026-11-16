// jsfxrun: runs a JSFX plugin over a raw float32 stereo file, so a JUCE port can be compared
// against the original. Mirrors ps_ref's command line.
//   jsfxrun <plugin.jsfx> <in.f32> <out.f32> <block> [sr=48000] [sN=value ...]
// sN sets slider N (1-based), before @init, the way REAPER has them set when the plugin loads.
#include "ysfx.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: jsfxrun <plugin.jsfx> <in.f32> <out.f32> <block> [sr=N] [sN=value ...]\n"); return 2; }
    const char* path = argv[1];
    const int block = atoi(argv[4]);
    double sr = 48000.0;
    std::vector<std::pair<int, double>> sliders;
    for (int a = 5; a < argc; ++a) {
        const char* eq = strchr(argv[a], '=');
        if (!eq) { fprintf(stderr, "bad argument %s\n", argv[a]); return 2; }
        const std::string key(argv[a], eq - argv[a]);
        const double val = atof(eq + 1);
        if (key == "sr") sr = val;
        else if (key[0] == 's') sliders.push_back({ atoi(key.c_str() + 1), val });
        else { fprintf(stderr, "bad argument %s\n", argv[a]); return 2; }
    }

    ysfx_config_t* cfg = ysfx_config_new();
    // Imports and data (the PNG atlases) sit next to the plugin; @gfx is not compiled anyway.
    std::string dir(path);
    const size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    ysfx_set_import_root(cfg, dir.c_str());
    ysfx_set_data_root(cfg, dir.c_str());
    ysfx_t* fx = ysfx_new(cfg);
    if (!ysfx_load_file(fx, path, 0)) { fprintf(stderr, "cannot load %s\n", path); return 1; }
    if (!ysfx_compile(fx, ysfx_compile_no_gfx)) { fprintf(stderr, "cannot compile %s\n", path); return 1; }

    ysfx_set_sample_rate(fx, sr);
    ysfx_set_block_size(fx, (uint32_t)block);
    for (auto& s : sliders) ysfx_slider_set_value(fx, (uint32_t)(s.first - 1), s.second);
    ysfx_init(fx);
    // @init can overwrite sliders; put them back, as REAPER does when it restores a project.
    for (auto& s : sliders) ysfx_slider_set_value(fx, (uint32_t)(s.first - 1), s.second);

    FILE* in = fopen(argv[2], "rb");
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
    fseek(in, 0, SEEK_END);
    const long bytes = ftell(in);
    fseek(in, 0, SEEK_SET);
    const int frames = (int)(bytes / (2 * sizeof(float)));
    std::vector<float> inter((size_t)frames * 2);
    if (fread(inter.data(), sizeof(float), inter.size(), in) != inter.size()) { fprintf(stderr, "short read\n"); return 1; }
    fclose(in);

    std::vector<float> l((size_t)frames), r((size_t)frames);
    for (int i = 0; i < frames; ++i) { l[(size_t)i] = inter[(size_t)(2 * i)]; r[(size_t)i] = inter[(size_t)(2 * i + 1)]; }

    for (int pos = 0; pos < frames; pos += block) {
        const int len = (frames - pos < block) ? frames - pos : block;
        float* ins[2] = { l.data() + pos, r.data() + pos };
        float* outs[2] = { l.data() + pos, r.data() + pos };
        ysfx_process_float(fx, ins, outs, 2, 2, (uint32_t)len);
    }

    for (int i = 0; i < frames; ++i) { inter[(size_t)(2 * i)] = l[(size_t)i]; inter[(size_t)(2 * i + 1)] = r[(size_t)i]; }
    FILE* out = fopen(argv[3], "wb");
    if (!out) { fprintf(stderr, "cannot open %s\n", argv[3]); return 1; }
    fwrite(inter.data(), sizeof(float), inter.size(), out);
    fclose(out);
    printf("%s: %d samples\n", path, frames);
    ysfx_free(fx);
    ysfx_config_free(cfg);
    return 0;
}
