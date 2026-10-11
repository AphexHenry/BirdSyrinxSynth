"""PITCH2 analysis, following the pre-registered plan (agent documents/studies/PITCH2_dance_around_the_note.md §4).

usage: python/.venv/bin/python scripts/studies/pitch2_analyze.py <results.json> [out_dir]
Scoring: yes = 1, not sure = 0.5, no = 0. Practice trials excluded.
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/PITCH2/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/PITCH2/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

SCORE = {"yes": 1.0, "unsure": 0.5, "no": 0.0}
key = {t["n"]: t for t in DESIGN["trials"]}
rows = []
for r in RES["results"]:
    if r["practice"]:
        continue
    k = key[r["n"]]
    assert k["material"] == r["material"] and k["rule"] == r["rule"] and k["delta"] == r["delta"]
    heard = {h: float(np.mean(v)) for h, v in k["predicted_offsets_by_hearing_rule"].items()}
    rows.append({**r, "y": SCORE[r["answer"]], "context": r["material"].split("_")[0], "heard": heard})
y_all = np.array([x["y"] for x in rows])
report = {"n_trials": len(rows), "answers": {a: sum(x["answer"] == a for x in rows) for a in SCORE}}

# 2. mapping check (pre-registered threshold 0.3)
p0 = np.mean([x["y"] for x in rows if x["delta"] == 0])
p600 = np.mean([x["y"] for x in rows if abs(x["delta"]) == 600])
report["mapping_check"] = {"P_yes_delta0": round(float(p0), 3), "P_yes_delta600": round(float(p600), 3),
                           "difference": round(float(p0 - p600), 3), "passes_0.3": bool(p0 - p600 >= 0.3)}


# 3. model comparison: P = f + (p - f) * exp(-o^2 / 2 sigma^2), soft-Bernoulli likelihood
def ll(P, y):
    P = np.clip(P, 1e-4, 1 - 1e-4)
    return float(np.sum(y * np.log(P) + (1 - y) * np.log(1 - P)))


def fit(o, y):
    best = None
    for s in np.r_[np.arange(50, 400, 25), np.arange(400, 1601, 100)]:
        g = np.exp(-(o**2) / (2 * s * s))
        for p in np.arange(0.40, 1.0, 0.025):
            for f in np.arange(0.0, p + 1e-9, 0.025):
                L = ll(f + (p - f) * g, y)
                if best is None or L > best[0]:
                    best = (L, s, p, f)
    return {"LL": round(best[0], 2), "sigma": float(best[1]), "peak": round(float(best[2]), 3),
            "floor": round(float(best[3]), 3)}


def compare(sub):
    y = np.array([x["y"] for x in sub])
    out = {"null (no tuning)": {"LL": round(ll(np.full(len(y), y.mean()), y), 2), "P": round(float(y.mean()), 3)}}
    for h in ("steady", "landing", "average"):
        out[f"H-{h}"] = fit(np.array([x["heard"][h] for x in sub]), y)
    # placement offset itself (what the trial asked for, ignoring which rule placed it)
    out["delta only"] = fit(np.array([float(x["delta"]) for x in sub]), y)
    return out


report["models_all"] = compare(rows)
report["models_phrases"] = compare([x for x in rows if x["context"] == "phrase"])
report["models_singles"] = compare([x for x in rows if x["context"] == "single"])

# 5. descriptives
tab = defaultdict(list)
for x in rows:
    tab[(x["context"], x["rule"], x["delta"])].append(x["y"])
report["mean_score_context_rule_delta"] = {f"{c}|{r}|{d:+d}": round(float(np.mean(v)), 2)
                                           for (c, r, d), v in sorted(tab.items())}
by_delta = defaultdict(list)
for x in rows:
    by_delta[x["delta"]].append(x["y"])
report["mean_score_by_delta"] = {f"{d:+d}": round(float(np.mean(v)), 2) for d, v in sorted(by_delta.items())}
by_mat = defaultdict(list)
for x in rows:
    by_mat[x["material"]].append(x["answer"])
report["answers_by_material"] = {m: {a: v.count(a) for a in SCORE} for m, v in sorted(by_mat.items())}
report["median_seconds_per_answer"] = round(float(np.median([x["ms"] for x in rows])) / 1000, 1)

(OUT / "pitch2_report.json").write_text(json.dumps(report, indent=1))
print(json.dumps(report, indent=1))
