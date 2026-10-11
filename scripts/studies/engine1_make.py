"""ENGINE1 — does the click fix bring the anatomical engine up to the best whistle?

MUSHRA-style page (shared template colour1_page.html), 2 passages of 22938, each with:
  ref           hidden reference (the real recording)
  whistle       pure sine + the bird's own harmonics (-30 / -33 dB): today's best voice (COLOUR2: 98)
  engine_today  Syrinx engine, best parameters only (tone 0.6, aspiration off, radiation 1, attack 3 / release 8 ms)
  engine_fixed  same engine, articulated by the bird's volume envelope: the labia start 12 ms early and stop
                10 ms late at constant pressure, so the onset/offset snaps happen in silence (offline proof of concept)
  anchor        sine with strong harmonics (buzzy low anchor, as COLOUR1)
All: real gestures, the recording's noise bed, loudness-matched, no room.

usage: python/.venv/bin/python scripts/studies/engine1_make.py
"""
from __future__ import annotations

import hashlib
import json

import numpy as np
import soundfile as sf

import colour1_make as C1
import engine_gate_poc as G
from engine_sweep import gestures, render

OUT = C1.ROOT / "listening/ENGINE1/test"
SEED = 20261015
PASSAGES = {"P1": (15.40, 17.85), "P3": (26.95, 29.50)}
RNG = np.random.default_rng(SEED)
G.PRE_MS, G.POST_MS = 12, 10


def main():
    ann = json.loads(C1.REC.with_suffix(".json").read_text())
    x, _ = sf.read(str(C1.REC), always_2d=True)
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
        versions = {
            "ref": ref,
            "whistle": C1.tone(f, a, ((2, -30.0), (3, -33.0))),
            "engine_today": render(gestures(tr, G.SETTINGS["perf.tone"]), G.SETTINGS, n),
            "engine_fixed": render(G.gated_gestures(tr, G.SETTINGS["perf.tone"]), G.SETTINGS, n) * G.envelope(tr, n),
            "anchor": C1.tone(f, a, ((2, -5.0), (3, -10.0), (4, -15.0), (5, -20.0))),
        }
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
        letters = "ABCDE"
        screens.append({"passage": pid, "dur_s": round(n / C1.SR, 3), "reference": C1.flac_b64(versions["ref"]),
                        "versions": [{"id": letters[i], "audio": C1.flac_b64(versions[k])} for i, k in enumerate(order)]})
        key[pid] = {"span_s": [t0, t1], "letters": {letters[i]: str(k) for i, k in enumerate(order)}}
    screens = [screens[i] for i in RNG.permutation(len(screens))]
    page = {"study": "ENGINE1", "version": 1, "seed": SEED, "minutes": 5, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps([[s["passage"], key[s["passage"]]["letters"]] for s in screens],
                                                    sort_keys=True).encode()).hexdigest()[:12]
    html = (C1.HERE / "colour1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({"study": "ENGINE1", "design_hash": page["design_hash"], "seed": SEED,
                                                 "settings": G.SETTINGS, "pre_ms": G.PRE_MS, "post_ms": G.POST_MS,
                                                 "key": key, "screen_order": [s["passage"] for s in screens]}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens x {len(screens[0]['versions'])} versions, page {len(html) / 1e6:.1f} MB")
    for pid, k in key.items():
        print(f"  {pid}: {k['letters']}")


if __name__ == "__main__":
    main()
