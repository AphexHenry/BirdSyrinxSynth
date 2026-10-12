"""R1CHECK — does the plugin's whistle path sound like the real bird? (WP-R1 criterion 8)

MUSHRA-style page (shared template colour1_page.html), 2 passages of 22938 (= ENGINE1's P1 and P3, which are
the bank's phrases 4 and 7), each with:
  ref             hidden reference (the real recording)
  plugin_natural  the plugin playing that phrase from MIDI (held key A, order "phrase start", first syllable
                  on the note OFF, everything else default: whistle + bird harmonics, room on)
  plugin_default  same, all defaults (the first syllable is moved onto the key's note A7)
  whistle_r0      the R0 study whistle on the tracked passage (COLOUR2/ENGINE1 favourite: 98-100)
  anchor          sine with strong harmonics (buzzy low anchor, as COLOUR1/ENGINE1)
All: the recording's background-noise bed added, loudness-matched on the bird's voiced frames (ENGINE1).
Pre-registered decision rule: agent documents/studies/R1CHECK_plugin_birdness.md.

usage: python/.venv/bin/python scripts/studies/r1check_make.py
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import colour1_make as C1  # noqa: E402
import render_listening_r1 as L  # noqa: E402

OUT = ROOT / "listening/R1/mushra"
SEED = 20261012
PASSAGES = {"P1": (15.40, 17.85, 4), "P3": (26.95, 29.50, 7)}
RNG = np.random.default_rng(SEED)


def main():
    ann = json.loads(C1.REC.with_suffix(".json").read_text())
    bank = json.loads(L.BANK_PATH.read_text())
    x, _ = sf.read(str(C1.REC), always_2d=True)
    x = x.mean(axis=1)
    bed_full = C1.load(*C1.NOISE_BED, x)
    (OUT / "wav").mkdir(parents=True, exist_ok=True)
    screens, key = [], {}
    for pid, (t0, t1, phrase) in PASSAGES.items():
        ref = C1.load(t0, t1, x)
        n = len(ref)
        tr = C1.T.track(ref, C1.SR, mask=C1.ann_mask(ann, t0, t1, int(n / C1.SR / C1.T.HOP_S) + 8))
        f, a = C1.per_sample(tr["t"], tr["f0"], tr["amp"], tr["voiced"], n)
        hop = int(C1.T.HOP_S * C1.SR)
        vmask = np.pad(np.repeat(tr["voiced"], hop)[:n], (0, max(0, n - len(tr["voiced"]) * hop)))[:n].astype(bool)
        lead = bank["phrases"][phrase]["start_s"] - t0
        nat, _ = L.phrase_render(f"r1check_{pid}_natural", phrase, lead, {"perf.first_on_note": 0}, 69)
        dflt, _ = L.phrase_render(f"r1check_{pid}_default", phrase, lead, {}, 69)
        versions = {
            "ref": ref,
            "plugin_natural": L.mono(nat)[:n],
            "plugin_default": L.mono(dflt)[:n],
            "whistle_r0": C1.tone(f, a, ((2, -30.0), (3, -33.0))),
            "anchor": C1.tone(f, a, ((2, -5.0), (3, -10.0), (4, -15.0), (5, -20.0))),
        }
        target = C1.bird_rms(ref, vmask)
        bed = np.resize(bed_full, n)
        for k in list(versions):
            if k != "ref":
                y = np.pad(versions[k], (0, max(0, n - len(versions[k]))))[:n]
                versions[k] = y * target / (C1.bird_rms(y, vmask) + 1e-12) + bed
        g = min(1.0, 0.89 / max(np.max(np.abs(y)) for y in versions.values()))
        for k in versions:
            versions[k] = versions[k] * g
            sf.write(OUT / "wav" / f"{pid}_{k}.wav", versions[k].astype(np.float32), C1.SR, subtype="FLOAT")
        order = list(RNG.permutation(list(versions)))
        letters = "ABCDE"
        screens.append({"passage": pid, "dur_s": round(n / C1.SR, 3), "reference": C1.flac_b64(versions["ref"]),
                        "versions": [{"id": letters[i], "audio": C1.flac_b64(versions[k])}
                                     for i, k in enumerate(order)]})
        key[pid] = {"span_s": [t0, t1], "bank_phrase": phrase, "letters": {letters[i]: str(k) for i, k in enumerate(order)}}
    screens = [screens[i] for i in RNG.permutation(len(screens))]
    page = {"study": "R1CHECK", "version": 1, "seed": SEED, "minutes": 5, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps([[s["passage"], key[s["passage"]]["letters"]] for s in screens],
                                                    sort_keys=True).encode()).hexdigest()[:12]
    html = (C1.HERE / "colour1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({"study": "R1CHECK", "design_hash": page["design_hash"], "seed": SEED,
                                                 "key": key, "screen_order": [s["passage"] for s in screens],
                                                 "bank_hash": bank["hash"]}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens x {len(screens[0]['versions'])} versions, "
          f"page {len(html) / 1e6:.1f} MB → {OUT / 'index.html'}")


if __name__ == "__main__":
    main()
