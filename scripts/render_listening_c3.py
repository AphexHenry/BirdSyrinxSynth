"""Render WP-C3 listening examples through plugin_render (M3)."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR",
    str(Path(__file__).resolve().parents[1] / "listening" / "C3" / ".mplconfig"),
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build" / "release" / "bin" / "plugin_render"
OUT = ROOT / "listening" / "C3"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "C3"
MIDI = ROOT / "examples" / "midi"
SR = 48000.0


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def _peak_norm(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y.astype(np.float32)
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _midi_hz(note: int) -> float:
    return 440.0 * (2.0 ** ((note - 69) / 12.0))


def _f0_near(x: np.ndarray, sr: float, f_ref: float, skip_s: float = 0.04) -> float:
    y = np.asarray(x, dtype=np.float64)
    i0 = int(round(skip_s * sr))
    sl = y[i0:] - np.mean(y[i0:]) if y.size > i0 + 32 else y
    if sl.size < 64 or not (f_ref > 1.0):
        return 0.0
    nfft = 1 << int(np.ceil(np.log2(max(int(sl.size), 4096))))
    nfft = min(nfft, 16384)
    w = np.hanning(sl.size)
    spec = np.abs(np.fft.rfft(sl * w, n=nfft))
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sr)
    mask = (freqs >= f_ref * 0.82) & (freqs <= f_ref * 1.22)
    if not np.any(mask):
        return 0.0
    idx = np.nonzero(mask)[0]
    k = int(idx[int(np.argmax(spec[mask]))])
    if 0 < k < spec.size - 1:
        a, b, c = float(spec[k - 1]), float(spec[k]), float(spec[k + 1])
        denom = a - 2.0 * b + c
        delta = 0.5 * (a - c) / denom if abs(denom) > 1e-18 else 0.0
        delta = float(np.clip(delta, -0.5, 0.5))
        return (k + delta) * sr / nfft
    return float(freqs[k])


def _params(path: Path, values: dict[str, float]) -> Path:
    path.write_text(json.dumps(values), encoding="utf-8")
    return path


def _render(wav: Path, midi: Path | None = None, notes: str | None = None, params: Path | None = None,
            automation: Path | None = None, tail: float = 0.5, duration: float = 0.0) -> None:
    cmd = [str(RENDER), "--sr", "48000", "--block", "256", "--tail", str(tail), "--seed", "1", "-o", str(wav)]
    if midi is not None:
        cmd += ["--midi", str(midi)]
    if notes is not None:
        cmd += ["--notes", notes]
    if params is not None:
        cmd += ["--params", str(params)]
    if automation is not None:
        cmd += ["--automation", str(automation)]
    if duration > 0:
        cmd += ["--duration", str(duration)]
    _run(cmd)
    y, sr = sf.read(str(wav), always_2d=True)
    y = _peak_norm(y)
    sf.write(str(wav), y, sr, subtype="FLOAT")


def _f0_track(y: np.ndarray, sr: float, notes: list[tuple[int, float, float]]) -> tuple[np.ndarray, np.ndarray, list[float]]:
    """notes: (midi, start_s, dur_s). Returns t, f0, per-note median cents error."""
    hop = int(0.010 * sr)
    win = int(0.040 * sr)
    mono = y[:, 0] if y.ndim == 2 else y
    ts = []
    fs = []
    cents = []
    for note, start, dur in notes:
        f_ref = _midi_hz(note)
        i0 = int(round((start + 0.08) * sr))
        i1 = int(round((start + dur - 0.02) * sr))
        if i1 <= i0 + win:
            continue
        errs = []
        for i in range(i0, i1 - win, hop):
            f = _f0_near(mono[i : i + win], sr, f_ref, 0.0)
            t = (i + win / 2) / sr
            ts.append(t)
            fs.append(f if f > 0 else np.nan)
            if f > 0:
                errs.append(1200.0 * np.log2(f / f_ref))
        cents.append(float(np.median(np.abs(errs))) if errs else 999.0)
    return np.asarray(ts), np.asarray(fs), cents


def _scale_notes() -> list[tuple[int, float, float]]:
    cmaj = [88, 89, 91, 93, 95, 96, 98, 100, 101, 103, 105, 107, 108]
    return [(n, 0.32 * i, 0.28) for i, n in enumerate(cmaj)]


def _melody_notes() -> list[tuple[int, float, float]]:
    melody = [96, 98, 100, 103, 101, 98, 96, 93, 95, 96]
    return [(n, 0.28 * i, 0.24) for i, n in enumerate(melody)]


def main() -> int:
    if not RENDER.is_file():
        print(f"missing {RENDER}; run scripts/build.sh release", file=sys.stderr)
        return 1
    _run([sys.executable, str(ROOT / "scripts" / "gen_midi_c3.py")])
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    figs = OUT / "figs"
    figs.mkdir(parents=True, exist_ok=True)

    pdir = OUT / "params"
    pdir.mkdir(parents=True, exist_ok=True)

    common = {
        "perf.mode": 0.0,
        "perf.snap": 0.0,
        "perf.glide_ms": 0.0,
        "out.master_db": 0.0,
        "engine.seed": 1.0,
    }

    def pfile(name: str, extra: dict[str, float]) -> Path:
        return _params(pdir / name, {**common, **extra})

    # 01 scale motion 0
    p01 = pfile("01.json", {"perf.motion": 0.0, "perf.tone": 0.6, "body.size": 1.0, "noise.beta_sigma": 0.0})
    wav01 = OUT / "01_scale_motion0.wav"
    _render(wav01, midi=MIDI / "scale.mid", params=p01, tail=0.6)

    # 02–04 same melody, motion 0 / 0.5 / 1
    for i, (fname, mot) in enumerate(
        (("02_melody_motion0.wav", 0.0), ("03_melody_motion05.wav", 0.5), ("04_melody_motion1.wav", 1.0)),
        start=2,
    ):
        p = pfile(f"{i:02d}.json", {"perf.motion": mot, "perf.tone": 0.6, "body.size": 1.0})
        wav = OUT / fname
        _render(wav, midi=MIDI / "melody.mid", params=p, tail=0.6)

    # 05 CC74 tone sweep
    p05 = pfile("05.json", {"perf.motion": 0.0, "perf.tone": 0.5, "body.size": 1.0})
    wav05 = OUT / "05_tone_cc74_sweep.wav"
    _render(wav05, midi=MIDI / "tone_cc74.mid", params=p05, tail=0.4)

    # 06 aftertouch + physics 0.5
    p06 = pfile(
        "06.json",
        {"perf.motion": 0.2, "perf.tone": 0.6, "perf.physics": 0.5, "perf.breath": 0.55, "body.size": 1.0},
    )
    wav06 = OUT / "06_breath_aftertouch.wav"
    _render(wav06, midi=MIDI / "aftertouch.mid", params=p06, tail=0.5)

    # 07 jumps — middle of the playable band, Motion 0, quieter tension noise.
    # D6 (the map floor) is a noisy onset regime; do not demo jumps there.
    p07 = pfile(
        "07.json",
        {
            "perf.motion": 0.0,
            "perf.glide_ms": 0.0,
            "perf.tone": 0.7,
            "perf.breath": 0.85,
            "body.size": 1.0,
            "noise.beta_sigma": 0.0,
        },
    )
    wav07 = OUT / "07_jumps_side_switch.wav"
    _render(wav07, midi=MIDI / "jumps.mid", params=p07, tail=0.5)

    # 08 snap demo: motion 1, snap 0 then 1 (concat)
    p08a = pfile("08a.json", {"perf.motion": 1.0, "perf.snap": 0.0, "body.size": 1.0})
    p08b = pfile("08b.json", {"perf.motion": 1.0, "perf.snap": 1.0, "body.size": 1.0})
    tmp_a = OUT / "_08a.wav"
    tmp_b = OUT / "_08b.wav"
    _render(tmp_a, midi=MIDI / "melody.mid", params=p08a, tail=0.3)
    _render(tmp_b, midi=MIDI / "melody.mid", params=p08b, tail=0.3)
    a, sr = sf.read(str(tmp_a), always_2d=True)
    b, _ = sf.read(str(tmp_b), always_2d=True)
    gap = np.zeros((int(0.25 * sr), a.shape[1]), dtype=np.float32)
    wav08 = OUT / "08_snap_demo.wav"
    y08 = _peak_norm(np.concatenate([a.astype(np.float32), gap, b.astype(np.float32)], axis=0))
    sf.write(str(wav08), y08, sr, subtype="FLOAT")
    tmp_a.unlink(missing_ok=True)
    tmp_b.unlink(missing_ok=True)

    # 09 sizes: σ = 0.5, 1, 2
    segs = []
    for sigma in (0.5, 1.0, 2.0):
        p = pfile(
            f"09_{sigma}.json",
            {"perf.motion": 0.25, "body.size": sigma, "perf.tone": 0.6},
        )
        tmp = OUT / f"_09_{sigma}.wav"
        _render(tmp, midi=MIDI / "melody.mid", params=p, tail=0.35)
        seg, _ = sf.read(str(tmp), always_2d=True)
        segs.append(seg.astype(np.float32))
        tmp.unlink(missing_ok=True)
        segs.append(np.zeros((int(0.2 * SR), segs[0].shape[1]), dtype=np.float32))
    wav09 = OUT / "09_sizes.wav"
    y09 = _peak_norm(np.concatenate(segs, axis=0))
    sf.write(str(wav09), y09, int(SR), subtype="FLOAT")

    # 10 legato glide 60 ms
    p10 = pfile("10.json", {"perf.motion": 0.2, "perf.glide_ms": 60.0, "body.size": 1.0})
    wav10 = OUT / "10_legato_glide.wav"
    _render(wav10, midi=MIDI / "legato.mid", params=p10, tail=0.5)

    # Chromatic tuning table (analysis only, not a listening item required by the WP list)
    p_ch = pfile(
        "chromatic.json",
        {"perf.motion": 0.0, "perf.tone": 0.6, "body.size": 1.0, "noise.beta_sigma": 0.0},
    )
    wav_ch = OUT / "_chromatic.wav"
    _render(wav_ch, midi=MIDI / "chromatic.mid", params=p_ch, tail=0.3)
    ych, _ = sf.read(str(wav_ch), always_2d=True)
    chrom_notes = [(n, 0.28 * (n - 86), 0.24) for n in range(86, 109)]
    _, _, chrom_cents = _f0_track(ych, SR, chrom_notes)
    wav_ch.unlink(missing_ok=True)

    y01, _ = sf.read(str(wav01), always_2d=True)
    t01, f01, c01 = _f0_track(y01, SR, _scale_notes())
    y04, _ = sf.read(str(OUT / "04_melody_motion1.wav"), always_2d=True)
    t04, f04, c04 = _f0_track(y04, SR, _melody_notes())

    def _plot_track(t, f, notes, title, dest: Path) -> None:
        fig, ax = plt.subplots(figsize=(8.5, 3.4))
        tgt_t = []
        tgt_f = []
        for note, start, dur in notes:
            tgt_t += [start, start + dur, np.nan]
            tgt_f += [_midi_hz(note), _midi_hz(note), np.nan]
        ax.plot(tgt_t, tgt_f, color="#888", lw=1.4, label="target")
        ax.plot(t, f, color="#c43", lw=1.2, label="measured f0")
        ax.set_yscale("log")
        ax.set_xlabel("time (s)")
        ax.set_ylabel("Hz")
        ax.set_title(title)
        ax.legend(loc="upper right")
        fig.tight_layout()
        fig.savefig(dest, dpi=120)
        plt.close(fig)

    fig01 = figs / "f0_scale.png"
    fig04 = figs / "f0_melody_motion1.png"
    _plot_track(t01, f01, _scale_notes(), "01 C-major scale — f0 vs target", fig01)
    _plot_track(t04, f04, _melody_notes(), "04 melody Motion=1 — f0 vs target", fig04)

    fig_tbl = figs / "tuning_error.png"
    notes = list(range(86, 109))
    fig, ax = plt.subplots(figsize=(8.5, 3.2))
    ax.bar([str(n) for n in notes], chrom_cents, color="#3a7")
    ax.axhline(5.0, color="#c43", ls="--", lw=1, label="±5 ¢")
    ax.set_xlabel("MIDI note (D6–C8)")
    ax.set_ylabel("median |cents|")
    ax.set_title("Chromatic tuning error, tone 0.6, size 1, Motion 0")
    ax.legend()
    fig.tight_layout()
    fig.savefig(fig_tbl, dpi=120)
    plt.close(fig)

    for src in (fig01, fig04, fig_tbl):
        dest = ASSETS / src.name
        dest.write_bytes(src.read_bytes())

    def metrics_wav(path: Path) -> dict:
        y, sr = sf.read(str(path), always_2d=True)
        return {"peak": round(float(np.max(np.abs(y))), 4), "duration_s": round(len(y) / sr, 3)}

    items = [
        {
            "file": "01_scale_motion0.wav",
            "title": "C major scale (Motion 0)",
            "listen_for": "Clean steps, in tune, bird timbre, physical onsets. Playable D6–C8 at size 1.",
            "params": {"perf.motion": 0.0, "perf.tone": 0.6},
            "metrics": {**metrics_wav(wav01), "median_abs_cents": round(float(np.median(c01)), 2)},
        },
        {
            "file": "02_melody_motion0.wav",
            "title": "Melody, Motion 0",
            "listen_for": "Theremin-like: pitch holds still between notes, no scoop or trill.",
            "params": {"perf.motion": 0.0},
            "metrics": metrics_wav(OUT / "02_melody_motion0.wav"),
            "group": "motion",
        },
        {
            "file": "03_melody_motion05.wav",
            "title": "Melody, Motion 0.5",
            "listen_for": "Gentle scoop into notes and a small vibrato after ~80 ms.",
            "params": {"perf.motion": 0.5},
            "metrics": metrics_wav(OUT / "03_melody_motion05.wav"),
            "group": "motion",
        },
        {
            "file": "04_melody_motion1.wav",
            "title": "Melody, Motion 1",
            "listen_for": "Deep scoop and a square-ish trill. Notes should still land near the melody pitches.",
            "params": {"perf.motion": 1.0},
            "metrics": {**metrics_wav(OUT / "04_melody_motion1.wav"), "median_abs_cents": round(float(np.median(c04)), 2)},
            "group": "motion",
        },
        {
            "file": "05_tone_cc74_sweep.wav",
            "title": "CC74 tone sweep",
            "listen_for": "Held C7: timbre morphs rich → whistle at a fixed pitch.",
            "params": {"cc": 74},
            "metrics": metrics_wav(wav05),
        },
        {
            "file": "06_breath_aftertouch.wav",
            "title": "Aftertouch breath, Physics 0.5",
            "listen_for": "Loudness and colour swell with aftertouch; pitch should stay put.",
            "params": {"perf.physics": 0.5},
            "metrics": metrics_wav(wav06),
        },
        {
            "file": "07_jumps_side_switch.wav",
            "title": "Octave and tenth jumps",
            "listen_for": "A6 → A7 → A6 → C8. Each landing should be a clear bird tone with a short tick at the splice, not a noisy burst. No glide through the middle.",
            "params": {"perf.glide_ms": 0.0, "perf.motion": 0.0, "midi": [93, 105, 93, 108, 93]},
            "metrics": metrics_wav(wav07),
        },
        {
            "file": "08_snap_demo.wav",
            "title": "Snap 0 then Snap 1 (Motion 1)",
            "listen_for": "First half: ornaments wander. Second half: trills still move, but notes land in tune.",
            "params": {"perf.motion": 1.0, "perf.snap": [0, 1]},
            "metrics": metrics_wav(wav08),
        },
        {
            "file": "09_sizes.wav",
            "title": "Size 0.5, 1, 2",
            "listen_for": "Tiny bird → zebra finch → bigger bird. Same melody, still in tune in each register.",
            "params": {"body.size": [0.5, 1.0, 2.0]},
            "metrics": metrics_wav(wav09),
        },
        {
            "file": "10_legato_glide.wav",
            "title": "Legato glide 60 ms",
            "listen_for": "Smooth portamento between overlapping notes; no scoop on the joined notes.",
            "params": {"perf.glide_ms": 60.0},
            "metrics": metrics_wav(wav10),
        },
    ]
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "C3",
        "title": "M3 — playable instrument (pitch-led MIDI)",
        "figures": [
            {"file": "figs/f0_scale.png", "title": "Scale f0 vs target", "caption": "01, Motion 0."},
            {"file": "figs/f0_melody_motion1.png", "title": "Melody Motion 1 f0", "caption": "Ornaments then land."},
            {
                "file": "figs/tuning_error.png",
                "title": "Chromatic median |cents|",
                "caption": f"median {float(np.median(chrom_cents)):.2f} ¢, max {float(np.max(chrom_cents)):.2f} ¢",
            },
        ],
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        f"wrote {OUT / 'manifest.json'}; chromatic median {float(np.median(chrom_cents)):.2f} ¢ "
        f"max {float(np.max(chrom_cents)):.2f} ¢"
    )
    (OUT / "chromatic_cents.json").write_text(
        json.dumps({"notes": list(range(86, 109)), "median_abs_cents": chrom_cents}, indent=2),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
