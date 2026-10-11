"""ENGINE1 — can the anatomical engine be cleaned up with parameters alone?

Renders the real gestures of a 22938 passage through the Syrinx engine (gesture score → VoiceController →
BirdVoice) for a grid of settings and measures, per render:
  click_db   median low-frequency (< 1.5 kHz) energy in the 13 ms around each syllable onset,
             relative to that syllable's overall level (the real recording sits far below the synth)
  click_max  the worst onset
  h2, h3     2nd / 3rd harmonic relative to the fundamental (target: the bird's -30 / -33 dB)
  pitch_err  median |tracked f0 - intended pitch| in cents
The same click metric is computed on the real recording for reference.

usage: python/.venv/bin/python scripts/studies/engine_sweep.py [stage]
"""
from __future__ import annotations

import itertools
import json
import sys

import numpy as np
import soundfile as sf
from scipy import signal

import colour1_make as C1

T = C1.T
SR = C1.SR
OUT = C1.ROOT / "listening/ENGINE1"
PASSAGE = (15.40, 17.85)
LP = signal.butter(4, 1500, "lowpass", fs=SR, output="sos")


def gestures(tr, tone):
    ev = []
    cents = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900
    for a, b in T.runs(tr["voiced"]):
        if b - a < 8:
            continue
        seg_c, seg_a = cents[a:b], tr["amp"][a:b]
        anchor_c = float(np.median(seg_c))
        u, src = np.linspace(0, 1, 64), np.linspace(0, 1, b - a)
        adb = np.clip(20 * np.log10(np.maximum(np.interp(u, src, seg_a) / (seg_a.max() + 1e-12), 1e-3)), -40, 0)
        adb[0], adb[-1] = min(adb[0], -30.0), min(adb[-1], -30.0)
        ev.append({"t_s": float(tr["t"][a]), "duration_ms": float(b - a), "type": "copy:22938",
                   "anchor_hz": float(440.0 * 2 ** ((anchor_c - 6900) / 1200)),
                   "curves": {"samples": 64, "pitch_cents": np.interp(u, src, seg_c - anchor_c).tolist(),
                              "amp_db": adb.tolist(), "tone": [tone] * 64, "side": [-1.0] * 64},
                   "hints": {"oec_ratio": 1.0, "gape": None, "second_voice_cents": None}})
    return ev


def render(ev, settings, n):
    score = {"format": "cuicui.gesture_score", "version": 1, "name": "engine1", "created_by": "ENGINE1",
             "created_at": "2026-10-12T00:00:00Z", "settings": settings, "gestures": ev}
    y = np.asarray(C1.cuicui_core.render_score(score, float(SR)), dtype=np.float64)
    return np.pad(y, (0, max(0, n - len(y))))[:n]


def click_metric(y, onsets_s, ends_s):
    lf = signal.sosfiltfilt(LP, y)
    vals = []
    for t0, t1 in zip(onsets_s, ends_s):
        i0, i1 = int((t0 - 0.003) * SR), int((t0 + 0.010) * SR)
        j0, j1 = int(t0 * SR), int(t1 * SR)
        if i0 < 0 or j1 > len(y) or j1 - j0 < 100:
            continue
        e_on = np.mean(lf[i0:i1] ** 2)
        e_syl = np.mean(y[j0:j1] ** 2)
        vals.append(10 * np.log10(e_on / (e_syl + 1e-20) + 1e-20))
    return float(np.median(vals)), float(np.max(vals))


def colour_and_pitch(y, intended_c):
    tr = T.track(y, SR)
    v = tr["voiced"][: len(intended_c)] & ~np.isnan(intended_c[: len(tr["voiced"])])
    c = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900
    err = np.abs(c[: len(v)][v] - intended_c[: len(v)][v])
    return float(np.nanmedian(tr["h2_db"])), float(np.nanmedian(tr["h3_db"])), float(np.median(err)) if err.size else np.nan


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else "1"
    ann = json.loads(C1.REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(C1.REC), always_2d=True)
    x = x.mean(axis=1)
    t0, t1 = PASSAGE
    ref = C1.load(t0, t1, x)
    n = len(ref)
    tr = T.track(ref, SR, mask=C1.ann_mask(ann, t0, t1, int(n / SR / T.HOP_S) + 8))
    runs = [r for r in T.runs(tr["voiced"]) if r[1] - r[0] >= 8]
    onsets = [tr["t"][a] for a, _ in runs]
    ends = [tr["t"][b - 1] for _, b in runs]
    intended = np.where(tr["voiced"], 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900, np.nan)
    # Size 0.75 transposes nothing: the controller re-solves pitch, so the intended contour is the real one
    real_click = click_metric(ref, onsets, ends)
    print(f"real recording: click median {real_click[0]:.1f} dB, worst {real_click[1]:.1f} dB")

    if stage == "1":
        grid = {"tone": [0.3, 0.6, 0.9, 1.0], "att_rel": [(3, 8), (8, 15), (20, 30)],
                "radiation": [1.0, 0.5, 0.0], "aspiration": [0.02, 0.0]}
    else:
        grid = json.loads(stage)
    keys = list(grid)
    rows = []
    OUT.mkdir(parents=True, exist_ok=True)
    for combo in itertools.product(*[grid[k] for k in keys]):
        p = dict(zip(keys, combo))
        att, rel = p.get("att_rel", (3, 8))
        settings = {"body.size": 0.75, "perf.breath": 0.75, "perf.tone": p.get("tone", 0.9),
                    "resp.attack_ms": att, "resp.release_ms": rel, "out.radiation": p.get("radiation", 1.0),
                    "noise.aspiration": p.get("aspiration", 0.02)}
        for k in ("trachea.loss", "trachea.reflection", "oec.damping", "beak.damping"):
            if k in p:
                settings[k] = p[k]
        y = render(gestures(tr, settings["perf.tone"]), settings, n)
        cm, cx = click_metric(y, onsets, ends)
        h2, h3, pe = colour_and_pitch(y, intended)
        rows.append({**{k: (list(v) if isinstance(v, tuple) else v) for k, v in p.items()},
                     "click_db": round(cm, 1), "click_max": round(cx, 1), "h2": round(h2, 1), "h3": round(h3, 1),
                     "pitch_err_c": round(pe, 1)})
        print(json.dumps(rows[-1]))
    (OUT / f"sweep_stage_{'1' if stage == '1' else 'x'}.json").write_text(json.dumps({"real_click": real_click, "rows": rows}, indent=1))


if __name__ == "__main__":
    main()
