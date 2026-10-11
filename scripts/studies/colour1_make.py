"""COLOUR1 — which ingredient makes the resynthesised bird sound real?

MUSHRA-style test (ITU-R BS.1534, simplified): for each passage of 22938, the listener hears the real
recording (reference) and rates hidden versions 0–100 for closeness to it. Versions:
  ref        the real recording again (hidden reference)
  sine       real pitch + loudness on a plain sine                      ("02")
  full       sine + faint harmonics + breath + small room               ("03")
  no_harm    full without harmonics
  no_breath  full without breath
  no_room    full without room
  voice2     full + the tracked second voice
  engine     real gestures through the Syrinx engine (tone 0.9, OEC on f0) + room   ("05", clicks unfixed)
  anchor     sine with strong harmonics (buzzy, like the C5 colour) + room
Every synthetic version gets the recording's own background noise bed and is loudness-matched.

Writes listening/COLOUR1/index.html (self-contained, FLAC inlined) and design.json (hidden key).
usage: python/.venv/bin/python scripts/studies/colour1_make.py
"""
from __future__ import annotations

import base64
import hashlib
import io
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts/review"))
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "python"))
import cuicui_core  # noqa: E402
import track as T  # noqa: E402
import voice2 as V2  # noqa: E402

OUT = ROOT / "listening/COLOUR1"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
SR = 48000
SEED = 20261013
PASSAGES = {"P1": (15.40, 17.85), "P2": (11.90, 13.00), "P3": (26.95, 29.50)}
NOISE_BED = (29.60, 34.20)  # stationary background between bouts (no annotated bird sound)
RNG = np.random.default_rng(SEED)
HP = signal.butter(4, 1000, "highpass", fs=SR, output="sos")


def load(t0, t1, x):
    return signal.sosfiltfilt(HP, x[int(t0 * SR): int(t1 * SR)])


def ann_mask(ann, t0, t1, n):
    m = np.zeros(n, bool)
    for s in ann["segments"]:
        if s["category"] == "BIRD_SOUND":
            a, b = int((s["start"] - t0) / T.HOP_S), int((s["end"] - t0) / T.HOP_S)
            m[max(a, 0): max(min(b, n), 0)] = True
    return m


def per_sample(t, f_frames, a_frames, voiced, n):
    idx = np.flatnonzero(voiced)
    f = np.interp(np.arange(len(f_frames)), idx, f_frames[idx]) if len(idx) else np.full(len(f_frames), 1000.0)
    a = np.convolve(np.where(voiced, a_frames, 0.0), np.ones(3) / 3, mode="same")
    ts = np.arange(n) / SR
    return np.interp(ts, t, f), np.interp(ts, t, a)


def tone(f, a, harmonics=(), breath_db=None):
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph)
    for k, db in harmonics:
        y = y + 10 ** (db / 20) * np.sin(k * ph + 0.3 * k)
    y = y * a
    if breath_db is not None:
        w = RNG.standard_normal(len(f))
        out = np.zeros_like(w)
        y1 = y2 = 0.0
        for i0 in range(0, len(w), 32):
            fc = min(f[i0], 9000.0)
            r = np.exp(-np.pi * (fc / 3.0) / SR)
            c1, c2, g = 2 * r * np.cos(2 * np.pi * fc / SR), -r * r, 1 - r
            for j in range(i0, min(i0 + 32, len(w))):
                yy = g * w[j] + c1 * y1 + c2 * y2
                y2, y1 = y1, yy
                out[j] = yy
        out /= np.sqrt(np.mean(out**2)) + 1e-12
        y = y + 10 ** (breath_db / 20) * out * a
    return y


def room_ir():
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(7)
    ir = rng.standard_normal(n) * np.exp(-6.91 * t / 0.18)
    ir = signal.sosfilt(signal.butter(2, 6500, "lowpass", fs=SR, output="sos"), ir)
    ir[: int(0.004 * SR)] = 0.0
    return ir / np.sqrt(np.sum(ir**2))


IR = room_ir()


def room(y, wet_db=-22.0):
    wet = signal.fftconvolve(y, IR)[: len(y)]
    return y + 10 ** (wet_db / 20) * wet * np.sqrt(np.mean(y**2)) / (np.sqrt(np.mean(wet**2)) + 1e-12)


def engine(tr, n):
    events = []
    cents = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900
    for a, b in T.runs(tr["voiced"]):
        if b - a < 8:
            continue
        seg_c, seg_a = cents[a:b], tr["amp"][a:b]
        anchor_c = float(np.median(seg_c))
        u, src = np.linspace(0, 1, 64), np.linspace(0, 1, b - a)
        adb = np.clip(20 * np.log10(np.maximum(np.interp(u, src, seg_a) / (seg_a.max() + 1e-12), 1e-3)), -40, 0)
        adb[0], adb[-1] = min(adb[0], -30.0), min(adb[-1], -30.0)
        events.append({"t_s": float(tr["t"][a]), "duration_ms": float(b - a), "type": "copy:22938",
                       "anchor_hz": float(440.0 * 2 ** ((anchor_c - 6900) / 1200)),
                       "curves": {"samples": 64, "pitch_cents": np.interp(u, src, seg_c - anchor_c).tolist(),
                                  "amp_db": adb.tolist(), "tone": [0.9] * 64, "side": [-1.0] * 64},
                       "hints": {"oec_ratio": 1.0, "gape": None, "second_voice_cents": None}})
    score = {"format": "cuicui.gesture_score", "version": 1, "name": "colour1", "created_by": "COLOUR1",
             "created_at": "2026-10-11T00:00:00Z",
             "settings": {"body.size": 0.75, "perf.breath": 0.75, "perf.tone": 0.9,
                          "resp.attack_ms": 3.0, "resp.release_ms": 8.0},
             "gestures": events}
    y = np.asarray(cuicui_core.render_score(score, float(SR)), dtype=np.float64)
    return np.pad(y, (0, max(0, n - len(y))))[:n]


def bird_rms(y, voiced_mask_samples):
    return np.sqrt(np.mean(y[voiced_mask_samples] ** 2))


def flac_b64(y):
    buf = io.BytesIO()
    sf.write(buf, np.clip(y, -1, 1).astype(np.float32), SR, format="FLAC", subtype="PCM_16")
    return base64.b64encode(buf.getvalue()).decode()


def main():
    ann = json.loads(REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(REC), always_2d=True)
    assert sr == SR
    x = x.mean(axis=1)
    bed_full = load(*NOISE_BED, x)

    screens, key = [], {}
    for pid, (t0, t1) in PASSAGES.items():
        ref = load(t0, t1, x)
        n = len(ref)
        nfr = int(n / SR / T.HOP_S) + 8
        tr = T.track(ref, SR, mask=ann_mask(ann, t0, t1, nfr))
        f2, a2 = V2.second_voice(ref, SR, tr)
        f, a = per_sample(tr["t"], tr["f0"], tr["amp"], tr["voiced"], n)
        hop = int(T.HOP_S * SR)
        vmask = np.repeat(tr["voiced"], hop)[:n]
        vmask = np.pad(vmask, (0, max(0, n - len(vmask))))[:n].astype(bool)

        harm = ((2, -30.0), (3, -33.0))
        sine = tone(f, a)
        full_dry = tone(f, a, harm, breath_db=-32.0)
        no_harm_dry = tone(f, a, (), breath_db=-32.0)
        no_breath_dry = tone(f, a, harm)
        v2 = np.zeros(n)
        if np.any(~np.isnan(f2)):
            v2voiced = ~np.isnan(f2)
            ff2, aa2 = per_sample(tr["t"][: len(f2)], np.where(v2voiced, f2, np.nan), a2, v2voiced, n)
            v2 = tone(ff2, aa2, harm, breath_db=-32.0)
        anchor = tone(f, a, ((2, -5.0), (3, -10.0), (4, -15.0), (5, -20.0)))
        eng = engine(tr, n)

        versions = {
            "ref": ref,
            "sine": sine,
            "full": room(full_dry),
            "no_harm": room(no_harm_dry),
            "no_breath": room(no_breath_dry),
            "no_room": full_dry,
            "voice2": room(full_dry + v2),
            "engine": room(eng),
            "anchor": room(anchor),
        }
        target = bird_rms(ref, vmask)
        bed = np.resize(bed_full, n)
        for k in list(versions):
            if k == "ref":
                continue
            y = versions[k]
            y = y * target / (bird_rms(y, vmask) + 1e-12)
            versions[k] = y + bed
        peak = max(np.max(np.abs(y)) for y in versions.values())
        g = 0.89 / peak if peak > 0.89 else 1.0
        (OUT / "wav").mkdir(parents=True, exist_ok=True)
        for k in versions:
            versions[k] = versions[k] * g
            sf.write(OUT / "wav" / f"{pid}_{k}.wav", versions[k].astype(np.float32), SR, subtype="FLOAT")

        order = list(RNG.permutation([k for k in versions]))
        letters = "ABCDEFGHIJ"
        screens.append({
            "passage": pid, "dur_s": round(n / SR, 3),
            "reference": flac_b64(versions["ref"]),
            "versions": [{"id": letters[i], "audio": flac_b64(versions[k])} for i, k in enumerate(order)],
        })
        key[pid] = {"span_s": [t0, t1], "letters": {letters[i]: k for i, k in enumerate(order)},
                    "second_voice_ms": int(np.sum(~np.isnan(f2))), "voiced_ms": int(np.sum(tr["voiced"]))}

    screen_order = list(RNG.permutation(len(screens)))
    screens = [screens[i] for i in screen_order]
    page = {"study": "COLOUR1", "version": 1, "seed": SEED, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps([s["passage"] for s in screens] + [key[s["passage"]]["letters"] for s in screens],
                                                    sort_keys=True).encode()).hexdigest()[:12]
    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "colour1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({"study": "COLOUR1", "design_hash": page["design_hash"], "seed": SEED,
                                                 "noise_bed_s": list(NOISE_BED), "key": key,
                                                 "screen_order": [s["passage"] for s in screens]}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens, page {len(html) / 1e6:.1f} MB")
    for pid, k in key.items():
        print(f"  {pid} {k['span_s']}: second voice {k['second_voice_ms']} ms of {k['voiced_ms']} voiced ms; {k['letters']}")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    main()
