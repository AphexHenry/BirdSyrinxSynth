"""MELODY1, part 2: the reference melody (plain tone at the bird's target pitches) with the bird's taps.

Adds to listening/MELODY/: the reference alone, the bird + reference (R1's syllables, 4 note rules),
and a figure of the bird's pitch against the notes.
usage: python/.venv/bin/python scripts/studies/melody1_reference.py "$PWD"   (after melody1_cases.py)
Also adds the R1 original (00), today's build with the alarm rule off (01b), and the tracker figure.
Study: agent documents/studies/MELODY1_tapped_melody.md
"""
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(sys.argv[1])
OUT = ROOT / "listening/MELODY"
WORK = OUT / "work"
os.environ.setdefault("MPLCONFIGDIR", str(WORK / ".mpl"))
import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RENDER = ROOT / "build/release/bin/plugin_render"
SR = 48000
bank = json.loads((ROOT / "assets/voicebanks/zf_22938/bank.json").read_text())
syls = [s for p in bank["phrases"] for s in p["syllables"]]
MELODY = [60, 62, 64, 60, 60, 62, 64, 60, 64, 65, 67, 64, 65, 67]
STEP = [0.42] * 7 + [0.6] + [0.42] * 2 + [0.84] + [0.42] * 2
TIMES = np.cumsum([0.0] + STEP)[: len(MELODY)] + 0.3
TARGET = [(n + 36) * 100.0 for n in MELODY]  # follow the keyboard: 3 octaves up
RULES = ["longest steady part, else ending (default)", "steady parts", "landing", "average"]
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def hz(c):
    return 440.0 * 2.0 ** ((np.asarray(c) - 6900.0) / 1200.0)


# ---- the reference melody: a soft sine on each target note (long G's held longer)
dur = 8.5
ref = np.zeros(int(dur * SR))
for k, (t, c) in enumerate(zip(TIMES, TARGET)):
    length = 0.6 if STEP[min(k, len(STEP) - 1)] > 0.8 or k == len(MELODY) - 1 else 0.3
    n = int(length * SR)
    tt = np.arange(n) / SR
    env = np.minimum(1.0, tt / 0.01) * np.minimum(1.0, (length - tt) / 0.06)
    a = int(t * SR)
    ref[a:a + n] += np.sin(2 * np.pi * hz(c) * tt) * env
ref *= 10 ** (-6 / 20) / np.max(np.abs(ref))
sf.write(str(OUT / "20_reference_melody.wav"), np.stack([ref, ref], 1).astype(np.float32), SR, subtype="FLOAT")


# ---- the bird's taps (R1's syllables: alarm rule off, seed 7), one render per note rule
def bird(rule):
    pj = WORK / f"ref_rule{rule}.params.json"
    pj.write_text(json.dumps({"perf.mapping": 1, "perf.note_rule": rule}))
    spec = ",".join(f"{n}:{t:.4f}:0.0600:100" for n, t in zip(MELODY, TIMES))
    wav, logp = WORK / f"ref_rule{rule}.wav", WORK / f"ref_rule{rule}.log.json"
    subprocess.run([str(RENDER), "--notes", spec, "--params", str(pj), "--seed", "7", "--duration", str(dur),
                    "--no-release-alarm", "--syllable-log", str(logp), "-o", str(wav)], check=True,
                   capture_output=True)
    y, _ = sf.read(str(wav), always_2d=True)
    return y, json.loads(logp.read_text())["starts"]


def heard_offsets(log):
    """Per note: the bird's loudness-weighted mean pitch minus the note (semitones)."""
    out = []
    for e, target in zip(log, TARGET):
        s = syls[e["syllable"]]
        c = 1200 * np.log2(np.array(s["f_hz"]) / 440.0) + 6900 + e["shift_cents"]
        w = np.array(s["a"]) ** 2
        out.append(float(np.sum(w * c) / np.sum(w) - target) / 100.0)
    return out


items, metrics = [], {}
logs = {}
for rule in range(4):
    y, log = bird(rule)
    logs[rule] = log
    off = heard_offsets(log)
    metrics[RULES[rule]] = {"mean_abs_offset_st": round(float(np.mean(np.abs(off))), 2),
                            "last_three_st": [round(x, 1) for x in off[-3:]],
                            "per_note_st": [round(x, 1) for x in off]}
    mix = y[: len(ref)] + 10 ** (-10 / 20) * np.max(np.abs(y)) * np.stack([ref, ref], 1)[: len(y)] / 0.5
    mix *= 10 ** (-3 / 20) / np.max(np.abs(mix))
    name = f"2{rule + 1}_bird_plus_reference_rule_{rule}.wav"
    sf.write(str(OUT / name), mix.astype(np.float32), SR, subtype="FLOAT")
    items.append({"file": name, "group": "with the reference melody",
                  "title": f"Bird + reference melody — a syllable's note is its {RULES[rule]}",
                  "listen_for": ("R1's syllables (as in 00), with the tune underneath on a soft tone. "
                                 if rule == 0 else "Same syllables, placed by another note rule. ")
                  + "Last three notes (E F G): heard offsets " + ", ".join(f"{x:+.1f}" for x in off[-3:])
                  + " semitones.",
                  "params": {"perf.mapping": 1, "perf.note_rule": rule}, "metrics": metrics[RULES[rule]],
                  "reference": "20_reference_melody.wav"})

# ---- figure: the bird's pitch against the notes, default rule and "average"
fig, axes = plt.subplots(2, 1, figsize=(11, 6.4), sharex=True)
for ax, rule in zip(axes, (0, 3)):
    for k, (t, c) in enumerate(zip(TIMES, TARGET)):
        ax.add_patch(plt.Rectangle((t - 0.02, c - 50), 0.34, 100, color="0.85", zorder=0))
    for e in logs[rule]:
        s = syls[e["syllable"]]
        a = np.array(s["a"])
        c = 1200 * np.log2(np.array(s["f_hz"]) / 440.0) + 6900 + e["shift_cents"]
        t = e["time_s"] + np.arange(len(c)) / 1000.0
        c = np.where(a > 0.08 * a.max(), c, np.nan)
        ax.plot(t, c, color="#e8572a", lw=2)
    ticks = range(9200, 11400, 100)
    ax.set_yticks(list(ticks))
    ax.set_yticklabels([f"{NAMES[(m // 100) % 12]}{m // 1200 - 1}" if (m // 100) % 12 in (0, 2, 4, 5, 7, 9, 11)
                        else "" for m in ticks], fontsize=7)
    ax.set_ylim(9150, 11350)
    ax.grid(axis="y", color="0.92", lw=0.6)
    off = metrics[RULES[rule]]["last_three_st"]
    ax.set_title(f"a syllable's note is its {RULES[rule]} — last three notes heard "
                 + ", ".join(f"{x:+.1f}" for x in off) + " st", fontsize=10)
    ax.set_ylabel("pitch")
axes[-1].set_xlabel("time (s) — grey bars: the reference notes (C D E C, C D E C, E F G, E F G)")
fig.tight_layout()
fig.savefig(OUT / "fig_bird_vs_reference.png", dpi=110)

# ---- 00 / 01b: R1's original file, and today's build with the alarm rule off (same syllables as R1)
r1_wav = ROOT / "listening/R1/02_melody_follow.wav"
if r1_wav.exists():
    import shutil

    shutil.copy(r1_wav, OUT / "00_r1_original.wav")
spec = ",".join(f"{n}:{t:.4f}:0.0600:100" for n, t in zip(MELODY, TIMES))
pj = WORK / "01b.params.json"
pj.write_text(json.dumps({"perf.mapping": 1}))
subprocess.run([str(RENDER), "--notes", spec, "--params", str(pj), "--seed", "7", "--tail", "1.5",
                "--no-release-alarm", "--syllable-log", str(WORK / "01b.log.json"),
                "-o", str(OUT / "01b_r1_playing_today.wav")], check=True, capture_output=True)

# ---- the last two notes: the bank's pitch curve over the real recording's spectrogram
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
if REC.exists():
    from scipy.signal import stft

    x, rsr = sf.read(str(REC), always_2d=True)
    x = x.mean(axis=1)
    flat = [(p, s) for p in bank["phrases"] for s in p["syllables"]]
    fig2, axes2 = plt.subplots(1, 2, figsize=(11, 4.2))
    for ax, k in zip(axes2, (29, 95)):
        p, s = flat[k]
        t0 = p["start_s"] + s["t_ms"] / 1000.0
        f = np.array(s["f_hz"])
        pad = 0.02
        seg = x[int((t0 - pad) * rsr): int((t0 + len(f) / 1000 + pad) * rsr)]
        F, T, Z = stft(seg, fs=rsr, nperseg=512, noverlap=512 - 48, window="hann")
        S = 20 * np.log10(np.abs(Z) + 1e-9)
        ax.pcolormesh((T - pad) * 1000, F / 1000, S - S.max(), vmin=-60, vmax=0, cmap="magma", shading="auto")
        ax.plot(np.arange(len(f)), f / 1000, color="cyan", lw=1.6, label="bank pitch curve (tracker)")
        ending = hz(s["anchors"]["rule"])
        ax.axhline(ending / 1000, color="white", ls="--", lw=1, label=f"its note by the ending rule: {ending:.0f} Hz")
        ax.set_ylim(1.5, 9)
        ax.set_xlim(-15, len(f) + 15)
        ax.set_title(f"#{k} {s['label']} — real recording (last F / last G of the tune)", fontsize=10)
        ax.set_xlabel("ms")
        ax.set_ylabel("kHz")
        ax.legend(fontsize=7, loc="upper right")
    fig2.tight_layout()
    fig2.savefig(OUT / "fig_last_notes_tracker.png", dpi=110)

m = json.loads((OUT / "manifest.json").read_text())
keep = [it for it in m["items"] if not it["file"].startswith(("2", "00_", "01b_"))]
r1_items = [{"file": "00_r1_original.wav", "group": "taps",
             "title": "R1 item 02, the original file (normalised to -3 dBFS)",
             "listen_for": "What you heard in R1: one syllable per tap, follow the keyboard.",
             "params": {}, "metrics": {}},
            {"file": "01b_r1_playing_today.wav", "group": "taps",
             "title": "Today's build, alarm rule off: R1's syllables again",
             "listen_for": "Should sound exactly like 00 (same syllables; 00 is normalised, so louder).",
             "params": {"perf.mapping": 1, "release_alarm": 0}, "metrics": {}}]
if not (OUT / "00_r1_original.wav").exists():
    r1_items = r1_items[1:]
for it in keep:
    if it["file"] == "01_r1_taps_follow.wav":
        it["title"] = "Today's build (alarm rule on): same taps, other random syllables"
m["items"] = r1_items + keep + [{"file": "20_reference_melody.wav", "group": "with the reference melody",
                      "title": "The reference melody alone (soft tone at the bird's target notes, C7–G7)",
                      "listen_for": "Frère Jacques, first two lines: C D E C, C D E C, E F G, E F G — "
                      "three octaves above the keys, where the bird lands.",
                      "params": {}, "metrics": {}}] + items
m["figures"] = [{"file": "fig_bird_vs_reference.png", "title": "The bird's pitch against the notes",
                 "caption": "Orange: each syllable's pitch as the bird sings it; grey: the note it should "
                 "land on. Top: the default note rule; bottom: 'average'."},
                {"file": "fig_last_notes_tracker.png", "title": "The last F and G: tracker vs recording",
                 "caption": "The glide down is real; only the quietest last ms are misread. The 'ending' "
                 "note sits at the quiet bottom of the glide."}]
(OUT / "manifest.json").write_text(json.dumps(m, indent=1, ensure_ascii=False))
print(json.dumps(metrics, indent=1, ensure_ascii=False))
