"""PITCH1 analysis, following the pre-registered plan (agent documents/studies/PITCH1_perceived_pitch.md §5).

usage: python/.venv/bin/python scripts/studies/pitch1_analyze.py <results.json> [out_dir]
All pitches are in absolute cents (6900 = A4). Predictions from design.json are shifted by each trial's rove.
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/PITCH1/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/PITCH1/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

KEY = DESIGN["key"]
RULES = ["mean", "energy_weighted", "median", "start_25pct", "end_25pct", "max", "min",
         "stable_v1000", "stable_v2000", "stable_v4000", "stable_recency"]

rows = []
for r in RES["results"]:
    k = KEY[r["stim"]]
    p = {rule: k["predictions_cents"][rule] + r["rove_cents"] for rule in RULES}
    rows.append({**r, "name": k["name"], "group": k["group"], "pred": p,
                 "rel_match": r["match_cents"] - p["mean"],      # match relative to the stimulus mean
                 "rel_start": r["cmp_start_cents"] - p["mean"]})  # where the comparison started

report = {}

# 1. listener precision on steady tones (practice + test)
steady = [x for x in rows if x["name"] == "steady"]
err = np.array([x["match_cents"] - x["pred"]["mean"] for x in steady])
report["steady_errors_cents"] = [round(e, 1) for e in err]
report["steady_bias_cents"] = round(float(err.mean()), 1)
report["steady_sd_cents"] = round(float(err.std(ddof=1)), 1)
report["steady_rms_cents"] = round(float(np.sqrt(np.mean(err**2))), 1)

test = [x for x in rows if not x["practice"]]
by = defaultdict(list)
for x in test:
    by[x["name"]].append(x)

# 2. test-retest
rt = {nm: abs(v[0]["rel_match"] - v[1]["rel_match"]) for nm, v in by.items() if len(v) == 2}
report["retest_abs_diff_cents"] = {nm: round(d, 0) for nm, d in sorted(rt.items(), key=lambda kv: kv[1])}
report["retest_median_cents"] = round(float(np.median(list(rt.values()))), 1)
h0 = [nm for nm, v in by.items()
      if (len(v) == 2 and rt[nm] > 150) or sum(x["clarity"] == "none" for x in v) == 2]
report["H0_candidates"] = h0

# 3. model comparison on the 15 non-control stimuli (all trials, then per group)
def rms_by_rule(trials):
    out = {}
    for rule in RULES:
        e = np.array([x["match_cents"] - x["pred"][rule] for x in trials])
        out[rule] = round(float(np.sqrt(np.mean(e**2))), 1)
    return dict(sorted(out.items(), key=lambda kv: kv[1]))

nonctrl = [x for x in test if x["name"] != "steady"]
report["rms_error_by_rule_all"] = rms_by_rule(nonctrl)
report["rms_error_by_rule_synthetic"] = rms_by_rule([x for x in nonctrl if x["group"] == "synthetic"])
report["rms_error_by_rule_real"] = rms_by_rule([x for x in nonctrl if x["group"] == "real"])

# same comparison on the per-stimulus average of the two repeats (less noisy)
def rms_by_rule_avg(names):
    out = {}
    for rule in RULES:
        e = [np.mean([x["match_cents"] - x["pred"][rule] for x in by[nm]]) for nm in names]
        out[rule] = round(float(np.sqrt(np.mean(np.square(e)))), 1)
    return dict(sorted(out.items(), key=lambda kv: kv[1]))

names_nc = [nm for nm in by if nm != "steady"]
report["rms_error_by_rule_stimulus_means"] = rms_by_rule_avg(names_nc)

# 4. key contrasts (observed = difference of mean relative matches; predicted per rule from design)
pred_rel = {k["name"]: {rule: k["predictions_cents"][rule] - k["predictions_cents"]["mean"] for rule in RULES}
            for k in KEY.values()}
means_abs = {k["name"]: k["predictions_cents"]["mean"] for k in KEY.values()}
contrasts = [("up_lin", "down_lin"), ("up_slow", "down_slow"), ("up_then_plat", "plat_then_up"),
             ("arch", "vee"), ("step_up", "step_down")]
report["contrasts"] = {}
for a, b in contrasts:
    obs = (np.mean([x["rel_match"] for x in by[a]]) + means_abs[a]) - \
          (np.mean([x["rel_match"] for x in by[b]]) + means_abs[b])
    pred = {rule: round((pred_rel[a][rule] + means_abs[a]) - (pred_rel[b][rule] + means_abs[b]), 0)
            for rule in ["mean", "start_25pct", "end_25pct", "stable_v2000", "stable_recency"]}
    report["contrasts"][f"{a} - {b}"] = {"observed": round(float(obs), 0), "predicted": pred}

# 5. per-stimulus table (relative to mean)
table = {}
for nm, v in by.items():
    table[nm] = {
        "matches_rel_mean": [round(x["rel_match"], 0) for x in v],
        "clarity": [x["clarity"] for x in v],
        "pred_rel_mean": {r: round(pred_rel[nm][r], 0) for r in
                          ["start_25pct", "end_25pct", "stable_v2000", "stable_recency", "energy_weighted"]},
        "group": KEY[next(s for s in KEY if KEY[s]["name"] == nm)]["group"],
    }
report["per_stimulus"] = table

# anchoring check: does the error follow where the comparison started?
e = np.array([x["match_cents"] - x["pred"]["stable_recency"] for x in test])
s = np.array([x["cmp_start_cents"] - x["pred"]["stable_recency"] for x in test])
slope = float(np.polyfit(s, e, 1)[0])
report["anchoring_slope"] = round(slope, 3)  # 0 = no pull toward the start, 1 = stays at the start
report["unmoved_trials"] = [x["n"] for x in test if x["moves"] == 0]

# 7. clarity
cl = defaultdict(lambda: defaultdict(int))
for x in test:
    cl[x["group"] if x["name"] != "steady" else "steady"][x["clarity"]] += 1
report["clarity_counts"] = {g: dict(v) for g, v in cl.items()}

# fitted weighting (exploratory): w = exp(-|slope|/v0) * (1 + k*(t/T - 0.5)), grid search
def weighted_pred(stim_key, v0, kk):
    return None  # placeholder; contours needed (see pitch1_make.py) — computed below

(OUT / "pitch1_report.json").write_text(json.dumps(report, indent=1))
print(json.dumps({k: v for k, v in report.items() if k != "per_stimulus"}, indent=1))
print("\nper stimulus (matches relative to stimulus mean, cents):")
for nm, t in sorted(table.items(), key=lambda kv: (kv[1]["group"], kv[0])):
    print(f"  {nm:22s} {str(t['matches_rel_mean']):18s} {str(t['clarity']):22s} "
          f"start {t['pred_rel_mean']['start_25pct']:+5.0f} end {t['pred_rel_mean']['end_25pct']:+5.0f} "
          f"stable {t['pred_rel_mean']['stable_v2000']:+5.0f} st×rec {t['pred_rel_mean']['stable_recency']:+5.0f}")
