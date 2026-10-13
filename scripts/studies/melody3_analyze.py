"""MELODY3 analysis, exactly as pre-registered (agent documents/studies/MELODY3_new_bank_and_wide_sweeps.md §3).

usage: python/.venv/bin/python scripts/studies/melody3_analyze.py <results.json> [out_dir]
Writes melody3_report.json to out_dir (default listening/R5/melody3/analysis).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/R5/melody3/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/R5/melody3/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"
VERSIONS = ["new", "no_wide", "old", "anchor"]
SCREENS = sorted(DESIGN["key"])
assert len(RES["results"]) == len(SCREENS), "the run is not complete"

r = {v: {} for v in VERSIONS}
rec = {v: {} for v in VERSIONS}
up = {v: {} for v in VERSIONS}
for s in RES["results"]:
    letters = DESIGN["key"][s["id"]]["letters"]
    for letter, v in letters.items():
        r[v][s["id"]] = float(s["ratings"][letter])
        rec[v][s["id"]] = bool(s.get("recognise", {}).get(letter, False))
        up[v][s["id"]] = bool(s.get("ending_up", {}).get(letter, False))
rep: dict = {"design_hash": DESIGN["design_hash"], "ratings": r,
             "mean": {v: round(float(np.mean(list(r[v].values()))), 1) for v in VERSIONS},
             "recognised_screens": {v: sum(rec[v].values()) for v in VERSIONS},
             "ending_up_screens": {v: sum(up[v].values()) for v in VERSIONS}}

# 1. listener check
lowest = [sid for sid in SCREENS if r["anchor"][sid] <= min(r[v][sid] for v in VERSIONS)]
rep["listener_check"] = {"anchor_lowest_screens": len(lowest), "needed": 4, "pass": len(lowest) >= 4}

# 2. very wide sweeps, on the screens where no_wide differs from new
differ = [sid for sid in SCREENS if DESIGN["key"][sid]["differs"]]
d = [r["no_wide"][sid] - r["new"][sid] for sid in differ]
higher = sum(x > 0 for x in d)
adopt = bool(rep["listener_check"]["pass"] and differ and np.mean(d) >= 10.0 and higher >= 3)
rep["wide_sweeps"] = {"screens": differ, "no_wide_minus_new": dict(zip(differ, d, strict=True)),
                      "mean_difference": round(float(np.mean(d)), 1) if d else None,
                      "no_wide_higher_on": higher, "adopted": adopt}

# 3. criterion 3 on the version that becomes the default
default = "no_wide" if adopt else "new"
c3 = rep["listener_check"]["pass"] and sum(rec[default].values()) == 5 and sum(up[default].values()) == 0
rep["criterion_3"] = {"version": default, "recognised": f"{sum(rec[default].values())}/5",
                      "ending_up": f"{sum(up[default].values())}/5", "pass": bool(c3)}

# 4. information
rep["new_minus_old"] = {sid: r["new"][sid] - r["old"][sid] for sid in SCREENS}
rep["new_minus_old_mean"] = round(float(np.mean(list(rep["new_minus_old"].values()))), 1)
same = [sid for sid in SCREENS if not DESIGN["key"][sid]["differs"]]
rep["identical_sounds_rating_difference"] = {sid: r["no_wide"][sid] - r["new"][sid] for sid in same}
rep["replaced"] = {sid: {"notes": DESIGN["key"][sid]["replaced_notes"],
                         "from": [DESIGN["key"][sid]["syllables"]["new"][i] for i in DESIGN["key"][sid]["replaced_notes"]],
                         "to": [DESIGN["key"][sid]["syllables"]["no_wide"][i] for i in DESIGN["key"][sid]["replaced_notes"]]}
                   for sid in differ}
rep["ticks"] = {"recognise": rec, "ending_up": up}
rep["minutes"] = round(sum(s.get("ms", 0) for s in RES["results"]) / 60000.0, 1)
rep["comment"] = RES.get("comment", "")
rep["verdict"] = ("unreliable run: repeat (listener check failed)" if not rep["listener_check"]["pass"] else
                  f"very wide sweeps {'adopted' if adopt else 'not adopted'}; criterion 3 "
                  f"{'PASS' if c3 else 'FAIL'} on '{default}'")
(OUT / "melody3_report.json").write_text(json.dumps(rep, indent=1, ensure_ascii=False))
print(json.dumps(rep, indent=1, ensure_ascii=False))
