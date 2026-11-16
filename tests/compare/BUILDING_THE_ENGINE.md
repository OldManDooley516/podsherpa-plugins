# Running the real JSFX, anywhere

`jsfxrun.cpp` is the missing piece the README used to refer to: a command-line runner around the
ysfx JSFX engine, with the same arguments as `ps_ref`, so a JSFX plugin and its port can be given
the same audio and settings and the outputs compared sample for sample.

    jsfxrun <plugin.jsfx> <in.f32> <out.f32> <block> [sr=48000] [sN=value ...]

`sN` sets slider N, counting from 1, before `@init` and again after it, which is what REAPER does
when it restores a project. Imports and the PNG atlases are looked for next to the plugin;
`@gfx` is not compiled, so the images are never needed.

## Building it on Linux (about two minutes)

    apt-get update && apt-get install -y build-essential cmake git
    git clone --depth 1 --recurse-submodules https://github.com/jpcima/ysfx.git
    cmake -S ysfx -B ysfx-build -DCMAKE_BUILD_TYPE=Release \
        -DYSFX_PLUGIN=OFF -DYSFX_GFX=OFF -DYSFX_TESTS=OFF -DYSFX_TOOLS=OFF
    cmake --build ysfx-build --target ysfx -j4
    g++ -O2 -std=c++17 tests/compare/jsfxrun.cpp -Iysfx/include -o jsfxrun \
        ysfx-build/libysfx.a -lpthread -ldl -lm

Graphics support is off, so no X11 or GUI libraries are needed for the engine itself. (Building
`ps_tests` does need them; see vst3-build-notes.md in the project docs.)

## Comparing a module

    jsfxrun PodSherpa_Scree.jsfx in.f32 jsfx.f32 512 s1=50 s2=1.5
    ps_ref  scree               in.f32 port.f32 512 sensitivity=50 longest=1.5
    cmp jsfx.f32 port.f32     # silence means byte for byte identical

Two things to watch:

- Sliders the shared wrapper owns rather than the module (enable, input and output gain, Look,
  interface mode, accent) must be left at the values that make the JSFX transparent: enable on,
  both gains 0. Those are the defaults in every plugin in the suite.
- **Converting a mono clip with `ffmpeg -ac 2` quietly drops it 3.01 dB** (ffmpeg's mono-to-stereo
  downmix gain). Use `-af "pan=stereo|c0=c0|c1=c0"` instead. It matters: Scree's detector has an
  absolute threshold floor, so the level changes how many clicks it finds.

## Results

Nov 16, 2026, Scree 0.4.2 against its port: **byte for byte identical**, 17 runs. Four clips of
Sean's real voice audio (2 to 7 minutes each) at Sensitivity 25, 50, 75 and 100 with Longest
click 3.0, 1.5, 0.5 and 2.0 ms, plus one run at a 64-sample block size to show that block
boundaries make no difference.

The other eight modules have not been run through this rig yet; the Nov 15 results in README.md
came from the same engine on another machine. Doing them needs their JSFX sources, which live in
the all-in-one bundle on Sean's machine rather than in either repository.
