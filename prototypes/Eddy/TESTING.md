# Validation — PodSherpa Eddy v0.1.0

Validated on 18 November 2026. The original JSFX source was executed in a WDL/EEL runner using ysfx, with independent offline renders in **REAPER 7.86 for Linux**. REAPER compiled the full plug-in, including the custom graphics section. The older external runner compiled the DSP sections only because it lacks the newer two-argument automation-touch API used by the interface.

All **21 automated checks passed**. Detailed values are in validation_results.json.

| Check | Result |
| --- | --- |
| Amount 0, at 44.1 / 48 / 96 kHz | Maximum double-precision reconstruction error at or below 1.12 × 10^-16. |
| Mix 0, at all three rates | Exact aligned dry output in the runner. |
| Internal bypass with Output trim and removed monitoring enabled | Aligned dry output; maximum error below 4.4 × 10^-19. |
| In-range 3 kHz sine, default 3 dB cap | Approximately 2.73 dB steady-state attenuation. |
| Same 3 kHz sine with opposite-polarity stereo | Same attenuation; detector does not cancel. |
| Linked stereo polarity | Left plus right remained zero to the checked tolerance. |
| Out-of-range 150 Hz sine | Level change was effectively 0 dB. |
| Strong 3 kHz sine with 1.5 dB cap | Approximately 1.37 dB attenuation; below the cap. |
| Normal output plus removed output, zero trim | Reconstructed the dry signal within numerical precision. |
| Silence | Exactly zero output in the runner. |
| Flat broadband noise at default settings | No measured broadband level change in this seeded fixture. |
| Impulses at startup and across a ring-buffer wrap, Amount 0 | Unity reconstruction within 2.0 × 10^-17. |
| Independent REAPER active render | Approximately 2.73 dB steady-state attenuation. |
| Independent REAPER Amount 0 render | Maximum error about 3.86 × 10^-8, including 24-bit output quantization. |
| Independent REAPER normal plus removed renders | Maximum reconstruction error about 1.04 × 10^-7, including output quantization. |

Runner checks used 64-, 256-, and deliberately non-dividing 511-sample host blocks. REAPER's batch converter independently verified active processing, delay compensation, and removed-monitor alignment.

## Interface review

The revised code was batch-rendered in REAPER 7.86 for Linux. An external graphics runner also rendered the badge and exercised both range handles, including grabbing without a jump, returning to the original frequency, clamping at the minimum and opposite edge, and editing endpoints reversed by host automation. The graphics runner substituted the legacy one-argument automation call for the release notification. Interactive REAPER automation and screen layout on Windows and macOS still need an on-machine check.

## Remaining validation

These checks establish arithmetic behavior and basic integration. They do not establish perceptual transparency, equivalence to any commercial processor, or good results on every voice. Real dialogue listening, live playback, CPU measurement, project save/reopen behavior, and on-machine Windows/macOS testing remain. No microphone or audio-device listening check was completed in this environment.
