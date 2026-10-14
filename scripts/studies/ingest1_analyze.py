"""INGEST1 analysis: the listener's marks → final annotation files (as pre-registered, WP-R3).

usage: python/.venv/bin/python scripts/studies/ingest1_analyze.py <ingest1_results.json> [--write]
  without --write: prints the counts and the check-round verdict;
  with --write: also writes the final annotations to assets/voicebanks/<bank>/annotation.json
  (22938: the human annotation's segments unchanged + the kept automatic syllables).
Study: agent documents/studies/INGEST1_new_syllables.md
"""
from __future__ import annotations

import argparse
import json
import shutil
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DESIGN = ROOT / "listening/R3/ingest1/design.json"
ANN = {"22938": ROOT / "build/r3/ann/22938_full.json", "22937": ROOT / "build/r3/ann/22937_auto.json"}
HUMAN_SEGMENTS_22938 = 145  # the human annotation's segments, kept as they are
OUT = {"22938": ROOT / "assets/voicebanks/hf_22938/annotation.json",
       "22937": ROOT / "assets/voicebanks/hf_22937/annotation.json"}
DATA = ROOT / "agent documents/studies/data/INGEST1"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results", type=Path)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    design = json.loads(DESIGN.read_text())
    res = json.loads(args.results.read_text())
    if res.get("design_hash") != design["design_hash"]:
        raise SystemExit(f"design hash {res.get('design_hash')} ≠ page {design['design_hash']}")
    items = design["items"]
    marks, labels = res.get("marks", {}), res.get("labels", {})

    # 1. counts
    by = Counter((it["rec"], marks.get(it["uid"], "keep")) for it in items)
    print("marks:", dict(sorted(by.items())))
    print("labels changed:", len(labels), dict(Counter(labels.values())))

    # 3. reliability gate
    chk = res.get("check") or {}
    agree = n = 0
    for uid, first in (chk.get("first") or {}).items():
        second = chk.get("marks", {}).get(uid)
        if second is None:
            continue
        n += 1
        agree += (first == "keep") == (second == "keep")
    ok = n == 10 and agree >= 8
    print(f"check round: {agree} / {n} agree (keep vs reject) → "
          + ("first-pass marks used" if ok else "ASK THE HUMAN before building the banks"))

    # 2. + 4. final annotations
    final = {}
    for rec, path in ANN.items():
        ann = json.loads(path.read_text())
        segs = ann["segments"]
        for it in (x for x in items if x["rec"] == rec):
            s = segs[it["segment"]]
            assert s["category"] == "BIRD_SOUND", it
            if rec == "22938":
                assert it["segment"] >= HUMAN_SEGMENTS_22938, "a human segment was on the page"
            m = marks.get(it["uid"], "keep")
            if m != "keep":
                s["category"] = "NOISE"
                s["ingest1"] = "not the bird" if m == "bird" else "whistle wrong"
                s.pop("elements", None)
                continue
            s["elements"] = [{"type_label": labels.get(it["uid"], it["label"])}]
            s["label_source"] = "listener" if it["uid"] in labels else "proposed (INGEST1)"
        # automatic syllables the builder skipped (too few voiced frames) keep the label "?":
        # they are not in any bank; mark them so a rebuild skips them too
        for k, s in enumerate(segs):
            if rec == "22938" and k < HUMAN_SEGMENTS_22938:
                continue
            if s["category"] == "BIRD_SOUND" and s["elements"][0]["type_label"] == "?":
                s["category"] = "NOISE"
                s["ingest1"] = "not tracked"
        ann["ingest1"] = {"design_hash": design["design_hash"], "check_agree": f"{agree}/{n}",
                          "results_finished": res.get("finished")}
        final[rec] = ann
        kept = sum(1 for s in segs if s["category"] == "BIRD_SOUND")
        print(f"{rec}: {kept} BIRD_SOUND segments in the final annotation")
    if args.write:
        for rec, ann in final.items():
            OUT[rec].parent.mkdir(parents=True, exist_ok=True)
            OUT[rec].write_text(json.dumps(ann, indent=1) + "\n")
            print("wrote", OUT[rec].relative_to(ROOT))
        DATA.mkdir(parents=True, exist_ok=True)
        shutil.copy(args.results, DATA / "results.json")
        shutil.copy(DESIGN, DATA / "design.json")


if __name__ == "__main__":
    main()
