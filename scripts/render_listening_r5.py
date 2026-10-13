"""Render the WP-R5 listening page: tunes before and after the new notes, held keys, and the bird itself.

Items (listening/R5/):
  - tapped tunes (60 ms taps, follow the keyboard, seed 7): the R1 bank (old notes, rule A) vs the WP-R5 bank (rule E +
    the notes set by ear), each with the tune on a soft tone as reference. Core path (cuicui_core.sing) for both.
  - held keys (D-033): Frère Jacques held 0.35 s and legato, today vs the factory preset "Finch, melody", rendered by
    the plugin itself (plugin_render --preset), so the preset file is what is heard.
  - the bird itself (criterion 6): bank phrase 4 (R1CHECK's P1) sung from MIDI in R1CHECK's "natural" setup (held key A,
    order phrase start, first syllable on the note off) with the old and the new bank: same syllables, same timing,
    one constant shift. Also the default setup (first syllable on the note).
Writes listening/R5/{*.wav, manifest.json, metrics.json}; then `scripts/listen.sh R5`.
The blind tests have their own pages: listening/R5/melody2/, tune1/, melody3/ (and the held-key choice: held/).

usage: python/.venv/bin/python scripts/render_listening_r5.py
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts/studies"))
import cuicui_core  # noqa: E402
from melody2_make import MELODIES, VELOCITY, reference  # noqa: E402

OUT = ROOT / "listening/R5"
WORK = OUT / "work"
RENDER = ROOT / "build/release/bin/plugin_render"
NEW = ROOT / "assets/voicebanks/zf_22938/bank.json"
OLD = WORK / "bank_R1_committed.json"
SR = 48000
SEED = 7
TAPS = {"perf.mapping": "follow", "perf.snap": 0.0, "perf.first_on_note": 1, "out.stereo_spread": 0.0}


def peak(y: np.ndarray, db: float = -3.0) -> np.ndarray:
    return y * (10 ** (db / 20) / (np.max(np.abs(y)) + 1e-12))


def write(name: str, y: np.ndarray) -> str:
    sf.write(str(OUT / name), y.astype(np.float32), SR, subtype="FLOAT")
    return name


def tap_events(m: dict) -> list[tuple]:
    ev = []
    for n, t in zip(m["notes"], m["times"], strict=True):
        ev += [(t, "on", n, VELOCITY), (t + 0.06, "off", n, 0.0)]
    return ev


def heard_offsets(bank: dict, log: list[dict], m: dict) -> list[float]:
    """Per note: the bird's loudness-weighted pitch minus the note (semitones), MELODY1's measure."""
    syl = [s for p in bank["phrases"] for s in p["syllables"]]
    out = []
    for e, n in zip(log, m["notes"], strict=True):
        s = syl[e["syllable"]]
        c = 1200 * np.log2(np.asarray(s["f_hz"]) / 440.0) + 6900 + e["shift_cents"]
        w = np.asarray(s["a"]) ** 2
        out.append(round(float(np.sum(w * c) / np.sum(w) - (n + 36) * 100) / 100.0, 1))
    return out


def plugin(name: str, notes: list[tuple[int, float, float]], preset: str | None, params: dict) -> tuple[np.ndarray, dict]:
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / f"{name}.params.json").write_text(json.dumps(params))
    spec = ",".join(f"{n}:{t:.4f}:{d:.4f}:100" for n, t, d in notes)
    cmd = [str(RENDER), "--notes", spec, "--params", str(WORK / f"{name}.params.json"), "--seed", str(SEED),
           "--tail", "1.5", "--syllable-log", str(WORK / f"{name}.log.json"), "-o", str(WORK / f"{name}.wav")]
    if preset:
        cmd += ["--preset", preset]
    subprocess.run(cmd, check=True, capture_output=True)
    y, sr = sf.read(str(WORK / f"{name}.wav"), always_2d=True)
    assert sr == SR
    return y, json.loads((WORK / f"{name}.log.json").read_text())


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    old, new = json.loads(OLD.read_text()), json.loads(NEW.read_text())
    assert old["hash"] == "09b16ee69ac8"
    items, metrics = [], {}

    # ---- tapped tunes: old vs new notes, same syllables (same seed, same playable set)
    for k, (mid, m) in enumerate(MELODIES.items()):
        dur = m["times"][-1] + 1.2
        renders = {}
        for tag, bank, path in (("old", old, OLD), ("new", new, NEW)):
            audio, log = cuicui_core.sing(str(path), tap_events(m), TAPS, float(SR), dur, SEED, 256)
            renders[tag] = (np.asarray(audio, dtype=np.float64), log, heard_offsets(bank, log, m))
        assert [e["syllable"] for e in renders["old"][1]] == [e["syllable"] for e in renders["new"][1]]
        g = 10 ** (-3 / 20) / max(np.max(np.abs(y)) for y, _, _ in renders.values())
        n = len(renders["new"][0])
        ref = reference(m, n)
        ref *= 0.7 * np.sqrt(np.mean((renders["new"][0][:, 0] * g) ** 2)) / (np.sqrt(np.mean(ref**2)) + 1e-12)
        ref_name = write(f"{k * 3 + 1:02d}_{mid}_reference.wav", np.stack([ref, ref], 1))
        items.append({"file": ref_name, "group": f"taps {mid}", "title": f"{m['title']}: the tune on a soft tone",
                      "listen_for": "The notes the bird should land on (3 octaves above the keys).", "params": {}})
        for j, tag in enumerate(("old", "new")):
            y, log, off = renders[tag]
            fname = write(f"{k * 3 + 2 + j:02d}_{mid}_taps_{tag}_notes.wav", y * g)
            mm = {"mean_abs_heard_offset_st": round(float(np.mean(np.abs(off))), 2), "per_note_st": off,
                  "bank": (old if tag == "old" else new)["hash"]}
            metrics[f"{mid}_taps_{tag}"] = mm
            items.append({
                "file": fname, "group": f"taps {mid}", "reference": ref_name,
                "title": f"{m['title']}: taps, {'old notes (R1, rule A)' if tag == 'old' else 'new notes (rule E + ear)'}",
                "listen_for": ("Before: the same syllables as after, placed by the old rule. "
                               if tag == "old" else "After: the same syllables, placed by their new notes. ")
                + f"Mean distance of the loud part from the note: {mm['mean_abs_heard_offset_st']} semitones.",
                "params": {**TAPS, "seed": SEED}, "metrics": mm})

    # ---- held keys: today vs the "Finch, melody" preset, through the plugin (D-033)
    m = MELODIES["frere_jacques"]
    for style, label in (("held", "notes held 0.35 s"), ("legato", "legato")):
        times = m["times"]
        notes = [(n, t, 0.35 if style == "held" else ((times[i + 1] - t - 0.01) if i + 1 < len(times) else 0.84))
                 for i, (n, t) in enumerate(zip(m["notes"], times, strict=True))]
        pair = {}
        for tag, preset, params in (("today", None, {"perf.mapping": 1}), ("melody", "Finch, melody", {})):
            y, log = plugin(f"held_{style}_{tag}", notes, preset, params)
            pair[tag] = (y, log)
        g = 10 ** (-3 / 20) / max(np.max(np.abs(y)) for y, _ in pair.values())
        names = {}
        for tag, (y, log) in pair.items():
            starts = log["starts"]
            per_key = round(len(starts) / len(notes), 2)
            syl = [s for p in new["phrases"] for s in p["syllables"]]
            on = [abs(syl[e["syllable"]]["anchors"]["rule"] + e["shift_cents"] - e["target_cents"]) <= 50 for e in starts]
            mm = {"syllables_per_key": per_key, "on_the_note_pct": round(100 * float(np.mean(on)), 0),
                  "release_calls": sum(1 for e in starts if e.get("release_alarm"))}
            metrics[f"held_{style}_{tag}"] = mm
            names[tag] = f"{len(items) + 1:02d}_frere_jacques_{style}_{tag}.wav"
            write(names[tag], y * g)
            items.append({
                "file": names[tag], "group": f"held {style}",
                "reference": names["today"] if tag == "melody" else None,
                "title": f"Frère Jacques, {label}: {'today (follow the keyboard)' if tag == 'today' else 'preset Finch, melody'}",
                "listen_for": ("Today: after the first syllable the bird goes on with its own melody, off your notes. "
                               if tag == "today" else
                               "Finch, melody: every syllable on your note, one bird; the tune should be clear. ")
                + f"{per_key} syllables per key, {mm['on_the_note_pct']:.0f} % on the note, "
                f"{mm['release_calls']} long call(s).",
                "params": {"preset": preset or "Finch (default) + follow", "seed": SEED}, "metrics": mm})

    # ---- the bird itself: phrase 4, R1CHECK natural setup, old vs new bank (criterion 6)
    phrase = 4
    taps = [(0.3 * k, "on", 48, 0.01) for k in range(phrase)] + [(0.3 * k + 0.03, "off", 48, 0.0) for k in range(phrase)]
    last = new["phrases"][phrase]["syllables"][-1]
    t_on = 0.3 * phrase + 1.0
    hold = (last["t_ms"] + len(last["f_hz"])) / 1000 + 0.02
    ev = taps + [(t_on, "on", 69, VELOCITY), (t_on + hold, "off", 69, 0.0)]
    crit6 = {}
    for setup, extra in (("natural", {"perf.first_on_note": 0}), ("default", {})):
        out = {}
        for tag, path in (("old", OLD), ("new", NEW)):
            audio, log = cuicui_core.sing(str(path), ev, {"perf.order": "phrase", **extra}, float(SR), t_on + hold + 1.0,
                                          SEED, 256)
            sung = [e for e in log if e["sample"] >= int(t_on * SR)]
            out[tag] = (np.asarray(audio, dtype=np.float64)[int((t_on - 0.2) * SR):], sung)
        so, sn = out["old"][1], out["new"][1]
        same = [(a["syllable"], a["sample"]) for a in so] == [(b["syllable"], b["sample"]) for b in sn]
        d = np.array([b["shift_cents"] - a["shift_cents"] for a, b in zip(so, sn, strict=True)])
        crit6[setup] = {"syllables": len(sn), "same_syllables_and_onsets": bool(same),
                        "shift_change_first_cents": round(float(d[0]), 1),
                        "shift_change_others_cents": [round(float(d[1:].min()), 1), round(float(d[1:].max()), 1)],
                        "centre_change_cents": round(new["centres_cents"]["rule"] - old["centres_cents"]["rule"], 1)}
        if setup == "natural":
            g = 10 ** (-3 / 20) / max(np.max(np.abs(y)) for y, _ in out.values())
            nm = {tag: f"{len(items) + 1 + i:02d}_phrase4_natural_{tag}_bank.wav" for i, tag in enumerate(("old", "new"))}
            for tag in ("old", "new"):
                write(nm[tag], out[tag][0] * g)
                items.append({
                    "file": nm[tag], "group": "the bird itself", "reference": nm["old"] if tag == "new" else None,
                    "title": f"Phrase 4 from MIDI, R1CHECK's natural setup — {tag} bank",
                    "listen_for": ("The bird's own phrase (the R1CHECK version rated 100/100 against the recording). "
                                   if tag == "old" else
                                   f"The same {len(sn)} syllables at the same times, all {abs(float(d[1])):.0f} cents "
                                   "lower (the bird's centre moved with the new notes): it should sound the same bird. "),
                    "params": {"perf.order": "phrase", "perf.first_on_note": 0, "key": 69}, "metrics": crit6["natural"]})
    metrics["criterion_6"] = crit6
    figs = [("agent documents/studies/assets/melody2_ratings.png", "fig_melody2_ratings.png",
             "MELODY2: which note rule makes tunes recognisable",
             "Ratings per version over 9 screens; black bars are the means. No rule beat A by 10 points; the human chose E."),
            ("agent documents/studies/assets/tune1_corrections.png", "fig_tune1_corrections.png",
             "TUNE1: the 11 syllables tuned by ear",
             "Black: where the ear put the note, relative to rule E; markers: where each other rule puts it.")]
    for src, dst, _, _ in figs:
        shutil.copy(ROOT / src, OUT / dst)
    manifest = {"format": "cuicui.listening_manifest", "version": 1, "wp": "R5",
                "title": "WP-R5 — melodies that sound right: new notes, held keys, the bird unchanged",
                "items": items, "figures": [{"file": d, "title": t, "caption": c} for _, d, t, c in figs]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
    (OUT / "metrics.json").write_text(json.dumps(metrics, indent=1, ensure_ascii=False))
    print(json.dumps(metrics, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
