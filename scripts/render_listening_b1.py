"""Render WP-B1 listening examples (raw labial source, no tract)."""

from __future__ import annotations

import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
RENDER = ROOT / "build" / "release" / "bin" / "bird_render"
OUT = ROOT / "listening" / "B1"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B1"
FIGS = OUT / "figs"

ALPHA_REST = -0.10
ALPHA_MAX = 0.45


def pressure_for_alpha(alpha: float) -> float:
    return float((alpha - ALPHA_REST) / (ALPHA_MAX - ALPHA_REST))


def _render(spec: dict, wav: Path) -> None:
    spec_path = wav.with_suffix(".render.json")
    spec_path.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    subprocess.run([str(RENDER), str(spec_path), "-o", str(wav)], check=True)


def _peak_normalize(wav: Path, peak_dbfs: float = -3.0) -> None:
    x, sr = sf.read(wav, dtype="float32")
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    sf.write(wav, (x * (target / peak)).astype(np.float32), sr, subtype="FLOAT")


def _dur(path: Path) -> float:
    info = sf.info(str(path))
    return info.frames / info.samplerate


def _peak_dbfs(path: Path) -> float:
    x, _sr = sf.read(path)
    peak = float(np.max(np.abs(x)))
    return 20.0 * math.log10(peak) if peak > 0 else -120.0


def _base(duration: float, beta: float, pressure: list[list[float]], gamma: float = 24000.0) -> dict:
    return {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": "auto",
        "params": {"src.gamma": gamma, "src.L.beta": beta, "out.gain_db": 0.0},
        "automation": {"resp.pressure": pressure},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }


def _sine_beta(duration: float, sr: int = 48000) -> list[list[float]]:
    n = int(round(duration * sr))
    pts: list[list[float]] = []
    step = max(1, sr // 200)  # 5 ms
    for i in range(0, n, step):
        t = i / sr
        pts.append([round(t, 8), 0.3 + 0.2 * math.sin(2.0 * math.pi * 20.0 * t)])
    pts.append([duration, 0.3 + 0.2 * math.sin(2.0 * math.pi * 20.0 * duration)])
    return pts


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit(f"missing {RENDER}; run scripts/build.sh release")
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    # 01 Hopf: β=0.5, p 0→0.4 over 1 s, hold 1 s, down 1 s
    _render(
        _base(3.0, 0.5, [[0.0, 0.0], [1.0, 0.4], [2.0, 0.4], [3.0, 0.0]]),
        OUT / "01_hopf_onset_beta05.wav",
    )
    _peak_normalize(OUT / "01_hopf_onset_beta05.wav")

    # 02 SNIC: β=0.05, p chosen so α≈0.3 at t=1 s
    p_snic = pressure_for_alpha(0.3)
    _render(
        _base(3.0, 0.05, [[0.0, 0.0], [1.0, p_snic], [2.0, p_snic], [3.0, 0.0]]),
        OUT / "02_snic_onset_beta005.wav",
    )
    _peak_normalize(OUT / "02_snic_onset_beta005.wav")

    # 03 β sweep at α=0.2
    p_a02 = pressure_for_alpha(0.2)
    spec03 = _base(4.0, -0.05, [[0.0, p_a02], [4.0, p_a02]])
    spec03["automation"]["src.L.beta"] = [[0.0, -0.05], [4.0, 0.8]]
    _render(spec03, OUT / "03_beta_sweep_alpha02.wav")
    _peak_normalize(OUT / "03_beta_sweep_alpha02.wav")

    # 04 α sweep at β=0.2
    _render(
        _base(
            4.0,
            0.2,
            [[0.0, pressure_for_alpha(0.03)], [4.0, pressure_for_alpha(0.45)]],
        ),
        OUT / "04_alpha_sweep_beta02.wav",
    )
    _peak_normalize(OUT / "04_alpha_sweep_beta02.wav")

    # 05 hysteresis, not normalised
    p_hi = pressure_for_alpha(0.40)
    _render(
        _base(8.0, 0.15, [[0.0, 0.0], [4.0, p_hi], [8.0, 0.0]]),
        OUT / "05_hysteresis_beta015.wav",
    )

    # 06 20 Hz β trill
    spec06 = _base(2.0, 0.3, [[0.0, pressure_for_alpha(0.25)], [2.0, pressure_for_alpha(0.25)]])
    spec06["automation"]["src.L.beta"] = _sine_beta(2.0)
    _render(spec06, OUT / "06_fast_beta_trill.wav")
    _peak_normalize(OUT / "06_fast_beta_trill.wav")

    # 07 same as 03 with γ=12000 (below registry min — BirdVoice settings, not render-spec clamp)
    import cuicui_core as cc

    sr = 48000
    n = int(4.0 * sr)
    t = np.arange(n, dtype=np.float32) / np.float32(sr)
    pressure = np.full(n, p_a02, dtype=np.float32)
    beta = np.float32(-0.05) + np.float32(0.85) * (t / np.float32(4.0))
    y = np.asarray(
        cc.render_labial(pressure, beta, 12000.0, float(sr), 4, 0.0, 0.0), dtype=np.float32
    )
    peak = float(np.max(np.abs(y)))
    if peak > 0:
        y = y * (10.0 ** (-3.0 / 20.0) / peak)
    sf.write(OUT / "07_gamma_half.wav", y.astype(np.float32), sr, subtype="FLOAT")

    fig_files = [
        "regime_f0.png",
        "regime_p2p.png",
        "regime_sci.png",
        "hysteresis.png",
        "f0_vs_beta.png",
    ]
    copied: list[str] = []
    for name in fig_files:
        src = FIGS / name
        if src.is_file():
            shutil.copy2(src, ASSETS / name)
            copied.append(name)

    def metrics(name: str, extra: dict | None = None) -> dict:
        p = OUT / name
        m = {"duration_s": round(_dur(p), 4), "peak_dbfs": round(_peak_dbfs(p), 2)}
        if extra:
            m.update(extra)
        return m

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B1",
        "title": "Labial oscillator — raw syrinx source (no trachea)",
        "figures": [
            {
                "file": "figs/regime_f0.png",
                "title": "Regime map: f₀",
                "caption": "Hopf line (white, α=0), saddle-node (cyan), onset-from-rest (magenta).",
            },
            {
                "file": "figs/regime_sci.png",
                "title": "Regime map: SCI (centroid / f₀)",
                "caption": "High SCI = buzzy/pulsed (SNIC). Low SCI ≈ 1 = whistle (Hopf).",
            },
            {
                "file": "figs/hysteresis.png",
                "title": "Onset vs offset vs β",
                "caption": "Sound starts and stops at different α. Ramp 0.2 s⁻¹.",
            },
            {
                "file": "figs/f0_vs_beta.png",
                "title": "f₀ vs β",
                "caption": "Pitch rises with tension. Dashed: Hopf approximation γ√β / 2π.",
            },
        ],
        "items": [
            {
                "file": "01_hopf_onset_beta05.wav",
                "title": "Hopf onset, β = 0.5",
                "listen_for": "Quiet until ~0.5 s (pressure still below onset — not a click), then a tone appears softly and grows; almost a pure whistle.",
                "params": {"src.L.beta": 0.5, "resp.pressure": "0→0.4→0"},
                "metrics": metrics("01_hopf_onset_beta05.wav"),
                "group": "onsets",
                "reference": None,
            },
            {
                "file": "02_snic_onset_beta005.wav",
                "title": "SNIC onset, β = 0.05",
                "listen_for": (
                    "Tone appears abruptly, low and buzzy at onset, then rises in pitch "
                    "and gets purer as pressure grows."
                ),
                "params": {"src.L.beta": 0.05, "alpha_peak": 0.3},
                "metrics": metrics("02_snic_onset_beta005.wav"),
                "group": "onsets",
                "reference": None,
            },
            {
                "file": "03_beta_sweep_alpha02.wav",
                "title": "β sweep at α = 0.2",
                "listen_for": "Pitch glide ~1.3 → 3.5 kHz; timbre from reedy to whistle.",
                "params": {"alpha": 0.2, "src.L.beta": "-0.05→0.8"},
                "metrics": metrics("03_beta_sweep_alpha02.wav"),
                "group": "sweeps",
                "reference": None,
            },
            {
                "file": "04_alpha_sweep_beta02.wav",
                "title": "α sweep at β = 0.2",
                "listen_for": "Pitch also rises with pressure (coupling you will later compensate).",
                "params": {"src.L.beta": 0.2, "alpha": "0.03→0.45"},
                "metrics": metrics("04_alpha_sweep_beta02.wav"),
                "group": "sweeps",
                "reference": None,
            },
            {
                "file": "05_hysteresis_beta015.wav",
                "title": "Hysteresis, β = 0.15",
                "listen_for": "Sound starts and stops at different pressure (not normalised).",
                "params": {"src.L.beta": 0.15, "alpha": "up/down 4 s"},
                "metrics": metrics("05_hysteresis_beta015.wav", {"normalised": False}),
                "group": None,
                "reference": None,
            },
            {
                "file": "06_fast_beta_trill.wav",
                "title": "20 Hz β trill",
                "listen_for": "Clean 20 Hz trill: the oscillator follows fast tension changes.",
                "params": {"alpha": 0.25, "beta": "0.3+0.2 sin(2π·20 t)"},
                "metrics": metrics("06_fast_beta_trill.wav"),
                "group": None,
                "reference": None,
            },
            {
                "file": "07_gamma_half.wav",
                "title": "β sweep with γ = 12 000",
                "listen_for": "Same shape as 03, one octave lower.",
                "params": {"src.gamma": 12000, "alpha": 0.2, "src.L.beta": "-0.05→0.8"},
                "metrics": metrics("07_gamma_half.wav"),
                "group": None,
                "reference": None,
            },
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote listening files in {OUT} (copied figures: {copied})")


if __name__ == "__main__":
    main()
