"""COLOUR1 analysis, following the pre-registered plan (agent documents/studies/COLOUR1_what_makes_it_real.md §3).

usage: python/.venv/bin/python scripts/studies/colour1_analyze.py <results.json> [out_dir]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/COLOUR1/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/COLOUR1/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

VERSIONS = ["ref", "voice2", "no_breath", "sine", "no_harm", "no_room", "full", "engine", "anchor"]
PASSAGES = ["P1", "P2", "P3"]
r = {v: {} for v in VERSIONS}
for s in RES["results"]:
    letters = DESIGN["key"][s["passage"]]["letters"]
    for letter, score in s["ratings"].items():
        r[letters[letter]][s["passage"]] = score

table = {v: {**{p: r[v][p] for p in PASSAGES}, "mean": round(float(np.mean([r[v][p] for p in PASSAGES])), 1)}
         for v in VERSIONS}
rep = {"ratings": table}

# 1. listener check
rep["hidden_ref_ge_90"] = sum(r["ref"][p] >= 90 for p in PASSAGES)
rep["anchor_lowest"] = sum(min(r[v][p] for v in VERSIONS) == r["anchor"][p] for p in PASSAGES)

# 2. leave-one-out ingredient effects (full - without)
loo = {}
for ing, v in (("harmonics", "no_harm"), ("breath", "no_breath"), ("room", "no_room")):
    d = {p: r["full"][p] - r[v][p] for p in PASSAGES}
    loo[ing] = {**d, "mean": round(float(np.mean(list(d.values()))), 1),
                "matters (>=10 mean and >=10 on 2/3)": bool(np.mean(list(d.values())) >= 10
                                                          and sum(x >= 10 for x in d.values()) >= 2),
                "hurts (<=-10 mean and <=-10 on 2/3)": bool(np.mean(list(d.values())) <= -10
                                                         and sum(x <= -10 for x in d.values()) >= 2)}
rep["leave_one_out_full_minus_without"] = loo

# 3. other contrasts
def contrast(a, b):
    d = {p: r[a][p] - r[b][p] for p in PASSAGES}
    return {**d, "mean": round(float(np.mean(list(d.values()))), 1)}

rep["voice2_minus_full"] = contrast("voice2", "full")
rep["engine_minus_full"] = contrast("engine", "full")
rep["full_minus_sine"] = contrast("full", "sine")
rep["within_10_of_hidden_ref"] = [v for v in VERSIONS if v != "ref" and table["ref"]["mean"] - table[v]["mean"] <= 10]
# rating noise: voice2 differs from full only while the second voice sounds (39-164 ms per passage)
rep["second_voice_ms"] = {p: DESIGN["key"][p]["second_voice_ms"] for p in PASSAGES}

(OUT / "colour1_report.json").write_text(json.dumps(rep, indent=1))
print(json.dumps(rep, indent=1))
