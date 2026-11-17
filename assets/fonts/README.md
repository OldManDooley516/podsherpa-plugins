# Fonts

The suite's three faces, compiled into every plugin as binary data so that nothing depends on
what is installed on the listener's machine.

| File | Used for |
| --- | --- |
| Montserrat-SemiBold.ttf | the plugin title |
| Manrope-Medium.ttf | labels, captions, supporting text |
| Manrope-SemiBold.ttf | large values, the footer wordmark, selected segments |

## Why these are embedded rather than pre-rendered as images

The interface standard says the suite's text is "pre-rendered as images". That rule was written
for the JSFX versions, and it exists because **JSFX cannot carry a font**: it can only ask the
host for one by name, so the only way to guarantee Montserrat and Manrope in REAPER was to draw
the words into PNG atlases ahead of time.

A VST3 built with JUCE has no such limit. The actual font files ship inside the binary and are
loaded at startup, which satisfies the standard's real requirement - the plugins must not depend
on installed fonts - and is better than an atlas in four ways:

- **It stays sharp at any size.** An atlas is drawn at one size and scales by stretching pixels,
  so it softens the moment a window is resized. Outlines re-render crisply at every size.
- **It can draw anything.** Preset names the user types, a guest's name, a module added next
  month, a number that grew a digit. An atlas can only draw the strings that were baked into it.
- **Nothing has to be regenerated.** Changing a caption with an atlas means re-rendering and
  re-committing image assets; here it is a string edit.
- **It is far less code.** No glyph maps, no packing, no per-string lookup, no fallback path for
  a string the atlas does not have.

The JSFX plugins keep their atlases, because they have no alternative. The C++ suite does not
need them, and the two will look the same either way.

## Licence

Both families are under the **SIL Open Font License 1.1**, which permits bundling and embedding
in commercial software. The full licences are in `Montserrat-OFL.txt` and `Manrope-OFL.txt` and
must travel with the plugins; the OFL's conditions are that the copyright and licence are kept,
that the fonts are not sold on their own, and that a modified font is not shipped under its
reserved name. The files here are unmodified apart from being unwrapped from WOFF back to plain
TrueType, which changes no glyph and no name table.

- Montserrat: Copyright 2011 The Montserrat Project Authors
- Manrope: Copyright 2019 The Manrope Project Authors

Both were taken from the `@fontsource/montserrat` and `@fontsource/manrope` npm packages
(version 5.3.0), which repackage the Google Fonts releases.
