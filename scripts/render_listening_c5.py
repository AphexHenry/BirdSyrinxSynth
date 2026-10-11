"""Render WP-C5 listening examples through plugin_render (generative engine)."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR",
    str(Path(__file__).resolve().parents[1] / "listening" / "C5" / ".mplconfig"),
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build" / "release" / "bin" / "plugin_render"
OUT = ROOT / "listening" / "C5"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "C5"
MIDI = ROOT / "examples" / "midi"
SR = 48000.0
HOLD = 96  # C7, middle of the D6–C8 playable band


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def _peak_norm(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y.astype(np.float32)
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _params(path: Path, values: dict[str, float]) -> Path:
    path.write_text(json.dumps(values), encoding="utf-8")
    return path


def _automation(path: Path, lanes: dict[str, list[list[float]]]) -> Path:
    path.write_text(json.dumps(lanes), encoding="utf-8")
    return path


def _render(
    wav: Path,
    *,
    midi: Path | None = None,
    notes: str | None = None,
    params: Path | None = None,
    automation: Path | None = None,
    tail: float = 0.4,
    duration: float = 0.0,
    bpm: float | None = None,
    playing: bool | None = None,
    seed: int = 7,
) -> None:
    cmd = [
        str(RENDER),
        "--sr",
        "48000",
        "--block",
        "256",
        "--tail",
        str(tail),
        "--seed",
        str(seed),
        "-o",
        str(wav),
    ]
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
    if bpm is not None:
        cmd += ["--bpm", str(bpm)]
    if playing is not None:
        cmd += ["--playing", "1" if playing else "0"]
    _run(cmd)


def _read(wav: Path) -> tuple[np.ndarray, int]:
    y, sr = sf.read(str(wav), always_2d=True)
    return y.astype(np.float32), int(sr)


def _write(wav: Path, y: np.ndarray, sr: int) -> None:
    sf.write(str(wav), y.astype(np.float32), sr, subtype="FLOAT")


def _concat(segs: list[np.ndarray], gap_s: float = 0.25) -> np.ndarray:
    ch = segs[0].shape[1]
    gap = np.zeros((int(gap_s * SR), ch), dtype=np.float32)
    out: list[np.ndarray] = []
    for i, s in enumerate(segs):
        out.append(s)
        if i + 1 < len(segs):
            out.append(gap)
    return np.concatenate(out, axis=0)


def _env(y: np.ndarray, sr: float, hop_s: float = 0.02) -> tuple[np.ndarray, np.ndarray]:
    mono = y[:, 0] if y.ndim == 2 else y
    hop = max(1, int(hop_s * sr))
    win = max(hop * 2, int(0.04 * sr))
    ts = []
    es = []
    for i in range(0, max(1, mono.size - win), hop):
        sl = mono[i : i + win]
        ts.append((i + win / 2) / sr)
        es.append(20.0 * np.log10(float(np.sqrt(np.mean(sl * sl)) + 1e-12)))
    return np.asarray(ts), np.asarray(es)


def main() -> int:
    if not RENDER.is_file():
        print(f"missing {RENDER}; run scripts/build.sh release", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    figs = OUT / "figs"
    figs.mkdir(parents=True, exist_ok=True)
    pdir = OUT / "params"
    pdir.mkdir(parents=True, exist_ok=True)

    common = {
        "perf.mode": 0.0,
        "perf.motion": 0.40,
        "perf.tone": 0.62,
        "perf.range": 0.55,
        "perf.speed": 1.25,
        "perf.snap": 0.25,
        "perf.breath": 0.75,
        "perf.variation": 0.85,
        "perf.density": 0.72,
        "perf.leash": 1.0,
        "perf.tempo_sync": 0.0,
        "perf.sync_division": 2.0,
        "perf.polyphony": 1.0,
        "perf.seed": 7.0,
        "perf.freeze": 0.0,
        "body.size": 1.0,
        "out.master_db": 0.0,
        "out.stereo_spread": 0.3,
        "engine.seed": 1.0,
        "noise.aspiration": 0.0,
        "noise.beta_sigma": 0.0,
        "resp.attack_ms": 3.0,
        "resp.release_ms": 8.0,
    }

    def pfile(name: str, extra: dict[str, float]) -> Path:
        return _params(pdir / name, {**common, **extra})

    def hold(dur: float) -> str:
        return f"{HOLD}:0.0:{dur}:100"

    # 01 Leash wakes in the first ~3 s, then sings
    p01 = pfile("01.json", {"perf.leash": 0.0, "perf.motion": 0.4})
    a01 = _automation(
        pdir / "01_auto.json",
        {"perf.leash": [[0.0, 0.0], [1.2, 0.55], [3.0, 1.0], [16.0, 1.0]]},
    )
    wav01 = OUT / "01_leash_0_to_1.wav"
    _render(wav01, notes=hold(16.0), params=p01, automation=a01, tail=0.5)
    y01, sr = _read(wav01)
    y01 = _peak_norm(y01)
    _write(wav01, y01, sr)

    # 02 Motion low then high
    segs = []
    for tag, mot in (("low", 0.1), ("high", 0.9)):
        p = pfile(f"02_{tag}.json", {"perf.leash": 1.0, "perf.motion": mot, "perf.variation": 0.8})
        tmp = OUT / f"_02_{tag}.wav"
        _render(tmp, notes=hold(10.0), params=p, tail=0.3)
        y, _ = _read(tmp)
        segs.append(y)
        tmp.unlink(missing_ok=True)
    wav02 = OUT / "02_motion_low_high.wav"
    _write(wav02, _peak_norm(_concat(segs)), sr)

    # 03 Variation 0 / 1 / 2
    segs = []
    for tag, var in (("0", 0.0), ("1", 1.0), ("2", 2.0)):
        p = pfile(f"03_{tag}.json", {"perf.leash": 1.0, "perf.variation": var, "perf.motion": 0.4})
        tmp = OUT / f"_03_{tag}.wav"
        _render(tmp, notes=hold(10.0), params=p, tail=0.3)
        y, _ = _read(tmp)
        segs.append(y)
        tmp.unlink(missing_ok=True)
    wav03 = OUT / "03_variation_0_1_2.wav"
    _write(wav03, _peak_norm(_concat(segs)), sr)

    # 04 Density
    segs = []
    for tag, dens in (("02", 0.2), ("05", 0.5), ("09", 0.9)):
        p = pfile(f"04_{tag}.json", {"perf.leash": 1.0, "perf.density": dens, "perf.variation": 0.8})
        tmp = OUT / f"_04_{tag}.wav"
        _render(tmp, notes=hold(8.0), params=p, tail=0.3)
        y, _ = _read(tmp)
        segs.append(y)
        tmp.unlink(missing_ok=True)
    wav04 = OUT / "04_density.wav"
    _write(wav04, _peak_norm(_concat(segs)), sr)

    # 05 Speed
    segs = []
    for tag, spd in (("05", 0.5), ("1", 1.0), ("2", 2.0)):
        p = pfile(f"05_{tag}.json", {"perf.leash": 1.0, "perf.speed": spd, "perf.density": 0.6})
        tmp = OUT / f"_05_{tag}.wav"
        _render(tmp, notes=hold(8.0), params=p, tail=0.3)
        y, _ = _read(tmp)
        segs.append(y)
        tmp.unlink(missing_ok=True)
    wav05 = OUT / "05_speed.wav"
    _write(wav05, _peak_norm(_concat(segs)), sr)

    # 06 Tempo sync 100 BPM 1/16 + click at −18 dBFS
    p06 = pfile(
        "06.json",
        {
            "perf.leash": 1.0,
            "perf.tempo_sync": 1.0,
            "perf.sync_division": 2.0,
            "perf.density": 0.7,
            "perf.variation": 0.6,
            "perf.motion": 0.45,
        },
    )
    wav06 = OUT / "06_tempo_sync_100bpm.wav"
    _render(wav06, notes=hold(12.0), params=p06, tail=0.4, bpm=100.0, playing=True)
    y06, _ = _read(wav06)
    y06 = _peak_norm(y06)
    n = y06.shape[0]
    click = np.zeros_like(y06)
    step = int(round((60.0 / 100.0) / 4.0 * SR))  # 1/16 at 100 BPM
    click_amp = 10.0 ** (-18.0 / 20.0)
    env = np.exp(-np.arange(int(0.008 * SR)) / (0.0015 * SR))
    for i in range(0, n, step):
        sl = min(env.size, n - i)
        click[i : i + sl, :] += (click_amp * env[:sl])[:, None]
    _write(wav06, np.clip(y06 + click, -1.0, 1.0), sr)

    # 07 Flock chord
    p07 = pfile(
        "07.json",
        {
            "perf.leash": 1.0,
            "perf.polyphony": 6.0,
            "out.stereo_spread": 0.8,
            "perf.variation": 1.0,
            "perf.motion": 0.4,
            "perf.density": 0.55,
        },
    )
    notes07 = ",".join(f"{n}:0.0:8.0:95" for n in (86, 90, 93, 96, 100, 103))
    wav07 = OUT / "07_flock_chord.wav"
    _render(wav07, notes=notes07, params=p07, tail=0.6)
    y07, _ = _read(wav07)
    _write(wav07, _peak_norm(y07), sr)

    # 08 Freeze after 5 s
    p08 = pfile("08.json", {"perf.leash": 1.0, "perf.freeze": 0.0, "perf.variation": 1.0, "perf.density": 0.65})
    a08 = _automation(pdir / "08_auto.json", {"perf.freeze": [[0.0, 0.0], [4.99, 0.0], [5.0, 1.0], [16.0, 1.0]]})
    wav08 = OUT / "08_freeze.wav"
    _render(wav08, notes=hold(16.0), params=p08, automation=a08, tail=0.4)
    y08, _ = _read(wav08)
    _write(wav08, _peak_norm(y08), sr)

    # 09 Play a melody at Leash 0.3
    p09 = pfile("09.json", {"perf.leash": 0.3, "perf.motion": 0.35, "perf.variation": 0.9, "perf.density": 0.5})
    wav09 = OUT / "09_play_and_let_go.wav"
    _render(wav09, midi=MIDI / "melody.mid", params=p09, tail=0.6)
    y09, _ = _read(wav09)
    _write(wav09, _peak_norm(y09), sr)

    t01, e01 = _env(y01, SR)
    fig01 = figs / "leash_envelope.png"
    fig, ax = plt.subplots(figsize=(8.5, 3.2))
    ax.plot(t01, e01, color="#c43", lw=1.2)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("RMS (dBFS)")
    ax.set_title("01 Leash 0 → 1 — loudness while the bird wakes up")
    fig.tight_layout()
    fig.savefig(fig01, dpi=120)
    plt.close(fig)

    fig07 = figs / "flock_lr.png"
    fig, ax = plt.subplots(figsize=(8.5, 3.2))
    t = np.arange(y07.shape[0]) / SR
    hop = max(1, int(0.01 * SR))
    ax.plot(t[::hop], y07[::hop, 0], color="#3a7", lw=0.8, label="L", alpha=0.85)
    ax.plot(t[::hop], y07[::hop, 1], color="#c43", lw=0.8, label="R", alpha=0.85)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("amp")
    ax.set_title("07 flock — left vs right (spread 0.8)")
    ax.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(fig07, dpi=120)
    plt.close(fig)

    for src in (fig01, fig07):
        dest = ASSETS / src.name
        dest.write_bytes(src.read_bytes())

    def metrics_wav(path: Path) -> dict:
        y, sr_i = sf.read(str(path), always_2d=True)
        peak = float(np.max(np.abs(y)))
        rms = float(np.sqrt(np.mean(y * y)))
        return {
            "peak": round(peak, 4),
            "rms": round(rms, 5),
            "duration_s": round(len(y) / sr_i, 3),
            "channels": int(y.shape[1]),
        }

    items = [
        {
            "file": "01_leash_0_to_1.wav",
            "title": "Leash 0 → 1",
            "listen_for": "Wakes within ~3 s. Then short, sharp syllables around the note — not a long piano glissando.",
            "params": {"perf.leash": "0→1", "note": 96},
            "metrics": metrics_wav(wav01),
        },
        {
            "file": "02_motion_low_high.wav",
            "title": "Motion 0.1 then 0.9",
            "listen_for": "First half: sparse short calls. Second half: faster chatter (chips, trills), still bird-like.",
            "params": {"perf.leash": 1.0, "perf.motion": [0.1, 0.9]},
            "metrics": metrics_wav(wav02),
            "group": "motion",
        },
        {
            "file": "03_variation_0_1_2.wav",
            "title": "Variation 0 / 1 / 2",
            "listen_for": "Stereotyped short motif → more variety → more invention. Still phonemes, not flute lines.",
            "params": {"perf.variation": [0, 1, 2]},
            "metrics": metrics_wav(wav03),
            "group": "variation",
        },
        {
            "file": "04_density.wav",
            "title": "Density 0.2 / 0.5 / 0.9",
            "listen_for": "Sparse calls → denser chatter.",
            "params": {"perf.density": [0.2, 0.5, 0.9]},
            "metrics": metrics_wav(wav04),
            "group": "density",
        },
        {
            "file": "05_speed.wav",
            "title": "Speed 0.5 / 1 / 2",
            "listen_for": "Slow, large-bird phrasing vs rapid syllables.",
            "params": {"perf.speed": [0.5, 1.0, 2.0]},
            "metrics": metrics_wav(wav05),
            "group": "speed",
        },
        {
            "file": "06_tempo_sync_100bpm.wav",
            "title": "Tempo sync 100 BPM, 1/16 + click",
            "listen_for": "Syllables lock to the click grid. Click mixed at −18 dBFS.",
            "params": {"perf.tempo_sync": 1, "bpm": 100, "perf.sync_division": "1/16"},
            "metrics": metrics_wav(wav06),
        },
        {
            "file": "07_flock_chord.wav",
            "title": "6-note flock, spread 0.8",
            "listen_for": "A small flock, spatially spread. Each voice sings its own phrase around its note.",
            "params": {"perf.polyphony": 6, "out.stereo_spread": 0.8, "notes": [86, 90, 93, 96, 100, 103]},
            "metrics": metrics_wav(wav07),
        },
        {
            "file": "08_freeze.wav",
            "title": "Freeze on after 5 s",
            "listen_for": "After five seconds the same motif repeats, zebra-finch-like.",
            "params": {"perf.freeze": "on at 5 s"},
            "metrics": metrics_wav(wav08),
        },
        {
            "file": "09_play_and_let_go.wav",
            "title": "Melody at Leash 0.3",
            "listen_for": "The melody stays recognisable, with short bird comments around it.",
            "params": {"perf.leash": 0.3, "midi": "melody.mid"},
            "metrics": metrics_wav(wav09),
        },
    ]
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "C5",
        "title": "Generative performance engine (Leash, flock, tempo sync)",
        "figures": [
            {
                "file": "figs/leash_envelope.png",
                "title": "Leash 0→1 envelope",
                "caption": "RMS while the bird wakes up on a held C7.",
            },
            {
                "file": "figs/flock_lr.png",
                "title": "Flock L/R",
                "caption": "Six voices, stereo spread 0.8.",
            },
        ],
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"wrote {OUT / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
