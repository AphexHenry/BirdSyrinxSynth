"""TUNE1 — every syllable on one note; the listener corrects the ones that sound off (WP-R5).

Builds a self-contained page (template tune1_page.html) that synthesises the whistle voice in the browser from the
bank's 1 ms curves (the PROTO1 voice: sine + the bird's 2nd/3rd harmonics, light room), so the listener can move a
syllable's pitch live. Every syllable is placed so that its rule-E note (loudness-weighted average) lands on A7;
the slider's zero is rule E. Each syllable can also be heard placed by every note rule:
  A-E      the MELODY2 candidates (listening/R5/work/bank_<X>.json, melody2_banks.py)
  landing  last quarter, plain average          average  plain average
  loudest  frames within 3 dB of the peak       high     80th percentile of the pitch over frames within 12 dB
(landing, average, loudest, high ignore frames more than 20 dB below the syllable's peak, as B-E do.)

Writes listening/R5/tune1/index.html and design.json.
usage: python/.venv/bin/python scripts/studies/tune1_make.py   (after melody2_banks.py)
Study: agent documents/studies/TUNE1_every_syllable_by_ear.md
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
WORK = ROOT / "listening/R5/work"
OUT = ROOT / "listening/R5/tune1"
REFERENCE_CENTS = 10500.0  # A7, the tempered note nearest the bird's rule-E centre (10546 c)
GROUPS = [[0, 1, 2, 3], [4], [5], [6, 7], [8], [9], [10], [11]]  # bank phrases; short ones merged
RULES = [
    ("A", "A", "longest steady part, else the ending (D-019, the old default)"),
    ("B", "B", "longest steady part, else the ending of the loud part"),
    ("C", "C", "longest steady part, else the loudness-weighted average"),
    ("D", "D", "steady parts (stability-weighted average)"),
    ("E", "E", "loudness-weighted average (the new default; the slider's zero)"),
    ("landing", "landing", "the last quarter (plain average)"),
    ("average", "average", "plain average"),
    ("loudest", "loudest", "the pitch at the loudest moment (frames within 3 dB of the peak)"),
    ("high", "high", "the high part: 80th percentile of the pitch over the frames within 12 dB of the peak"),
]


def db_mask(a: np.ndarray, below_db: float) -> np.ndarray:
    return a >= a.max() * 10.0 ** (-below_db / 20.0)


def main() -> None:
    banks = {x: json.loads((WORK / f"bank_{x}.json").read_text()) for x in "ABCDE"}
    flat = {x: [(p["id"], i, s) for p in b["phrases"] for i, s in enumerate(p["syllables"])] for x, b in banks.items()}
    items = []
    for k, (pid, idx, s) in enumerate(flat["E"]):
        f = np.asarray(s["f_hz"], dtype=float)
        a = np.asarray(s["a"], dtype=float)
        c = 1200.0 * np.log2(f / 440.0) + 6900.0
        keep = db_mask(a, 20.0)
        ck = c[keep]
        q = max(1, len(ck) // 4)
        notes = {x: flat[x][k][2]["anchors"]["rule"] for x in "ABCDE"}
        notes["landing"] = float(ck[-q:].mean())
        notes["average"] = float(ck.mean())
        notes["loudest"] = float(c[db_mask(a, 3.0)].mean())
        notes["high"] = float(np.percentile(c[db_mask(a, 12.0)], 80))
        for x in "ABCD":  # same syllables in every candidate bank
            assert flat[x][k][2]["f_hz"] == s["f_hz"] and flat[x][k][2]["label"] == s["label"]
        items.append({"id": k, "phrase": pid, "index": idx, "segment": s["segment"], "label": s["label"],
                      "ms": len(f), "f": s["f_hz"], "a": [round(v / a.max(), 3) for v in a],
                      "notes": {r: round(v, 1) for r, v in notes.items()},
                      "call": s["label"].startswith("alarm")})
    by_phrase: dict[int, list[int]] = {}
    for it in items:
        by_phrase.setdefault(it["phrase"], []).append(it["id"])
    groups = []
    for g in GROUPS:
        ids = [k for pid in g for k in by_phrase[pid]]
        title = f"Phrase {g[0] + 1}" if len(g) == 1 else f"Phrases {g[0] + 1}–{g[-1] + 1}"
        groups.append({"title": title, "items": ids})
    assert sorted(k for g in groups for k in g["items"]) == list(range(len(items)))
    page = {"study": "TUNE1", "version": 1, "reference_cents": REFERENCE_CENTS, "reference_name": "A7 (3520 Hz)",
            "bank": banks["E"]["name"], "bank_hash_E": banks["E"]["hash"],
            "rules": [{"id": r, "label": lab, "text": t} for r, lab, t in RULES],
            "groups": groups, "items": items}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [page["reference_cents"], page["bank_hash_E"], groups, [it["notes"] for it in items]], sort_keys=True
    ).encode()).hexdigest()[:12]
    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "tune1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.STUDY = " + json.dumps(page, separators=(",", ":"),
                                                                           ensure_ascii=False) + ";</script>")
    (OUT / "index.html").write_text(html)
    design = {k: v for k, v in page.items() if k != "items"}
    design["items"] = [{k: v for k, v in it.items() if k not in ("f", "a")} for it in items]
    (OUT / "design.json").write_text(json.dumps(design, indent=1, ensure_ascii=False))
    spread = [max(it["notes"].values()) - min(it["notes"].values()) for it in items]
    print(f"design {page['design_hash']}: {len(items)} syllables in {len(groups)} groups, page "
          f"{len(html) / 1e6:.2f} MB; rules disagree by a median {np.median(spread):.0f} c "
          f"(max {max(spread):.0f} c) per syllable")


if __name__ == "__main__":
    main()
