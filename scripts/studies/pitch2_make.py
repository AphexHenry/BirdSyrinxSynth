"""PITCH2 — does the bird "dance around the note"? Phrases vs single syllables, two anchor rules.

Builds in listening/PITCH2/:
  index.html       blind rating test (self-contained: material + trial list inlined)
  playground.html  free exploration of the same placement logic (rule toggle, snap, offset)
  design.json      hidden key: provenance, factors, per-trial predicted offsets per hypothesis
Protocol: agent documents/studies/PITCH2_dance_around_the_note.md

usage: python/.venv/bin/python scripts/studies/pitch2_make.py
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/review"))
import track as T  # noqa: E402

HERE = Path(__file__).resolve().parent
OUT = ROOT / "listening/PITCH2"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
SEED = 20261012
RULES = ("steady", "landing")  # measured; the playground also offers "average"
DELTAS = (-600, -300, -150, 0, 150, 300, 600)
PHRASES = {  # name: (start s, end s, segment used as the single-syllable item)
    "A": (15.44, 16.47, 20),
    "B": (16.46, 17.35, 27),
    "C": (11.95, 12.90, 15),
}
V0 = 2000.0  # cents/s, stability weight of the "steady" rule (as PITCH1 stable_v2000)


def anchors(c):
    slope = np.abs(np.gradient(c)) * 1000.0
    w = np.exp(-slope / V0)
    q = max(1, len(c) // 4)
    return {
        "steady": float(np.sum(w * c) / np.sum(w)),
        "landing": float(c[-q:].mean()),
        "average": float(c.mean()),
    }


def syllables_of(x, sr, ann, t_start, t_end):
    """Tracked syllables (one per annotated segment) inside [t_start, t_end]."""
    sos = signal.butter(4, 1000, "highpass", fs=sr, output="sos")
    seg = signal.sosfiltfilt(sos, x[int(t_start * sr): int(t_end * sr)])
    tr = T.track(seg, sr)
    out = []
    for k, s in enumerate(ann["segments"]):
        if s["category"] != "BIRD_SOUND" or s["start"] < t_start - 0.005 or s["end"] > t_end + 0.005:
            continue
        i0, i1 = int((s["start"] - t_start) / T.HOP_S), int((s["end"] - t_start) / T.HOP_S)
        v = tr["voiced"][i0:i1]
        if v.sum() < 15:
            continue
        idx = np.flatnonzero(v)
        j0, j1 = i0 + idx[0], i0 + idx[-1] + 1  # first..last voiced frame of the syllable
        vv = tr["voiced"][j0:j1]
        f = tr["f0"][j0:j1].copy()
        f[~vv] = np.nan
        good = np.flatnonzero(~np.isnan(f))
        f = np.interp(np.arange(len(f)), good, f[good])  # hold pitch through short inner gaps
        c = signal.medfilt(1200 * np.log2(f / 440.0) + 6900.0, 5)  # remove 1–2 ms tracker spikes
        a = np.where(vv, tr["amp"][j0:j1], 0.0)
        a = np.convolve(a, np.ones(3) / 3, mode="same")
        w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, 3))
        a[:3] *= w
        a[-3:] *= w[::-1]
        out.append({
            "segment": k,
            "label": "+".join(e.get("type_label", "") for e in s.get("elements", [])),
            "t0_ms": int(j0),
            "c": c,
            "a": a,
            "anchors": anchors(c[vv]),
        })
    return out


def wrap(c):
    """Pitch-class distance in cents, wrapped to [-600, 600)."""
    return (np.asarray(c) + 600.0) % 1200.0 - 600.0


def main():
    rng = np.random.default_rng(SEED)
    ann = json.loads(REC.with_suffix(".json").read_text())
    x, sr = sf.read(str(REC), always_2d=True)
    x = x.mean(axis=1)

    materials, provenance = {}, {}
    for name, (t0, t1, single_seg) in PHRASES.items():
        syl = syllables_of(x, sr, ann, t0, t1)
        amax = max(s["a"].max() for s in syl)
        base = syl[0]["t0_ms"]

        def pack(s, t_ref, amax=amax):
            return {
                "t0_ms": int(s["t0_ms"] - t_ref),
                "f_hz": [round(float(v), 2) for v in 440.0 * 2 ** ((s["c"] - 6900.0) / 1200.0)],
                "a": [round(float(v), 4) for v in s["a"] / amax],
                "anchors": {k: round(v, 1) for k, v in s["anchors"].items()},
            }

        materials[f"phrase_{name}"] = {"kind": "phrase", "syllables": [pack(s, base) for s in syl]}
        one = next(s for s in syl if s["segment"] == single_seg)
        materials[f"single_{name}"] = {"kind": "single", "syllables": [pack(one, one["t0_ms"])]}
        for m in (f"phrase_{name}", f"single_{name}"):
            sy = materials[m]["syllables"]
            materials[m]["dur_ms"] = max(s["t0_ms"] + len(s["f_hz"]) for s in sy)
        provenance[f"phrase_{name}"] = {"recording": "22938", "span_s": [t0, t1],
                                        "segments": [s["segment"] for s in syl],
                                        "labels": [s["label"] for s in syl]}
        provenance[f"single_{name}"] = {"recording": "22938", "segment": single_seg, "label": one["label"]}

    # ---------------------------------------------------------------- trials
    def placed_anchor_offsets(mat, rule_place, rule_hear, delta):
        """Pitch-class offset from the target at which each syllable's `rule_hear` anchor lands
        when every syllable is placed (snap = 1) so that its `rule_place` anchor sits at target+delta."""
        sy = materials[mat]["syllables"]
        return [float(wrap(delta + s["anchors"][rule_hear] - s["anchors"][rule_place])) for s in sy]

    cells = [(m, r, d) for m in materials for r in RULES for d in DELTAS]
    practice_cells = [("phrase_A", "landing", 0), ("phrase_C", "steady", 600),
                      ("single_B", "steady", 0), ("single_C", "landing", 300)]
    while True:
        order = [cells[i] for i in rng.permutation(len(cells))]
        if all(a[0] != b[0] for a, b in zip(order, order[1:])):  # never the same material twice in a row
            break
    trials = []
    for i, (m, r, d) in enumerate(practice_cells + order):
        sy = materials[m]["syllables"]
        centre = float(np.median([s["anchors"][r] for s in sy]))
        target = float(np.round(centre / 100.0 + rng.uniform(-3, 3)) * 100.0)  # a tempered note near the bird's height
        ref_midi = 60 + int(round(target / 100.0)) % 12                       # the same note name, octave 4
        trials.append({
            "n": i + 1, "practice": i < len(practice_cells), "material": m, "rule": r, "delta": d,
            "target_cents": target, "ref_midi": ref_midi,
        })

    page = {"study": "PITCH2", "version": 1, "seed": SEED, "rules": list(RULES), "deltas": list(DELTAS),
            "materials": materials, "trials": trials}
    page["design_hash"] = hashlib.sha256(json.dumps(page, sort_keys=True).encode()).hexdigest()[:12]

    key_trials = []
    for t in trials:
        pred = {h: placed_anchor_offsets(t["material"], t["rule"], h, t["delta"])
                for h in ("steady", "landing", "average")}
        key_trials.append({**t, "predicted_offsets_by_hearing_rule": pred})
    design = {"study": "PITCH2", "design_hash": page["design_hash"], "seed": SEED, "v0_cents_per_s": V0,
              "provenance": provenance,
              "anchors": {m: [s["anchors"] for s in mat["syllables"]] for m, mat in materials.items()},
              "trials": key_trials}

    OUT.mkdir(parents=True, exist_ok=True)
    common = (HERE / "pitch2_common.js").read_text()
    data = "window.PITCH2 = " + json.dumps(page, separators=(",", ":")) + ";"
    for src, dst in (("pitch2_test.html", "index.html"), ("pitch2_playground.html", "playground.html")):
        html = (HERE / src).read_text()
        html = html.replace("<!--DATA-->", "<script>" + data + "</script>")
        html = html.replace("<!--COMMON-->", "<script>" + common + "</script>")
        (OUT / dst).write_text(html)
    (OUT / "design.json").write_text(json.dumps(design, indent=1))

    print(f"design {page['design_hash']}: {len(materials)} materials, {len(trials)} trials "
          f"({len(practice_cells)} practice)")
    for m, mat in materials.items():
        sy = mat["syllables"]
        diffs = [s["anchors"]["landing"] - s["anchors"]["steady"] for s in sy]
        print(f"  {m:9s} {len(sy)} syll, {mat['dur_ms']:4d} ms, landing−steady per syllable: "
              + " ".join(f"{d:+.0f}" for d in diffs))


if __name__ == "__main__":
    main()
