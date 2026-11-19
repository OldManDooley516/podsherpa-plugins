# PodSherpa Eddy v0.1.0

An experimental REAPER plug-in that detects prominent spectral peaks and applies time-varying cuts. The first starting points focus on spoken-word harshness, nasal tones, and low-mid buildup. The interface follows the blue-on-black direction of the PodSherpa tools.

This folder contains the installable JSFX and its complete editable source. It runs inside REAPER; a standalone application and VST3 build are future development options. The DSP is an original prototype, inspired by the general idea of automatic resonance suppression. It does not reproduce oeksound's proprietary algorithm and is not affiliated with oeksound.

## Install

1. Open `prototypes/Eddy` in the repository checkout.
2. In REAPER, select **Options → Show REAPER resource path in explorer/finder**.
3. Open that location's **Effects** folder. Create a **PodSherpa** folder if you need one.
4. Copy **Effects/PodSherpa_Resonance.jsfx** into **Effects/PodSherpa**.
5. Open a track's FX browser and search for **PodSherpa Eddy**. If it does not appear, refresh the FX browser or restart REAPER.

The source format is intended for REAPER on Windows, macOS, and Linux. This release was executed and inspected on Linux with REAPER 7.86. Windows and macOS need an on-machine check.

Keep the original file name and folder when replacing the Resonance prototype. REAPER uses the JSFX path to find the effect in existing projects and presets.

## First audition

Use an unprocessed dialogue track, before your limiter. Start with **Gentle voice**, then increase **Amount** until the effect becomes obvious and back it down. The default maximum spectral cut is **3 dB**, over the **1–9 kHz** range with soft edges. This range avoids intentionally targeting the lower body of the voice.

Use **Hear removed** briefly to hear what the processor is taking away. Compare with **Processing / Bypassed** at a similar perceived loudness. Listen for softened harshness, and also for dulled consonants, thin vowels, ringing, or watery texture. If natural vocal character is being removed, lower Amount or Maximum cut, increase Selectivity, or narrow the range.

These are starting settings for auditioning, not validated treatments for every voice. In particular, broad vocal formants and natural harmonics can look like resonances to this early detector.

## Controls

| Control | What it changes |
| --- | --- |
| Amount | Scales the detected peak excess before the cut limit. Zero Amount returns to unity after any existing reduction releases. |
| Maximum cut | Caps the spectral gain mask in dB. It is a per-frequency limit, not a broadband compressor meter or a sample-peak limiter. |
| Selectivity | Sets the prominence a peak must have above its neighbouring spectral energy. Higher values target fewer peaks. |
| Range low / high | Bound the area being processed. Both edges fade inward over half an octave. Reversed endpoints are sorted internally. |
| Attack | How quickly detected reduction builds. |
| Release | How quickly reduction fades after a peak disappears. |
| Mix | Blends the processed signal with a time-aligned dry signal. |
| Output | Trims the normal processed/mixed output. |
| Hear removed | Monitors Mix × (aligned dry − processed audio), before Output trim. |
| Processing / Bypassed | Toggles a smoothed internal bypass; bypass overrides trim and removed monitoring. |

Drag either highlighted edge in the spectrum to adjust the frequency range while listening. Drag the Amount knob vertically. Drag horizontally within the other parameter cards, or use the mouse wheel. Right-click a control to reset its value. All eleven parameters are available for host automation.

The gray trace shows the input spectrum. The blue trace shows the applied cut, referenced to the left-hand **CUT dB** scale. The input spectrum has a separate internal dBFS scale; it does not use those cut labels. **Peak spectral cut** reports the deepest current mask cut, rather than the level reduction of the complete voice.

## Starting points

| Button | Amount | Maximum cut | Selectivity | Range | Attack / release |
| --- | ---: | ---: | ---: | --- | --- |
| Gentle voice | 45% | 3 dB | 9 dB | 1–9 kHz | 30 / 180 ms |
| Harshness | 55% | 4 dB | 8 dB | 1.8–9.5 kHz | 20 / 160 ms |
| Nasal | 35% | 2.5 dB | 11 dB | 650 Hz–2.2 kHz | 40 / 220 ms |
| Low-mid | 30% | 2.5 dB | 12 dB | 180–700 Hz | 50 / 250 ms |

Each button also restores 100% Mix, 0 dB Output, normal monitoring, and active processing. The range edges fade; the centre of the selected area receives the full detector weighting.

## Latency and current limits

This version uses a 4096-sample analysis window and reports **4096 samples** of latency to REAPER. That is approximately **92.9 ms at 44.1 kHz**, **85.3 ms at 48 kHz**, or **42.7 ms at 96 kHz**. It is intended for editing and mixing. REAPER can compensate playback timing, but compensation does not remove the delay heard when monitoring a live microphone through the plug-in.

Stereo detection is fully linked and based on channel power. The same gain mask processes both channels. Attack and release are updated at 1024-sample analysis intervals. Automation is handled at host block/analysis boundaries, with sample smoothing on Mix, Output, monitoring, and bypass switches.

The first release has range controls and four starting points. A draggable weighting curve, selectable processing widths/quality, mid/side controls, and deeper voice/formant protection are development options. No commercial-quality transparency claim is made from synthetic signal checks alone.

The next useful step is to audition a short dialogue recording with a real harshness problem. The listening targets are reduced harshness, retained consonant clarity and vocal body, and no obvious modulation on sustained vowels.

## Included files

- **Effects/PodSherpa_Resonance.jsfx** — Eddy plug-in and editable source.
- **DSP_NOTES.md** — engine design and porting notes.
- **TESTING.md** — completed checks and their limits.
- **validation_results.json** — measured results from this release.

Reference documentation: [oeksound soothe2 manual](https://oeksound.com/manuals/soothe2/), [REAPER JSFX programming](https://www.reaper.fm/sdk/js/js.php), and [REAPER FFT functions](https://www.reaper.fm/sdk/js/advfunc.php).
