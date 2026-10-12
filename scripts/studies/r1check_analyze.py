"""R1CHECK analysis, following the pre-registered plan (agent documents/studies/R1CHECK_plugin_birdness.md).

usage: python/.venv/bin/python scripts/studies/r1check_analyze.py <results.json> [out_dir]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/R1/mushra/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/R1/mushra/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

VERSIONS = ["ref", "plugin_natural", "plugin_default", "whistle_r0", "anchor"]
PASSAGES = ["P1", "P3"]
r = {v: {} for v in VERSIONS}
for s in RES["results"]:
    letters = DESIGN["key"][s["passage"]]["letters"]
    for letter, score in s["ratings"].items():
        r[letters[letter]][s["passage"]] = score

table = {v: {**{p: r[v][p] for p in PASSAGES}, "mean": round(float(np.mean([r[v][p] for p in PASSAGES])), 1)}
         for v in VERSIONS}


def contrast(a: str, b: str) -> dict:
    d = {p: r[a][p] - r[b][p] for p in PASSAGES}
    return {**d, "mean": round(float(np.mean(list(d.values()))), 1)}


rep = {
    "ratings": table,
    # listener checks (as COLOUR1 / ENGINE1)
    "hidden_ref_ge_90_on_both": all(r["ref"][p] >= 90 for p in PASSAGES),
    "anchor_lowest_on_both": all(min(r[v][p] for v in VERSIONS) == r["anchor"][p] for p in PASSAGES),
    # criterion 8 (primary)
    "criterion_8_plugin_natural_mean": table["plugin_natural"]["mean"],
    "criterion_8_pass": table["plugin_natural"]["mean"] >= 90,
    # information
    "plugin_natural_minus_whistle_r0": contrast("plugin_natural", "whistle_r0"),
    "plugin_default_minus_plugin_natural": contrast("plugin_default", "plugin_natural"),
}
(OUT / "r1check_report.json").write_text(json.dumps(rep, indent=1))
print(json.dumps(rep, indent=1))
