"""MELODY2, step 1: one candidate bank per note rule (A-E) and the proxy of WP-R5 §4.1.

Builds listening/R5/work/bank_<X>.json from recording 22938 with `BuildSettings(note_rule=X)`: same
syllables, same order, same contours; only the `rule` anchor (and its centre) differ. Then reports, per
candidate, the share of playable syllables (not "alarm") whose note is within ±2 / ±4 semitones of their
loudness-weighted pitch (amplitude² weights over the bank contour, as MELODY1).
usage: python/.venv/bin/python scripts/studies/melody2_banks.py
Study: agent documents/studies/MELODY2_note_rule.md
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
from cuicui.bank import NOTE_CANDIDATES, BuildSettings, build_bank, write_bank  # noqa: E402

REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
WORK = ROOT / "listening/R5/work"
COMMITTED = ROOT / "assets/voicebanks/zf_22938/bank.json"


def syllables(bank: dict) -> list[dict]:
    return [s for p in bank["phrases"] for s in p["syllables"]]


def loud_pitch(s: dict) -> float:
    """MELODY1's proxy: loudness-weighted (a²) mean pitch over the whole bank contour, in cents."""
    c = 1200 * np.log2(np.asarray(s["f_hz"]) / 440.0) + 6900
    w = np.asarray(s["a"]) ** 2
    return float(np.sum(w * c) / np.sum(w))


def proxy(bank: dict, rule: str = "rule") -> dict:
    off = np.array([s["anchors"][rule] - loud_pitch(s) for s in syllables(bank)
                    if not s["label"].startswith("alarm")])
    return {"playable": int(len(off)),
            "within_2st_pct": round(100.0 * float(np.mean(np.abs(off) <= 200.0)), 1),
            "beyond_4st_pct": round(100.0 * float(np.mean(np.abs(off) > 400.0)), 1),
            "median_offset_c": round(float(np.median(off)), 1),
            "mean_abs_offset_c": round(float(np.mean(np.abs(off))), 1)}


def main() -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    committed = json.loads(COMMITTED.read_text())
    out = {"committed_bank": {r: proxy(committed, r) for r in ("rule", "steady", "landing", "average")}}
    ref_syl = syllables(committed)
    for x in NOTE_CANDIDATES:
        bank = build_bank(REC, REC.with_suffix(".json"), name="zf_22938", species="zebra finch",
                          description=committed["description"], created_by=committed["source"]["created_by"],
                          settings=BuildSettings(note_rule=x))
        syl = syllables(bank)
        assert len(syl) == len(ref_syl)
        assert all(a["label"] == b["label"] and a["f_hz"] == b["f_hz"] and a["a"] == b["a"]
                   for a, b in zip(syl, ref_syl, strict=True)), "contours must not change"
        write_bank(bank, WORK / f"bank_{x}.json")
        out[x] = {"hash": bank["hash"], "centre_cents": bank["centres_cents"]["rule"], **proxy(bank)}
        if x == "A":
            out[x]["equals_committed_bank"] = bank["hash"] == committed["hash"]
    (WORK / "proxy.json").write_text(json.dumps(out, indent=1))
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
