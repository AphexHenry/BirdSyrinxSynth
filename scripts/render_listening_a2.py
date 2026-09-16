"""Render WP-A2 listening examples (run from repo root via uv)."""

from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
RENDER = ROOT / "build" / "release" / "bin" / "bird_render"
OUT = ROOT / "listening" / "A2"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "A2"


def _render(spec: dict, wav: Path, extra: list[str] | None = None) -> None:
    spec_path = wav.with_suffix(".render.json")
    spec_path.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    cmd = [str(RENDER), str(spec_path), "-o", str(wav)]
    if extra:
        cmd.extend(extra)
    subprocess.run(cmd, check=True)


def _peak_normalize(wav: Path, peak_dbfs: float = -3.0) -> None:
    x, sr = sf.read(wav, dtype="float32")
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    sf.write(wav, x * (target / peak), sr, subtype="FLOAT")


def _dur(path: Path) -> float:
    info = sf.info(str(path))
    return info.frames / info.samplerate


def _peak_dbfs(path: Path) -> float:
    x, _sr = sf.read(path)
    peak = float(np.max(np.abs(x)))
    return 20.0 * math.log10(peak) if peak > 0 else -120.0


def _gain_steps() -> list[list[float]]:
    """1 kHz-worth of out.gain_db jumps every 250 ms: -18 ↔ 0 dB."""
    points: list[list[float]] = []
    levels = [-18.0, 0.0, -18.0, 0.0, -12.0, -24.0, 0.0, -18.0]
    dt = 1.0 / 48000.0
    for i, lv in enumerate(levels):
        t0 = i * 0.25
        t1 = (i + 1) * 0.25 - dt
        points.append([round(t0, 8), lv])
        points.append([round(t1, 8), lv])
    return points


def _plot_decimator() -> Path:
    import cuicui_core as cc

    host = 48000.0
    os_factor = 4
    n_stages = int(round(math.log2(os_factor)))
    fig, ax = plt.subplots(figsize=(8.0, 3.6), dpi=120)
    freqs = np.linspace(0.0, 96000.0, 4000)
    for stage in range(n_stages):
        info = cc.decimator_stage_taps(host, os_factor, stage)
        h = np.asarray(info["taps"], dtype=np.float64)
        fin = float(info["input_rate"])
        w = 2.0 * np.pi * freqs / fin
        z = np.exp(-1j * w)
        # DTFT: H(e^{jw}) = sum h[n] z^n
        n = np.arange(h.size)
        h_f = np.exp(-1j * np.outer(w, n)) @ h
        mag = 20.0 * np.log10(np.maximum(np.abs(h_f), 1e-12))
        # Only plot up to Nyquist of this stage's input.
        nyq = fin * 0.5
        mask = freqs <= nyq + 1.0
        ax.plot(
            freqs[mask] / 1000.0,
            mag[mask],
            lw=1.2,
            label=f"stage {stage} ({fin / 1000:.1f}→{fin / 2000:.1f} kHz, {int(info['num_taps'])} taps)",
        )
    ax.axhline(-90.0, color="#888", ls="--", lw=0.8, label="−90 dB")
    ax.axvline(20.0, color="#e8a54b", ls=":", lw=0.8, label="20 kHz")
    ax.set_xlim(0, 96)
    ax.set_ylim(-140, 5)
    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Magnitude (dB)")
    ax.set_title("Half-band decimator, OS = 4 at 48 kHz host")
    ax.grid(True, alpha=0.25)
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = OUT / "decimator_mag_os4.png"
    fig.savefig(out)
    plt.close(fig)
    return out


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit(f"missing {RENDER}; run scripts/build.sh release")
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    sweep_pts = [[0.0, 200.0], [4.0, 8000.0]]
    saw = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": 4.0,
        "seed": 1,
        "params": {"test.waveform": "saw", "test.gain_db": -12.0},
        "automation": {"test.freq_hz": sweep_pts},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    _render({**saw, "oversampling": 1}, OUT / "01_saw_sweep_os1.wav")
    _render({**saw, "oversampling": 4}, OUT / "02_saw_sweep_os4.wav")
    _peak_normalize(OUT / "01_saw_sweep_os1.wav")
    _peak_normalize(OUT / "02_saw_sweep_os4.wav")

    steps = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": 2.0,
        "seed": 1,
        "oversampling": 1,
        "params": {"test.waveform": "sine", "test.freq_hz": 1000.0, "test.gain_db": -6.0},
        "automation": {"out.gain_db": _gain_steps()},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    _render(steps, OUT / "03_gain_steps_unsmoothed.wav", extra=["--smoothing", "off"])
    _render(steps, OUT / "04_gain_steps_smoothed.wav", extra=["--smoothing", "on"])

    plot = _plot_decimator()
    shutil.copy2(plot, ASSETS / plot.name)

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "A2",
        "title": "Parameter smoothing, naive saw aliasing, half-band decimation",
        "figures": [
            {
                "file": "decimator_mag_os4.png",
                "title": "Decimator magnitude, OS = 4 at 48 kHz",
                "caption": "Each half-band stage. Passband should be flat to 20 kHz; stopband ≤ −90 dB.",
            }
        ],
        "items": [
            {
                "file": "01_saw_sweep_os1.wav",
                "title": "Naive saw sweep, OS = 1",
                "listen_for": (
                    "Inharmonic whistling aliases that fall while the pitch rises "
                    "(especially above ~2 kHz)."
                ),
                "params": {"test.waveform": "saw", "oversampling": 1, "f0": "200→8000 Hz"},
                "metrics": {
                    "duration_s": round(_dur(OUT / "01_saw_sweep_os1.wav"), 4),
                    "peak_dbfs": round(_peak_dbfs(OUT / "01_saw_sweep_os1.wav"), 2),
                },
                "group": "saw_sweep",
                "reference": None,
            },
            {
                "file": "02_saw_sweep_os4.wav",
                "title": "Naive saw sweep, OS = 4",
                "listen_for": "Same sweep, aliases strongly reduced; brighter but clean.",
                "params": {"test.waveform": "saw", "oversampling": 4, "f0": "200→8000 Hz"},
                "metrics": {
                    "duration_s": round(_dur(OUT / "02_saw_sweep_os4.wav"), 4),
                    "peak_dbfs": round(_peak_dbfs(OUT / "02_saw_sweep_os4.wav"), 2),
                },
                "group": "saw_sweep",
                "reference": None,
            },
            {
                "file": "03_gain_steps_unsmoothed.wav",
                "title": "Gain steps, smoothing off",
                "listen_for": "Zipper clicks at each jump (every 250 ms).",
                "params": {"out.gain_db": "steps", "smoothing_ms": 0},
                "metrics": {
                    "duration_s": round(_dur(OUT / "03_gain_steps_unsmoothed.wav"), 4),
                    "peak_dbfs": round(_peak_dbfs(OUT / "03_gain_steps_unsmoothed.wav"), 2),
                    "normalised": False,
                },
                "group": "gain_steps",
                "reference": None,
            },
            {
                "file": "04_gain_steps_smoothed.wav",
                "title": "Gain steps, registry smoothing (20 ms)",
                "listen_for": "Same jumps, no clicks — the 20 ms smoother rounds each step.",
                "params": {"out.gain_db": "steps", "smoothing_ms": 20},
                "metrics": {
                    "duration_s": round(_dur(OUT / "04_gain_steps_smoothed.wav"), 4),
                    "peak_dbfs": round(_peak_dbfs(OUT / "04_gain_steps_smoothed.wav"), 2),
                    "normalised": False,
                },
                "group": "gain_steps",
                "reference": None,
            },
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote listening files in {OUT}")


if __name__ == "__main__":
    main()
