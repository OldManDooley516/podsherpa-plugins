#!/usr/bin/env python3
"""Runs each JSFX plugin and its C++ port over the same audio and settings, and reports the
difference. Port parameter ids are mapped onto JSFX slider numbers by hand, per plugin."""
import subprocess, sys, os
import numpy as np

JS = "/home/claude/incoming/allp/PodSherpa"
RUN = "/home/claude/jsfxrun/jsfxrun"
REF = "/home/claude/build/ps_ref_artefacts/Release/ps_ref"
CLIP = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/f32b/2024-03-21--t01-09-54pm--guest770900--tony-mormino.f32"
BLOCK = "512"

def band(prefix, first, names):
    return {f"{prefix}.{n}": first + i for i, n in enumerate(names)}

RG = ["threshold", "ratio", "attack", "release", "makeup"]
CD = ["freq", "q", "direction", "range", "threshold", "attack", "release", "on"]
CT = ["freq", "gain", "q", "on"]

MODULES = {
 "summit":  ("PodSherpa_Summit.jsfx", {"threshold":1,"ceiling":2,"speed":3,"truepeak":4,"mode":5},
             [{}, {"threshold":-12,"ceiling":-1,"speed":2,"mode":1}, {"threshold":-20,"ceiling":-3,"speed":9,"mode":2},
              {"threshold":-6,"truepeak":0,"mode":0}]),
 "traverse":("PodSherpa_Traverse.jsfx", {"threshold":1,"ratio":2,"attack":3,"release":4,"knee":5,"makeup":6,"mix":7},
             [{}, {"threshold":-30,"ratio":4,"attack":2,"release":40,"knee":0,"makeup":3,"mix":100},
              {"threshold":-24,"ratio":8,"attack":150,"release":1000,"knee":18,"makeup":-6,"mix":50}]),
 "altitude":("PodSherpa_Altitude.jsfx", {"target":1,"silence":2,"maxboost":3,"maxcut":4,"rise":5,"fall":6,"soft":9,"loud":10,"lookahead":15},
             [{}, {"target":-18,"silence":-60,"maxboost":12,"maxcut":18,"rise":6,"fall":18,"lookahead":50},
              {"target":-30,"soft":40,"loud":80,"lookahead":0}]),
 "dropoff": ("PodSherpa_Dropoff.jsfx", {"freq":1,"slope":2,"phase":3},
             [{}, {"freq":40,"slope":0}, {"freq":200,"slope":5}, {"freq":120,"slope":3,"phase":1}]),
 "clearing":("PodSherpa_Clearing.jsfx", {"reduction":1,"sensitivity":2,"smoothing":3,"source":4},
             [{}, {"reduction":24,"sensitivity":80,"smoothing":20}, {"reduction":6,"sensitivity":20,"smoothing":90}]),
 "contour": ("PodSherpa_Contour.jsfx",
             {**{"hp.freq":1,"hp.slope":2,"hp.on":3}, **band("b2",4,CT), **band("b3",8,CT), **band("b4",12,CT),
              **{"shelf.freq":16,"shelf.gain":17,"shelf.on":18,"phase":19}},
             [{}, {"b2.gain":-6,"b3.gain":4.5,"b4.gain":-3,"shelf.gain":2.5,"hp.freq":120},
              {"b2.gain":6,"b2.q":4,"b3.gain":-8,"phase":1}]),
 "contourdynamic": ("PodSherpa_ContourDynamic.jsfx",
             {**band("b1",1,CD), **band("b2",9,CD), **band("b3",17,CD), **{"phase":25}},
             [{}, {"b1.range":9,"b1.threshold":-45,"b2.range":6,"b3.range":12,"b3.threshold":-50},
              {"b1.direction":1,"b2.direction":0,"b1.attack":1,"b1.release":10,"phase":1}]),
 "ridgeline":("PodSherpa_Ridgeline.jsfx",
             {**{"xover1":1,"xover2":2,"xover3":3}, **band("low",4,RG), **band("lowmid",9,RG),
              **band("highmid",14,RG), **band("high",19,RG)},
             [{}, {"low.threshold":-40,"low.ratio":6,"lowmid.threshold":-42,"highmid.ratio":8,"high.makeup":3},
              {"xover1":100,"xover2":2500,"xover3":9000,"low.attack":1,"low.release":1000}]),
}

mod = sys.argv[2] if len(sys.argv) > 2 else None
rows = []
for name, (jsfx, smap, settings) in MODULES.items():
    if mod and name != mod: continue
    for i, s in enumerate(settings):
        for k in s:
            if k not in smap: sys.exit(f"{name}: no slider for {k}")
        ja = [RUN, os.path.join(JS, jsfx), CLIP, "/tmp/cj.f32", BLOCK] + [f"s{smap[k]}={v}" for k, v in s.items()]
        pa = [REF, name, CLIP, "/tmp/cp.f32", BLOCK] + [f"{k}={v}" for k, v in s.items()]
        for cmd in (ja, pa):
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0: sys.exit(f"{name}: {' '.join(cmd)}\n{r.stderr}")
        a = np.fromfile("/tmp/cj.f32", dtype=np.float32).astype(np.float64)
        b = np.fromfile("/tmp/cp.f32", dtype=np.float32).astype(np.float64)
        n = min(len(a), len(b)); a, b = a[:n], b[:n]
        d = a - b
        sig = np.sqrt((a ** 2).mean())
        rel = 20 * np.log10(max(np.sqrt((d ** 2).mean()), 1e-30) / max(sig, 1e-30))
        label = "defaults" if not s else ", ".join(f"{k}={v}" for k, v in list(s.items())[:3]) + ("..." if len(s) > 3 else "")
        verdict = "IDENTICAL" if np.array_equal(a, b) else f"{rel:7.1f} dB below"
        rows.append((name, label, verdict, float(np.abs(d).max())))
        print(f"{name:16s} {label[:52]:54s} {verdict}")
ident = sum(1 for r in rows if r[2] == "IDENTICAL")
worst = max((r for r in rows if r[2] != "IDENTICAL"), key=lambda r: r[3], default=None)
print(f"\n{ident}/{len(rows)} bit identical" + (f"; worst other: {worst[0]} {worst[2]} (max {worst[3]:.2e})" if worst else ""))
