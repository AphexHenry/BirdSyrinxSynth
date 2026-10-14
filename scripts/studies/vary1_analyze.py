"""VARY1 analysis, exactly as pre-registered (agent documents/studies/VARY1_variations.md §4).

usage: python/.venv/bin/python scripts/studies/vary1_analyze.py <vary1_results.json> [--save]
  --save copies the results and the design to agent documents/studies/data/VARY1/.
"""
from __future__ import annotations

import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = ROOT / "listening/R3/vary1/design.json"
DATA = ROOT / "agent documents/studies/data/VARY1"
BAR, BELOW_REAL, TIE = 90.0, 10.0, 5.0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results", type=Path)
    ap.add_argument("--save", action="store_true")
    args = ap.parse_args()
    design = json.loads(DESIGN.read_text())
    res = json.loads(args.results.read_text())
    if res.get("design_hash") != design["design_hash"]:
        raise SystemExit(f"design hash {res.get('design_hash')} ≠ page {design['design_hash']}")
    if design.get("provisional"):
        print("WARNING: this page was built from the provisional banks (before INGEST1)")
    ratings: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    print("per screen:")
    for r in res["results"]:
        k = design["key"][r["passage"]]
        by = {k["letters"][letter]: v for letter, v in r["ratings"].items()}
        for name, v in by.items():
            ratings[k["part"]][name].append(float(v))
        flag = "" if by.get("ref", 0) >= BAR else "   ← hidden reference below 90 (reported)"
        print(f"  {r['passage']} ({k['part']}): " + ", ".join(f"{n} {by[n]:.0f}" for n in sorted(by))
              + flag)
    mean = {p: {n: float(np.mean(v)) for n, v in d.items()} for p, d in ratings.items()}
    for p in sorted(mean):
        print(f"part {p} means: " + ", ".join(f"{n} {v:.1f}" for n, v in sorted(mean[p].items(),
                                                                        key=lambda kv: -kv[1])))

    def keeps(part: str, name: str) -> bool:
        m = mean[part]
        return m[name] >= BAR and m[name] >= m["real"] - BELOW_REAL

    print("\ndecisions (pre-registered):")
    a = mean.get("A")
    if a:
        if keeps("A", "shape1") and a["shape1"] >= a["nudge1"] - TIE:
            method = "shape"
        elif keeps("A", "nudge1"):
            method = "nudge"
        else:
            method = None
        print(f"  2. method: {method or 'NONE — no syllable variation; ask the human'} "
              f"(shape1 {a['shape1']:.1f}, nudge1 {a['nudge1']:.1f}, real {a['real']:.1f})")
        if method:
            top = 2 if keeps("A", f"{method}2") else 1
            print(f"  3. range: Variation up to {top} ({method}2 {a[method + '2']:.1f})")
        print(f"  6. same {a['same']:.1f} vs real {a['real']:.1f}; anchor {a['anchor']:.1f} "
              f"({'discriminates' if a['anchor'] <= a['real'] - 20 else 'LITTLE discrimination'})")
    b = mean.get("B")
    if b:
        kept = keeps("B", "order2")
        print(f"  4. new order: {'KEPT' if kept else 'not kept'} (order2 {b['order2']:.1f}, "
              f"real {b['real']:.1f})")
        if kept and keeps("B", "order1") and b["order1"] >= b["order2"] - TIE:
            print(f"     order 1 also keeps the bird ({b['order1']:.1f}): a possible wilder setting")
        print(f"  5. together (order2s {b['order2s']:.1f}): "
              + ("Variation 1 = new songs + varied syllables" if keeps("B", "order2s")
                 else "offered separately; ask the human"))
        print(f"  6. shuffle {b['shuffle']:.1f} ({'order NOT audible' if keeps('B', 'shuffle') else 'order audible'}); "
              f"anchor {b['anchor']:.1f}")
    if res.get("comment"):
        print("\ncomment:", res["comment"])
    if args.save:
        DATA.mkdir(parents=True, exist_ok=True)
        shutil.copy(args.results, DATA / "results.json")
        shutil.copy(DESIGN, DATA / "design.json")
        print("saved to", DATA.relative_to(ROOT))


if __name__ == "__main__":
    main()
