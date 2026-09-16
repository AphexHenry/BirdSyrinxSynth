"""Render WP-B2 listening examples (labial source through the trachea)."""

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
OUT = ROOT / "listening" / "B2"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B2"
FIGS = OUT / "figs"

ALPHA_REST = -0.10
ALPHA_MAX = 0.45
# Listening uses a strong |r| and no loss: |r|=0.5 + ℓ=0.3 was inaudible on this source (human, 2026-09-16).
LISTEN_R = 0.9
LISTEN_LOSS = 0.0


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


def _shared_peak_normalize(wavs: list[Path], peak_dbfs: float = -3.0) -> None:
    """Same gain on every file, so A/B timbre is not flattened by per-file peak-norm."""
    arrays: list[tuple[Path, np.ndarray, int]] = []
    peak = 0.0
    for wav in wavs:
        x, sr = sf.read(wav, dtype="float32")
        peak = max(peak, float(np.max(np.abs(x))))
        arrays.append((wav, x, sr))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    g = target / peak
    for wav, x, sr in arrays:
        sf.write(wav, (x * g).astype(np.float32), sr, subtype="FLOAT")


def _normalize_from_prefix(wav: Path, prefix_s: float, peak_dbfs: float = -3.0) -> None:
    """Scale the whole file from the first region's peak (keeps a quieter second half quieter)."""
    x, sr = sf.read(wav, dtype="float32")
    n0 = min(x.shape[0], int(round(prefix_s * sr)))
    peak = float(np.max(np.abs(x[:n0]))) if n0 > 0 else float(np.max(np.abs(x)))
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


def _labial(
    duration: float,
    beta: float,
    pressure: list[list[float]],
    *,
    reflection: float = LISTEN_R,
    length_mm: float = 33.0,
    loss: float = LISTEN_LOSS,
    extra_auto: dict | None = None,
) -> dict:
    spec: dict = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": "auto",
        "params": {
            "src.gamma": 24000.0,
            "src.L.beta": beta,
            "out.gain_db": 0.0,
            "trachea.length_mm": length_mm,
            "trachea.reflection": reflection,
            "trachea.loss": loss,
            "trachea.input_gain": 1.0,
        },
        "automation": {"resp.pressure": pressure},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    if extra_auto:
        spec["automation"].update(extra_auto)
    return spec


def _noise(
    duration: float,
    *,
    reflection: float = LISTEN_R,
    loss: float = LISTEN_LOSS,
    length_mm: float = 33.0,
    extra_auto: dict | None = None,
) -> dict:
    spec: dict = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": "auto",
        "params": {
            "test.waveform": "noise",
            "test.gain_db": -12.0,
            "trachea.length_mm": length_mm,
            "trachea.reflection": reflection,
            "trachea.loss": loss,
            "trachea.input_gain": 1.0,
            "out.gain_db": 0.0,
        },
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    if extra_auto:
        spec["automation"] = extra_auto
    return spec


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit(f"missing {RENDER}; run scripts/build.sh release")
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    import cuicui_core as cc

    sr = 48000
    rich_dur = 2.0
    n_rich = int(rich_dur * sr)
    # Stronger SNIC buzz so harmonics actually hit the tube peaks.
    alpha_rich = 0.28
    p_rich = np.full(n_rich, pressure_for_alpha(alpha_rich), dtype=np.float32)
    b_rich = np.full(n_rich, 0.05, dtype=np.float32)

    def render_labial(name: str, use_trachea: bool, reflection: float) -> Path:
        y = np.asarray(
            cc.render_labial(
                p_rich,
                b_rich,
                24000.0,
                float(sr),
                4,
                0.0,
                0.0,
                use_trachea,
                33.0,
                reflection,
                LISTEN_LOSS,
                1.0,
            ),
            dtype=np.float32,
        )
        wav = OUT / name
        sf.write(wav, y, sr, subtype="FLOAT")
        return wav

    w01 = render_labial("01_src_rich_raw.wav", False, LISTEN_R)
    w02 = render_labial("02_src_rich_trachea_r+05.wav", True, LISTEN_R)
    w03 = render_labial("03_src_rich_trachea_r-05.wav", True, -LISTEN_R)
    _shared_peak_normalize([w01, w02, w03])

    # 04: β sweep, lossless so resonance bumps are not damped
    p_a02 = pressure_for_alpha(0.2)
    spec04 = _labial(4.0, -0.05, [[0.0, p_a02], [4.0, p_a02]], reflection=LISTEN_R, loss=LISTEN_LOSS)
    spec04["automation"]["src.L.beta"] = [[0.0, -0.05], [4.0, 0.8]]
    _render(spec04, OUT / "04_src_tonal_trachea_r+09.wav")
    _peak_normalize(OUT / "04_src_tonal_trachea_r+09.wav")

    # 05: noise carrier — a 1.5 kHz tone does not show moving formants
    spec05 = _noise(4.0, extra_auto={"trachea.length_mm": [[0.0, 15.0], [4.0, 80.0]]})
    _render(spec05, OUT / "05_length_sweep.wav")
    _peak_normalize(OUT / "05_length_sweep.wav")

    # 06: same noise tube as 07; first 2 s lossless, then ℓ=1. Gain from first half.
    spec06 = _noise(
        4.0,
        extra_auto={"trachea.loss": [[0.0, 0.0], [2.0, 0.0], [2.02, 1.0], [4.0, 1.0]]},
    )
    _render(spec06, OUT / "06_loss_0_vs_1.wav")
    _normalize_from_prefix(OUT / "06_loss_0_vs_1.wav", 2.0)

    # 07: white noise through r = +0.9 (the clip that already made the tube obvious)
    _render(_noise(2.0, loss=0.0), OUT / "07_noise_through_trachea.wav")
    _peak_normalize(OUT / "07_noise_through_trachea.wav")

    copied: list[str] = []
    for name in ("trachea_response.png", "trachea_loss.png"):
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
        "wp": "B2",
        "title": "Trachea waveguide — tube resonances (exaggerated |r| = 0.9, no loss)",
        "figures": [
            {
                "file": "figs/trachea_response.png",
                "title": "Measured vs analytic |H| (ℓ = 0, L = 33 mm)",
                "caption": "r > 0: quarter-wave peaks (~2.65, 7.95 kHz). r < 0: half-wave (~5.3, 10.6 kHz).",
            },
            {
                "file": "figs/trachea_loss.png",
                "title": "Loss ℓ = 0 vs 1 around 8 kHz",
                "caption": "ℓ = 1 pulls the ~8 kHz quarter-wave peak down (and darkens the sound).",
            },
        ],
        "items": [
            {
                "file": "01_src_rich_raw.wav",
                "title": "Rich source, no trachea",
                "listen_for": "Buzzy stack, no tube. Same source as 02 and 03 (shared gain).",
                "params": {"src.L.beta": 0.05, "alpha": 0.28, "trachea": "off"},
                "metrics": metrics("01_src_rich_raw.wav"),
                "group": "reflection sign",
                "reference": None,
            },
            {
                "file": "02_src_rich_trachea_r+05.wav",
                "title": "Same source, r = +0.9 (quarter-wave)",
                "listen_for": (
                    "Brighter / ‘hollow tube’: extra ring around 2.6 kHz and a thin 8 kHz sheen. "
                    "|r|=0.9 and ℓ=0 so the sign is actually hearable (0.5 was too mild)."
                ),
                "params": {"trachea.reflection": 0.9, "trachea.loss": 0.0, "trachea.length_mm": 33},
                "metrics": metrics("02_src_rich_trachea_r+05.wav"),
                "group": "reflection sign",
                "reference": "01_src_rich_raw.wav",
            },
            {
                "file": "03_src_rich_trachea_r-05.wav",
                "title": "Same source, r = −0.9 (half-wave)",
                "listen_for": (
                    "Chewier / more mid-focused: a ring around 5.3 kHz, a bit of boom. "
                    "Compare with 02 — they should no longer sound the same."
                ),
                "params": {"trachea.reflection": -0.9, "trachea.loss": 0.0, "trachea.length_mm": 33},
                "metrics": metrics("03_src_rich_trachea_r-05.wav"),
                "group": "reflection sign",
                "reference": "01_src_rich_raw.wav",
            },
            {
                "file": "04_src_tonal_trachea_r+09.wav",
                "title": "β sweep through r = +0.9, ℓ = 0",
                "listen_for": (
                    "Pitch glide. Around 1 s the note should swell as f₀ crosses ~2.6 kHz; "
                    "a second, thinner bump later near 8 kHz."
                ),
                "params": {"src.L.beta": "-0.05→0.8", "trachea.reflection": 0.9, "trachea.loss": 0.0},
                "metrics": metrics("04_src_tonal_trachea_r+09.wav"),
                "group": None,
                "reference": None,
            },
            {
                "file": "05_length_sweep.wav",
                "title": "Noise: tube length 15 → 80 mm",
                "listen_for": (
                    "Same kind of pitched tube as 07, but the whistle slides down over 4 s "
                    "(short tube → long tube). No clicks."
                ),
                "params": {
                    "test.waveform": "noise",
                    "trachea.length_mm": "15→80",
                    "trachea.reflection": 0.9,
                    "trachea.loss": 0.0,
                },
                "metrics": metrics("05_length_sweep.wav"),
                "group": None,
                "reference": None,
            },
            {
                "file": "06_loss_0_vs_1.wav",
                "title": "Noise: ℓ = 0 then ℓ = 1 (not loudness-matched)",
                "listen_for": (
                    "First 2 s: sharp pitched tube (like 07). After 2 s: the whistle goes dull "
                    "and the hiss loses top — not just quieter overall."
                ),
                "params": {"test.waveform": "noise", "trachea.loss": "0 for 2 s, then 1", "normalised": "from first 2 s"},
                "metrics": metrics("06_loss_0_vs_1.wav", {"normalised": "prefix 2 s"}),
                "group": None,
                "reference": None,
            },
            {
                "file": "07_noise_through_trachea.wav",
                "title": "White noise through r = +0.9, ℓ = 0",
                "listen_for": "Pitched ‘tube’ colour, quarter-wave ~2.65 kHz. Reference for 05 and 06.",
                "params": {"test.waveform": "noise", "trachea.reflection": 0.9, "trachea.loss": 0.0},
                "metrics": metrics("07_noise_through_trachea.wav"),
                "group": None,
                "reference": None,
            },
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote listening files in {OUT} (copied figures: {copied})")


if __name__ == "__main__":
    main()
