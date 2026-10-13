"""MELODY2 — which note rule makes tunes recognisable? (WP-R5 §4.2, pre-registered)

Rating page (template melody2_page.html, adapted from colour1_page.html): 9 screens = 3 melodies x 3 seeds.
Each screen: a reference button (the tune on a soft tone at the bird's target pitches) and six hidden
versions of the bird tapping the tune, rated 0 (not the tune) - 100 (clearly the tune):
  A-E     one candidate note rule each (listening/R5/work/bank_<X>.json, from melody2_banks.py)
  anchor  candidate A with each syllable moved a random 4-6 semitones up or down
All versions of a screen use the same syllables (same seed, same playable set; checked here), the same
gain and the same length. Rendered by the core whistle path (cuicui_core.sing).

Writes listening/R5/melody2/index.html (self-contained, FLAC inlined), design.json (hidden key) and wav/.
usage: python/.venv/bin/python scripts/studies/melody2_make.py   (after melody2_banks.py)
Study: agent documents/studies/MELODY2_note_rule.md
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

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))
import cuicui_core  # noqa: E402

WORK = ROOT / "listening/R5/work"
OUT = ROOT / "listening/R5/melody2"
SR = 48000
SEED = 20261012
RNG = np.random.default_rng(SEED)
CANDIDATES = ("A", "B", "C", "D", "E")
VELOCITY = 100 / 127
TAP_S = 0.06
SETTINGS = {"perf.mapping": "follow", "perf.snap": 0.0, "perf.first_on_note": 1, "voice.harmonics": 1,
            "out.room": 1, "out.stereo_spread": 0.0}  # spread 0: centred, so the page can be mono


def _times(beats: list[float], beat_s: float = 0.42, start: float = 0.3) -> list[float]:
    return [start + beat_s * float(x) for x in np.cumsum([0.0] + beats[:-1])]


FJ_STEP = [0.42] * 7 + [0.6] + [0.42] * 2 + [0.84] + [0.42] * 2  # R1's timing (MELODY1)
MELODIES = {
    "frere_jacques": {
        "title": "Frère Jacques (first two lines)",
        "notes": [60, 62, 64, 60, 60, 62, 64, 60, 64, 65, 67, 64, 65, 67],
        "times": [0.3 + float(t) for t in np.cumsum([0.0] + FJ_STEP)[:14]],
    },
    "au_clair": {
        "title": "Au clair de la lune (first line)",
        "notes": [60, 60, 60, 62, 64, 62, 60, 64, 62, 62, 60],
        "times": _times([1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 4]),
    },
    "scale": {
        "title": "C major scale, up and down",
        "notes": [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60],
        "times": _times([1] * 15),
    },
}
SEEDS = {"frere_jacques": (101, 102, 103), "au_clair": (201, 202, 203), "scale": (301, 302, 303)}


def hz(cents):
    return 440.0 * 2.0 ** ((np.asarray(cents, dtype=float) - 6900.0) / 1200.0)


def anchor_bank() -> tuple[Path, dict]:
    """Candidate A with every syllable's note moved by a random ±400-600 c (so the syllable moves the other way)."""
    bank = json.loads((WORK / "bank_A.json").read_text())
    rng = np.random.default_rng(SEED + 1)
    offsets = {}
    k = 0
    for p in bank["phrases"]:
        for s in p["syllables"]:
            off = float(rng.choice([-1.0, 1.0]) * rng.uniform(400.0, 600.0))
            s["anchors"]["rule"] = round(s["anchors"]["rule"] + off, 1)
            offsets[k] = round(-off, 1)  # how far the sung syllable moves
            k += 1
    bank["name"] = "zf_22938_melody2_anchor"
    bank["hash"] = "melody2anchr"
    path = WORK / "bank_anchor.json"
    path.write_text(json.dumps(bank, separators=(",", ":"), ensure_ascii=False) + "\n")
    return path, offsets


def events(m: dict) -> list[tuple]:
    ev = []
    for n, t in zip(m["notes"], m["times"], strict=True):
        ev += [(t, "on", n, VELOCITY), (t + TAP_S, "off", n, 0.0)]
    return ev


def reference(m: dict, n: int) -> np.ndarray:
    """The tune on a soft sine at the bird's target pitches (follow: 3 octaves above the key), as MELODY1 item 20."""
    y = np.zeros(n)
    last = len(m["notes"]) - 1
    for k, (note, t) in enumerate(zip(m["notes"], m["times"], strict=True)):
        length = 0.6 if k == last else 0.3
        nn = int(length * SR)
        tt = np.arange(nn) / SR
        env = np.minimum(1.0, tt / 0.01) * np.minimum(1.0, (length - tt) / 0.06)
        a = int(t * SR)
        seg = np.sin(2 * np.pi * hz((note + 36) * 100.0) * tt) * env
        y[a:a + nn] += seg[: max(0, min(nn, n - a))]
    return y


def active_rms(y: np.ndarray) -> float:
    """RMS over the 10 ms frames within 40 dB of the loudest frame."""
    fr = y[: len(y) // 480 * 480].reshape(-1, 480)
    r = np.sqrt(np.mean(fr**2, axis=1))
    keep = r > r.max() * 10 ** (-40 / 20)
    return float(np.sqrt(np.mean(r[keep] ** 2)))


def flac_b64(y: np.ndarray) -> str:
    buf = io.BytesIO()
    sf.write(buf, np.clip(y, -1, 1).astype(np.float32), SR, format="FLAC", subtype="PCM_16")
    return base64.b64encode(buf.getvalue()).decode()


def main() -> None:
    (OUT / "wav").mkdir(parents=True, exist_ok=True)
    banks = {x: WORK / f"bank_{x}.json" for x in CANDIDATES}
    banks["anchor"], offsets = anchor_bank()
    screens, key = [], {}
    for mid, m in MELODIES.items():
        dur = m["times"][-1] + 1.2
        for rep, seed in enumerate(SEEDS[mid], start=1):
            sid = f"{mid}-{rep}"
            versions, logs = {}, {}
            for v, path in banks.items():
                audio, log = cuicui_core.sing(str(path), events(m), SETTINGS, float(SR), dur, seed, 256)
                versions[v] = np.asarray(audio, dtype=np.float64)[:, 0] * np.sqrt(2.0)  # centred: L = x·cos(π/4)
                logs[v] = log
            same = {v: [(e["syllable"], e["sample"]) for e in logs[v]] for v in logs}
            assert all(same[v] == same["A"] for v in same), f"{sid}: versions must use the same syllables"
            assert len(logs["A"]) == len(m["notes"]) and all(e["first"] for e in logs["A"]), sid
            n = len(versions["A"])
            g = 0.5 / max(np.max(np.abs(y)) for y in versions.values())  # one gain for every version
            versions = {v: y * g for v, y in versions.items()}
            bird = np.mean([active_rms(y) for y in versions.values()])
            ref = reference(m, n)
            ref *= bird * 10 ** (-3 / 20) / active_rms(ref)  # soft: 3 dB under the bird
            for v, y in versions.items():
                sf.write(OUT / "wav" / f"{sid}_{v}.wav", y.astype(np.float32), SR, subtype="FLOAT")
            sf.write(OUT / "wav" / f"{sid}_reference.wav", ref.astype(np.float32), SR, subtype="FLOAT")
            order = list(RNG.permutation(list(versions)))
            letters = "ABCDEF"
            screens.append({"id": sid, "title": m["title"], "dur_s": round(n / SR, 3),
                            "reference": flac_b64(ref),
                            "versions": [{"id": letters[i], "audio": flac_b64(versions[v])}
                                         for i, v in enumerate(order)]})
            key[sid] = {"melody": mid, "seed": seed, "letters": {letters[i]: str(v) for i, v in enumerate(order)},
                        "syllables": [e["syllable"] for e in logs["A"]],
                        "labels": [e["label"] for e in logs["A"]],
                        "shift_cents": {v: [round(e["shift_cents"], 1) for e in logs[v]] for v in logs},
                        "anchor_moves_cents": [offsets[e["syllable"]] for e in logs["A"]]}
    screens = [screens[i] for i in RNG.permutation(len(screens))]
    page = {"study": "MELODY2", "version": 1, "seed": SEED, "minutes": 15, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [[s["id"], key[s["id"]]["seed"], key[s["id"]]["letters"]] for s in screens], sort_keys=True
    ).encode()).hexdigest()[:12]
    html = (HERE / "melody2_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.STUDY = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    design = {"study": "MELODY2", "design_hash": page["design_hash"], "seed": SEED, "settings": SETTINGS,
              "tap_s": TAP_S, "velocity": VELOCITY, "melodies": MELODIES, "banks": {v: str(p.relative_to(ROOT))
                                                                                    for v, p in banks.items()},
              "bank_hashes": {v: json.loads(p.read_text())["hash"] for v, p in banks.items()},
              "screen_order": [s["id"] for s in screens], "key": key}
    (OUT / "design.json").write_text(json.dumps(design, indent=1, ensure_ascii=False))
    print(f"design {page['design_hash']}: {len(screens)} screens, page {len(html) / 1e6:.1f} MB")
    for s in screens:
        k = key[s["id"]]
        print(f"  {s['id']:16s} seed {k['seed']}  {s['dur_s']:.2f} s  {' '.join(k['labels'])}")


if __name__ == "__main__":
    main()
