# PodSherpa plugins (Southern Digital)

JUCE (C++) versions of the PodSherpa suite: VST3 on Windows, VST3 and AU on macOS.

## What is here
- `core/` shared core every plugin is built on: the module contract (`Module.h`), parameters
  (`ParamSpec.h`, `HostParam.h`), bypass and in/out meters (`ModuleSlot.h`), true peak and
  loudness (`Meters.h`), the plain first-pass interface (`Look.h`) and the wrapper that turns
  one module into its own plugin (`SinglePlugin.h`).
- `modules/` one file per module. So far: `Summit.h` (limiter) and `Dropoff.h` (high-pass),
  both ported from the JSFX 0.3.1 audio code. `tests/jsfx_reference.py` holds a line-by-line
  transcription of that JSFX code and checks the C++ output against it.
- `plugins/Basecamp/` the master rack: 12 slots in series, any module in any slot.
- `plugins/Summit`, `plugins/Dropoff` the same modules as their own plugins.
- `tests/` headless checks, run on every build.
- `.github/workflows/build.yml` builds Windows and Mac plugins in the cloud on every push.

## Getting a build
Push to `main` (or run the "Build plugins" workflow by hand). When it finishes, open the run
on GitHub's Actions tab and download `PodSherpa-macOS` or `PodSherpa-Windows`. Install steps
are in `INSTALL.txt` inside the download.

## Building on your own machine
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ./build/ps_tests_artefacts/Release/ps_tests
    ./build/ps_tests_artefacts/Release/ps_tests --dump /tmp/dump && python3 tests/jsfx_reference.py /tmp/dump

## Adding a module
1. Write `modules/<Name>.h` as a class derived from `ps::Module`. Parameter IDs are permanent.
2. Add it to the list in `core/ModuleFactory.h`. It now appears in Basecamp.
3. Add a `plugins/<Name>/Plugin.cpp` and a `ps_plugin(...)` line in `CMakeLists.txt` for the
   standalone plugin.

## Rules that must not change once anything ships
- Module IDs (`summit`), parameter IDs (`threshold`), plugin codes (`PsSm`, `PsDo`, `PsBc`)
  and manufacturer code (`SoDg`).
- Basecamp's host parameter list: 12 slots, each with bypass, solo and 48 pooled parameters
  (`slot01.p01` ...). Saved projects and automation depend on it.
