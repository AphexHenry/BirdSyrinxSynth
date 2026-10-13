"""MELODY3 — does the new bank play Frère Jacques, and do very wide sweeps blur it? (WP-R5, pre-registered)

Rating page (template melody3_page.html = MELODY2's + two ticks per version): 5 screens = Frère Jacques tapped
with seeds 501-505. Each screen: the reference (the tune on a soft tone) and four hidden versions:
  new      the rebuilt bank (rule E + the notes set by ear), assets/voicebanks/zf_22938/bank.json
  no_wide  the same with the very wide sweeps flagged "no_tap" (a tap never starts with one; D-032 redraw)
  old      the R1 bank (rule A), same syllables as new
  anchor   the new bank with each syllable's note moved a random 4-6 semitones (one offset per syllable)
Very wide sweep: a playable syllable whose loud part (frames within 12 dB of its peak) spans more than 8 semitones.
Checked here: new / old / anchor use the same syllables; no_wide differs only where new drew a very wide sweep.

Writes listening/R5/melody3/index.html, design.json and wav/; the variant banks go to listening/R5/work/.
usage: python/.venv/bin/python scripts/studies/melody3_make.py
Study: agent documents/studies/MELODY3_new_bank_and_wide_sweeps.md
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(HERE))
import cuicui_core  # noqa: E402
from melody2_make import MELODIES, SETTINGS, SR, active_rms, events, flac_b64, reference  # noqa: E402

WORK = ROOT / "listening/R5/work"
OUT = ROOT / "listening/R5/melody3"
NEW = ROOT / "assets/voicebanks/zf_22938/bank.json"
OLD = WORK / "bank_R1_committed.json"  # the committed R1 bank (hash 09b16ee69ac8), saved before the rebuild
SEED = 20261013
SEEDS = (501, 502, 503, 504, 505)
WIDE_LOUD_DB = 12.0
WIDE_ST = 8.0
RNG = np.random.default_rng(SEED)


def syllables(bank: dict) -> list[dict]:
    return [s for p in bank["phrases"] for s in p["syllables"]]


def loud_range_st(s: dict) -> float:
    c = 1200 * np.log2(np.asarray(s["f_hz"]) / 440.0) + 6900
    a = np.asarray(s["a"])
    loud = c[a >= a.max() * 10 ** (-WIDE_LOUD_DB / 20)]
    return float((loud.max() - loud.min()) / 100.0)


def write(bank: dict, name: str) -> Path:
    path = WORK / name
    path.write_text(json.dumps(bank, separators=(",", ":"), ensure_ascii=False) + "\n")
    return path


def variant_banks() -> tuple[dict[str, Path], list[int]]:
    new = json.loads(NEW.read_text())
    old = json.loads(OLD.read_text())
    assert old["hash"] == "09b16ee69ac8", "the old bank must be R1's"
    wide = [k for k, s in enumerate(syllables(new)) if not s["label"].startswith("alarm") and loud_range_st(s) > WIDE_ST]
    no_wide = json.loads(NEW.read_text())
    for k, s in enumerate(syllables(no_wide)):
        if k in wide:
            s["flags"] = s["flags"] + ["no_tap"]
    no_wide["name"], no_wide["hash"] = "zf_22938_melody3_no_wide", "melody3nowid"
    anchor = json.loads(NEW.read_text())
    rng = np.random.default_rng(SEED + 1)
    for s in syllables(anchor):
        s["anchors"]["rule"] = round(s["anchors"]["rule"] + float(rng.choice([-1.0, 1.0]) * rng.uniform(400.0, 600.0)), 1)
    anchor["name"], anchor["hash"] = "zf_22938_melody3_anchor", "melody3anchr"
    return {"new": NEW, "no_wide": write(no_wide, "bank_no_wide.json"), "old": OLD,
            "anchor": write(anchor, "bank_anchor3.json")}, wide


def main() -> None:
    (OUT / "wav").mkdir(parents=True, exist_ok=True)
    banks, wide = variant_banks()
    m = MELODIES["frere_jacques"]
    dur = m["times"][-1] + 1.2
    screens, key = [], {}
    for seed in SEEDS:
        sid = f"seed-{seed}"
        versions, logs = {}, {}
        for v, path in banks.items():
            audio, log = cuicui_core.sing(str(path), events(m), SETTINGS, float(SR), dur, seed, 256)
            versions[v] = np.asarray(audio, dtype=np.float64)[:, 0] * np.sqrt(2.0)  # centred: L = x·cos(π/4)
            logs[v] = log
        ids = {v: [e["syllable"] for e in logs[v]] for v in logs}
        samples = {v: [e["sample"] for e in logs[v]] for v in logs}
        assert all(len(ids[v]) == len(m["notes"]) and all(e["first"] for e in logs[v]) for v in logs), sid
        assert ids["old"] == ids["new"] == ids["anchor"] and samples["old"] == samples["new"] == samples["anchor"], sid
        assert samples["no_wide"] == samples["new"], sid
        replaced = [k for k, (a, b) in enumerate(zip(ids["new"], ids["no_wide"], strict=True)) if a != b]
        assert all(ids["new"][k] in wide for k in replaced), f"{sid}: only very wide sweeps may be redrawn"
        assert all(i not in wide for i in ids["no_wide"]), f"{sid}: no_wide must not start a tap with a wide sweep"
        assert len(replaced) == sum(i in wide for i in ids["new"]), sid
        n = len(versions["new"])
        g = 0.5 / max(np.max(np.abs(y)) for y in versions.values())  # one gain for every version
        versions = {v: y * g for v, y in versions.items()}
        bird = np.mean([active_rms(y) for y in versions.values()])
        ref = reference(m, n)
        ref *= bird * 10 ** (-3 / 20) / active_rms(ref)
        for v, y in versions.items():
            sf.write(OUT / "wav" / f"{sid}_{v}.wav", y.astype(np.float32), SR, subtype="FLOAT")
        sf.write(OUT / "wav" / f"{sid}_reference.wav", ref.astype(np.float32), SR, subtype="FLOAT")
        order = list(RNG.permutation(list(versions)))
        letters = "ABCD"
        screens.append({"id": sid, "title": "Frère Jacques (first two lines)", "dur_s": round(n / SR, 3),
                        "reference": flac_b64(ref),
                        "versions": [{"id": letters[i], "audio": flac_b64(versions[v])} for i, v in enumerate(order)]})
        key[sid] = {"seed": seed, "letters": {letters[i]: str(v) for i, v in enumerate(order)},
                    "syllables": ids, "replaced_notes": replaced,
                    "differs": bool(replaced), "labels": [e["label"] for e in logs["new"]],
                    "shift_cents": {v: [round(e["shift_cents"], 1) for e in logs[v]] for v in logs}}
    screens = [screens[i] for i in RNG.permutation(len(screens))]
    page = {"study": "MELODY3", "version": 1, "seed": SEED, "minutes": 7, "screens": screens}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [[s["id"], key[s["id"]]["letters"]] for s in screens], sort_keys=True).encode()).hexdigest()[:12]
    html = (HERE / "melody3_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.STUDY = " + json.dumps(page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    design = {"study": "MELODY3", "design_hash": page["design_hash"], "seed": SEED, "settings": SETTINGS,
              "melody": m, "seeds": list(SEEDS), "wide_rule": {"loud_db": WIDE_LOUD_DB, "more_than_st": WIDE_ST},
              "wide_syllables": wide, "banks": {v: str(p.relative_to(ROOT)) for v, p in banks.items()},
              "bank_hashes": {v: json.loads(p.read_text())["hash"] for v, p in banks.items()},
              "screen_order": [s["id"] for s in screens], "key": key}
    (OUT / "design.json").write_text(json.dumps(design, indent=1, ensure_ascii=False))
    print(f"design {page['design_hash']}: {len(screens)} screens, page {len(html) / 1e6:.1f} MB, "
          f"{len(wide)} very wide sweeps {wide}")
    for sid in sorted(key):
        k = key[sid]
        print(f"  {sid}: replaced notes {k['replaced_notes']} "
              f"({[k['syllables']['new'][i] for i in k['replaced_notes']]} -> "
              f"{[k['syllables']['no_wide'][i] for i in k['replaced_notes']]})")


if __name__ == "__main__":
    main()
