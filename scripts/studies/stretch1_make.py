"""STRETCH1 — hear two simple variations on a dozen syllables: length and pitch range (WP-R3, feedback).

The human, after VARY2 ("i cannot tell the difference between the synthesising versions"): "let's work only
with syllable duration, going from 80% to 130%, and with frequency amplitude around the note. basically
compressing or extending the variation of frequency around the note, also from 80% to 130%. make me a test so
i hear the differences on a dozen syllables".

Per syllable, the page plays (voice synthesised in the page: the plugin's whistle):
  length   the curve stretched in time to 60 / 80 / 100 / 130 / 160 / 200 % (linear resampling, as the
           plugin's lengthScale); the pitch itself is unchanged
  range    the pitch around the syllable's note scaled by 60 … 200 %: note + k·(pitch − note). The note is the
           bank's (loudness-weighted mean, rule E, or the note set by ear)
and random draws of both in context: two real phrases sung with each syllable at a random length and range in
60–200 % (log-uniform: shorter and longer equally likely; or only one of them). Widened from 80–130 % at the
human's request ("add a bit of variation. let's go from 60 to 200%, same for pitch amplitude"). Not a blind test:
a listening tool; the listener writes the ranges that sound natural.

Writes listening/R3/stretch1/index.html and design.json.
usage: python/.venv/bin/python scripts/studies/stretch1_make.py
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / "listening/R3/stretch1"
BANKS = {"22938": ROOT / "assets/voicebanks/hf_22938/bank.json",
         "22937": ROOT / "assets/voicebanks/hf_22937/bank.json"}
# (bird, label, what to pick) — a dozen syllables of different kinds
PICKS = [
    ("22938", "swipe", "falls"), ("22938", "swipe", "rises"), ("22938", "peep", "typical"),
    ("22938", "swipe+swipe", "typical"), ("22938", "Hswipe", "typical"), ("22938", "Lswipe", "typical"),
    ("22938", "alarm", "typical"),
    ("22937", "swipe", "falls"), ("22937", "peep", "typical"), ("22937", "Hswipe", "typical"),
    ("22937", "Lpeep", "typical"), ("22937", "swipe+swipe", "typical"),
]
PHRASES = [("22938", 4), ("22937", 0)]  # 22938 phrase 4 = R1CHECK's P1; 22937 phrase 0 = a whole song


def flat(bank: dict) -> list[tuple[int, int, dict]]:
    return [(p["id"], k, s) for p in bank["phrases"] for k, s in enumerate(p["syllables"])]


def sweep(s: dict) -> float:
    return float(1200 * np.log2(s["f_hz"][-1] / s["f_hz"][0]))


def item(rec: str, s: dict, title: str) -> dict:
    return {"rec": rec, "label": s["label"], "title": title, "segment": s["segment"],
            "ms": len(s["f_hz"]), "f": [round(v, 1) for v in s["f_hz"]], "a": s["a"],
            "note": s["anchors"]["rule"], "sweep": round(sweep(s))}


def main() -> None:
    banks = {r: json.loads(p.read_text()) for r, p in BANKS.items()}
    items, used = [], set()
    for rec, label, what in PICKS:
        cand = [s for _, _, s in flat(banks[rec]) if s["label"] == label and "hop" not in s["flags"]
                and (rec, s["segment"]) not in used]
        cand.sort(key=lambda s: s["segment"])
        if what == "falls":
            s = sorted(cand, key=sweep)[len(cand) // 10]  # a clear fall, not the most extreme
        elif what == "rises":
            s = sorted(cand, key=sweep)[-(len(cand) // 10) - 1]
        else:  # typical: the median length
            s = sorted(cand, key=lambda x: len(x["f_hz"]))[len(cand) // 2]
        used.add((rec, s["segment"]))
        word = {"falls": "falling", "rises": "rising", "typical": ""}[what]
        items.append(item(rec, s, f"{rec} · {label}{' (' + word + ')' if word else ''}"))
    phrases = []
    for rec, pid in PHRASES:
        p = banks[rec]["phrases"][pid]
        phrases.append({"rec": rec, "phrase": pid, "title": f"{rec}, phrase {pid + 1}",
                        "syllables": [{**item(rec, s, s["label"]), "t_ms": s["t_ms"]} for s in p["syllables"]]})
    page = {"study": "STRETCH1", "version": 1, "items": items, "phrases": phrases,
            "levels": [60, 80, 100, 130, 160, 200], "random_range": [60, 200]}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [[(it["rec"], it["segment"]) for it in items], [(p["rec"], p["phrase"]) for p in phrases],
         page["levels"]]).encode()).hexdigest()[:12]
    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "stretch1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.STUDY = " + json.dumps(
        page, separators=(",", ":"), ensure_ascii=False) + ";</script>")
    (OUT / "index.html").write_text(html)
    design = {k: v for k, v in page.items() if k not in ("items", "phrases")}
    design["items"] = [{k: v for k, v in it.items() if k not in ("f", "a")} for it in items]
    design["phrases"] = [{"rec": p["rec"], "phrase": p["phrase"], "syllables": len(p["syllables"])}
                         for p in phrases]
    (OUT / "design.json").write_text(json.dumps(design, indent=1, ensure_ascii=False))
    print(f"design {page['design_hash']}: {len(items)} syllables, {len(phrases)} phrases, "
          f"page {len(html) / 1e6:.2f} MB")
    for it in items:
        print(f"  {it['title']:32s} {it['ms']:4d} ms  sweep {it['sweep']:+5d} c")


if __name__ == "__main__":
    main()
