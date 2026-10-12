"""Render the WP-R4 listening page: every factory preset playing the same test, and the editor.

Everything is rendered by the plugin itself (`plugin_render --preset <name>`), so the renders go
through the same preset loader as the plugin. The test, identical for every preset:
  - 0.2–6.2 s: one key (A) held for 6 s → the bird's phrases around the note;
  - 6.8–11.4 s: a tapped tune (Frère Jacques, C4–G4; 0.15 s taps) → one syllable per tap, two
    when a short syllable ends before the key is released;
  - 12.0–18.0 s: three keys held together (C4 E4 G4) → up to three birds.
Own height ignores a key's octave; follow sings 3 octaves up, so these keys sit at C7–G7.
Also checks that the default preset leaves the sound exactly as before (criterion 7), and draws
the editor. Writes listening/R4/{*.wav, *.png, manifest.json, metrics.json}; then run
`scripts/listen.sh R4`.

usage: python/.venv/bin/python scripts/render_listening_r4.py
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

import numpy as np
import soundfile as sf
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
ANN = REC.with_suffix(".json")
RENDER = ROOT / "build/release/bin/plugin_render"
OUT = ROOT / "listening/R4"
ASSETS = ROOT / "agent documents/reports/assets/R4"
BANK = ROOT / "assets/voicebanks/zf_22938/bank.json"
SR = 48000
SEED = 7

HOLD = [(69, 0.2, 6.0, 100)]
TUNE_NOTES = [60, 62, 64, 60, 60, 62, 64, 60, 64, 65, 67]
TUNE = [(n, 6.8 + 0.42 * i, 0.15, 100) for i, n in enumerate(TUNE_NOTES)]
CHORD = [(60, 12.0, 6.0, 100), (64, 12.05, 5.95, 90), (67, 12.1, 5.9, 95)]
SECTIONS = {"hold": (0.2, 6.2), "tune": (6.8, 11.6), "chord": (12.0, 18.0)}
NOTES = HOLD + TUNE + CHORD


def notes_arg(notes: list[tuple[int, float, float, int]]) -> str:
    return ",".join(f"{n}:{t:.4f}:{d:.4f}:{v}" for n, t, d, v in notes)


def render(
    name: str, preset: str | None, notes: list, extra: list[str] | None = None
) -> tuple[np.ndarray, dict]:
    work = OUT / "work"
    work.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(RENDER),
        "--notes",
        notes_arg(notes),
        "--seed",
        str(SEED),
        "--tail",
        "1.5",
        "--syllable-log",
        str(work / f"{name}.log.json"),
        "-o",
        str(work / f"{name}.wav"),
    ]
    if preset is not None:
        cmd += ["--preset", preset]
    cmd += extra or []
    subprocess.run(cmd, check=True, capture_output=True)
    y, sr = sf.read(str(work / f"{name}.wav"), always_2d=True)
    assert sr == SR
    return y, json.loads((work / f"{name}.log.json").read_text())


def anchors_by_index() -> list[float]:
    bank = json.loads(BANK.read_text())
    return [s["anchors"]["rule"] for p in bank["phrases"] for s in p["syllables"]]


def db(x: float) -> float:
    return float(20.0 * np.log10(max(x, 1e-9)))


def metrics(y: np.ndarray, log: dict, anchors: list[float]) -> dict:
    starts = log["starts"]
    out: dict = {}
    for sec, (a, b) in SECTIONS.items():
        s = [e for e in starts if a <= e["time_s"] < b]
        out[f"{sec}_syllables"] = len(s)
        if sec == "chord":
            out["chord_keys_that_sang"] = len({e["key"] for e in s})
    out["hold_syllables_per_s"] = round(
        out["hold_syllables"] / (SECTIONS["hold"][1] - SECTIONS["hold"][0]), 2
    )
    sung = np.array([anchors[e["syllable"]] + e["shift_cents"] for e in starts])
    hz = 440.0 * 2.0 ** ((sung - 6900.0) / 1200.0)
    out["median_note_hz"] = round(float(np.median(hz)))
    out["note_range_hz"] = [round(float(np.percentile(hz, 5))), round(float(np.percentile(hz, 95)))]
    out["pan_spread"] = round(float(np.max(np.abs([e["pan"] for e in starts]))), 2)
    mono = y.mean(axis=1)
    out["peak_dbfs"] = round(db(float(np.max(np.abs(y)))), 1)
    active = np.abs(mono) > 1e-4
    out["rms_dbfs_while_singing"] = (
        round(db(float(np.sqrt(np.mean(mono[active] ** 2)))), 1) if active.any() else None
    )
    return out


def release_call_items() -> tuple[list[dict], dict]:
    """D-030: the bird's long rising call ("alarm") ends a long key press, and only that."""
    items: list[dict] = []
    found: dict = {}
    if REC.exists() and ANN.exists():  # the five real calls, cut from the recording
        segs = json.loads(ANN.read_text())["segments"]
        x, sr = sf.read(str(REC), always_2d=True)
        x = x.mean(axis=1)
        gap = np.zeros(int(0.6 * sr))
        parts, times = [], []
        for seg in segs:
            labels = [e.get("type_label", "") for e in seg.get("elements", [])]
            if any(lab.startswith("alarm") for lab in labels):
                a, b = int((seg["start"] - 0.06) * sr), int((seg["end"] + 0.2) * sr)
                parts += [x[a:b], gap]
                times.append(round(seg["start"], 2))
        y = np.concatenate(parts)
        y = 10 ** (-3 / 20) * y / (np.max(np.abs(y)) + 1e-12)
        if sr != SR:  # the page plays 48 kHz files
            from scipy.signal import resample_poly

            y = resample_poly(y, SR, sr)
        sf.write(
            str(OUT / "11_real_alarms.wav"),
            np.stack([y, y], axis=1).astype(np.float32),
            SR,
            subtype="FLOAT",
        )
        items.append(
            {
                "file": "11_real_alarms.wav",
                "group": "Release call",
                "title": "The five real alarms in 22938 (from the recording)",
                "listen_for": "The long call you named: about 0.4-0.5 s, a slow glide up (about "
                "3.2 to 4 kHz), rougher than the other syllables. Normalised to -3 dBFS.",
                "params": {"recording_times_s": times},
                "metrics": {"calls": len(times)},
            }
        )
        found["real_alarm_times_s"] = times

    def calls(log: dict) -> list[dict]:
        return [e for e in log["starts"] if e.get("release_alarm")]

    demos = [
        (
            "12_long_press.wav",
            "A held 2.5 s, then let go",
            "The bird sings its phrases (no alarm among them); when you let go, the syllable "
            "sounding finishes, about 60 ms of silence, then the long rising call.",
            [(69, 0.2, 2.5, 100)],
            {},
        ),
        (
            "13_short_presses.wav",
            "Presses of 0.1, 0.3, 0.55 and 0.65 s",
            "Only the last press (0.65 s) ends with the call; the shorter ones never do.",
            [(60, 0.2, 0.1, 100), (62, 1.4, 0.3, 100), (64, 2.6, 0.55, 100), (65, 3.8, 0.65, 100)],
            {},
        ),
        (
            "15_chord_released.wav",
            "Three keys held 3 s, let go together",
            "Three birds, each ends with its own call (slightly apart: each waits for its own "
            "syllable to finish).",
            [(60, 0.2, 3.0, 100), (64, 0.25, 2.95, 90), (67, 0.3, 2.9, 95)],
            {},
        ),
    ]
    bank = json.loads(BANK.read_text())
    ph0 = bank["phrases"][0]["syllables"]
    end0 = (ph0[-1]["t_ms"] + len(ph0[-1]["f_hz"])) / 1000.0
    demos.insert(
        2,
        (
            "14_let_go_in_a_pause.wav",
            "Let go between two phrases",
            "Phrase start, 1.5 s pauses: the first phrase, then silence; you let go in the "
            "silence and the call comes at once.",
            [(69, 0.2, end0 + 0.6, 100)],
            {"perf.order": 1, "perf.pause_s": 1.5},
        ),
    )
    work = OUT / "work"
    work.mkdir(parents=True, exist_ok=True)
    for fname, title, listen, notes, params in demos:
        extra: list[str] = []
        if params:
            pj = work / (fname + ".params.json")
            pj.write_text(json.dumps(params))
            extra = ["--params", str(pj)]
        y, log = render(fname[:-4], None, notes, extra)
        sf.write(str(OUT / fname), y.astype(np.float32), SR, subtype="FLOAT")
        releases = sorted(t + d for _, t, d, _ in notes)
        c = calls(log)
        m = {
            "presses_s": [round(d, 2) for _, _, d, _ in notes],
            "calls": len(c),
            "call_after_release_ms": [
                round(1000 * (e["time_s"] - max(r for r in releases if r <= e["time_s"] + 1e-9)))
                for e in c
            ],
        }
        found[fname] = m
        items.append(
            {
                "file": fname,
                "group": "Release call",
                "title": title,
                "listen_for": listen,
                "params": params | {"seed": SEED},
                "metrics": m,
            }
        )
    return items, found


def screenshot(name: str, args: list[str]) -> str:
    subprocess.run(
        [str(RENDER), *args, "--screenshot", str(OUT / name)], check=True, capture_output=True
    )
    return name


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    anchors = anchors_by_index()
    presets = [
        json.loads(p.read_text()) for p in sorted((ROOT / "assets/presets").glob("*.preset.json"))
    ]

    # Criterion 7: the default preset ("Finch") is the sound R1 shipped, sample for sample.
    y_plain, _ = render("check_no_preset", None, NOTES)
    y_def, _ = render("check_default_preset", presets[0]["name"], NOTES)
    default_check = {
        "preset": presets[0]["name"],
        "max_abs_difference": float(np.max(np.abs(y_plain - y_def))),
        "samples": int(y_plain.shape[0]),
    }

    items, all_metrics = [], {"default_preset_vs_no_preset": default_check, "presets": {}}
    call_items, all_metrics["release_call"] = release_call_items()
    extra_listen = {
        "Finch": "The reference: every other preset is heard against this one. Tune: one "
        "syllable per tap, sometimes two (a short syllable ends before the key is up). "
        "At 6.2 s and 18 s (letting go after long presses): the long rising call (D-030).",
        "Finch, natural": "Hold (0–6 s): the bird's own melody, even its first syllable. Tune: "
        "each tap keeps the syllable's natural place, so the tune is barely recognisable.",
        "Finch, on the note": "Hold: every syllable on A, a more tonal, pinned bird. "
        "Tune: as Finch.",
        "Finch, follow the keyboard": "Tune (7–11 s): Frère Jacques keeps its shape (three "
        "octaves up). Chord: three birds a third and a fifth apart.",
        "Solo bird": "One bird in the middle. Chord: C and E sing one syllable each, then G "
        "takes over and sings alone.",
        "Small flock": "Chord (12–18 s): three birds clearly apart, left to right.",
        "Sparse": "Hold: long silences (2 s) between phrases.",
        "Chatty": "Hold: hardly any pause, syllables in a new random order.",
        "Big bird": "Everything a fifth lower than Finch.",
        "Dry": "No room: the bare whistle, closer and drier than Finch.",
    }
    for k, p in enumerate(presets, start=1):
        slug = p["name"].lower().replace(", ", "_").replace(" ", "_")
        y, log = render(f"p{k:02d}", p["name"], NOTES)
        fname = f"{k:02d}_{slug}.wav"
        sf.write(str(OUT / fname), y.astype(np.float32), SR, subtype="FLOAT")
        m = metrics(y, log, anchors)
        all_metrics["presets"][p["name"]] = m
        items.append(
            {
                "file": fname,
                "title": f"{p['name']}  ({p['category']})",
                "listen_for": p["description"] + " " + extra_listen.get(p["name"], ""),
                "params": p["parameters"] | {"seed": SEED},
                "metrics": m,
                "group": p["category"],
                "reference": "01_finch.wav" if k > 1 else None,
            }
        )

    figures = [
        {
            "file": screenshot(
                "editor_play_flock.png",
                [
                    "--preset",
                    "Small flock",
                    "--notes",
                    "69:0.1:4.4:110,64:0.6:3.9:90,72:1.1:3.4:100,76:1.6:2.9:80",
                    "--seed",
                    "5",
                    "--duration",
                    "4.2",
                    "-o",
                    str(OUT / "work" / "shot_flock.wav"),
                ],
            ),
            "title": "Play page: four keys held (Small flock)",
            "caption": "Each bird has its colour, in the display and on its key. "
            "Lines: the held keys' notes.",
        },
        {
            "file": screenshot(
                "editor_play_follow.png",
                [
                    "--preset",
                    "Finch, follow the keyboard",
                    "--notes",
                    notes_arg(TUNE),
                    "--seed",
                    "2",
                    "--duration",
                    "11.6",
                    "-o",
                    str(OUT / "work" / "shot_follow.wav"),
                ],
            ),
            "title": "Play page: a tapped tune (Finch, follow the keyboard)",
            "caption": "One syllable per tap, following the tune.",
        },
        {
            "file": screenshot("editor_advanced.png", ["--screenshot-page", "advanced"]),
            "title": "Advanced page",
            "caption": "The anatomical voice's controls, as they were in R1.",
        },
    ]
    for f in figures:  # report copies: lossless, optimised (≤ 300 KB each, 02_architecture §11)
        Image.open(OUT / f["file"]).convert("RGB").save(ASSETS / f["file"], optimize=True)

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "R4",
        "title": "WP-R4 — factory presets and the release call (plugin renders not normalised)",
        "items": items + call_items,
        "figures": figures,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
    (OUT / "metrics.json").write_text(json.dumps(all_metrics, indent=1, ensure_ascii=False))
    print(json.dumps(all_metrics, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
