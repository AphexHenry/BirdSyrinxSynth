"""Render WP-B3 listening examples (first complete bird: source + trachea + UVT)."""

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
OUT = ROOT / "listening" / "B3"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B3"
FIGS = OUT / "figs"
REAL_WAV = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")

ALPHA_REST = -0.10
ALPHA_MAX = 0.45
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
    """Scale the whole file from the first region's peak (keeps a louder second half louder)."""
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
    oec: float = 3000.0,
    gape: float = 0.5,
    radiation: float = 1.0,
    extra_auto: dict | None = None,
    extra_params: dict | None = None,
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
            "trachea.length_mm": 33.0,
            "trachea.reflection": LISTEN_R,
            "trachea.loss": LISTEN_LOSS,
            "trachea.input_gain": 1.0,
            "oec.freq_hz": oec,
            "oec.damping": 0.1,
            "beak.gape": gape,
            "beak.damping": 0.3,
            "out.radiation": radiation,
        },
        "automation": {"resp.pressure": pressure},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    if extra_params:
        spec["params"].update(extra_params)
    if extra_auto:
        spec["automation"].update(extra_auto)
    return spec


def estimate_f0(x: np.ndarray, sr: int, lo: float = 350.0, hi: float = 2500.0) -> float:
    y = np.asarray(x, dtype=np.float64)
    y = y - np.mean(y)
    if y.shape[0] < 64:
        return float("nan")
    y = y * np.hanning(y.shape[0])
    nfft = 1 << int(np.ceil(np.log2(2 * y.shape[0])))
    spec = np.fft.rfft(y, n=nfft)
    ac = np.fft.irfft(np.abs(spec) ** 2)
    minlag = max(1, int(sr / hi))
    maxlag = min(ac.shape[0] - 1, int(sr / lo))
    if maxlag <= minlag:
        return float("nan")
    lag = minlag + int(np.argmax(ac[minlag:maxlag]))
    return float(sr / lag)


def extract_clip(src: Path, t0: float, t1: float, dest: Path) -> tuple[float, int]:
    x, sr = sf.read(src, dtype="float32", always_2d=False)
    if x.ndim > 1:
        x = x[:, 0]
    i0 = max(0, int(round(t0 * sr)))
    i1 = min(x.shape[0], int(round(t1 * sr)))
    y = x[i0:i1]
    sf.write(dest, y, sr, subtype="FLOAT")
    return estimate_f0(y, sr), sr


def chain_spectra_overlay(wavs: list[Path], out: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(8.5, 4.2))
    colors = ("#888888", "#4c78a8", "#f58518")
    labels = ("source", "trachea", "trachea + UVT")
    for wav, color, lab in zip(wavs, colors, labels, strict=True):
        x, sr = sf.read(wav, dtype="float32")
        n = x.shape[0]
        spec = np.abs(np.fft.rfft(x * np.hanning(n)))
        freqs = np.fft.rfftfreq(n, 1.0 / sr)
        mag = 20.0 * np.log10(np.maximum(spec, 1e-12))
        mag -= float(np.max(mag))
        ax.plot(freqs / 1000.0, mag, color=color, lw=1.1, label=lab)
    ax.set_xlim(0.0, 12.0)
    ax.set_ylim(-60.0, 3.0)
    ax.set_xlabel("kHz")
    ax.set_ylabel("dB (peak-normalised)")
    ax.set_title("Chain spectra (01–03)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit(f"missing {RENDER}; run scripts/build.sh release")
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)

    import cuicui_core as cc

    sr = 48000
    # SNIC buzz ~1.5 kHz with several harmonics (α=0.28 / 2.5 kHz sounded like a sine after UVT).
    alpha_buzz = 0.16
    beta_buzz = 0.05
    p_buzz = pressure_for_alpha(alpha_buzz)
    listen_zh = 0.35  # broader formant so 03 stays a buzz, not a flute
    f_h_chain = 4600.0  # near H3 of ~1534 Hz

    rich_dur = 2.0
    n_rich = int(rich_dur * sr)
    p_rich = np.full(n_rich, p_buzz, dtype=np.float32)
    b_rich = np.full(n_rich, beta_buzz, dtype=np.float32)

    def render_chain(name: str, use_trachea: bool, use_uvt: bool) -> Path:
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
                LISTEN_R,
                LISTEN_LOSS,
                1.0,
                use_uvt,
                f_h_chain,
                listen_zh,
                0.5,
                0.3,
                1.0,
            ),
            dtype=np.float32,
        )
        wav = OUT / name
        sf.write(wav, y, sr, subtype="FLOAT")
        return wav

    w01 = render_chain("01_chain_source.wav", False, False)
    w02 = render_chain("02_chain_trachea.wav", True, False)
    w03 = render_chain("03_chain_full.wav", True, True)
    _shared_peak_normalize([w01, w02, w03])
    chain_spectra_overlay([w01, w02, w03], FIGS / "chain_spectra.png")

    buzz_params = {"oec.damping": listen_zh}
    hold = [[0.0, p_buzz], [0.05, p_buzz]]

    spec04 = _labial(
        5.0,
        beta_buzz,
        hold + [[5.0, p_buzz]],
        extra_params=buzz_params,
        extra_auto={"oec.freq_hz": [[0.0, 800.0], [5.0, 6000.0]]},
    )
    _render(spec04, OUT / "04_oec_sweep.wav")
    _peak_normalize(OUT / "04_oec_sweep.wav")

    spec05 = _labial(
        4.0,
        beta_buzz,
        hold + [[4.0, p_buzz]],
        oec=3000.0,
        extra_params=buzz_params,
        extra_auto={"beak.gape": [[0.0, 0.0], [2.0, 0.0], [2.02, 1.0], [4.0, 1.0]]},
    )
    _render(spec05, OUT / "05_gape_sweep.wav")
    _peak_normalize(OUT / "05_gape_sweep.wav")

    spec06 = _labial(
        4.0,
        beta_buzz,
        hold + [[4.0, p_buzz]],
        extra_params=buzz_params,
        extra_auto={"out.radiation": [[0.0, 0.0], [2.0, 0.0], [2.02, 1.0], [4.0, 1.0]]},
    )
    _render(spec06, OUT / "06_flow_vs_radiation.wav")
    _normalize_from_prefix(OUT / "06_flow_vs_radiation.wav", 2.0)

    p_note = pressure_for_alpha(0.20)
    if REAL_WAV.is_file():
        extract_clip(REAL_WAV, 7.828, 7.994, OUT / "07_real_stack.wav")
        extract_clip(REAL_WAV, 23.476, 23.976, OUT / "08_real_high.wav")
        _peak_normalize(OUT / "07_real_stack.wav")
        _peak_normalize(OUT / "08_real_high.wav")

    # Fast tweet (~166 ms), falling pitch, attack/release — not a held note.
    spec07 = _labial(
        0.17,
        0.55,
        [[0.0, 0.0], [0.01, p_note], [0.12, p_note], [0.17, 0.0]],
        oec=4500.0,
        gape=0.55,
        extra_params=buzz_params,
        extra_auto={
            "src.L.beta": [[0.0, 0.55], [0.14, 0.08]],
            "oec.freq_hz": [[0.0, 5000.0], [0.14, 2800.0]],
        },
    )
    _render(spec07, OUT / "07_stack_attempt.wav")
    _peak_normalize(OUT / "07_stack_attempt.wav")

    # Alarm-like: 0.5 s, pitch UP 2.7→4 kHz, swell then fade (real clip goes up).
    spec08 = _labial(
        0.50,
        0.22,
        [[0.0, 0.0], [0.04, p_note], [0.40, p_note], [0.50, 0.0]],
        oec=2800.0,
        gape=0.75,
        extra_params=buzz_params,
        extra_auto={
            "src.L.beta": [[0.0, 0.22], [0.42, 0.75], [0.50, 0.75]],
            "oec.freq_hz": [[0.0, 2800.0], [0.42, 4000.0], [0.50, 4000.0]],
        },
    )
    _render(spec08, OUT / "08_high_note_attempt.wav")
    _peak_normalize(OUT / "08_high_note_attempt.wav")

    spec09 = _labial(
        4.0,
        -0.05,
        [[0.0, p_note], [4.0, p_note]],
        oec=3000.0,
        extra_params=buzz_params,
        extra_auto={"src.L.beta": [[0.0, -0.05], [4.0, 0.8]]},
    )
    _render(spec09, OUT / "09_tonal_glide_full.wav")
    _peak_normalize(OUT / "09_tonal_glide_full.wav")

    copied: list[str] = []
    for name in ("uvt_radiated_family.png", "uvt_peak_table.png", "chain_spectra.png"):
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

    items = [
        {
            "file": "01_chain_source.wav",
            "title": "Source only (labial buzz ~1.5 kHz)",
            "listen_for": "Buzzy stack of harmonics, no tube or mouth. Shared gain with 02/03.",
            "params": {"src.L.beta": 0.05, "alpha": 0.16, "trachea": False, "uvt": False},
            "metrics": metrics("01_chain_source.wav"),
            "group": "chain",
            "reference": None,
        },
        {
            "file": "02_chain_trachea.wav",
            "title": "Source + trachea",
            "listen_for": "Same buzz through a tube. Hollow ring, not yet a beak.",
            "params": {"trachea.reflection": LISTEN_R, "trachea.loss": LISTEN_LOSS},
            "metrics": metrics("02_chain_trachea.wav"),
            "group": "chain",
            "reference": None,
        },
        {
            "file": "03_chain_full.wav",
            "title": "Source + trachea + mouth (first bird)",
            "listen_for": "Same buzz as 01/02. A band around 4.6 kHz is now louder (vowel-like). Still a buzz — not a flute.",
            "params": {"oec.freq_hz": 4600.0, "oec.damping": 0.35, "beak.gape": 0.5},
            "metrics": metrics("03_chain_full.wav"),
            "group": "chain",
            "reference": None,
        },
        {
            "file": "04_oec_sweep.wav",
            "title": "Mouth cavity sweep 800 → 6000 Hz",
            "listen_for": "Wah-wah: different teeth of the buzz get loud one after another. Same pitch the whole time.",
            "params": {"src.L.beta": 0.05, "alpha": 0.16, "oec.freq_hz": "800→6000"},
            "metrics": metrics("04_oec_sweep.wav", {"alpha": 0.16, "beta": 0.05, "f0_hz": 1534}),
            "group": None,
            "reference": None,
        },
        {
            "file": "05_gape_sweep.wav",
            "title": "Beak closed then open",
            "listen_for": "First 2 s: beak shut (narrower, more pinched). After 2 s: beak wide (broader, less nasal). Same pitch.",
            "params": {"oec.freq_hz": 3000.0, "beak.gape": "0 then 1"},
            "metrics": metrics("05_gape_sweep.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "06_flow_vs_radiation.wav",
            "title": "Inside the beak, then in front of it",
            "listen_for": "First 2 s: duller, more bass (air flow). After 2 s: highs come in, more ‘in the room’. Same buzz.",
            "params": {"out.radiation": "0 then 1"},
            "metrics": metrics("06_flow_vs_radiation.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "07_stack_attempt.wav",
            "title": "Short falling tweet vs real peep",
            "listen_for": "Both should be a quick chirp, not a held note. We are not matching the bird yet — judge motion, not timbre.",
            "params": {"src.L.beta": "0.55→0.08", "duration_s": 0.17},
            "metrics": metrics("07_stack_attempt.wav", {"real_clip": "7.828–7.994 s peep"}),
            "group": None,
            "reference": "07_real_stack.wav" if (OUT / "07_real_stack.wav").is_file() else None,
        },
        {
            "file": "08_high_note_attempt.wav",
            "title": "Rising whistle vs real alarm",
            "listen_for": "Pitch should go UP (like B), swell, then fade. Colour will still be wrong (no breath noise yet).",
            "params": {"src.L.beta": "0.22→0.75", "alpha": 0.20},
            "metrics": metrics("08_high_note_attempt.wav", {"real_clip": "23.48–23.98 s alarm"}),
            "group": None,
            "reference": "08_real_high.wav" if (OUT / "08_real_high.wav").is_file() else None,
        },
        {
            "file": "09_tonal_glide_full.wav",
            "title": "Pitch glide across the 3 kHz mouth cavity",
            "listen_for": "Pitch rises for 4 s. Around 1.7 s it crosses 3 kHz and should swell/ring for a moment, then keep going. One glide, one bump.",
            "params": {"src.L.beta": "-0.05→0.8", "oec.freq_hz": 3000.0},
            "metrics": metrics("09_tonal_glide_full.wav", {"bump_s": 1.7}),
            "group": None,
            "reference": None,
        },
    ]

    figures = [
        {
            "file": "figs/uvt_radiated_family.png",
            "title": "Radiated UVT response family",
            "caption": "Measured (solid) vs analytic (dotted) radiation; dashed line = f_H.",
        },
        {
            "file": "figs/uvt_peak_table.png",
            "title": "Peak frequency and Q",
            "caption": "Radiated peak vs oec.freq_hz at default dampings, three gapes.",
        },
        {
            "file": "figs/chain_spectra.png",
            "title": "Chain spectra 01–03",
            "caption": "Same source: dry / trachea / trachea+UVT.",
        },
    ]

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B3",
        "title": "Upper vocal tract — first complete bird (M1)",
        "figures": figures,
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("wrote", OUT / "manifest.json", "copied", copied)


if __name__ == "__main__":
    main()
