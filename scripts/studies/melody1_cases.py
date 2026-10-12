"""MELODY1, part 1: R1's tapped Frère Jacques vs the same melody played with held keys.

Renders through the plugin (plugin_render) into listening/MELODY/, then measures per case:
syllables per key, share of syllables on the key's note (±50 c), release calls, birds singing at once.
usage: python/.venv/bin/python scripts/studies/melody1_cases.py "$PWD"   (then melody1_reference.py)
Study: agent documents/studies/MELODY1_tapped_melody.md
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(sys.argv[1])
RENDER = ROOT / "build/release/bin/plugin_render"
OUT = ROOT / "listening/MELODY"
WORK = OUT / "work"
WORK.mkdir(parents=True, exist_ok=True)
SR = 48000
bank = json.loads((ROOT / "assets/voicebanks/zf_22938/bank.json").read_text())
syls = [s for p in bank["phrases"] for s in p["syllables"]]

MELODY = [60, 62, 64, 60, 60, 62, 64, 60, 64, 65, 67, 64, 65, 67]
STEP = [0.42] * 7 + [0.6] + [0.42] * 2 + [0.84] + [0.42] * 2  # R1's rhythm
TIMES = np.cumsum([0.0] + STEP)[: len(MELODY)] + 0.3


def notes(hold):  # hold: seconds, or "legato" (to the next note)
    out = []
    for i, (n, t) in enumerate(zip(MELODY, TIMES)):
        d = (STEP[i] if i < len(STEP) else 0.84) - 0.01 if hold == "legato" else hold
        out.append((n, float(t), float(d), 100))
    return out


CASES = [
    ("01_r1_taps_follow", "R1 item 02: taps of 60 ms, follow the keyboard", 0.06, {"perf.mapping": 1}),
    ("02_taps_own_height", "Same taps, own height (the default mapping)", 0.06, {}),
    ("03_held_350ms_follow", "Keys held 0.35 s (detached), follow", 0.35, {"perf.mapping": 1}),
    ("04_legato_follow", "Keys held to the next note (legato), follow", "legato", {"perf.mapping": 1}),
    ("05_legato_follow_snap", "Legato, follow, Snap to note 100 %", "legato",
     {"perf.mapping": 1, "perf.snap": 1.0}),
    ("06_legato_follow_snap_solo", "Legato, follow, Snap 100 %, Birds 1", "legato",
     {"perf.mapping": 1, "perf.snap": 1.0, "perf.polyphony": 1}),
]

results = {}
items = []
for name, title, hold, params in CASES:
    pj = WORK / f"{name}.params.json"
    pj.write_text(json.dumps(params))
    ns = notes(hold)
    spec = ",".join(f"{n}:{t:.4f}:{d:.4f}:{v}" for n, t, d, v in ns)
    subprocess.run([str(RENDER), "--notes", spec, "--params", str(pj), "--seed", "7", "--tail", "1.5",
                    "--syllable-log", str(WORK / f"{name}.log.json"), "-o", str(OUT / f"{name}.wav")],
                   check=True, capture_output=True)
    log = json.loads((WORK / f"{name}.log.json").read_text())["starts"]
    on_note = []
    spans = []
    for e in log:
        s = syls[e["syllable"]]
        sung = s["anchors"]["rule"] + e["shift_cents"]
        on_note.append(abs(sung - e["target_cents"]) <= 50.0)
        spans.append((e["time_s"], e["time_s"] + len(s["f_hz"]) / 1000.0))
    # most syllables sounding at the same moment
    edges = sorted([(a, 1) for a, _ in spans] + [(b, -1) for _, b in spans])
    cur = most = 0
    for _, d in edges:
        cur += d
        most = max(most, cur)
    calls = sum(1 for e in log if e.get("release_alarm"))
    m = {"keys": len(ns), "syllables": len(log), "syllables_per_key": round(len(log) / len(ns), 2),
         "on_the_note_pct": round(100.0 * float(np.mean(on_note)), 0), "release_calls": calls,
         "max_at_once": most}
    results[name] = m
    items.append({"file": f"{name}.wav", "title": title, "group": "melody",
                  "listen_for": "", "params": params | {"hold_s": hold}, "metrics": m,
                  "reference": "01_r1_taps_follow.wav" if name != "01_r1_taps_follow" else None})

texts = {
    "01_r1_taps_follow": "The R1 item: one syllable per key, each landing on its note. The tune is clear.",
    "02_taps_own_height": "Own height folds the tune into the bird's octave: its shape is broken.",
    "03_held_350ms_follow": "Held keys: after the first syllable (on the note) the bird goes on with its "
    "phrase at its own pitches; the tune gets extra, off-note syllables.",
    "04_legato_follow": "Legato: as 03, plus the previous bird still singing under the next note, and the "
    "long rising call after the two long notes (0.6 s and 0.84 s, D-030).",
    "05_legato_follow_snap": "Snap to note 100 %: the extra syllables now land on the note too.",
    "06_legato_follow_snap_solo": "And one bird only: each key takes over from the last, no overlap.",
}
for it in items:
    it["listen_for"] = texts[it["file"][:-4]]
manifest = {"format": "cuicui.listening_manifest", "version": 1, "wp": "MELODY",
            "title": "Frère Jacques: R1's taps vs keys held as when playing (plugin renders, seed 7)",
            "items": items}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
print(json.dumps(results, indent=1))
