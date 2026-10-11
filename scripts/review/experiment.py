"""Approach review: where is 'bird' lost — in the gestures or in the voice?

2x2 copy-synthesis experiment on recording 22938:
  gestures  {REAL contour, GENERATED (C5) contour}
  voice     {plain whistle, Syrinx engine}
All renders are 48 kHz mono, loudness-matched on voiced frames.

usage: python experiment.py <scratch_dir> <repo_root> <out_dir>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

SCR = Path(sys.argv[1])
ROOT = Path(sys.argv[2])
OUT = Path(sys.argv[3])
sys.path.insert(0, str(SCR))
sys.path.insert(0, str(ROOT / "python"))
import cuicui_core  # noqa: E402
import track as T  # noqa: E402

SR = 48000
REAL = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
ANN = REAL.with_suffix(".json")
PASSAGES = [(11.95, 12.95), (15.40, 17.85)]  # seconds in 22938
GAP_S = 0.45
C5_WAV = ROOT / "listening/C5/02_motion_low_high.wav"
C5_SPAN = (12.0, 15.5)
RNG = np.random.default_rng(7)

OUT.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------- helpers
def load(path, t0, t1):
    x, sr = sf.read(str(path), always_2d=True)
    assert sr == SR, sr
    x = x.mean(axis=1)[int(t0 * sr): int(t1 * sr)]
    sos = signal.butter(4, 1000, "highpass", fs=sr, output="sos")
    return signal.sosfiltfilt(sos, x)


def ann_mask(t0, t1, n):
    d = json.load(open(ANN))
    m = np.zeros(n, bool)
    for s in d["segments"]:
        if s["category"] == "BIRD_SOUND":
            a, b = int((s["start"] - t0) / T.HOP_S), int((s["end"] - t0) / T.HOP_S)
            m[max(a, 0): max(min(b, n), 0)] = True
    return m


def concat(parts):
    gap = np.zeros(int(GAP_S * SR))
    out = []
    for p in parts:
        out += [p, gap]
    return np.concatenate(out[:-1])


def track_signal(x, mask=None):
    n = int(len(x) / SR / T.HOP_S) + 2
    return T.track(x, SR, mask=None if mask is None else mask[:n])


def frames_to_samples(tr, n):
    """Per-sample f0 (Hz, held through unvoiced) and amplitude (0 when unvoiced)."""
    t = tr["t"]
    f0 = tr["f0"].copy()
    v = tr["voiced"]
    # hold f0 through gaps so phase stays continuous
    idx = np.where(v)[0]
    if len(idx) == 0:
        return np.full(n, 1000.0), np.zeros(n)
    f0 = np.interp(np.arange(len(f0)), idx, f0[idx])
    amp = tr["amp"].copy()
    amp = np.convolve(amp, np.ones(3) / 3, mode="same")  # 3 ms smoothing
    ts = np.arange(n) / SR
    return np.interp(ts, t, f0), np.interp(ts, t, amp)


def whistle(f0s, amps, harmonics=(), noise_db=None):
    ph = 2 * np.pi * np.cumsum(f0s) / SR
    y = np.sin(ph)
    for k, db in harmonics:
        y += 10 ** (db / 20) * np.sin(k * ph + 0.3 * k)
    y *= amps
    if noise_db is not None:
        # breath: white noise through a resonator that follows f0 (Q~3), shaped by amplitude
        w = RNG.standard_normal(len(f0s))
        out = np.zeros_like(w)
        y1 = y2 = 0.0
        for i in range(0, len(w), 32):
            fc = min(f0s[i], 9000.0)
            r = np.exp(-np.pi * (fc / 3.0) / SR)
            c1, c2 = 2 * r * np.cos(2 * np.pi * fc / SR), -r * r
            g = 1 - r
            seg = w[i: i + 32]
            o = np.empty_like(seg)
            for j, s in enumerate(seg):
                yy = g * s + c1 * y1 + c2 * y2
                y2, y1 = y1, yy
                o[j] = yy
            out[i: i + 32] = o
        out /= np.sqrt(np.mean(out**2)) + 1e-12
        y += 10 ** (noise_db / 20) * out * amps
    return y


def room(x, wet_db=-22.0, rt60=0.18):
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    ir = RNG.standard_normal(n) * np.exp(-6.91 * t / rt60)
    sos = signal.butter(2, 6500, "lowpass", fs=SR, output="sos")
    ir = signal.sosfilt(sos, ir)
    ir[: int(0.004 * SR)] = 0.0
    ir /= np.sqrt(np.sum(ir**2))
    wet = signal.fftconvolve(x, ir)[: len(x)]
    return x + 10 ** (wet_db / 20) * wet * np.sqrt(np.mean(x**2)) / (np.sqrt(np.mean(wet**2)) + 1e-12)


def loudness_match(x, target_db=-24.0):
    tr = track_signal(x)
    v = tr["voiced"]
    hop = int(T.HOP_S * SR)
    mask = np.repeat(v, hop)[: len(x)]
    if mask.sum() < 100:
        mask = np.ones(len(x), bool)
    rms = np.sqrt(np.mean(x[: len(mask)][mask[: len(x)]] ** 2))
    y = x * 10 ** (target_db / 20) / (rms + 1e-12)
    pk = np.max(np.abs(y))
    if pk > 0.89:
        y *= 0.89 / pk
    return y


# ---------------------------------------------------------------- engine copy-synthesis
def gestures_from_track(tr, t_offset=0.0, min_ms=8):
    """One gesture event per voiced element: pitch rel. to element median, amp rel. to element peak."""
    events = []
    t = tr["t"]
    cents = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], 440.0) / 440.0) + 6900
    for a, b in T.runs(tr["voiced"]):
        if (b - a) * T.HOP_S * 1000 < min_ms:
            continue
        seg_c = cents[a:b]
        seg_a = tr["amp"][a:b]
        anchor_c = float(np.median(seg_c))
        u = np.linspace(0, 1, 64)
        src_u = np.linspace(0, 1, b - a)
        pc = np.interp(u, src_u, seg_c - anchor_c)
        adb = 20 * np.log10(np.maximum(np.interp(u, src_u, seg_a) / (seg_a.max() + 1e-12), 1e-3))
        adb = np.clip(adb, -40.0, 0.0)
        # short fade at both ends, as the real element starts/ends from silence
        adb[0] = min(adb[0], -30.0)
        adb[-1] = min(adb[-1], -30.0)
        events.append(
            {
                "t_s": float(t[a] + t_offset),
                "duration_ms": float((b - a) * T.HOP_S * 1000),
                "type": "copy:22938",
                "anchor_hz": float(440.0 * 2 ** ((anchor_c - 6900) / 1200)),
                "curves": {
                    "samples": 64,
                    "pitch_cents": pc.tolist(),
                    "amp_db": adb.tolist(),
                    "tone": [TONE] * 64,
                    "side": [-1.0] * 64,
                },
                "hints": {"oec_ratio": OEC_RATIO, "gape": None, "second_voice_cents": None},
            }
        )
    return events


def engine_render(events, settings, dur_s):
    score = {
        "format": "cuicui.gesture_score",
        "version": 1,
        "name": "review_copy_22938",
        "created_by": "approach-review",
        "created_at": "2026-10-11T00:00:00Z",
        "settings": settings,
        "gestures": events,
    }
    y = np.asarray(cuicui_core.render_score(score, float(SR)), dtype=np.float64)
    n = int(dur_s * SR)
    return np.pad(y, (0, max(0, n - len(y))))[:n], score


# ---------------------------------------------------------------- build
items, table = [], {}

# real passages + tracks
real_parts, tracks, offsets = [], [], []
off = 0.0
for t0, t1 in PASSAGES:
    x = load(REAL, t0, t1)
    n = int(len(x) / SR / T.HOP_S) + 2
    tracks.append(track_signal(x, ann_mask(t0, t1, n)))
    real_parts.append(x)
    offsets.append(off)
    off += (t1 - t0) + GAP_S
real = concat(real_parts)
total_s = len(real) / SR

# R0: the recording
r0 = loudness_match(real)

# W1: real contour -> pure sine
w1_parts, w2_parts = [], []
for x, tr in zip(real_parts, tracks):
    f0s, amps = frames_to_samples(tr, len(x))
    w1_parts.append(whistle(f0s, amps))
    w2_parts.append(whistle(f0s, amps, harmonics=((2, -30.0), (3, -33.0)), noise_db=-32.0))
w1 = loudness_match(concat(w1_parts))
w2_dry = concat(w2_parts)
w2 = loudness_match(room(w2_dry))
DRY = {"03": w2_dry}

# E1/E2: real contour -> Syrinx engine (gesture score through VoiceController + BirdVoice)
base_settings = {
    "body.size": 0.75,  # σ=1 tops out at C8 (4.19 kHz); this bird reaches ~4.9 kHz
    "perf.breath": 0.75,
    "resp.attack_ms": 3.0,
    "resp.release_ms": 8.0,
}
all_events = {}
for name, tone, oec in (("default", 0.6, None), ("tuned", 0.9, 1.0)):
    TONE, OEC_RATIO = tone, oec
    ev = []
    for tr, o in zip(tracks, offsets):
        ev += gestures_from_track(tr, t_offset=o)
    st = dict(base_settings, **{"perf.tone": tone})
    y, score = engine_render(ev, st, total_s)
    all_events[name] = score
    (OUT / f"score_{name}.gscore.json").write_text(json.dumps(score))
    if name == "default":
        e1 = loudness_match(room(y))
        DRY["04"] = y
    else:
        e2 = loudness_match(room(y))
        DRY["05"] = y

# G1: generated (C5) contour -> pure sine; C0: the C5 render itself
c5 = load(C5_WAV, *C5_SPAN)
trg = track_signal(c5)
f0s, amps = frames_to_samples(trg, len(c5))
g1 = loudness_match(whistle(f0s, amps))
c0 = loudness_match(c5)

renders = [
    ("01_real_recording.wav", r0, "Real bird (22938), two passages",
     "Reference. Fast swoops of ~half an octave in ~75 ms, almost pure whistle.", "reference"),
    ("02_real_gestures_pure_whistle.wav", w1, "Real gestures → plain sine whistle",
     "Only the bird's pitch + loudness curves on a bare sine. Is the bird already there?", "real gestures"),
    ("03_real_gestures_whistle_colour.wav", w2, "Real gestures → whistle + weak harmonics + breath + small room",
     "Same curves, harmonics at the bird's measured −30 dB, a little breath noise and room.", "real gestures"),
    ("04_real_gestures_engine_default.wav", e1, "Real gestures → Syrinx engine (current defaults)",
     "The real bird's curves played by our anatomical engine as the plugin is set today (tone 0.6, OEC tracking). Same room as 03.",
     "real gestures"),
    ("05_real_gestures_engine_tuned.wav", e2, "Real gestures → Syrinx engine (tone 0.9, OEC on f0)",
     "Best quick physical setting: purer source, OEC resonance placed on the fundamental (Riede 2006). Same room.",
     "real gestures"),
    ("06_c5_gestures_pure_whistle.wav", g1, "C5 generated gestures → plain sine whistle",
     "Our generator's pitch + loudness curves on the same bare sine as 02. Compare 02 vs 06: the melody shapes alone.",
     "generated gestures"),
    ("07_c5_original.wav", c0, "C5 as you heard it (02, high motion)", "Generated gestures + engine.",
     "generated gestures"),
]
for fname, y, title, lf, grp in renders:
    sf.write(OUT / fname, y.astype(np.float32), SR, subtype="FLOAT")
    tr = track_signal(DRY.get(fname[:2], y), None) if not fname.startswith("01") else None
    if fname.startswith("01"):
        # metrics of the real passages from the annotated tracks
        ms = [T.metrics(t) for t in tracks]
        m = {k: (ms[1][k] if not isinstance(ms[1][k], tuple) else ms[1][k]) for k in ms[1]}
    else:
        m = T.metrics(tr)
    table[fname] = m
    items.append({
        "file": fname, "title": title, "listen_for": lf, "group": grp,
        "metrics": {
            "median f0 (Hz)": round(m["median_f0_hz"]),
            "f0 5–95 % (Hz)": f"{m['f0_p5_p95_hz'][0]:.0f}–{m['f0_p5_p95_hz'][1]:.0f}",
            "pitch speed, median (cents/s)": round(m["fm_median_cents_per_s"]),
            "steady frames (<1500 c/s)": f"{100 * m['steady_frac']:.0f} %",
            "pitch range per element (cents)": round(m["range_per_element_cents"]),
            "element length, median (ms)": round(m["element_ms_median"]),
            "2nd harmonic vs fundamental (dB)": round(m["h2_minus_h1_db"], 1),
        },
    })

manifest = {
    "format": "cuicui.listening_manifest", "version": 1, "wp": "REVIEW",
    "title": "Approach review — is the bird lost in the gestures or in the voice?",
    "items": items,
}
(OUT / "manifest.json").write_text(json.dumps(manifest, indent=1))
(OUT / "metrics.json").write_text(json.dumps(table, indent=1))
for k, m in table.items():
    print(f"{k:42s} f0={m['median_f0_hz']:6.0f}  speed={m['fm_median_cents_per_s']:7.0f}  "
          f"steady={100 * m['steady_frac']:3.0f}%  range={m['range_per_element_cents']:5.0f}  "
          f"elem={m['element_ms_median']:4.0f}ms  H2-H1={m['h2_minus_h1_db']:6.1f}  H3-H1={m['h3_minus_h1_db']:6.1f}")
