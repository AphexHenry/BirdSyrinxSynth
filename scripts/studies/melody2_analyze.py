"""MELODY2 analysis, exactly as pre-registered (agent documents/studies/MELODY2_note_rule.md §4).

usage: python/.venv/bin/python scripts/studies/melody2_analyze.py <results.json> [out_dir]
Writes melody2_report.json and fig_melody2_ratings.png to out_dir (default listening/R5/melody2/analysis).
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/R5/melody2/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/R5/melody2/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

CANDIDATES = ["A", "B", "C", "D", "E"]
VERSIONS = CANDIDATES + ["anchor"]
SCREENS = sorted(DESIGN["key"])
assert len(RES["results"]) == len(SCREENS), "the run is not complete"

r = {v: {} for v in VERSIONS}
for s in RES["results"]:
    letters = DESIGN["key"][s["id"]]["letters"]
    for letter, score in s["ratings"].items():
        r[letters[letter]][s["id"]] = float(score)
rep: dict = {"design_hash": DESIGN["design_hash"],
             "ratings": {v: {sid: r[v][sid] for sid in SCREENS} for v in VERSIONS}}

# 1. listener check: the anchor lowest (ties count) on >= 7 of 9 screens
lowest = [sid for sid in SCREENS if r["anchor"][sid] <= min(r[v][sid] for v in VERSIONS)]
rep["listener_check"] = {"anchor_lowest_screens": len(lowest), "needed": 7, "pass": len(lowest) >= 7}

# 2. means and the winner
mean = {v: float(np.mean([r[v][sid] for sid in SCREENS])) for v in VERSIONS}
rep["mean"] = {v: round(mean[v], 1) for v in VERSIONS}
rep["sd_over_screens"] = {v: round(float(np.std([r[v][sid] for sid in SCREENS], ddof=1)), 1) for v in VERSIONS}
best = max(CANDIDATES, key=lambda v: mean[v])
beats_a = [v for v in CANDIDATES if v != "A" and mean[v] - mean["A"] >= 10.0]
rep["best_mean"] = best
rep["beat_A_by_10"] = beats_a
if not rep["listener_check"]["pass"]:
    decision = {"result": "unreliable run: repeat (listener check failed)", "default": None}
elif not beats_a:
    decision = {"result": "no candidate beats A by 10 points: keep A, ask the human", "default": "A"}
else:
    top = max(mean[v] for v in beats_a)
    near = [v for v in beats_a if top - mean[v] <= 5.0]
    preference = ["D", "E", "C", "B"]  # in the bank first (D, then E), then the simplest (C before B)
    chosen = next(v for v in preference if v in near) if len(near) > 1 else near[0]
    decision = {"result": f"{chosen} becomes the default note rule", "default": chosen,
                "near_ties_within_5": near, "tie_break_used": len(near) > 1}
rep["decision"] = decision

# 5. information
by_mel: dict = {}
for sid in SCREENS:
    mel = DESIGN["key"][sid]["melody"]
    for v in VERSIONS:
        by_mel.setdefault(mel, {}).setdefault(v, []).append(r[v][sid])
rep["mean_per_melody"] = {m: {v: round(float(np.mean(x)), 1) for v, x in d.items()} for m, d in by_mel.items()}
rep["screens_beating_A"] = {v: sum(r[v][sid] > r["A"][sid] for sid in SCREENS) for v in CANDIDATES if v != "A"}
rep["screens_below_A"] = {v: sum(r[v][sid] < r["A"][sid] for sid in SCREENS) for v in CANDIDATES if v != "A"}
proxy = ROOT / "listening/R5/work/proxy.json"
if proxy.exists():
    px = json.loads(proxy.read_text())
    rep["proxy_within_2st_pct"] = {v: px[v]["within_2st_pct"] for v in CANDIDATES}
rep["minutes"] = round(sum(s.get("ms", 0) for s in RES["results"]) / 60000.0, 1)
rep["comment"] = RES.get("comment", "")

(OUT / "melody2_report.json").write_text(json.dumps(rep, indent=1, ensure_ascii=False))
print(json.dumps(rep, indent=1, ensure_ascii=False))

# figure: every screen's rating per version, and the means
os.environ.setdefault("MPLCONFIGDIR", str(OUT / ".mpl"))
import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

fig, ax = plt.subplots(figsize=(8, 4.2))
colours = {"frere_jacques": "#e8572a", "au_clair": "#2e6b4f", "scale": "#3b5ba5"}
for i, v in enumerate(VERSIONS):
    for j, sid in enumerate(SCREENS):
        mel = DESIGN["key"][sid]["melody"]
        ax.scatter(i + (j % 3 - 1) * 0.08, r[v][sid], s=18, color=colours[mel], alpha=0.8,
                   label=mel if i == 0 and j % 3 == 0 else None)
    ax.plot([i - 0.25, i + 0.25], [mean[v]] * 2, color="black", lw=2)
ax.set_xticks(range(len(VERSIONS)))
ax.set_xticklabels([f"{v}\n{mean[v]:.0f}" for v in VERSIONS])
ax.set_ylim(-3, 103)
ax.set_ylabel("How well does the bird play the tune? (0-100)")
ax.set_title(f"MELODY2 — {decision['result']}", fontsize=10)
ax.legend(fontsize=8, loc="lower left")
ax.grid(axis="y", color="0.9")
fig.tight_layout()
fig.savefig(OUT / "fig_melody2_ratings.png", dpi=110)
