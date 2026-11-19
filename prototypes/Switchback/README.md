# PodSherpa Switchback v0.1.1

Switchback is a stereo utility JSFX for REAPER, with width, mono audition,
channel swap, independent input polarity, and output correlation metering.

- **Polar** is the default vectorscope. Mono concentrates toward the Mid
  axis. Side-heavy samples appear outside the 45-degree guides in amber.
  Use the Correlation meter and a mono listen to judge cancellation.
- **XY** shows the same output in a traditional Mid/Side display.
- The scope shows about 300 ms of output with older points fading behind
  newer ones. The view auto-scales to show quiet material.

## Install

Open REAPER's resource folder from the Options menu. Place
`PodSherpa_Waypoint.jsfx` in the Effects folder, then restart REAPER or
refresh the FX browser and search for **PodSherpa Switchback**.

If you already installed the stereo utility under its original Waypoint
name, replace that JSFX in the same folder. The original filename lets
existing sessions resolve the effect.

The PNG previews use synthetic stereo audio at 150% width.
