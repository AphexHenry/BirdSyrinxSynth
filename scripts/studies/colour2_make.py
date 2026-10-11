"""COLOUR2 — pure sine vs gentler breath and harmonics (graded levels).

Follow-up to COLOUR1: breath at -32 dB hurt and faint harmonics at -30 dB did nothing consistent.
Same MUSHRA-style page and processing as COLOUR1 (real gestures, recording's noise bed, loudness-matched),
no room, two passages. Versions:
  ref                     hidden reference (the real recording)
  sine                    pure sine
  breath_50/44/38/32      sine + breath (noise on f0) at -50 / -44 / -38 / -32 dB (-32 = the COLOUR1 level)
  harm_46/38/30           sine + H2 at -46 / -38 / -30 dB (H3 3 dB lower; -30 = the COLOUR1 level)
  mix                     sine + H2 -40 dB + breath -46 dB

usage: python/.venv/bin/python scripts/studies/colour2_make.py
"""
from __future__ import annotations

import hashlib
import json

import numpy as np
import soundfile as sf

import colour1_make as C1

OUT = C1.ROOT / "listening/COLOUR2"
SEED = 20261014
PASSAGES = {"P1": (15.40, 17.85), "P3": (26.95, 29.50)}
RNG = np.random.default_rng(SEED)


def harm(db):
    return ((2, db), (3, db - 3.0))


def main():
    ann = json.loads(C1.REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(C1.REC), always_2d=True)
    x = x.mean(axis=1)
    bed_full = C1.load(*C1.NOISE_BED, x)
    (OUT / "wav").mkdir(parents=True, exist_ok=True)

    screens, key = [], {}
    for pid, (t0, t1) in PASSAGES.items():
        ref = C1.load(t0, t1, x)
        n = len(ref)
        tr = C1.T.track(ref, C1.SR, mask=C1.ann_mask(ann, t0, t1, int(n / C1.SR / C1.T.HOP_S) + 8))
        f, a = C1.per_sample(tr["t"], tr["f0"], tr["amp"], tr["voiced"], n)
        hop = int(C1.T.HOP_S * C1.SR)
        vmask = np.pad(np.repeat(tr["voiced"], hop)[:n], (0, max(0, n - len(tr["voiced"]) * hop)))[:n].astype(bool)

        versions = {"ref": ref, "sine": C1.tone(f, a)}
        for db in (50, 44, 38, 32):
            versions[f"breath_{db}"] = C1.tone(f, a, (), breath_db=-float(db))
        for db in (46, 38, 30):
            versions[f"harm_{db}"] = C1.tone(f, a, harm(-float(db)))
        versions["mix"] = C1.tone(f, a, harm(-40.0), breath_db=-46.0)

        target = C1.bird_rms(ref, vmask)
        bed = np.resize(bed_full, n)
        for k in list(versions):
            if k != "ref":
                y = versions[k]
                versions[k] = y * target / (C1.bird_rms(y, vmask) + 1e-12) + bed
        g = min(1.0, 0.89 / max(np.max(np.abs(y)) for y in versions.values()))
        for k in versions:
            versions[k] = versions[k] * g
            sf.write(OUT / "wav" / f"{pid}_{k}.wav", versions[k].astype(np.float32), C1.SR, subtype="FLOAT")

        order = list(RNG.permutation(list(versions)))
        letters = "ABCDEFGHIJ"
        screens.append({"passage": pid, "dur_s": round(n / C1.SR, 3), "reference": C1.flac_b64(versions["ref"]),
                        "versions": [{"id": letters[i], "audio": C1.flac_b64(versions[k])} for i, k in enumerate(order)]})
        key[pid] = {"span_s": [t0, t1], "letters": {letters[i]: str(k) for i, k in enumerate(order)}}

    screens = [screens[i] for i in RNG.permutation(len(screens))]
    page = {"study": "COLOUR2", "version": 1, "seed": SEED, "minutes": 8, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps([[s["passage"], key[s["passage"]]["letters"]] for s in screens],
                                                    sort_keys=True).encode()).hexdigest()[:12]
    html = (C1.HERE / "colour1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({"study": "COLOUR2", "design_hash": page["design_hash"], "seed": SEED,
                                                 "key": key, "screen_order": [s["passage"] for s in screens]}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens x {len(screens[0]['versions'])} versions, "
          f"page {len(html) / 1e6:.1f} MB")
    for pid, k in key.items():
        print(f"  {pid}: {k['letters']}")


if __name__ == "__main__":
    main()
