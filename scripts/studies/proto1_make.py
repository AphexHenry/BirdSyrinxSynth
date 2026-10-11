"""PROTO1 — playable web prototype of playing modes A + B with real syllables.

Builds listening/PROTO1/index.html (self-contained): every voiced syllable of 22938, grouped into the
bird's real phrases, with per-syllable note anchors for each pitch rule. Audio is synthesised in the
page (whistle, or whistle + colour); see scripts/studies/proto1_page.html.

usage: python/.venv/bin/python scripts/studies/proto1_make.py
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

HERE = Path(__file__).resolve().parent
OUT = ROOT / "listening/PROTO1"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
PHRASE_GAP_S = 0.15     # annotated segments further apart than this start a new phrase
STEADY_TOL_C = 50.0     # "steady": pitch stays within ±50 cents
STEADY_MIN_MS = 20      # shorter steady parts don't count: use the ending instead
V0 = 2000.0             # stability weight for the "steady parts" (PITCH1/2) rule


def longest_steady(c, tol=STEADY_TOL_C):
    best = (0, 0, 0)
    n = len(c)
    for i in range(n):
        lo = hi = c[i]
        for j in range(i, n):
            lo, hi = min(lo, c[j]), max(hi, c[j])
            if hi - lo > 2 * tol:
                break
            if j + 1 - i > best[0]:
                best = (j + 1 - i, i, j + 1)
    return best


def anchors(c):
    q = max(1, len(c) // 4)
    landing = float(c[-q:].mean())
    n, i, j = longest_steady(c)
    use_steady = n >= STEADY_MIN_MS
    slope = np.abs(np.gradient(c)) * 1000.0
    w = np.exp(-slope / V0)
    return {
        "rule": float(c[i:j].mean()) if use_steady else landing,  # the human's rule (decision 12)
        "rule_used": "steady part" if use_steady else "ending",
        "steady_ms": int(n),
        "landing": landing,
        "steady": float(np.sum(w * c) / np.sum(w)),
        "average": float(c.mean()),
    }


def main():
    ann = json.loads(REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(REC), always_2d=True)
    x = x.mean(axis=1)
    sos = signal.butter(4, 1000, "highpass", fs=sr, output="sos")

    segs = [(k, s) for k, s in enumerate(ann["segments"]) if s["category"] == "BIRD_SOUND"]
    groups, cur = [], []
    for k, s in segs:
        if cur and s["start"] - cur[-1][1]["end"] > PHRASE_GAP_S:
            groups.append(cur)
            cur = []
        cur.append((k, s))
    if cur:
        groups.append(cur)

    phrases = []
    for g in groups:
        t0, t1 = g[0][1]["start"] - 0.02, g[-1][1]["end"] + 0.02
        seg = signal.sosfiltfilt(sos, x[int(t0 * sr): int(t1 * sr)])
        tr = T.track(seg, sr)
        syl = []
        for k, s in g:
            i0, i1 = int((s["start"] - t0) / T.HOP_S), int((s["end"] - t0) / T.HOP_S)
            v = tr["voiced"][i0:i1]
            if v.sum() < 15:
                continue
            idx = np.flatnonzero(v)
            j0, j1 = i0 + idx[0], i0 + idx[-1] + 1
            vv = tr["voiced"][j0:j1]
            f = tr["f0"][j0:j1].copy()
            f[~vv] = np.nan
            good = np.flatnonzero(~np.isnan(f))
            f = np.interp(np.arange(len(f)), good, f[good])
            c = signal.medfilt(1200 * np.log2(f / 440.0) + 6900.0, 5)
            a = np.where(vv, tr["amp"][j0:j1], 0.0)
            a = np.convolve(a, np.ones(3) / 3, mode="same")
            w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, 3))
            a[:3] *= w
            a[-3:] *= w[::-1]
            syl.append({"segment": k, "label": "+".join(e.get("type_label", "") for e in s.get("elements", [])),
                        "t_ms": int(j0), "c": c, "a": a, "anchors": anchors(c[vv])})
        if not syl:
            continue
        amax = max(s["a"].max() for s in syl)
        base = syl[0]["t_ms"]
        phrases.append({
            "id": len(phrases),
            "start_s": round(t0 + base * T.HOP_S, 3),
            "syllables": [{
                "segment": s["segment"], "label": s["label"], "t_ms": s["t_ms"] - base,
                "f_hz": [round(float(v), 1) for v in 440.0 * 2 ** ((s["c"] - 6900.0) / 1200.0)],
                "a": [round(float(v), 3) for v in s["a"] / amax],
                "anchors": {k: (round(v, 1) if isinstance(v, float) else v) for k, v in s["anchors"].items()},
            } for s in syl],
        })

    all_rule = [s["anchors"]["rule"] for p in phrases for s in p["syllables"]]
    bank = {"source": "22938 (zebra finch)", "phrases": phrases,
            "bird_centre_cents": round(float(np.median(all_rule)), 1),
            "rules": {"rule": "longest steady part (±50 c, ≥ 20 ms), else ending",
                      "steady": "stability-weighted (PITCH1/2)", "landing": "last quarter", "average": "plain mean"}}
    bank["hash"] = hashlib.sha256(json.dumps(bank, sort_keys=True).encode()).hexdigest()[:12]

    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "proto1_page.html").read_text()
    html = html.replace("<!--BANK-->", "<script>window.BANK = " + json.dumps(bank, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)

    n_syl = sum(len(p["syllables"]) for p in phrases)
    print(f"bank {bank['hash']}: {len(phrases)} phrases, {n_syl} syllables, bird centre "
          f"{bank['bird_centre_cents']:.0f} c ({440 * 2 ** ((bank['bird_centre_cents'] - 6900) / 1200):.0f} Hz), "
          f"page {len(html) / 1e6:.2f} MB")
    for p in phrases:
        labels = " ".join(s["label"] for s in p["syllables"])
        dur = p["syllables"][-1]["t_ms"] + len(p["syllables"][-1]["f_hz"])
        print(f"  phrase {p['id']:2d} @ {p['start_s']:6.2f}s  {len(p['syllables']):2d} syll  {dur:5d} ms  {labels}")


if __name__ == "__main__":
    main()
