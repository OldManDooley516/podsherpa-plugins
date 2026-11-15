# Checking a port against the original JSFX

`ps_ref` (built from tests/ref.cpp) runs one module over a raw float32 stereo file:

    ps_ref <module id> <in.f32> <out.f32> <block size> [sr=48000] [param=value ...]

`compare_with_jsfx.sh` runs the same audio and settings through the real JSFX plugin in a JSFX
engine and prints the largest difference. The engine used was ysfx (github.com/JoepVanlier/ysfx)
driven by a small command-line runner that is not in this repository; the paths in the script
point at the machine the ports were made on and need editing to use it elsewhere.

Results on Nov 15, 2026 against the JSFX 0.4.1 plugins, 48 kHz, 512-sample blocks, real speech
and dense multi-tone test files, several settings each: Traverse, Ridgeline, Altitude, Scree,
Contour (Natural and Linear), Contour Dynamic (Natural and Linear) and Clearing (Auto) all match
to within one step of 32-bit float output (differences 150 dB or more below the signal).
Not compared: Clearing's Learned mode, sample rates other than 48 kHz, mono.
