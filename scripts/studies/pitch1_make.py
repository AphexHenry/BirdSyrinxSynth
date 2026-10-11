"""PITCH1 — what pitch does a listener assign to a bird syllable?

Builds a pitch-matching (method of adjustment) test page in listening/PITCH1/:
  index.html   the self-contained test page (contours + trial list inlined; no labels, no predictions)
  design.json  hidden key: stimulus identities and the prediction of every hypothesis
Protocol: agent documents/studies/PITCH1_perceived_pitch.md

usage: python/.venv/bin/python scripts/studies/pitch1_make.py
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/review"))
import track as T  # noqa: E402

OUT = ROOT / "listening/PITCH1"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
SEED = 20261011
CENTER_HZ = 3400.0  # synthetic stimuli are centred here, then roved per trial
REAL_SEGMENTS = [7, 117, 42, 35, 64, 63]  # annotated segments of 22938 (3 swipes, 3 peeps)
RAMP_MS = 5.0


def cents(hz):
    return 6900.0 + 1200.0 * np.log2(np.asarray(hz) / 440.0)


def hz(c):
    return 440.0 * 2.0 ** ((np.asarray(c) - 6900.0) / 1200.0)


def ramp_env(n_ms, ramp_ms=RAMP_MS):
    a = np.ones(n_ms)
    r = int(ramp_ms)
    w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, r))
    a[:r] = w
    a[-r:] = w[::-1]
    return a


# ------------------------------------------------------------------ synthetic contours
def synthetic():
    c0 = float(cents(CENTER_HZ))
    defs = {}

    def add(name, rel_cents, note):
        rel = np.asarray(rel_cents, dtype=float)
        defs[name] = {"c": c0 + rel, "a": ramp_env(len(rel)), "note": note}

    u75 = np.linspace(0, 1, 75)
    u200 = np.linspace(0, 1, 200)
    add("steady", np.zeros(75), "control: steady tone")
    add("up_lin", -300 + 600 * u75, "linear rise, 600 c in 75 ms")
    add("down_lin", 300 - 600 * u75, "linear fall, 600 c in 75 ms")
    add("plat_then_up", np.r_[np.full(40, -300.0), np.linspace(-300, 300, 35)],
        "40 ms plateau low, then 35 ms rise")
    add("up_then_plat", np.r_[np.linspace(-300, 300, 35), np.full(40, 300.0)],
        "35 ms rise, then 40 ms plateau high")
    add("arch", -300 + 600 * (1 - (2 * u75 - 1) ** 2), "parabolic arch, turning point on top")
    add("vee", 300 - 600 * (1 - (2 * u75 - 1) ** 2), "parabolic U, turning point at bottom")
    add("up_slow", -300 + 600 * u200, "linear rise, 600 c in 200 ms")
    add("down_slow", 300 - 600 * u200, "linear fall, 600 c in 200 ms")
    add("step_up", np.r_[np.full(35, -300.0), np.linspace(-300, 300, 5), np.full(35, 300.0)],
        "two plateaus: low then high")
    add("step_down", np.r_[np.full(35, 300.0), np.linspace(300, -300, 5), np.full(35, -300.0)],
        "two plateaus: high then low")
    return defs


# ------------------------------------------------------------------ real contours
def real():
    ann = json.loads(REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(REC), always_2d=True)
    x = x.mean(axis=1)
    sos = signal.butter(4, 1000, "highpass", fs=sr, output="sos")
    defs = {}
    for k in REAL_SEGMENTS:
        s = ann["segments"][k]
        a0, a1 = s["start"] - 0.01, s["end"] + 0.01
        seg = signal.sosfiltfilt(sos, x[int(a0 * sr): int(a1 * sr)])
        tr = T.track(seg, sr)
        r0, r1 = max(T.runs(tr["voiced"]), key=lambda r: r[1] - r[0])
        c = cents(tr["f0"][r0:r1])
        amp = tr["amp"][r0:r1].copy()
        amp = np.convolve(amp, np.ones(3) / 3, mode="same")
        amp /= amp.max()
        fade = 3
        w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, fade))
        amp[:fade] *= w
        amp[-fade:] *= w[::-1]
        label = s["elements"][0]["type_label"]
        defs[f"real_{label}_seg{k}"] = {
            "c": c, "a": amp,
            "note": f"22938 segment {k} ({label}) at {s['start']:.3f} s, {r1 - r0} ms",
        }
    return defs


# ------------------------------------------------------------------ hypotheses
def predictions(c, a, v0=2000.0):
    """Predicted perceived pitch (absolute cents) under each candidate rule."""
    act = a >= 0.1 * a.max()
    cc, aa = c[act], a[act]
    n = len(cc)
    q = max(1, n // 4)
    slope = np.abs(np.gradient(cc)) * 1000.0  # cents per second (1 ms grid)
    out = {
        "mean": cc.mean(),
        "energy_weighted": np.sum(aa**2 * cc) / np.sum(aa**2),
        "median": np.median(cc),
        "start_25pct": cc[:q].mean(),
        "end_25pct": cc[-q:].mean(),
        "max": cc.max(),
        "min": cc.min(),
    }
    for v in (1000.0, 2000.0, 4000.0):
        w = np.exp(-slope / v)
        out[f"stable_v{int(v)}"] = np.sum(w * cc) / np.sum(w)
    ramp = np.linspace(0.5, 1.5, n)  # recency x stability, the combined candidate
    w = np.exp(-slope / v0) * ramp
    out["stable_recency"] = np.sum(w * cc) / np.sum(w)
    return {k: round(float(v), 1) for k, v in out.items()}


# ------------------------------------------------------------------ build
def main():
    rng = np.random.default_rng(SEED)
    stims = {**synthetic(), **real()}
    names = list(stims)
    ids = {nm: f"S{i + 1:02d}" for i, nm in enumerate(rng.permutation(names))}

    page_stimuli, key = {}, {}
    for nm, s in stims.items():
        sid = ids[nm]
        page_stimuli[sid] = {
            "dur_ms": int(len(s["c"])),
            "f_hz": [round(float(v), 2) for v in hz(s["c"])],
            "a": [round(float(v), 4) for v in s["a"]],
        }
        key[sid] = {
            "name": nm,
            "group": "real" if nm.startswith("real_") else "synthetic",
            "note": s["note"],
            "dur_ms": int(len(s["c"])),
            "predictions_cents": predictions(s["c"], s["a"]),
        }

    # trial list: practice (2 steady tones), then every stimulus twice, shuffled,
    # never the same stimulus twice in a row
    ids_list = [ids[nm] for nm in names] * 2
    while True:
        order = list(rng.permutation(ids_list))
        if all(a != b for a, b in zip(order, order[1:])):
            break
    trials = []
    for i, sid in enumerate([ids["steady"], ids["steady"]] + order):
        practice = i < 2
        real_stim = key[sid]["group"] == "real"
        rove = float(rng.uniform(-200, 200) if real_stim else rng.uniform(-300, 300))
        ref = key[sid]["predictions_cents"]["mean"] + rove
        start = ref + float(rng.choice([-1, 1]) * rng.uniform(200, 700))
        smid = ref + float(rng.uniform(-400, 400))
        smin, smax = smid - 1200.0, smid + 1200.0
        start = float(np.clip(start, smin + 50, smax - 50))
        trials.append({
            "n": i + 1, "stim": sid, "practice": practice,
            "rove_cents": round(rove, 1), "cmp_start_cents": round(start, 1),
            "slider_min_cents": round(smin, 1), "slider_max_cents": round(smax, 1),
        })

    page = {"study": "PITCH1", "version": 1, "seed": SEED, "stimuli": page_stimuli, "trials": trials}
    blob = json.dumps(page, separators=(",", ":"))
    page["design_hash"] = hashlib.sha256(blob.encode()).hexdigest()[:12]

    OUT.mkdir(parents=True, exist_ok=True)
    # one self-contained file: the data is inlined so the page works from file://, a preview pane or a copy
    html = Path(__file__).with_name("pitch1_page.html").read_text()
    tag = '<script src="stimuli.js"></script>'
    assert tag in html
    data = "<script>window.PITCH1 = " + json.dumps(page, separators=(",", ":")) + ";</script>"
    (OUT / "index.html").write_text(html.replace(tag, data))
    (OUT / "stimuli.js").unlink(missing_ok=True)
    design = {"study": "PITCH1", "design_hash": page["design_hash"], "seed": SEED,
              "center_hz": CENTER_HZ, "key": key, "trials": trials}
    (OUT / "design.json").write_text(json.dumps(design, indent=1))

    print(f"{len(page_stimuli)} stimuli, {len(trials)} trials, design {page['design_hash']}")
    for sid in sorted(key):
        k = key[sid]
        p = k["predictions_cents"]
        print(f"{sid} {k['name']:24s} {k['dur_ms']:4d} ms  mean {p['mean']:8.0f}  "
              f"end-mean {p['end_25pct'] - p['mean']:+5.0f}  start-mean {p['start_25pct'] - p['mean']:+5.0f}  "
              f"stable-mean {p['stable_v2000'] - p['mean']:+5.0f}")


if __name__ == "__main__":
    main()
