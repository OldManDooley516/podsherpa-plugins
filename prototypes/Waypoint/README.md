# PodSherpa Waypoint v0.1.3

Waypoint scans a finished episode or audiobook chapter. With a podcast profile selected, it calculates whether a single gain change can bring the program into the delivery window. You can review the proposed change and save a verified WAV master. ACX chapter mode provides a read-only preflight.

## New in v0.1.3

Drop one audio or video file anywhere inside Waypoint. The blue border shows when the window will accept the file. A drop starts a scan with the selected delivery profile. Drop another file over the results to check the next render. While a scan or export is running, wait for its result before dropping another file.

The interface follows PodSherpa Summit and Altitude JSFX. The blue plot shows the episode’s short-term loudness. The mint segmented meters at the right show the measured whole-file levels. Blue markers indicate the target and peak ceiling, or the RMS boundaries and sample-peak ceiling in ACX mode. The two readouts below the plot show additional measurements for the selected profile.

## Run the desktop app

Open a terminal in `prototypes/Waypoint`. Install Python 3.10 or newer with Tkinter and make sure FFmpeg provides both `ffmpeg` and `ffprobe` on your PATH. Install file-drop support with the same interpreter you use to launch the app, for example `python3.13 -m pip install -r requirements.txt` on macOS or `py -m pip install -r requirements.txt` on Windows.

| Platform | Command |
| --- | --- |
| Windows | `py waypoint.py` |
| Mac with the previous Homebrew setup | `python3.13 waypoint.py` |
| Other macOS or Linux Python 3.10+ with Tk | `python3 waypoint.py` |

In Terminal on your Mac, you can type `python3.13` followed by a space, drag `waypoint.py` from Finder into Terminal, and press Return. The file picker works when the optional drop dependency is unavailable. For the prototype typography, install Manrope and Montserrat from the repository’s [font assets](../../assets/fonts).

Drag a WAV, MP3, other supported audio file, or a video export into the window. **Open export…** also opens the file picker. Waypoint scans the first audio stream. Mono and stereo are supported. Switch profiles after the scan to apply different criteria to the same measurements.

## Delivery profiles

| Profile | Criteria |
| --- | --- |
| Auto | PodSherpa working target of −19 LUFS for mono or −16 LUFS for stereo |
| Mono / Stereo | The same working targets with a check for the selected channel count |
| Apple Podcasts | Apple’s recommended −16 LKFS ±1 for the encoded channel layout, with a −1 dBTP ceiling. This profile checks levels only. |
| ACX chapter | Whole-file RMS of −23 to −18 dBFS, sample peak at or below −3 dBFS, standalone MP3 at 44.1 kHz and reported bitrate of at least 192 kb/s |

All podcast profiles allow ±1 LU and use a −1 dBTP ceiling. A displayed peak right at the ceiling prompts a headroom review because the meter reports tenths of a decibel. Select Auto for the PodSherpa mono target and Apple Podcasts for Apple’s published target.

ACX chapter mode measures RMS across all samples, averaging channel power for stereo. Check constant bitrate separately because a reported average can also come from a variable-bitrate encode. Noise floor, spacing, chapter length, editorial quality and book-wide consistency need separate review. The screen and report say which measured checks need attention.

## Export a gain-adjusted WAV

1. Scan the episode and choose its podcast profile.
2. Read the proposed gain, predicted integrated loudness and true peak. Waypoint aims for the center of the window. If headroom is tight it can land elsewhere inside the tolerance, allowing 0.1 dB for peak-meter rounding.
3. Select **Export gain-adjusted WAV…** and give the master a new filename. Waypoint applies the same gain to every sample and channel. It renders 24-bit PCM at the source sample rate and channel count.
4. The app scans the rendered WAV before saving it and shows the verified loudness and true peak below the export controls. **Copy report** includes the source readings, plan and verified result. Scan your final MP3 or AAC separately after encoding.

If the source is already within the delivery window, Waypoint leaves it alone. When peaks prevent an in-range gain adjustment, the plan shows the loudness achievable at the peak ceiling. If the meter can locate the highest peak, it also shows the first matching 100 ms window. Use that time to find the passage in REAPER. A silent channel, negative stereo correlation, channel-count mismatch or missing loudness reading also calls for source review.

An exported WAV is an audio master. Source video, chapters, artwork and delivery metadata are excluded. Existing files are protected.

## Meter and report

Integrated LUFS covers the entire program. The graph shows three-second short-term loudness against elapsed time. Loudness range is shown after ten seconds of usable measurements. Whole-file stereo correlation can reveal polarity problems, but a positive average can hide a short cancellation. **Copy report** includes the file format, measured levels and review notes.

## Command line

Use `py` on Windows or `python3` on macOS/Linux.

```sh
python3 waypoint.py "episode.wav"
python3 waypoint.py "episode.wav" --profile "Apple Podcasts" --export-wav "episode-master.wav"
python3 waypoint.py "chapter.mp3" --profile "ACX chapter"
```

A scan prints JSON. An export additionally returns verified output readings. An unavailable or failed export exits with an error. This version is a Python desktop prototype.

## Profile references

- [Apple Podcasts audio requirements](https://podcasters.apple.com/support/893-audio-requirements)
- [ACX mastering guidance](https://www.acx.com/mp/blog/mastering-audiobooks-with-alex-the-audio-scientist)
- [ACX Audio Lab](https://www.acx.com/mp/audiolab)

## Typefaces and native drop support

Waypoint uses the same Montserrat SemiBold title and Manrope faces as the PodSherpa JSFX suite when those fonts are installed. On Mac, open the three TTF files in `assets/Fonts` in Font Book, install them, then reopen Waypoint. Existing JSFX font installations are shared. System fonts are used automatically when those families are unavailable. The supplied OFL notices accompany the fonts.

The bundled [TkinterDnD2 0.7.0](https://pypi.org/project/tkinterdnd2/0.7.0/) includes native TkDnD libraries for Apple Silicon with Tcl/Tk 8 and 9. On unsupported Tk/native combinations the file picker remains available. The package and license are included under `vendor`.
