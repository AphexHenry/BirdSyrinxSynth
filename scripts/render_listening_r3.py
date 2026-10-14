"""Render the WP-R3 listening page: new renditions (shape model), the two birds, held keys, tunes.

Items (listening/R3/):
  - one syllable, again and again: the bird's own renditions of one syllable type, vs one rendition repeated
    identically (Variation 0), vs that rendition at Variation 100 % (the plugin's variation: length and pitch
    range, control::drawStretch + makeVariant). Two types: a 22937 one and a 22938 one.
  - a held key, through the plugin (plugin_render, seed 7): Variation 0 vs 1 (same choices, new renditions).
  - the birds: 22938 alone, 22937 alone, both (the default, merged at load), and the R5 bank (what it was).
  - a tune with "Finch, melody" at Variation 1: every syllable still lands on its note.
Writes listening/R3/{*.wav, manifest.json, metrics.json}; then `scripts/listen.sh R3`.
The studies have their own pages: listening/R3/ingest1/, listening/R3/vary1/.

usage: python/.venv/bin/python scripts/render_listening_r3.py
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts/studies"))
import cuicui_core  # noqa: E402
import r3_render as R  # noqa: E402
from melody2_make import MELODIES, reference  # noqa: E402

OUT = ROOT / "listening/R3"
WORK = OUT / "work"
RENDER = ROOT / "build/release/bin/plugin_render"
VB = ROOT / "assets/voicebanks"
BANKS = {"22938": VB / "hf_22938/bank.json", "22937": VB / "hf_22937/bank.json"}
R5 = VB / "zf_22938/bank.json"
SR = 48000
SEED = 7


def write(name: str, y: np.ndarray, db: float = -3.0) -> str:
    y = y * (10 ** (db / 20) / (np.max(np.abs(y)) + 1e-12))
    sf.write(str(OUT / name), y.astype(np.float32), SR, subtype="FLOAT")
    return name


def plugin(name: str, notes: list[tuple[int, float, float]], params: dict, preset: str | None = None,
           bank: list[Path] | None = None) -> tuple[np.ndarray, list[dict]]:
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / f"{name}.params.json").write_text(json.dumps(params))
    spec = ",".join(f"{n}:{t:.4f}:{d:.4f}:100" for n, t, d in notes)
    cmd = [str(RENDER), "--notes", spec, "--params", str(WORK / f"{name}.params.json"), "--seed", str(SEED),
           "--tail", "1.5", "--syllable-log", str(WORK / f"{name}.log.json"), "-o", str(WORK / f"{name}.wav")]
    if preset:
        cmd += ["--preset", preset]
    if bank:
        cmd += ["--bank", ",".join(str(b) for b in bank)]
    subprocess.run(cmd, check=True, capture_output=True)
    y, sr = sf.read(str(WORK / f"{name}.wav"), always_2d=True)
    assert sr == SR
    return y, json.loads((WORK / f"{name}.log.json").read_text())["starts"]


def flat(bank: dict) -> list[dict]:
    return [s for p in bank["phrases"] for s in p["syllables"]]


def repeated(rec: str, bank: dict, renditions: list[int], v: float, seeds: list[int] | None) -> np.ndarray:
    """Syllables one after another (0.3 s apart): `renditions` as they are, or the first one varied."""
    syl = flat(bank)
    events, t = [], 0.2
    for k, i in enumerate(renditions):
        if seeds is None:
            c = 1200 * np.log2(np.asarray(syl[i]["f_hz"]) / 440) + 6900
            a = np.asarray(syl[i]["a"], float)
        else:
            s0 = syl[renditions[0]]  # the plugin's variation: length and pitch range (STRETCH1)
            ls, rs = cuicui_core.stretch_draw(seeds[k], v, "swipe" in s0["label"].lower(), "hop" in s0["flags"])
            c, a, _ = cuicui_core.variant(str(BANKS[rec]), renditions[0], 0.0, [0.0] * 7, ls, rs)
            c, a = np.asarray(c), np.asarray(a)
        events.append((t, c, a))
        t += len(c) / 1000.0 + 0.3
    y, _ = R.whistle(events, int((t + 0.5) * SR))
    return y


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    banks = {r: json.loads(p.read_text()) for r, p in BANKS.items()}
    items, metrics = [], {}

    # ---- one syllable, again and again (the shape model)
    n = 1
    for rec in ("22937", "22938"):
        bank = banks[rec]
        syl = flat(bank)
        by = Counter(s["model"] for s in syl if "model" in s and "hop" not in s["flags"])
        model, _ = max(by.items(), key=lambda kv: (kv[1], -kv[0]))
        rend = [i for i, s in enumerate(syl) if s.get("model") == model and "hop" not in s["flags"]][:6]
        label = Counter(syl[i]["label"] for i in rend).most_common(1)[0][0]
        group = f"one syllable again and again ({rec}, a '{label}')"
        own = write(f"{n:02d}_{rec}_own_renditions.wav", repeated(rec, bank, rend, 0.0, None))
        items.append({"file": own, "group": group, "title": f"{rec}: the bird's own {len(rend)} renditions",
                      "listen_for": "How the bird itself varies one syllable from song to song: the target.",
                      "params": {"bank": f"hf_{rec}", "syllables": rend}})
        same = write(f"{n + 1:02d}_{rec}_one_rendition_variation0.wav",
                     repeated(rec, bank, [rend[0]] * len(rend), 0.0, None))
        items.append({"file": same, "group": group, "reference": own,
                      "title": f"{rec}: one rendition, {len(rend)} times — Variation 0",
                      "listen_for": "The same syllable, identical every time (Variation 0).",
                      "params": {"perf.natural_variation": 0.0}})
        var = write(f"{n + 2:02d}_{rec}_one_rendition_variation1.wav",
                    repeated(rec, bank, [rend[0]] * len(rend), 1.0, list(range(101, 101 + len(rend)))))
        items.append({"file": var, "group": group, "reference": own,
                      "title": f"{rec}: that rendition, {len(rend)} times — Variation 100 %",
                      "listen_for": "Every time a little shorter or longer and with a narrower or wider sweep "
                                    "(the spans you chose in STRETCH1).",
                      "params": {"perf.natural_variation": 1.0}})
        n += 3

    # ---- a held key: Variation 0 vs 1, through the plugin
    hold = [(64, 0.1, 10.0)]
    logs = {}
    for v in (0.0, 1.0):
        y, log = plugin(f"hold_v{int(v)}", hold, {"perf.natural_variation": v, "out.room": 1})
        logs[v] = log
        name = write(f"{n:02d}_hold_variation{int(v)}.wav", y)
        items.append({"file": name, "group": "a held key (the plugin)", "reference": None if v == 0 else prev,
                      "title": f"Key E held 10 s — Variation {int(v)}",
                      "listen_for": ("The two birds' real syllables and phrases, as recorded. " if v == 0 else
                                     "The same syllables, each a little shorter or longer and with a narrower or "
                                     "wider sweep; the next one waits for it. "),
                      "params": {"perf.natural_variation": v, "seed": SEED, "key": 64}})
        prev = name
        n += 1
    same_choices = [e["syllable"] for e in logs[0.0]] == [e["syllable"] for e in logs[1.0]]
    metrics["hold_same_choices"] = same_choices
    metrics["hold_syllables"] = len(logs[1.0])
    metrics["hold_birds"] = dict(Counter(int(e.get("bird", -1)) for e in logs[1.0]))

    # ---- the birds
    hold2 = [(64, 0.1, 25.0)]
    for tag, files, title, lf in (
            ("22938", [BANKS["22938"]], "22938 alone (hf_22938: 436 syllables)",
             "Its songs always start the same way, then branch."),
            ("22937", [BANKS["22937"]], "22937 alone (hf_22937: 358 syllables)",
             "Fixed songs, sung again and again; the long rising ends are often thinner than the bird (INGEST1)."),
            ("both", None, "Both birds (the default: merged when the plugin starts)",
             "Phrases from either bird, at random (over 5 minutes: 66 % / 34 % of the phrases)."),
            ("r5", [R5], "The R5 bank (zf_22938: 123 syllables) — what the plugin had",
             "Fewer phrases: they come back more often.")):
        y, log = plugin(f"bird_{tag}", hold2, {"out.room": 1}, bank=files)
        name = write(f"{n:02d}_bird_{tag}.wav", y)
        if tag == "both":
            changes = [round(e["time_s"], 1) for a, e in zip(log, log[1:], strict=False) if e["bird"] != a["bird"]]
            lf += f" Here the bird changes at {', '.join(f'{t} s' for t in changes[:6])}."
            metrics["bird_both_changes_s"] = changes
        items.append({"file": name, "group": "the birds", "title": title, "listen_for": lf,
                      "params": {"seed": SEED, "key": 64, "bank": tag, "held_s": 25}})
        metrics[f"bird_{tag}_syllables"] = len(log)
        n += 1

    # ---- a tune with "Finch, melody" at Variation 1
    m = MELODIES["frere_jacques"]
    times = m["times"]
    notes = [(nn, t, (times[i + 1] - t - 0.01) if i + 1 < len(times) else 0.84)
             for i, (nn, t) in enumerate(zip(m["notes"], times, strict=True))]
    y, log = plugin("tune_melody_v1", notes, {}, preset="Finch, melody")
    ref = reference(m, len(y))
    ref *= 0.6 * np.sqrt(np.mean(y[:, 0] ** 2)) / (np.sqrt(np.mean(ref**2)) + 1e-12)
    rname = write(f"{n:02d}_tune_reference.wav", np.stack([ref, ref], 1))
    items.append({"file": rname, "group": "a tune", "title": f"{m['title']}: the tune on a soft tone",
                  "listen_for": "The notes (3 octaves above the keys).", "params": {}})
    tname = write(f"{n + 1:02d}_tune_finch_melody_variation1.wav", y)
    anchors = [s["anchors"]["rule"] for r in ("22938", "22937") for s in flat(banks[r])]  # merged order
    off = [anchors[e["syllable"]] + e["variant_note_cents"] + e["shift_cents"] - e["target_cents"] for e in log]
    items.append({"file": tname, "group": "a tune", "reference": rname,
                  "title": f"{m['title']}, legato, preset 'Finch, melody' (Variation 1)",
                  "listen_for": "Every syllable still lands on its note (its pitch range changes around the note); "
                                "only lengths and sweeps vary.", "params": {"preset": "Finch, melody"}})
    metrics["tune_syllables"] = len(log)
    metrics["tune_max_note_error_cents"] = round(float(np.max(np.abs(off))), 6)
    metrics["tune_varied_syllables"] = sum(1 for e in log if e["range_scale"] != 1.0 or e["length_scale"] != 1.0)

    figs = [(ROOT / "build/r3/types_top.png", "fig_types.png", "Syllable types: the bird's renditions",
             "The 24 largest types of both birds (pitch in semitones over ms): each a tight bundle of renditions. "
             "Their spread is what Variation 1 reproduces."),
            (ROOT / "agent documents/reports/assets/R3/vary1_ratings.png", "fig_vary1.png", "VARY1 ratings",
             "The first blind test. VARY2 then showed the shape model is not audible once its loudness changes "
             "are removed; the plugin now varies length and pitch range instead (STRETCH1).")]
    for src, dst, _, _ in figs:
        if src.exists():
            shutil.copy(src, OUT / dst)
    manifest = {"format": "cuicui.listening_manifest", "version": 1, "wp": "R3",
                "title": "WP-R3 — variations within the bird's style: length and pitch range, two birds",
                "items": items,
                "figures": [{"file": d, "title": t, "caption": c} for s, d, t, c in figs if s.exists()]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
    (OUT / "metrics.json").write_text(json.dumps(metrics, indent=1, ensure_ascii=False))
    print(json.dumps(metrics, indent=1))


if __name__ == "__main__":
    main()
