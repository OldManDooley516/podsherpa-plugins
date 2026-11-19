# DSP notes — v0.1.0

## Engine

The source is a self-contained JSFX with no sample assets, model downloads, or external imports. Two complex 4096-point FFTs analyse the left and right channels. Frames advance by 1024 samples. The periodic Hann window is used for both analysis and synthesis; dividing the overlap-add result by 1.5 and the inverse FFT by 4096 gives unity reconstruction when the mask is flat.

The input and overlap-add ring buffers share a position counter. Each output sample is read before the new analysis frame is scheduled. A frame is scheduled beginning at the following sample, giving an exact 4096-sample delay. The dry path reads the old input ring value before it is overwritten. REAPER receives the same PDC delay for both output channels.

## Detection and reduction

Each positive-frequency bin uses the average of left and right channel power, rather than a summed waveform. Opposite-phase stereo therefore remains detectable.

For bin k, the reference is the arithmetic mean of neighbouring power in a radius of max(6, floor(0.18 × k)) bins, excluding the central three bins. Prefix sums make these neighbourhood queries constant-time after one pass through the spectrum.

Prominence is 10 × log10(bin power / reference power). Selectivity is subtracted, and the remainder is clamped to zero. Amount scales that excess, and Maximum cut caps it. A fixed activity weight fades processing between -85 and -70 dBFS of normalized per-bin power.

The target reduction receives three-bin [1, 2, 1] / 4 smoothing. A per-bin attack/release envelope follows it. The resulting positive reduction in dB is multiplied by a range weight, then converted to a gain between zero and one. This identical real-valued gain multiplies the positive and negative conjugate bins in both channels.

Range weighting is zero outside the sorted endpoints, with smooth half-octave fades inward. The high endpoint is limited to 49% of the sample rate. Multiplying after the envelope keeps out-of-range spectral-bin gains at unity, even while a previous envelope releases.

Maximum cut bounds the spectral mask. Finite windows and a varying mask mean that individual time samples and short-term waveform peaks are not themselves bounded by an equivalent dB rule. This processor is not a peak limiter.

## Output

Normal output is `(aligned dry + Mix × (wet − aligned dry)) × Output trim`.

Removed monitoring is `Mix × (aligned dry − wet)` and excludes Output trim. At zero trim, adding normal and removed outputs reconstructs aligned dry, subject to floating-point error. Internal bypass returns aligned dry, overriding trim and monitoring. Ten-millisecond one-pole smoothing handles the Mix, Output, monitoring, and bypass values.

The analysis and synthesis continue during internal bypass. Host deactivation is needed to stop processing work.

## What needs research next

The current detector identifies prominence, not the perceptual cause of a peak. A natural harmonic or formant can trigger it. Priority work is dialogue listening tests, better discrimination of persistent objectionable resonances from vocal character, and controlled comparisons of temporal/frequency smoothing against consonant clarity and modulation artifacts.

A later VST3 or standalone port should preserve exact overlap-add normalization, dry/wet alignment, real-time memory allocation discipline, stereo power detection, and declared latency. Re-run the unity, polarity, silence, cut-limit, and removed-monitor checks after any FFT/library change. Measure CPU and live playback on target Windows and Mac hardware before optimizing quality modes.
