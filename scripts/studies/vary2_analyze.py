"""VARY2 analysis, exactly as pre-registered (agent documents/studies/VARY2_how_much_variation.md §4).

usage: python/.venv/bin/python scripts/studies/vary2_analyze.py <vary2_results.json> [--save]
"""
from __future__ import annotations

import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = ROOT / "listening/R3/vary2/design.json"
DATA = ROOT / "agent documents/studies/data/VARY2"
KEEP_BELOW_REAL = 5.0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results", type=Path)
    ap.add_argument("--save", action="store_true")
    args = ap.parse_args()
    design = json.loads(DESIGN.read_text())
    res = json.loads(args.results.read_text())
    if res.get("design_hash") != design["design_hash"]:
        raise SystemExit(f"design hash {res.get('design_hash')} ≠ page {design['design_hash']}")
    ratings: dict[str, list[float]] = defaultdict(list)
    print("per screen:")
    for r in res["results"]:
        k = design["key"][r["passage"]]
        by = {k["letters"][letter]: float(v) for letter, v in r["ratings"].items()}
        for name, v in by.items():
            ratings[name].append(v)
        flag = "" if by.get("ref", 0) >= 90 else "   ← hidden reference below 90 (reported)"
        print(f"  {r['passage']}: " + ", ".join(f"{n} {by[n]:.0f}" for n in sorted(by)) + flag)
    mean = {n: float(np.mean(v)) for n, v in ratings.items()}
    print("means: " + ", ".join(f"{n} {v:.1f}" for n, v in sorted(mean.items(), key=lambda kv: -kv[1])))
    bar = mean["real"] - KEEP_BELOW_REAL
    keeps = {lv: mean[lv] >= bar for lv in ("v100", "v130", "v160")}
    print(f"\nkeeps the bird (≥ real − 5 = {bar:.1f}): " + ", ".join(f"{k} {'yes' if v else 'no'}"
                                                               for k, v in keeps.items()))
    if not keeps["v100"]:
        print("decision: 100 % does not keep the bird → nothing changes (default 1.0); ASK THE HUMAN")
    else:
        top = 100
        for lv in (130, 160):
            if keeps[f"v{lv}"] and top == lv - (30 if lv == 130 else 30):
                top = lv
            else:
                break
        print(f"decision: L = {top} % → perf.natural_variation 0–{top / 100:.1f}, default {top / 100:.1f}")
        if keeps["v160"] and not keeps["v130"]:
            print("  note: non-monotone (160 kept, 130 not)")
    print(f"exploratory: same {mean['same']:.1f} vs real {mean['real']:.1f}; anchor {mean['anchor']:.1f} "
          f"({'discriminates' if mean['anchor'] <= mean['real'] - 20 else 'LITTLE discrimination'})")
    if res.get("comment"):
        print("\ncomment:", res["comment"])
    if args.save:
        DATA.mkdir(parents=True, exist_ok=True)
        shutil.copy(args.results, DATA / "results.json")
        shutil.copy(DESIGN, DATA / "design.json")
        print("saved to", DATA.relative_to(ROOT))


if __name__ == "__main__":
    main()
