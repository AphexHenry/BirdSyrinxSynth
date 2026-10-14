"""INGEST1 — the listener checks the automatically marked syllables of the new recordings (WP-R3).

Material (marked by `cuicui.bank.segment`, tracked by the bank builder):
  22938, 53.3 s to the end: the part the human annotation left out (same bird as the factory bank);
  22937 (house finch, Macaulay 22937), the whole recording.
Each phrase is shown as a spectrogram with the tracked pitch curves on top. The listener plays the
real phrase and its whistle copy (the PROTO1 voice, synthesised in the page), and marks a syllable
"not the bird" or "whistle wrong"; unmarked syllables are kept. Labels are proposed (nearest
human-labelled 22938 syllables; long rising sounds → "alarm") and can be changed. A check round asks
10 syllables again. `ingest1_analyze.py` turns the results into the final annotation files.

Writes listening/R3/ingest1/index.html and design.json.
usage: python/.venv/bin/python scripts/studies/ingest1_make.py   (after r3_material.py)
Study: agent documents/studies/INGEST1_new_syllables.md
"""
from __future__ import annotations

import base64
import hashlib
import io
import json
import sys
from collections import Counter
from pathlib import Path

import matplotlib
import numpy as np
import soundfile as sf
from scipy import signal

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))
from cuicui.analysis import f0_track  # noqa: E402

WORK = ROOT / "build/r3"
OUT = ROOT / "listening/R3/ingest1"
REC = {
    "22938": Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav"),
    "22937": ROOT / "recordings/22937 - House Finch.wav",
}
BANKS = {"22938": WORK / "bank_22938_full.json", "22937": WORK / "bank_22937.json"}
FIRST_NEW_PHRASE = {"22938": 12, "22937": 0}  # 22938's first 12 phrases are the factory bank
SR_OUT = 32000
CTX_S = 0.15  # audio context around a phrase
FMIN, FMAX = 1000.0, 8000.0
PX_PER_MS = 0.4
IMG_H = 150
LABELS = ["swipe", "peep", "swipe+swipe", "Hswipe", "Lswipe", "Lpeep", "peep_grillon", "double",
          "alarm", "alarm_grillon", "other"]
ALARM_MIN_MS, ALARM_MIN_RISE = 200, 400.0  # a long rising sound is proposed as a release call
PER_SCREEN_MS = 9000  # about this much phrase audio per screen
N_POINTS = 24


def cents(f: list[float]) -> np.ndarray:
    return 1200.0 * np.log2(np.asarray(f, float) / 440.0) + 6900.0


def shape(c: np.ndarray) -> np.ndarray:
    return np.interp(np.linspace(0, len(c) - 1, N_POINTS), np.arange(len(c)), c)


def distance(a: tuple[np.ndarray, int], b: tuple[np.ndarray, int]) -> float:
    """Median |Δ cents| over 24 points + 600 c per e-fold of length (WP-R3 clustering)."""
    return float(np.median(np.abs(a[0] - b[0])) + 600.0 * abs(np.log(a[1] / b[1])))


def flac_b64(y: np.ndarray) -> str:
    buf = io.BytesIO()
    sf.write(buf, np.clip(y, -1, 1).astype(np.float32), SR_OUT, format="FLAC", subtype="PCM_16")
    return base64.b64encode(buf.getvalue()).decode()


def png_b64(y: np.ndarray, ms: int) -> str:
    f, _t, s = signal.spectrogram(y, SR_OUT, nperseg=256, noverlap=256 - 32)
    band = (f >= FMIN) & (f <= FMAX)
    db = 10 * np.log10(s[band] + 1e-14)
    lo, hi = np.percentile(db, 55), db.max()
    w = max(120, int(ms * PX_PER_MS))
    fig = plt.figure(figsize=(w / 100, IMG_H / 100), dpi=100)
    ax = fig.add_axes((0, 0, 1, 1))
    ax.imshow(np.clip((db - lo) / (hi - lo), 0, 1), origin="lower", aspect="auto", cmap="gray_r",
              interpolation="bilinear")
    ax.set_axis_off()
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=100)
    plt.close(fig)
    return base64.b64encode(buf.getvalue()).decode()


def main() -> None:
    factory = json.loads((ROOT / "assets/voicebanks/zf_22938/bank.json").read_text())
    ref = [(shape(cents(s["f_hz"])), len(s["f_hz"]), s["label"])
           for p in factory["phrases"] for s in p["syllables"]]
    phrases, items = [], []
    for rec, bank_path in BANKS.items():
        bank = json.loads(bank_path.read_text())
        x, sr = sf.read(str(REC[rec]), always_2d=True)
        x = signal.resample_poly(x.mean(axis=1), SR_OUT, sr)
        sos = signal.butter(4, 800.0, "highpass", fs=SR_OUT, output="sos")
        for p in bank["phrases"][FIRST_NEW_PHRASE[rec]:]:
            syl = p["syllables"]
            end_ms = syl[-1]["t_ms"] + len(syl[-1]["f_hz"])
            t0 = p["start_s"] - CTX_S
            ms = int(end_ms + 2 * CTX_S * 1000)
            y = x[max(0, int(t0 * SR_OUT)): int(t0 * SR_OUT) + int(ms * SR_OUT / 1000)]
            y = signal.sosfiltfilt(sos, y)
            y = 0.5 * y / (np.abs(y).max() + 1e-9)
            ids = []
            for k, s in enumerate(syl):
                c = cents(s["f_hz"])
                me = (shape(c), len(c))
                d = sorted((distance(me, (r[0], r[1])), r[2]) for r in ref)
                votes = Counter(lab for _, lab in d[:3]).most_common()
                label = votes[0][0] if votes[0][1] > 1 else d[0][1]
                rise = float(c[-20:].mean() - c[:20].mean())
                if len(c) >= ALARM_MIN_MS and rise >= ALARM_MIN_RISE:
                    label = "alarm"
                elif label.startswith("alarm"):  # never a short or flat sound
                    label = "other"
                uid = f"{rec}:{s['segment']}"
                items.append({"id": len(items), "uid": uid, "rec": rec, "segment": s["segment"],
                              "phrase": len(phrases), "k": k,
                              "t_ms": int(s["t_ms"] + CTX_S * 1000), "ms": len(c),
                              "f": [round(v) for v in s["f_hz"]], "a": s["a"],
                              "label": label, "near": round(d[0][0]), "rise": round(rise),
                              "hop": "hop" in s["flags"]})
                ids.append(items[-1]["id"])
            phrases.append({"id": len(phrases), "rec": rec, "bank_phrase": p["id"],
                            "start_s": round(t0, 3), "ms": ms, "items": ids,
                            "audio": flac_b64(y), "png": png_b64(y, ms)})
    # screens: consecutive phrases of one recording, about PER_SCREEN_MS of audio each
    screens: list[dict] = []
    for p in phrases:
        last = screens[-1] if screens else None
        if (last is None or last["rec"] != p["rec"] or last["ms"] + p["ms"] > PER_SCREEN_MS):
            screens.append({"rec": p["rec"], "phrases": [], "ms": 0})
        screens[-1]["phrases"].append(p["id"])
        screens[-1]["ms"] += p["ms"]
    page = {"study": "INGEST1", "version": 1, "labels": LABELS, "fmin": FMIN, "fmax": FMAX,
            "banks": {r: json.loads(b.read_text())["hash"] for r, b in BANKS.items()},
            "screens": screens, "phrases": phrases, "items": items}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [page["banks"], screens, [(it["uid"], it["label"]) for it in items]], sort_keys=True
    ).encode()).hexdigest()[:12]
    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "ingest1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.STUDY = " + json.dumps(
        page, separators=(",", ":"), ensure_ascii=False) + ";</script>")
    (OUT / "index.html").write_text(html)
    design = {k: v for k, v in page.items() if k not in ("phrases", "items")}
    design["phrases"] = [{k: v for k, v in p.items() if k not in ("audio", "png")} for p in phrases]
    design["items"] = [{k: v for k, v in it.items() if k not in ("f", "a")} for it in items]
    (OUT / "design.json").write_text(json.dumps(design, indent=1, ensure_ascii=False))
    by = Counter((it["rec"], it["label"]) for it in items)
    print(f"design {page['design_hash']}: {len(items)} syllables, {len(phrases)} phrases, "
          f"{len(screens)} screens, {sum(p['ms'] for p in phrases) / 1000:.0f} s of audio, "
          f"page {len(html) / 1e6:.1f} MB")
    print("  proposed labels:", dict(sorted(by.items())))
    print("  hops:", sum(it["hop"] for it in items), " tracker hop threshold:", f0_track.HOP_S)


if __name__ == "__main__":
    main()
