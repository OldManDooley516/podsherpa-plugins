"""Delivery profiles and verified constant-gain PCM renders."""

import os
import tempfile
from dataclasses import dataclass
from pathlib import Path

from meter import LIMIT, TOLERANCE, analyze, delivery, run

ACX = "ACX chapter"
PROFILES = ("Auto", "Mono", "Stereo", "Apple Podcasts", ACX)
GUARD_DB = 0.1  # FFmpeg's EBU summary reports tenths of a dB.


@dataclass
class Assessment:
    spec: str
    status: str
    notes: list[str]
    plan: str
    target_lufs: float | None = None
    gain_db: float | None = None


def acx_assessment(m):
    notes = []
    if m.rms_dbfs is None or not -23 <= m.rms_dbfs <= -18:
        notes.append("Chapter RMS falls outside the delivery window or is unavailable.")
    if m.sample_peak_dbfs is None or m.sample_peak_dbfs > -3:
        notes.append("Sample peak exceeds the ceiling or is unavailable.")
    if m.codec != "mp3" or m.container != "mp3":
        notes.append("Deliver a standalone MP3.")
    if m.sample_rate != 44100:
        notes.append("Delivery requires 44.1 kHz.")
    if m.codec == "mp3" and (m.bitrate is None or m.bitrate < 192000):
        notes.append("Reported bitrate is below 192 kb/s or unavailable.")
    if m.silent_channels or (m.correlation is not None and m.correlation < 0):
        notes.append("Review stereo channel balance and mono compatibility.")
    return Assessment(
        "−23 to −18 dBFS RMS  ·  −3 dBFS sample peak max  ·  44.1 kHz MP3",
        "Review chapter" if notes else "Measured checks within range",
        notes,
        "Chapter preflight only. Check constant bitrate, noise floor, spacing and book-wide consistency separately.",
    )


def assess(m, profile):
    if profile == ACX:
        return acx_assessment(m)
    target, notes = delivery(m, profile)
    result = Assessment(
        f"{target:g} LUFS ±{TOLERANCE:g} LU  ·  {LIMIT:g} dBTP ceiling",
        "Review" if notes else "Within target", notes, "", target,
    )
    if (m.integrated is None or m.true_peak is None or m.silent_channels
            or (m.correlation is not None and m.correlation < 0)
            or (profile in ("Mono", "Stereo") and m.channels != (1 if profile == "Mono" else 2))):
        result.plan = "Review the source or channel layout before adjusting gain."
        return result
    if not notes:
        result.plan = "Already within the delivery window. Keep the original level."
        return result

    ideal = round(target - m.integrated, 1)
    allowed = round(LIMIT - GUARD_DB - m.true_peak, 1)
    gain = min(ideal, allowed)
    projected = round(m.integrated + gain, 1)
    projected_peak = round(m.true_peak + gain, 1)
    if projected < target - TOLERANCE:
        result.plan = (f"Reaching {target:g} LUFS needs {ideal:+.1f} dB. "
                       f"At the peak-safe gain of {allowed:+.1f} dB, the file would reach "
                       f"{projected:.1f} LUFS. Review the loudest peaks in the mix.")
        if m.peak_window_start is not None:
            result.plan += f" First maximum true-peak window near {peak_time(m.peak_window_start)}."
        return result
    result.gain_db = gain
    result.plan = (f"Apply {gain:+.1f} dB across the file. "
                   f"Estimated output {projected:.1f} LUFS / {projected_peak:.1f} dBTP.")
    return result


def peak_time(seconds):
    minutes, tenths = divmod(round(seconds * 10), 600)
    return f"{minutes}:{tenths / 10:04.1f}"


def source_stamp(path):
    stat = Path(path).stat()
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns


def scan_file(path):
    stamp = source_stamp(path)
    measured = analyze(path)
    if source_stamp(path) != stamp:
        raise ValueError("The source changed during the scan. Open it again.")
    return measured, stamp


def export_wav(source, destination, measured, stamp, profile):
    """Render in the destination directory and publish after checking the output."""
    source, destination = Path(source), Path(destination)
    plan = assess(measured, profile)
    if plan.gain_db is None:
        raise ValueError(plan.plan)
    if destination.suffix.lower() != ".wav":
        raise ValueError("Choose a .wav filename for the PCM master.")
    if destination.exists() or destination.is_symlink():
        raise ValueError("Choose a new filename. This path already exists.")
    if source_stamp(source) != stamp:
        raise ValueError("The source has changed. Scan it again before export.")
    with tempfile.TemporaryDirectory(prefix=".waypoint-", dir=destination.parent) as stage:
        temp = Path(stage) / "render.wav"
        output = run(["ffmpeg", "-nostdin", "-v", "error", "-xerror", "-i", str(source.resolve()),
                      "-map", "0:a:0", "-map_metadata", "-1", "-map_chapters", "-1",
                      "-af", f"volume={plan.gain_db:.1f}dB:precision=double",
                      "-c:a", "pcm_s24le", "-rf64", "auto", str(temp)], capture_output=True)
        if output.returncode:
            raise ValueError(f"WAV export failed. {output.stderr.strip()[-350:]}")
        checked = analyze(temp)
        verdict = assess(checked, profile)
        if (verdict.notes or measured.channels != checked.channels
                or measured.sample_rate != checked.sample_rate):
            raise ValueError("Rendered WAV failed verification. " + " ".join(verdict.notes))
        if source_stamp(source) != stamp:
            raise ValueError("The source changed during export. Scan it again.")
        # Exclusive reservation also works on FAT/exFAT volumes without hard links.
        with destination.open("xb"):
            pass
        try:
            os.replace(temp, destination)
        except OSError:
            destination.unlink(missing_ok=True)
            raise
    return checked
