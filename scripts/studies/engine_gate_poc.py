"""ENGINE1 proof of concept: articulate syllables with a volume envelope instead of the bifurcation.

Today a syllable starts and stops when air pressure crosses the oscillation threshold, so the simulated
labia jump into (and snap out of) vibration at full level: clicks. Here, without changing engine code,
each gesture is rendered with constant pressure and extended by a few ms on both sides (the labia start
early and stop late), then the real amplitude envelope is applied as a gain after the engine. The
snaps then happen while the gain is zero.

Writes listening/ENGINE1/{real,engine_raw,engine_gated}.wav and prints the click metrics at onsets and
at offsets.
usage: python/.venv/bin/python scripts/studies/engine_gate_poc.py
"""
from __future__ import annotations

import json

import numpy as np
import soundfile as sf
from scipy import signal

import colour1_make as C1
from engine_sweep import PASSAGE, gestures, render

T, SR = C1.T, C1.SR
OUT = C1.ROOT / "listening/ENGINE1"
PRE_MS, POST_MS = 6, 10
SETTINGS = {"body.size": 0.75, "perf.breath": 0.75, "perf.tone": 0.6, "resp.attack_ms": 3, "resp.release_ms": 8,
            "out.radiation": 1.0, "noise.aspiration": 0.0}
LP = signal.butter(4, 1500, "lowpass", fs=SR, output="sos")
BAND = signal.butter(4, [1500, 4500], "bandpass", fs=SR, output="sos")


def gated_gestures(tr, tone):
    """Flat pressure, pitch held at both ends, each gesture extended by PRE/POST ms."""
    ev = []
    cents = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900
    for a, b in T.runs(tr["voiced"]):
        if b - a < 8:
            continue
        seg = np.r_[np.full(PRE_MS, cents[a]), cents[a:b], np.full(POST_MS, cents[b - 1])]
        anchor = float(np.median(seg))
        u, src = np.linspace(0, 1, 64), np.linspace(0, 1, len(seg))
        ev.append({"t_s": float(tr["t"][a]) - PRE_MS / 1000, "duration_ms": float(len(seg)), "type": "gated",
                   "anchor_hz": float(440.0 * 2 ** ((anchor - 6900) / 1200)),
                   "curves": {"samples": 64, "pitch_cents": np.interp(u, src, seg - anchor).tolist(),
                              "amp_db": [0.0] * 64, "tone": [tone] * 64, "side": [-1.0] * 64},
                   "hints": {"oec_ratio": 1.0, "gape": None, "second_voice_cents": None}})
    return ev


def envelope(tr, n):
    """The bird's own amplitude, per sample, with 2 ms fades at every voiced boundary."""
    a = np.where(tr["voiced"], tr["amp"], 0.0)
    a = np.convolve(a, np.ones(3) / 3, mode="same")
    for r0, r1 in T.runs(tr["voiced"]):
        k = min(2, (r1 - r0) // 2)
        if k > 0:
            a[r0:r0 + k] *= np.linspace(0.2, 1, k)
            a[r1 - k:r1] *= np.linspace(1, 0.2, k)
    return np.interp(np.arange(n) / SR, tr["t"], a)


def metrics(y, on, off):
    lf = signal.sosfiltfilt(LP, y)
    band = signal.sosfiltfilt(BAND, y)
    res = {}
    for name, times in (("onset", on), ("offset", off)):
        lfv, bv = [], []
        for t, (a, b) in zip(times, zip(on, off)):
            i0, i1 = int((t - 0.004) * SR), int((t + 0.010) * SR)
            syl = np.mean(y[int(a * SR):int(b * SR)] ** 2)
            lfv.append(10 * np.log10(np.mean(lf[i0:i1] ** 2) / (syl + 1e-20) + 1e-20))
            # in-band transient: peak of the 1.5-4.5 kHz band derivative (a click is a fast edge)
            d = np.abs(np.diff(band[i0:i1]))
            bv.append(20 * np.log10(d.max() / (np.sqrt(syl) + 1e-12) + 1e-12))
        res[name] = (float(np.median(lfv)), float(np.median(bv)))
    return res


def main():
    ann = json.loads(C1.REC.with_suffix(".json").read_text())
    x, _ = sf.read(str(C1.REC), always_2d=True)
    x = x.mean(axis=1)
    t0, t1 = PASSAGE
    ref = C1.load(t0, t1, x)
    n = len(ref)
    tr = T.track(ref, SR, mask=C1.ann_mask(ann, t0, t1, int(n / SR / T.HOP_S) + 8))
    runs = [r for r in T.runs(tr["voiced"]) if r[1] - r[0] >= 8]
    on = [tr["t"][a] for a, _ in runs]
    off = [tr["t"][b - 1] for _, b in runs]

    raw = render(gestures(tr, SETTINGS["perf.tone"]), SETTINGS, n)
    held = render(gated_gestures(tr, SETTINGS["perf.tone"]), SETTINGS, n)
    gated = held * envelope(tr, n)

    OUT.mkdir(parents=True, exist_ok=True)
    for name, y in (("real", ref), ("engine_raw", raw), ("engine_gated", gated)):
        y = y / (np.max(np.abs(y)) + 1e-12) * 0.5
        sf.write(OUT / f"{name}.wav", y.astype(np.float32), SR, subtype="FLOAT")
        m = metrics(y, on, off)
        print(f"{name:13s} onset: LF {m['onset'][0]:6.1f} dB, in-band edge {m['onset'][1]:6.1f} dB | "
              f"offset: LF {m['offset'][0]:6.1f} dB, in-band edge {m['offset'][1]:6.1f} dB")


if __name__ == "__main__":
    main()
