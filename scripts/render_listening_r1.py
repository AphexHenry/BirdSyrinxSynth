"""Render the WP-R1 listening page (whistle voice, real syllables, modes A + B) and its measurements.

Everything audible is rendered through the plugin (`build/release/bin/plugin_render`). Measurements:
  - criterion 4: H2/H3 of the whistle with the R0 tracker (scripts/review/track.py), no breath noise;
  - criterion 5: click metric of scripts/studies/engine_gate_poc.py, plugin vs the real recording;
  - criterion 6 (illustration): plugin vs the prototype algorithm (python/cuicui/reference/singing.py).
Writes listening/R1/{*.wav, manifest.json, metrics.json, fig_*.png}; then run `scripts/listen.sh R1`.

usage: python/.venv/bin/python scripts/render_listening_r1.py
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / "listening" / "R1" / ".mplconfig"))
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts" / "studies"))

import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import soundfile as sf  # noqa: E402
from scipy import signal  # noqa: E402

import cuicui_core as cc  # noqa: E402
from cuicui.analysis import f0_track as T  # noqa: E402
from cuicui.bank import load_bank  # noqa: E402
from cuicui.reference import singing as ref  # noqa: E402

RENDER = ROOT / "build/release/bin/plugin_render"
OUT = ROOT / "listening/R1"
ASSETS = ROOT / "agent documents/reports/assets/R1"
BANK_PATH = ROOT / "assets/voicebanks/zf_22938/bank.json"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
SR = 48000
SEED = 7
PASSAGES = {"P1": (15.40, 17.85, 4), "P3": (26.95, 29.50, 7)}  # (start, end, bank phrase), as ENGINE1


# ---------------------------------------------------------------- plugin rendering
def render(name: str, notes: list[tuple[int, float, float, int]], params: dict | None = None,
           seed: int = SEED, duration: float = 0.0, tail: float = 1.0) -> tuple[np.ndarray, dict]:
    """plugin_render a note list (note, start_s, dur_s, velocity) → (stereo, syllable log)."""
    work = OUT / "work"
    work.mkdir(parents=True, exist_ok=True)
    (work / f"{name}.params.json").write_text(json.dumps(params or {}))
    spec = ",".join(f"{n}:{t:.4f}:{d:.4f}:{v}" for n, t, d, v in notes)
    cmd = [str(RENDER), "--notes", spec, "--params", str(work / f"{name}.params.json"), "--seed",
           str(seed), "--tail", str(tail), "--syllable-log", str(work / f"{name}.log.json"),
           "-o", str(work / f"{name}.wav")]
    if duration > 0:
        cmd += ["--duration", str(duration)]
    subprocess.run(cmd, check=True, capture_output=True)
    y, sr = sf.read(str(work / f"{name}.wav"), always_2d=True)
    assert sr == SR
    return y, json.loads((work / f"{name}.log.json").read_text())


def write(name: str, y: np.ndarray, peak_db: float | None = -3.0) -> str:
    if peak_db is not None:
        y = y * (10 ** (peak_db / 20) / (np.max(np.abs(y)) + 1e-12))
    sf.write(str(OUT / name), y.astype(np.float32), SR, subtype="FLOAT")
    return name


def mono(y: np.ndarray) -> np.ndarray:
    return y.mean(axis=1) * np.sqrt(2) if y.ndim == 2 else y


# ---------------------------------------------------------------- measurements
def colour_metrics(y: np.ndarray) -> dict:
    """Criterion 4 on a dry render: H2/H3 re H1 with the R0 tracker (median over voiced frames)."""
    tr = T.track(mono(y), SR)
    v = tr["voiced"]
    return {"h2_db_median": round(float(np.nanmedian(tr["h2_db"][v])), 2),
            "h3_db_median": round(float(np.nanmedian(tr["h3_db"][v])), 2), "voiced_frames": int(v.sum())}


def noise_floor_db(y: np.ndarray, a: int, b: int) -> float:
    """Energy between the harmonics (1–20 kHz, outside ±4 % of k·f0, k = 1…5) re the fundamental, over
    the samples [a, b) of a steady part, Blackman-Harris window: breath or hiss would show up here."""
    seg = mono(y)[a:b]
    w = signal.windows.blackmanharris(len(seg))
    spec = np.abs(np.fft.rfft(seg * w, 16 * len(seg))) ** 2
    f = np.fft.rfftfreq(16 * len(seg), 1 / SR)
    band = (f > 1200) & (f < 9000)
    f0 = f[band][np.argmax(spec[band])]
    keep = (f > 1000) & (f < 20000)
    lobe = 6 * SR / len(seg)
    for k in range(1, 6):
        keep &= np.abs(f - k * f0) > max(0.04 * k * f0, lobe)
    h1 = np.sum(spec[np.abs(f - f0) <= max(0.04 * f0, lobe)])
    return round(float(10 * np.log10(np.sum(spec[keep]) / h1 + 1e-30)), 1)


def click_metrics(x: np.ndarray, mask: np.ndarray | None = None) -> dict:
    """ENGINE1 click metric (engine_gate_poc.metrics) at every tracked syllable onset / offset."""
    import engine_gate_poc as G  # (imports the R0 study helpers)

    tr = T.track(x, SR, mask=mask)
    runs = [r for r in T.runs(tr["voiced"]) if r[1] - r[0] >= 8]
    on = [tr["t"][a] for a, _ in runs]
    off = [tr["t"][b - 1] for _, b in runs]
    m = G.metrics(x / (np.max(np.abs(x)) + 1e-12) * 0.5, on, off)
    return {"onset_lf_db": round(m["onset"][0], 1), "offset_lf_db": round(m["offset"][0], 1),
            "onset_edge_db": round(m["onset"][1], 1), "offset_edge_db": round(m["offset"][1], 1),
            "syllables": len(runs)}


def real_passage(x: np.ndarray, ann: dict, t0: float, t1: float) -> tuple[np.ndarray, np.ndarray]:
    import colour1_make as C1

    seg = C1.load(t0, t1, x)
    return seg, C1.ann_mask(ann, t0, t1, int(len(seg) / SR / T.HOP_S) + 8)


def phrase_render(name: str, phrase: int, t_lead: float, params: dict, key: int) -> tuple[np.ndarray, dict]:
    """The plugin singing bank phrase `phrase` from its first syllable (order = phrase start):
    `phrase` short taps first advance the phrase pointer, then the held key; returns the cut
    passage starting `t_lead` s before the phrase's first syllable."""
    taps = [(48, 0.3 * k, 0.03, 1) for k in range(phrase)]
    t_on = 0.3 * phrase + 1.0
    last = load_bank(BANK_PATH)["phrases"][phrase]["syllables"][-1]
    dur = (last["t_ms"] + len(last["f_hz"])) / 1000
    y, log = render(name, taps + [(key, t_on, dur + 0.02, 100)], {**params, "perf.order": 1})
    lat = log["latency_samples"]
    a = int(round((t_on - t_lead) * SR)) + lat
    return y[a:], log


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    bank_json = load_bank(BANK_PATH)
    bank = ref.Bank.from_json(bank_json)
    items: list[dict] = []
    metrics: dict = {}

    # 01 / 02 — a melody tapped (mode A), own height vs follow
    melody = [60, 62, 64, 60, 60, 62, 64, 60, 64, 65, 67, 64, 65, 67]
    times = np.cumsum([0.0] + [0.42] * 7 + [0.6] + [0.42] * 2 + [0.84] + [0.42] * 2)[: len(melody)]
    notes = [(n, 0.3 + float(t), 0.06, 100) for n, t in zip(melody, times, strict=True)]
    for k, (mapping, label) in enumerate(((0, "own height"), (1, "follow the keyboard")), start=1):
        y, log = render(f"melody_{mapping}", notes, {"perf.mapping": mapping})
        starts = log["starts"]
        lat = log["latency_samples"]
        delays = [s["sample"] - int(round(n[1] * SR)) for s, n in zip(starts, notes, strict=True)]
        tgt = [ref.target(n[0], bank.centres["rule"], "follow" if mapping else "own_height", 0.0) for n in notes]
        err = [bank.syllables[s["syllable"]].anchors["rule"] + s["shift_cents"] - t
               for s, t in zip(starts, tgt, strict=True)]
        m = {"syllables": len(starts), "taps": len(notes), "max_start_delay_ms": round(max(delays) / 48 + lat / 48, 2),
             "first_syllable_on_note_max_err_cents": float(np.max(np.abs(err))),
             "sung_notes_cents": [round(t) for t in tgt]}
        metrics[f"0{k}"] = m
        items.append({"file": write(f"0{k}_melody_{'own' if mapping == 0 else 'follow'}.wav", y),
                      "title": f"Tapped melody (Frère Jacques), mapping: {label}",
                      "listen_for": ("Each tap sings exactly one real syllable, on the note. Own height: every key "
                                     "sings near the bird's natural height (A7), so the tune's shape is folded."
                                     if mapping == 0 else
                                     "Follow: the bird sings 3 octaves above the keys, so the tune keeps its "
                                     "shape (C7–G7). Each tap is a different random syllable."),
                      "params": {"perf.mapping": label, "perf.first_on_note": "on"}, "metrics": m,
                      "group": "melody"})

    # 03 — one key held 6 s (mode B, random start)
    y, log = render("held", [(69, 0.3, 6.0, 100)], {})
    s = log["starts"]
    phr = sorted({x["phrase"] for x in s})
    metrics["03"] = {"syllables": len(s), "phrases": phr, "labels": [x["label"] for x in s][:12]}
    items.append({"file": write("03_held_6s.wav", y), "title": "One key (A) held for 6 s: real phrases, random start",
                  "listen_for": "The bird keeps singing its real phrases around the note, with its own timing, a "
                                "0.5 s pause between phrases, and the first syllable exactly on A. Release at 6.3 s: "
                                "the syllable in progress finishes, nothing after.",
                  "params": {"perf.order": "random start", "perf.pause_s": 0.5}, "metrics": metrics["03"]})
    held_dry, held_log = render("held_dry", [(69, 0.3, 6.0, 100)], {"out.room": 0})
    metrics["clicks_plugin_held_6s_dry"] = click_metrics(mono(held_dry))
    metrics["clicks_plugin_held_6s_room"] = click_metrics(mono(y))

    # 04 — a chord of 4 held keys (a small flock)
    y, log = render("chord", [(n, 0.3, 6.0, 90) for n in (60, 64, 67, 71)], {})
    metrics["04"] = {"syllables": len(log["starts"]), "keys": 4,
                     "pans": sorted({round(x["pan"], 3) for x in log["starts"]})}
    items.append({"file": write("04_chord_4_keys.wav", y), "title": "Chord C E G B held 6 s: four birds",
                  "listen_for": "Four independent birds, each singing real phrases around its own note, spread a "
                                "little left/right. Busy but not mechanical; no clicks when they overlap.",
                  "params": {"perf.polyphony": 4}, "metrics": metrics["04"]})

    # 05–07 — the same phrase with snap 0 / 0.5 / 1 (same seed → same syllables, only the placement differs)
    for k, snap in enumerate((0.0, 0.5, 1.0), start=5):
        y, log = render(f"snap_{snap}", [(69, 0.3, 4.0, 100)], {"perf.snap": snap, "perf.order": 1})
        sh = [round(x["shift_cents"], 1) for x in log["starts"]]
        notes_c = [round(bank.syllables[x["syllable"]].anchors["rule"] + x["shift_cents"]) for x in log["starts"]]
        metrics[f"0{k}"] = {"snap": snap, "syllables": len(sh), "sung_notes_cents": notes_c}
        items.append({"file": write(f"0{k}_snap_{int(snap * 100):03d}.wav", y),
                      "title": f"Phrase 0 → 1 held on A, snap {snap:g}",
                      "listen_for": {0.0: "Snap 0: the bird's natural melody, moved as a whole to A.",
                                     0.5: "Snap 0.5: the melody squeezed halfway towards A.",
                                     1.0: "Snap 1: every syllable's note lands on A (monotone, the gestures stay)."}[
                                         snap],
                      "params": {"perf.snap": snap, "perf.order": "phrase start"}, "metrics": metrics[f"0{k}"],
                      "group": "snap"})

    # 08 / 09 — plugin vs the prototype algorithm (same MIDI, same seed), dry
    notes_cmp = [(64, 0.2, 2.0, 100), (71, 2.9, 0.05, 80), (67, 3.6, 1.5, 110), (60, 5.8, 0.05, 60)]
    dry = {"out.room": 0, "out.master_db": 0}
    y_plug, log = render("cmp_plugin", notes_cmp, dry, seed=20261012, duration=8.0)
    events = []
    for n, t, d, v in notes_cmp:
        events += [(t, "on", n, np.float32(v / 127)), (t + d, "off", n, 0.0)]
    expect = ref.perform(bank, events, ref.Settings(), SR, 8.0, 20261012)
    lat = log["latency_samples"]
    y_ref = np.zeros_like(y_plug)
    y_ref[lat:] = ref.render(bank, expect, SR, len(y_plug) - lat)
    same = [(a["syllable"], a["sample"]) for a in log["starts"]] == [(e["syllable"], e["sample"]) for e in expect]
    diff_db = 20 * np.log10(np.max(np.abs(y_plug - y_ref)) / np.max(np.abs(y_ref)))
    metrics["08_09"] = {"same_syllables_and_starts": same, "syllables": len(expect),
                        "max_difference_db_re_peak": round(float(diff_db), 1)}
    g = 10 ** (-3 / 20) / np.max(np.abs(y_ref))
    items.append({"file": write("08_plugin.wav", y_plug * g, None), "title": "Plugin (seed 20261012), dry",
                  "listen_for": "Should be indistinguishable from 09: same syllables, same times, same voice.",
                  "params": dry, "metrics": metrics["08_09"], "group": "proto"})
    items.append({"file": write("09_prototype_algorithm.wav", y_ref * g, None),
                  "title": "Prototype algorithm + voice (Python port of PROTO1), same seed",
                  "listen_for": "Reference for 08.", "params": {}, "metrics": {}, "group": "proto"})

    # 10 / 11 — the real bird vs the plugin playing the same phrase (phrase 4, as MUSHRA passage P1)
    x, _sr = sf.read(str(REC), always_2d=True)
    x = x.mean(axis=1)
    ann = json.loads(REC.with_suffix(".json").read_text())
    for pid, (t0, t1, phrase) in PASSAGES.items():
        seg, mask = real_passage(x, ann, t0, t1)
        metrics[f"clicks_real_{pid}"] = click_metrics(seg, mask)
    t0, t1, phrase = PASSAGES["P1"]
    seg, _ = real_passage(x, ann, t0, t1)
    lead = bank_json["phrases"][phrase]["start_s"] - t0
    key = 69  # A: own height puts it at 10500 c, 2.9 c under the bird's centre → the natural melody
    y_p, _ = phrase_render("phrase4", phrase, lead, {"perf.first_on_note": 0}, key)
    y_p = y_p[: len(seg)]
    metrics["clicks_plugin_P1"] = click_metrics(mono(y_p))
    items.append({"file": write("10_real_phrase4.wav", seg[:, None] * np.ones((1, 2))),
                  "title": "Real recording 22938, 15.40–17.85 s (bank phrase 4)",
                  "listen_for": "The real bird (high-passed at 1 kHz, its own room and background noise).",
                  "params": {}, "metrics": metrics["clicks_real_P1"], "group": "real"})
    items.append({"file": write("11_plugin_phrase4.wav", y_p),
                  "title": "Plugin, same phrase (key A, first syllable on the note off, room on)",
                  "listen_for": "The same phrase from the plugin: the bird's own melody (moved −3 cents) and "
                                "timing. It should match 10, minus the background noise.",
                  "params": {"key": "A4", "perf.first_on_note": "off"}, "metrics": metrics["clicks_plugin_P1"],
                  "group": "real"})

    # criterion 4 — colour on a steady syllable: find a seed whose first tap is the steadiest syllable
    steady = max(range(len(bank.syllables)), key=lambda i: bank_json["phrases"][bank.syllables[i].phrase]
                 ["syllables"][bank.syllables[i].index]["anchors"]["steady_ms"])
    seed = next(sd for sd in range(1, 5000)
                if cc.sing(str(BANK_PATH), [(0.1, "on", 69, 0.8), (0.12, "off", 69, 0.0)], {}, SR, 0.2, sd)[1][0][
                    "syllable"] == steady)
    y, log = render("steady", [(69, 0.1, 0.03, 100)], {"out.room": 0}, seed=seed)
    assert log["starts"][0]["syllable"] == steady
    anc = bank_json["phrases"][bank.syllables[steady].phrase]["syllables"][bank.syllables[steady].index]["anchors"]
    a0 = log["starts"][0]["sample"] + log["latency_samples"]
    span = (a0 + anc["rule_span_ms"][0] * 48, a0 + anc["rule_span_ms"][1] * 48)
    metrics["colour_steady_syllable"] = {"syllable": steady, "label": bank.syllables[steady].label,
                                         "steady_ms": anc["steady_ms"], "seed": seed, **colour_metrics(y),
                                         "noise_between_harmonics_db": noise_floor_db(y, *span)}
    metrics["colour_all_frames_held_6s"] = colour_metrics(held_dry)
    pure, _ = render("steady_pure", [(69, 0.1, 0.03, 100)], {"out.room": 0, "voice.harmonics": 0}, seed=seed)
    metrics["colour_steady_syllable_harmonics_off"] = {**colour_metrics(pure),
                                                       "noise_between_harmonics_db": noise_floor_db(pure, *span)}
    # the real bird on the same measure: its steadiest syllable, straight from the recording
    rec_syl = bank_json["phrases"][bank.syllables[steady].phrase]
    t_syl = rec_syl["start_s"] + rec_syl["syllables"][bank.syllables[steady].index]["t_ms"] / 1000
    xs = signal.sosfiltfilt(signal.butter(4, 1000, "highpass", fs=SR, output="sos"),
                            x[int(t_syl * SR) - 4800: int(t_syl * SR) + 48000])
    metrics["colour_real_same_syllable"] = {
        "noise_between_harmonics_db": noise_floor_db(xs, 4800 + anc["rule_span_ms"][0] * 48,
                                                     4800 + anc["rule_span_ms"][1] * 48)}
    items.append({"file": write("12_steady_syllable.wav", y), "title": "Colour check: the steadiest syllable",
                  "listen_for": "A clean whistle with a faint brightness from the bird's harmonics; no hiss, "
                                "no breath.", "params": {"seed": seed}, "metrics": metrics["colour_steady_syllable"]})

    figures(metrics, bank, y_plug, y_ref, log, lat, seg, y_p)
    manifest = {"format": "cuicui.listening_manifest", "version": 1, "wp": "R1",
                "title": "WP-R1 — the bird's real syllables, whistle voice, playing modes A + B (plugin renders)",
                "items": items,
                "figures": [{"file": "fig_pitch_plugin_vs_prototype.png", "title": "Plugin vs prototype algorithm",
                             "caption": "Pitch curves of item 08 (plugin) and 09 (prototype port): identical."},
                            {"file": "fig_real_vs_plugin.png", "title": "Real phrase 4 vs the plugin",
                             "caption": "Spectrograms of items 10 and 11."},
                            {"file": "fig_clicks.png", "title": "Clicks at syllable edges",
                             "caption": "Low-frequency energy around onsets/offsets (ENGINE1 metric); lower is "
                                        "cleaner. Criterion 5: plugin ≤ real + 3 dB."},
                            {"file": "editor.png", "title": "Editor", "caption": "The minimal R1 controls."}]}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1))
    (OUT / "metrics.json").write_text(json.dumps(metrics, indent=1, default=float))
    print(json.dumps(metrics, indent=1, default=float))


def figures(metrics, bank, y_plug, y_ref, log, lat, seg, y_p) -> None:
    # pitch overlay plugin vs prototype
    fig, ax = plt.subplots(figsize=(10, 3.2))
    for y, lab, st in ((y_ref, "prototype algorithm", "-"), (y_plug, "plugin", "--")):
        tr = T.track(mono(y), SR)
        c = 1200 * np.log2(np.where(tr["voiced"], tr["f0"], np.nan) / 440) + 6900
        ax.plot(tr["t"], c / 100, st, lw=1.2, label=lab)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("pitch (MIDI note)")
    ax.legend(loc="upper right")
    ax.set_title("Same MIDI, same seed: plugin and prototype sing the same pitch curve")
    fig.tight_layout()
    fig.savefig(OUT / "fig_pitch_plugin_vs_prototype.png", dpi=110)
    plt.close(fig)
    # spectrograms real vs plugin
    fig, axs = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
    for ax, y, lab in ((axs[0], seg, "real 22938, phrase 4"), (axs[1], mono(y_p), "plugin, same phrase")):
        f, t, s = signal.spectrogram(y, SR, nperseg=1024, noverlap=896)
        ax.pcolormesh(t, f / 1000, 10 * np.log10(s / s.max() + 1e-10), vmin=-80, vmax=0, shading="auto")
        ax.set_ylim(0, 12)
        ax.set_ylabel("kHz")
        ax.set_title(lab)
    axs[1].set_xlabel("time (s)")
    fig.tight_layout()
    fig.savefig(OUT / "fig_real_vs_plugin.png", dpi=110)
    plt.close(fig)
    # clicks
    rows = [("real P1", metrics["clicks_real_P1"]), ("real P3", metrics["clicks_real_P3"]),
            ("plugin phrase 4", metrics["clicks_plugin_P1"]),
            ("plugin held 6 s, dry", metrics["clicks_plugin_held_6s_dry"]),
            ("plugin held 6 s, room", metrics["clicks_plugin_held_6s_room"])]
    fig, ax = plt.subplots(figsize=(8, 3.2))
    xs = np.arange(len(rows))
    ax.bar(xs - 0.2, [r[1]["onset_lf_db"] for r in rows], 0.4, label="onsets")
    ax.bar(xs + 0.2, [r[1]["offset_lf_db"] for r in rows], 0.4, label="offsets")
    ref_on = np.mean([metrics["clicks_real_P1"]["onset_lf_db"], metrics["clicks_real_P3"]["onset_lf_db"]])
    ax.axhline(ref_on + 3, color="k", lw=0.8, ls=":", label="real + 3 dB (onsets)")
    ax.set_xticks(xs, [r[0] for r in rows], fontsize=8)
    ax.set_ylabel("LF energy re syllable (dB)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(OUT / "fig_clicks.png", dpi=110)
    plt.close(fig)


if __name__ == "__main__":
    main()
