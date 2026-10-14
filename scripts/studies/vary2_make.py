"""VARY2 — how much variation: 100 %, 130 % or 160 % of the bird's own spread (WP-R3, feedback round).

MUSHRA-style page (template vary2_page.html), VARY1 part A's two "same song again" passages. Versions:
  real    the whistle on the bird's own renditions
  same    the first song's rendition, sung again identically (Variation 0)
  v100    that rendition, a new rendition every time, at 100 % (the plugin's control::makeVariant)
  v130    … 130 %
  v160    … 160 %
  anchor  the real renditions with strong harmonics
The plugin's shape model as it is now: no loudness variation, draws capped at ±2 sd. The same draws for the
three levels (one seed per syllable occurrence). Background bed and loudness matching as VARY1.
Pre-registered decision rule: agent documents/studies/VARY2_how_much_variation.md.

usage: python/.venv/bin/python scripts/studies/vary2_make.py
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(HERE))
import cuicui_core  # noqa: E402
import r3_render as R  # noqa: E402
from vary1_make import songs_in  # noqa: E402

from cuicui.variation import Model  # noqa: E402

OUT = ROOT / "listening/R3/vary2"
SEED = 20261015
BANKS = {"22938": ROOT / "assets/voicebanks/hf_22938/bank.json",
         "22937": ROOT / "assets/voicebanks/hf_22937/bank.json"}
PASSAGES = [("A1", "22937", 16.15, 27.25), ("A2", "22938", 63.80, 73.17)]  # = VARY1 part A
LEVELS = {"v100": 1.0, "v130": 1.3, "v160": 1.6}


def main() -> None:
    rng_master = np.random.default_rng(SEED)
    banks = {r: json.loads(p.read_text()) for r, p in BANKS.items()}
    models = {r: Model(b) for r, b in banks.items()}  # phrase plans only (the variants come from the core)
    recs = {r: R.recording(r) for r in banks}
    (OUT / "wav").mkdir(parents=True, exist_ok=True)
    screens, key = [], {}
    for sid, rec, t0, t1 in PASSAGES:
        m, bank = models[rec], banks[rec]
        songs = songs_in(m, bank, t0 + 0.1, t1)
        assert len(songs) >= 2, (sid, songs)
        ref = R.clip(recs[rec], t0, t1)
        n = len(ref)
        busy = [(p["start_s"] - 0.2, p["start_s"] + 5.0) for p in bank["phrases"]]
        bed = R.noise_bed(recs[rec], busy)
        real = [(s - t0, m.real_phrase(p)) for s, p in songs]
        first = real[0][1]
        seeds = [[int(x) for x in rng_master.integers(1, 2**62, len(first))] for _ in real]

        def events(plan_on_list, v=None):
            ev = []
            for (on, plan), sd in plan_on_list:
                for k, (i, t_ms) in enumerate(plan):
                    if v is None:  # the real syllable
                        c, a = m.syls[i].cents, m.syls[i].amp
                    else:
                        c, a, _ = cuicui_core.variant(str(BANKS[rec]), int(i), v,
                                                      cuicui_core.variant_draws(sd[k]))
                    ev.append((on + t_ms / 1000.0, np.asarray(c), np.asarray(a)))
            return ev

        versions = {"real": R.whistle(events([(r, None) for r in real]), n),
                    "anchor": R.whistle(events([(r, None) for r in real]), n, R.ANCHOR_HARM),
                    "same": R.whistle(events([((on, first), None) for on, _ in real]), n)}
        for name, v in LEVELS.items():
            versions[name] = R.whistle(events([((on, first), sd) for (on, _), sd in zip(real, seeds,
                                                                                         strict=True)], v), n)
        target = float(np.sqrt(np.mean(ref[versions["real"][1]] ** 2)))
        out = {"ref": ref}
        for k, (y, mask) in versions.items():
            out[k] = R.match(y, mask, target, bed)
        g = min(1.0, 0.89 / max(float(np.max(np.abs(y))) for y in out.values()))
        for k in out:
            out[k] = out[k] * g
            sf.write(OUT / "wav" / f"{sid}_{k}.wav", out[k].astype(np.float32), R.SR, subtype="FLOAT")
        order = [str(k) for k in rng_master.permutation(list(out))]
        letters = "ABCDEFGHIJ"
        screens.append({"passage": sid, "part": "A", "dur_s": round(n / R.SR, 3),
                        "reference": R.C1.flac_b64(out["ref"]),
                        "versions": [{"id": letters[i], "audio": R.C1.flac_b64(out[k])}
                                     for i, k in enumerate(order)]})
        key[sid] = {"part": "A", "recording": rec, "span_s": [t0, t1],
                    "songs": [[round(s, 3), int(p)] for s, p in songs],
                    "letters": {letters[i]: k for i, k in enumerate(order)}}
        print(f"{sid}: {len(songs)} songs, {n / R.SR:.1f} s, versions {sorted(out)}")
    screens = [screens[i] for i in rng_master.permutation(len(screens))]
    page = {"study": "VARY2", "version": 1, "seed": SEED, "minutes": 6, "screens": screens,
            "parts": {"A": "The same song again: the bird sings one song several times. "
                           "How much does each version sound like this bird singing its song again?"}}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [[s["passage"], key[s["passage"]]["letters"]] for s in screens]
        + [{r: b["hash"] for r, b in banks.items()}]
        + [hashlib.sha256("".join(v["audio"] for v in s["versions"]).encode()).hexdigest()
           for s in screens], sort_keys=True).encode()).hexdigest()[:12]
    html = (HERE / "vary2_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(
        page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({
        "study": "VARY2", "design_hash": page["design_hash"], "seed": SEED, "key": key,
        "levels": LEVELS, "screen_order": [s["passage"] for s in screens],
        "banks": {r: b["hash"] for r, b in banks.items()}}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens, page {len(html) / 1e6:.1f} MB "
          f"→ {OUT / 'index.html'}")


if __name__ == "__main__":
    main()
