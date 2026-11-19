"""Read-only measurements for the first audio stream of a mono or stereo file."""

import json
import math
import os
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


NUMBER = r"[+-]?(?:\d+(?:\.\d+)?|inf|nan)"
FRAME = re.compile(rf"\bt:\s*({NUMBER}).*?\bS:\s*({NUMBER})")
FRAME_PEAKS = re.compile(r"\bFTPK:\s*(.*?)\s+dBFS")
SUMMARY_FIELD = re.compile(rf"^\s*(I|LRA|Peak|Threshold):\s*({NUMBER})")
RMS_FIELD = re.compile(rf"RMS level dB:\s*({NUMBER})")
SAMPLE_PEAK_FIELD = re.compile(rf"Peak level dB:\s*({NUMBER})")
LIMIT = -1.0
TOLERANCE = 1.0
MEDIA_TIMEOUT = 1800


@dataclass
class Measurement:
    channels: int
    integrated: float | None
    true_peak: float | None
    loudness_range: float | None
    correlation: float | None
    short_term: list[tuple[float, float | None]]
    silent_channels: tuple[str, ...] = ()
    rms_dbfs: float | None = None
    sample_peak_dbfs: float | None = None
    sample_rate: int = 0
    codec: str = ""
    bitrate: int | None = None
    container: str = ""
    peak_window_start: float | None = None


def run(args, **options):
    try:
        return subprocess.run(
            args, stdin=subprocess.DEVNULL, timeout=MEDIA_TIMEOUT,
            encoding="utf-8", errors="replace",
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            **options,
        )
    except FileNotFoundError as error:
        raise ValueError("Install FFmpeg and add ffmpeg and ffprobe to your PATH.") from error
    except subprocess.TimeoutExpired as error:
        raise ValueError("The audio scan timed out.") from error


def analyze(path):
    path = str(Path(path).resolve())
    probe = run(
        ["ffprobe", "-v", "error", "-select_streams", "a:0",
         "-show_entries", "stream=channels,sample_rate,codec_name,bit_rate:format=format_name", "-of", "json", path],
        capture_output=True,
    )
    if probe.returncode:
        raise ValueError("Could not open this media file.")
    info = json.loads(probe.stdout)
    streams = info.get("streams", [])
    if not streams:
        raise ValueError("This file has no audio stream.")
    channels = streams[0].get("channels")
    if channels not in (1, 2):
        raise ValueError("Choose a file with one or two channels in its first audio stream.")

    rate = int(streams[0]["sample_rate"])
    loudness = "ebur128=peak=true:framelog=info"
    pan = "pan=3.0|c0=c0|c1=c1|c2=c0+c1," if channels == 2 else ""
    graph = (
        f"[0:a:0]asetpts=N/SR/TB,asplit[loud][stats];[loud]{loudness}[meter];"
        f"[stats]aformat=sample_fmts=dbl,{pan}"
        "astats=measure_perchannel=RMS_level+Peak_level:measure_overall=none[levels]"
    )
    maps = ["-map", "[meter]", "-map", "[levels]"]

    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as log:
        process = run(
            ["ffmpeg", "-hide_banner", "-nostats", "-xerror", "-i", path,
             "-filter_complex", graph, *maps, "-f", "null", "-"],
            stdout=subprocess.DEVNULL, stderr=log,
        )
        log.seek(0)
        if process.returncode:
            raise ValueError("FFmpeg could not decode or measure this audio stream.")
        result = parse_log(log, channels, rate)
        result.codec = streams[0].get("codec_name", "")
        bitrate = streams[0].get("bit_rate", "")
        result.bitrate = int(bitrate) if str(bitrate).isdigit() else None
        result.container = info.get("format", {}).get("format_name", "")
        return result


def parse_log(lines, channels, rate):
    summary = {}
    rms = []
    peaks = []
    short_term = []
    last_time = 0.0
    complete = False
    top_frame_peak = -math.inf
    top_frame_start = None

    for line in lines:
        if "astats" in line and (match := RMS_FIELD.search(line)):
            rms.append(float(match.group(1)))
            continue
        if "astats" in line and (match := SAMPLE_PEAK_FIELD.search(line)):
            peaks.append(float(match.group(1)))
            continue
        if "] Summary:" in line and "ebur128" in line:
            summary.clear()
            complete = True
        if complete:
            match = SUMMARY_FIELD.search(line)
            if match:
                name, value = match.groups()
                summary.setdefault(name, float(value))
                continue
        if "ebur128" in line and (match := FRAME.search(line)):
            last_time, level = map(float, match.groups())
            last_time += 1.0 / rate
            if peak_values := FRAME_PEAKS.search(line):
                levels = [float(value) for value in re.findall(NUMBER, peak_values.group(1))]
                frame_peak = max(levels, default=-math.inf)
                if frame_peak > top_frame_peak:
                    top_frame_peak = frame_peak
                    top_frame_start = round(max(0, last_time - 0.1), 1)
            if last_time >= 2.9999:
                short_term.append((last_time, level if level > -100 else None))

    if not complete or not {"I", "Threshold", "LRA", "Peak"} <= summary.keys():
        raise ValueError("FFmpeg did not return a complete measurement.")
    absolute_gate_only = summary["I"] == -70.0 and summary["Threshold"] == 0.0
    integrated = None if last_time < 0.3999 or absolute_gate_only else summary["I"]
    peak = summary["Peak"]
    lra = summary["LRA"] if last_time >= 9.9999 and integrated is not None else None
    correlation = stereo_correlation(rms)
    silent_channels = ()
    if channels == 2 and len(rms) == 3:
        silent_channels = tuple(
            side for side, level in zip(("Left", "Right"), rms[:2])
            if level == -math.inf
        )
    # Stereo stats include a synthesized L+R channel used only by the phase meter.
    real_rms = rms[:channels]
    mean_power = sum(10 ** (level / 10) for level in real_rms) / channels
    rms_dbfs = 10 * math.log10(mean_power) if len(real_rms) == channels and mean_power > 0 else None
    sample_peak = max(peaks[:channels], default=-math.inf)
    return Measurement(channels, integrated, peak if math.isfinite(peak) else None,
                       lra, correlation, short_term, silent_channels, rms_dbfs,
                       sample_peak if math.isfinite(sample_peak) else None, rate,
                       peak_window_start=top_frame_start if top_frame_peak == peak else None)


def stereo_correlation(rms):
    if len(rms) != 3 or not all(math.isfinite(level) for level in rms[:2]):
        return None
    left, right, combined = (10 ** (level / 10) for level in rms)
    value = (combined - left - right) / (2 * math.sqrt(left * right))
    return max(-1.0, min(1.0, value))


def delivery(result, profile):
    expected = {"Mono": 1, "Stereo": 2}.get(profile, result.channels)
    target = -19.0 if expected == 1 and profile != "Apple Podcasts" else -16.0
    notes = []
    if len(result.silent_channels) == 2:
        notes.append("Both stereo channels are silent.")
    elif result.silent_channels:
        notes.append(f"{result.silent_channels[0]} channel is silent.")
    if result.channels != expected:
        notes.append("File channel count differs from the selected profile.")
    if result.integrated is None:
        notes.append("No integrated loudness reading.")
    elif abs(result.integrated - target) > TOLERANCE:
        change = target - result.integrated
        notes.append(f"Integrated loudness is {abs(change):.1f} LU "
                     f"{'below' if change > 0 else 'above'} target.")
    if result.true_peak is None:
        notes.append("No true-peak reading.")
    elif result.true_peak > LIMIT:
        notes.append(f"True peak is {result.true_peak - LIMIT:.1f} dB over the ceiling.")
    elif result.true_peak == LIMIT:
        notes.append("True peak rounds to the ceiling. Review peak headroom.")
    if result.correlation is not None and result.correlation < 0:
        notes.append("Stereo correlation is negative. Check the episode in mono.")
    return target, notes
