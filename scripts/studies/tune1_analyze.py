"""TUNE1 analysis, exactly as pre-registered (agent documents/studies/TUNE1_every_syllable_by_ear.md §3-4).

usage: python/.venv/bin/python scripts/studies/tune1_analyze.py <results.json> [out_dir]
Writes to out_dir (default listening/R5/tune1/analysis):
  tune1_report.json        the pre-registered numbers and verdicts
  note_corrections.json    draft corrections file (format cuicui.note_corrections v1) for `python -m cuicui.bank`
  fig_tune1_corrections.png
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DESIGN = json.loads((ROOT / "listening/R5/tune1/design.json").read_text())
RES = json.loads(Path(sys.argv[1]).read_text())
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "listening/R5/tune1/analysis"
OUT.mkdir(parents=True, exist_ok=True)
assert RES["design_hash"] == DESIGN["design_hash"], "results do not belong to this design"

CORRECTED_C = 25.0  # §3.1
GATE_C = 150.0  # §3.3
AGREE_C = 100.0  # §3.6
RULES = [r["id"] for r in DESIGN["rules"]]
items = DESIGN["items"]
moves = {int(k): float(v) for k, v in RES.get("moves", {}).items()}
no_clear = {int(k) for k, v in RES.get("no_clear_note", {}).items() if v}
mv = lambda k: moves.get(k, 0.0)  # noqa: E731

corrected = [it["id"] for it in items if abs(mv(it["id"])) >= CORRECTED_C and it["id"] not in no_clear]
rep: dict = {"design_hash": DESIGN["design_hash"], "minutes": round(sum(RES.get("group_ms", {}).values()) / 60000, 1)}
rep["counts"] = {"syllables": len(items), "corrected": len(corrected), "no_clear_note": len(no_clear),
                 "on_the_note": len(items) - len(corrected) - len(no_clear)}
m = np.array([mv(k) for k in corrected]) if corrected else np.zeros(0)
rep["corrections_cents"] = {"median_abs": round(float(np.median(np.abs(m))), 0) if len(m) else 0,
                            "largest_abs": round(float(np.max(np.abs(m))), 0) if len(m) else 0,
                            "moved_down": int(np.sum(m < 0)), "moved_up": int(np.sum(m > 0))}

# §3.3 reliability gate (check round)
chk = RES.get("check") or {}
ids = [int(k) for k in chk.get("ids", [])]
second = {int(k): float(v) for k, v in chk.get("moves", {}).items()}
diffs = [abs(mv(k) - second.get(k, 0.0)) for k in ids]
rep["check_round"] = {"items": [{"id": k, "first": mv(k), "second": second.get(k, 0.0)} for k in ids],
                      "median_abs_diff_cents": round(float(np.median(diffs)), 0) if diffs else None,
                      "median_abs_diff_corrected_only": (round(float(np.median([abs(mv(k) - second.get(k, 0.0)) for k in ids
                                                                                 if abs(mv(k)) >= CORRECTED_C])), 0)
                                                         if any(abs(mv(k)) >= CORRECTED_C for k in ids) else None),
                      "gate_cents": GATE_C,
                      "pass": bool(diffs) and float(np.median(diffs)) <= GATE_C}

# §3.2 heard notes; §3.6 can a rule do it?
heard = {}
for it in items:
    if it["id"] in no_clear:
        continue
    heard[it["id"]] = it["notes"]["E"] - mv(it["id"]) if it["id"] in corrected else it["notes"]["E"]
agree = {}
for r in RULES:
    close = {k: abs(next(i for i in items if i["id"] == k)["notes"][r] - h) <= AGREE_C for k, h in heard.items()}
    all_pct = 100.0 * float(np.mean(list(close.values())))
    cor_pct = 100.0 * float(np.mean([close[k] for k in corrected])) if corrected else float("nan")
    agree[r] = {"all_pct": round(all_pct, 1), "corrected_pct": round(cor_pct, 1),
                "matches_the_ear": bool(all_pct >= 90.0 and corrected and cor_pct >= 70.0)}
rep["rules_within_100c_of_heard"] = agree
rep["a_rule_matches_the_ear"] = [r for r in RULES if agree[r]["matches_the_ear"]]
rep["verdict"] = (
    "unreliable: ask the human before using the corrections" if not rep["check_round"]["pass"]
    else (f"corrections used; rule(s) {rep['a_rule_matches_the_ear']} also match the ear: show the human"
          if rep["a_rule_matches_the_ear"] else "corrections used; no rule matches the ear (corrections needed)"))

# §4 information
by_id = {it["id"]: it for it in items}
rep["corrected"] = [{"id": k, "label": by_id[k]["label"], "segment": by_id[k]["segment"], "move_cents": mv(k),
                     "nearest_rule": min(RULES, key=lambda r: abs(by_id[k]["notes"][r] - heard[k]))}
                    for k in sorted(corrected, key=lambda k: mv(k))]
rep["no_clear_note"] = [{"id": k, "label": by_id[k]["label"], "segment": by_id[k]["segment"]} for k in sorted(no_clear)]
labels: dict = {}
for k in corrected:
    labels[by_id[k]["label"]] = labels.get(by_id[k]["label"], 0) + 1
rep["corrected_labels"] = labels
bank = json.loads((ROOT / "listening/R5/work/bank_E.json").read_text())
flat = [s for p in bank["phrases"] for s in p["syllables"]]


def reaches_above(k: int) -> float:
    s = flat[k]
    c = 1200 * np.log2(np.asarray(s["f_hz"]) / 440.0) + 6900
    a = np.asarray(s["a"])
    return float((c[a >= a.max() * 10 ** (-12 / 20)].max() - s["anchors"]["rule"]) / 100.0)


flagged = {it["id"] for it in items if not it["call"] and reaches_above(it["id"]) > 3.0}
rep["melody2_flagged_reach_above_3st"] = {"flagged": len(flagged), "flagged_and_corrected": len(flagged & set(corrected)),
                                          "corrected_not_flagged": len(set(corrected) - flagged)}
rep["plays"] = {"row_plays": RES.get("row_plays", {}),
                "rule_buttons": {r: sum(v.get("rules", {}).get(r, 0) for v in RES.get("listens", {}).values()) for r in RULES},
                "play_buttons": sum(v.get("play", 0) for v in RES.get("listens", {}).values())}
rep["settings"] = RES.get("settings")
rep["comment"] = RES.get("comment", "")
(OUT / "tune1_report.json").write_text(json.dumps(rep, indent=1, ensure_ascii=False))

# the corrections file the bank builder reads (cuicui.note_corrections v1)
corr = {"format": "cuicui.note_corrections", "version": 1, "bank": DESIGN["bank"],
        "made_by": f"TUNE1 by ear ({RES.get('finished', '')[:10]}), design {DESIGN['design_hash']}",
        "reference_rule": "E", "reference_cents": DESIGN["reference_cents"],
        "syllables": [{"segment": by_id[k]["segment"], "label": by_id[k]["label"], "note_cents": round(heard[k], 1),
                       "move_cents": mv(k), "rule_e_cents": by_id[k]["notes"]["E"]} for k in sorted(corrected)]
        + [{"segment": by_id[k]["segment"], "label": by_id[k]["label"], "no_clear_note": True} for k in sorted(no_clear)]}
(OUT / "note_corrections.json").write_text(json.dumps(corr, indent=1, ensure_ascii=False))
print(json.dumps({k: v for k, v in rep.items() if k not in ("corrected", "check_round")}, indent=1, ensure_ascii=False))
print("check round:", rep["check_round"]["median_abs_diff_cents"], "c median |diff| → pass" if rep["check_round"]["pass"]
      else "→ FAIL")

# figure: for every corrected syllable, where the ear put its note and where each rule puts it (relative to E)
os.environ.setdefault("MPLCONFIGDIR", str(OUT / ".mpl"))
import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

if corrected:
    order = sorted(corrected, key=lambda k: -mv(k))
    fig, ax = plt.subplots(figsize=(max(6.0, 0.35 * len(order) + 2), 4.2))
    marks = {"A": "v", "B": "^", "C": "s", "D": "D", "landing": "<", "average": ">", "loudest": "*", "high": "P"}
    for r, mk in marks.items():
        ax.scatter(range(len(order)), [(by_id[k]["notes"][r] - by_id[k]["notes"]["E"]) / 100 for k in order],
                   marker=mk, s=18, alpha=0.6, label=r)
    ax.scatter(range(len(order)), [-mv(k) / 100 for k in order], s=60, color="black", label="the ear", zorder=5)
    ax.axhline(0, color="0.6", lw=0.8)
    ax.set_xticks(range(len(order)))
    ax.set_xticklabels([f"#{k}" for k in order], rotation=90, fontsize=7)
    ax.set_ylabel("note relative to rule E (semitones)")
    ax.set_title("TUNE1 — corrected syllables: the ear's note vs each rule's", fontsize=10)
    ax.legend(fontsize=7, ncol=5, loc="upper right")
    ax.grid(axis="y", color="0.92")
    fig.tight_layout()
    fig.savefig(OUT / "fig_tune1_corrections.png", dpi=110)
