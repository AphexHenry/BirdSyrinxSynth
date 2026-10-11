"""COLOUR2 analysis, following the pre-registered plan (agent documents/studies/COLOUR2_gentler_colour.md §3).

usage: python/.venv/bin/python scripts/studies/colour2_analyze.py <results.json>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/COLOUR2/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = ROOT / "listening/COLOUR2/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

P = ["P1", "P3"]
r = {}
for s in RES["results"]:
    for letter, score in s["ratings"].items():
        r.setdefault(DESIGN["key"][s["passage"]]["letters"][letter], {})[s["passage"]] = score
table = {v: {**{p: r[v][p] for p in P}, "mean": float(np.mean([r[v][p] for p in P]))} for v in r}
rep = {"ratings": dict(sorted(table.items(), key=lambda kv: -kv[1]["mean"]))}
rep["hidden_ref"] = {p: r["ref"][p] for p in P}


def slope(names, dbs):  # points per +6 dB of ingredient (louder), least squares over all ratings
    x = np.repeat(dbs, len(P)).astype(float)
    y = np.array([r[n][p] for n in names for p in P], dtype=float)
    return round(float(np.polyfit(x, y, 1)[0] * 6), 1)


rep["breath_slope_points_per_6dB_louder"] = slope(["breath_50", "breath_44", "breath_38", "breath_32"], [-50, -44, -38, -32])
rep["harm_slope_points_per_6dB_louder"] = slope(["harm_46", "harm_38", "harm_30"], [-46, -38, -30])
beats = {}
for v in r:
    if v in ("ref", "sine"):
        continue
    d = {p: r[v][p] - r["sine"][p] for p in P}
    beats[v] = {**d, "mean": float(np.mean(list(d.values()))),
                "beats_sine (higher on both and >=10 mean)": bool(all(x > 0 for x in d.values()) and np.mean(list(d.values())) >= 10)}
rep["vs_sine"] = beats
(OUT / "colour2_report.json").write_text(json.dumps(rep, indent=1))
print(json.dumps(rep, indent=1))
