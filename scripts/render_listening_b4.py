"""Render WP-B4 listening examples (breath, tension noise, aspiration)."""

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
OUT = ROOT / "listening" / "B4"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B4"
FIGS = OUT / "figs"
REAL_WAV = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")

ALPHA_REST = -0.10
ALPHA_MAX = 0.45
LISTEN_R = 0.9
LISTEN_LOSS = 0.0


def pressure_for_alpha(alpha: float) -> float:
    return float((alpha - ALPHA_REST) / (ALPHA_MAX - ALPHA_REST))


def lfo_span(
    t0: float, t1: float, freq_hz: float, lo: float, hi: float, n_per_cycle: int = 24
) -> list[list[float]]:
    """Sine LFO breakpoints on [t0, t1]."""
    dur = max(1e-6, t1 - t0)
    n = max(8, int(round(dur * freq_hz * n_per_cycle)))
    mid = 0.5 * (lo + hi)
    amp = 0.5 * (hi - lo)
    pts: list[list[float]] = []
    for i in range(n + 1):
        t = t0 + dur * i / n
        pts.append([round(t, 6), float(mid + amp * math.sin(2.0 * math.pi * freq_hz * (t - t0)))])
    return pts


def rising_fm(
    t0: float, t1: float, freq_hz: float, start: float, end: float, depth: float, n_per_cycle: int = 24
) -> list[list[float]]:
    """Linear ramp plus sine FM."""
    dur = max(1e-6, t1 - t0)
    n = max(8, int(round(dur * freq_hz * n_per_cycle)))
    pts: list[list[float]] = []
    for i in range(n + 1):
        t = t0 + dur * i / n
        u = (t - t0) / dur
        mid = start + u * (end - start)
        pts.append([round(t, 6), float(mid + depth * math.sin(2.0 * math.pi * freq_hz * (t - t0)))])
    return pts


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
    x, sr = sf.read(wav, dtype="float32")
    n0 = min(x.shape[0], int(round(prefix_s * sr)))
    peak = float(np.max(np.abs(x[:n0]))) if n0 > 0 else float(np.max(np.abs(x)))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    sf.write(wav, (x * (target / peak)).astype(np.float32), sr, subtype="FLOAT")


def _concat(wavs: list[Path], dest: Path, gap_s: float = 0.15) -> None:
    chunks: list[np.ndarray] = []
    sr = 48000
    for i, wav in enumerate(wavs):
        x, sr = sf.read(wav, dtype="float32")
        chunks.append(x)
        if i + 1 < len(wavs) and gap_s > 0:
            chunks.append(np.zeros(int(round(gap_s * sr)), dtype=np.float32))
    sf.write(dest, np.concatenate(chunks), sr, subtype="FLOAT")


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
            "oec.damping": 0.35,
            "beak.gape": gape,
            "beak.damping": 0.3,
            "out.radiation": 1.0,
            "noise.beta_sigma": 0.0,
            "noise.aspiration": 0.0,
            "resp.attack_ms": 8.0,
            "resp.release_ms": 15.0,
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


def instantaneous_f0(x: np.ndarray, sr: int, skip_s: float = 0.08) -> tuple[np.ndarray, np.ndarray]:
    y = np.asarray(x, dtype=np.float64)
    i0 = int(skip_s * sr)
    times = []
    for i in range(max(i0, 1), y.shape[0]):
        a = y[i - 1]
        b = y[i]
        if a <= 0.0 < b:
            frac = -a / (b - a)
            times.append((i - 1 + frac) / sr)
    t = np.asarray(times)
    if t.size < 8:
        return t, np.full_like(t, np.nan)
    dt = np.diff(t)
    f0 = np.where((dt > 1e-5) & (dt < 0.02), 1.0 / dt, np.nan)
    return t[1:], f0


def extract_clip(src: Path, t0: float, t1: float, dest: Path) -> None:
    x, sr = sf.read(src, dtype="float32", always_2d=False)
    if x.ndim > 1:
        x = x[:, 0]
    i0 = max(0, int(round(t0 * sr)))
    i1 = min(x.shape[0], int(round(t1 * sr)))
    sf.write(dest, x[i0:i1], sr, subtype="FLOAT")


def pressure_step_figure(out: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fs = 48000.0
    t = np.arange(0, int(0.08 * fs)) / fs
    fig, ax = plt.subplots(figsize=(7.2, 3.6))
    for tau_ms, color in ((2.0, "#4c78a8"), (8.0, "#f58518"), (50.0, "#54a24b")):
        tau = tau_ms * 0.001
        p = 1.0 - np.exp(-t / tau)
        ax.plot(t * 1000.0, p, color=color, label=f"attack {tau_ms:.0f} ms")
        ax.axvline(tau_ms, color=color, ls=":", lw=0.8, alpha=0.7)
    ax.axhline(1.0 - math.e**-1, color="#888", ls="--", lw=0.8, label="63.2 %")
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("air-sac pressure p")
    ax.set_title("Respiratory attack: step u = 0 → 1")
    ax.legend(frameon=False)
    ax.set_xlim(0, 80)
    fig.tight_layout()
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)


def f0_track_figure(quiet: Path, noisy: Path, out: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    xq, sr = sf.read(quiet, dtype="float32")
    xn, _ = sf.read(noisy, dtype="float32")
    tq, fq = instantaneous_f0(xq, sr)
    tn, fn = instantaneous_f0(xn, sr)
    fig, ax = plt.subplots(figsize=(7.2, 3.6))
    ax.plot(tq, fq, color="#4c78a8", lw=0.8, label="σ_β = 0")
    ax.plot(tn, fn, color="#e45756", lw=0.7, alpha=0.8, label="σ_β = 0.02")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("cycle f₀ (Hz)")
    ax.set_title("Pitch track: tension noise adds cycle jitter")
    ax.legend(frameon=False)
    mid = float(np.nanmedian(fq)) if np.any(np.isfinite(fq)) else 3400.0
    ax.set_ylim(mid - 120.0, mid + 120.0)
    fig.tight_layout()
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)


def spectro_pair(a: Path, b: Path, out: Path, title_a: str, title_b: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(9.2, 3.6), sharey=True)
    for ax, wav, title in ((axes[0], a, title_a), (axes[1], b, title_b)):
        x, sr = sf.read(wav, dtype="float32")
        nfft = 1024
        hop = 128
        specgram, _freqs, _times, _im = ax.specgram(
            x,
            NFFT=nfft,
            Fs=sr,
            noverlap=nfft - hop,
            cmap="magma",
            vmin=-80,
            vmax=0,
        )
        ax.set_title(title)
        ax.set_xlabel("time (s)")
        ax.set_ylim(0, 12000)
    axes[0].set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    p_note = pressure_for_alpha(0.20)
    p_buzz = pressure_for_alpha(0.16)  # just above SNIC at β≈0.02

    # 01: three notes, different attacks
    note_files = []
    for tau, name in ((2.0, "01a_att2.wav"), (8.0, "01b_att8.wav"), (30.0, "01c_att30.wav")):
        spec = _labial(
            0.55,
            0.20,
            [[0.0, 0.0], [0.005, p_note], [0.40, p_note], [0.55, 0.0]],
            extra_params={"resp.attack_ms": tau, "resp.release_ms": 18.0},
        )
        wav = OUT / name
        _render(spec, wav)
        note_files.append(wav)
    _shared_peak_normalize(note_files)
    _concat(note_files, OUT / "01_attack_2_8_30ms.wav", gap_s=0.12)

    # 02: six 60 ms syllables, 40 ms mini-breaths, slight β motion
    t = 0.05
    press: list[list[float]] = [[0.0, 0.0]]
    beta_auto: list[list[float]] = [[0.0, 0.18]]
    for i in range(6):
        p_i = p_note * (0.92 + 0.08 * (i % 2))
        b_i = 0.18 + 0.04 * (i % 3)
        press += [[t, 0.0], [t + 0.004, p_i], [t + 0.055, p_i], [t + 0.060, 0.0]]
        beta_auto += [[t, b_i], [t + 0.060, b_i + 0.03]]
        t += 0.100
    spec02 = _labial(
        0.75,
        0.20,
        press + [[0.75, 0.0]],
        extra_params={"resp.attack_ms": 6.0, "resp.release_ms": 12.0},
        extra_auto={"src.L.beta": beta_auto},
    )
    _render(spec02, OUT / "02_syllable_train.wav")
    _peak_normalize(OUT / "02_syllable_train.wav")

    # 03: buzz, σ_β 0 → 0.05
    spec03 = _labial(
        4.0,
        0.02,
        [[0.0, p_buzz], [4.0, p_buzz]],
        oec=2800.0,
        extra_params={"noise.aspiration": 0.0, "noise.beta_bandwidth_hz": 300.0},
        extra_auto={"noise.beta_sigma": [[0.0, 0.0], [4.0, 0.05]]},
    )
    _render(spec03, OUT / "03_buzz_sigma_sweep.wav")
    _peak_normalize(OUT / "03_buzz_sigma_sweep.wav")

    # 04: low pulse train (~360 Hz) so chops are audible; then 4 kHz air gated on those pulses.
    p_pulse = pressure_for_alpha(0.22)
    spec04 = _labial(
        4.0,
        -0.10,
        [[0.0, p_pulse], [4.0, p_pulse]],
        oec=900.0,
        extra_auto={"noise.aspiration": [[0.0, 0.0], [2.0, 0.0], [2.05, 0.45], [4.0, 0.45]]},
    )
    _render(spec04, OUT / "04_aspiration_sweep.wav")
    _normalize_from_prefix(OUT / "04_aspiration_sweep.wav", 2.0)

    # 05: explicit 4 Hz pitch LFO (wow). Tension-noise BW cannot go below 50 Hz — that still sounds like hiss.
    spec05 = _labial(
        4.0,
        0.45,
        [[0.0, p_note], [4.0, p_note]],
        extra_params={"noise.beta_sigma": 0.0, "noise.aspiration": 0.0},
        extra_auto={
            "src.L.beta": [[0.0, 0.45], [2.0, 0.45]] + lfo_span(2.0, 4.0, 4.0, 0.37, 0.55),
        },
    )
    _render(spec05, OUT / "05_jitter_ab.wav")
    _normalize_from_prefix(OUT / "05_jitter_ab.wav", 2.0)

    if REAL_WAV.is_file():
        extract_clip(REAL_WAV, 23.476, 23.976, OUT / "06_real_noisy.wav")
        extract_clip(REAL_WAV, 7.828, 7.994, OUT / "07_real_stack.wav")
        _peak_normalize(OUT / "06_real_noisy.wav")
        _peak_normalize(OUT / "07_real_stack.wav")

    # 06: ~60 Hz AM (pressure) + FM (β) — 3× the previous 18 Hz try.
    p_lo, p_hi = 0.42, 0.68
    spec06 = _labial(
        0.50,
        0.22,
        [[0.0, 0.0], [0.03, p_note]] + lfo_span(0.03, 0.40, 60.0, p_lo, p_hi) + [[0.50, 0.0]],
        oec=2800.0,
        gape=0.75,
        extra_params={
            "noise.beta_sigma": 0.0,
            "noise.aspiration": 0.0,
            "resp.attack_ms": 6.0,
            "resp.release_ms": 12.0,
        },
        extra_auto={
            "src.L.beta": rising_fm(0.0, 0.42, 60.0, 0.22, 0.72, 0.07) + [[0.50, 0.72]],
            "oec.freq_hz": [[0.0, 2800.0], [0.42, 4000.0], [0.50, 4000.0]],
        },
    )
    _render(spec06, OUT / "06_noisy_note_attempt.wav")
    _peak_normalize(OUT / "06_noisy_note_attempt.wav")

    # 07: falling tweet, no extra hiss — honest motion vs the real peep.
    spec07 = _labial(
        0.17,
        0.55,
        [[0.0, 0.0], [0.01, p_note], [0.12, p_note], [0.17, 0.0]],
        oec=4500.0,
        gape=0.55,
        extra_params={"resp.attack_ms": 5.0, "resp.release_ms": 10.0},
        extra_auto={
            "src.L.beta": [[0.0, 0.55], [0.14, 0.08]],
            "oec.freq_hz": [[0.0, 5000.0], [0.14, 2800.0]],
        },
    )
    _render(spec07, OUT / "07_stack_attempt_v2.wav")
    _peak_normalize(OUT / "07_stack_attempt_v2.wav")

    # figures
    pressure_step_figure(FIGS / "pressure_steps.png")
    spec_q = _labial(0.6, 0.50, [[0.0, p_note], [0.6, p_note]], extra_params={"noise.beta_sigma": 0.0})
    spec_n = _labial(0.6, 0.50, [[0.0, p_note], [0.6, p_note]], extra_params={"noise.beta_sigma": 0.02})
    _render(spec_q, OUT / "_f0_quiet.wav")
    _render(spec_n, OUT / "_f0_noisy.wav")
    f0_track_figure(OUT / "_f0_quiet.wav", OUT / "_f0_noisy.wav", FIGS / "f0_jitter.png")
    x, sr = sf.read(OUT / "03_buzz_sigma_sweep.wav", dtype="float32")
    n0 = int(0.7 * sr)
    n1 = max(0, x.shape[0] - n0)
    sf.write(OUT / "_03_head.wav", x[:n0], sr, subtype="FLOAT")
    sf.write(OUT / "_03_tail.wav", x[n1:], sr, subtype="FLOAT")
    spectro_pair(OUT / "_03_head.wav", OUT / "_03_tail.wav", FIGS / "spec_03.png", "03 first 0.7 s (σ≈0)", "03 last 0.7 s (σ≈0.05)")
    if (OUT / "06_real_noisy.wav").is_file():
        spectro_pair(
            OUT / "06_noisy_note_attempt.wav",
            OUT / "06_real_noisy.wav",
            FIGS / "spec_06_vs_real.png",
            "06 attempt",
            "real alarm",
        )

    copied: list[str] = []
    for name in ("pressure_steps.png", "f0_jitter.png", "spec_03.png", "spec_06_vs_real.png"):
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
            "file": "01_attack_2_8_30ms.wav",
            "title": "Three notes: attack 2 / 8 / 30 ms",
            "listen_for": "Same pitch three times. First: sharp tick at the start. Middle: a bit of swell. Last: soft fade-in, no click.",
            "params": {"resp.attack_ms": "2, 8, 30", "src.L.beta": 0.2, "alpha": 0.2},
            "metrics": metrics("01_attack_2_8_30ms.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "02_syllable_train.wav",
            "title": "Six short syllables with mini-breaths",
            "listen_for": "Articulated phrasing, 60 ms notes / 40 ms gaps. Offsets should not click. Slight pitch jog between notes.",
            "params": {"resp.attack_ms": 6.0, "resp.release_ms": 12.0},
            "metrics": metrics("02_syllable_train.wav", {"syllables": 6}),
            "group": None,
            "reference": None,
        },
        {
            "file": "03_buzz_sigma_sweep.wav",
            "title": "Tension noise 0 → 0.05 near onset",
            "listen_for": "Starts as a clean reedy tone. Over 4 s it gets rough, then intermittent / buzzy as noise kicks the oscillator below threshold.",
            "params": {"src.L.beta": 0.02, "alpha": 0.16, "noise.beta_sigma": "0→0.05"},
            "metrics": metrics("03_buzz_sigma_sweep.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "04_aspiration_sweep.wav",
            "title": "Low pulse train, then chopped air",
            "listen_for": "First 2 s: a low motor-like chop (~360 Hz), not a whistle. After 2 s: high air should flicker with those chops — if it is a steady hiss, that failed.",
            "params": {"src.L.beta": -0.10, "alpha": 0.22, "noise.aspiration": "0 then 0.45"},
            "metrics": metrics("04_aspiration_sweep.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "05_jitter_ab.wav",
            "title": "Still whistle, then 4 Hz wow",
            "listen_for": "First 2 s: a still high whistle. After 2 s: the pitch should bob about 4 times per second (wow), like a slow siren — not a sizzle on top.",
            "params": {"src.L.beta": "0.45 then 4 Hz LFO", "noise.beta_sigma": 0.0},
            "metrics": metrics("05_jitter_ab.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "06_noisy_note_attempt.wav",
            "title": "60 Hz AM+FM vs real alarm",
            "listen_for": "Same rising whistle as before, throb now ~60 Hz (3× faster). No hiss. Compare the metallic beat with B.",
            "params": {"lfo_hz": 60.0, "src.L.beta": "0.22→0.72 + 60 Hz FM", "noise.beta_sigma": 0.0},
            "metrics": metrics("06_noisy_note_attempt.wav", {"real_clip": "23.48–23.98 s alarm"}),
            "group": None,
            "reference": "06_real_noisy.wav" if (OUT / "06_real_noisy.wav").is_file() else None,
        },
        {
            "file": "07_stack_attempt_v2.wav",
            "title": "Short falling chirp vs real peep",
            "listen_for": "A: a quick chirp that falls. B: the real peep. Judge the motion (down, short). Timbre will not match — we are not fitting this bird yet.",
            "params": {"src.L.beta": "0.55→0.08", "duration_s": 0.17},
            "metrics": metrics("07_stack_attempt_v2.wav", {"real_clip": "7.828–7.994 s peep"}),
            "group": None,
            "reference": "07_real_stack.wav" if (OUT / "07_real_stack.wav").is_file() else None,
        },
    ]

    figures = [
        {
            "file": "figs/pressure_steps.png",
            "title": "Pressure step responses",
            "caption": "Exact one-pole attack; dotted lines mark τ. Horizontal dashed = 63.2 %.",
        },
        {
            "file": "figs/f0_jitter.png",
            "title": "f₀ with / without tension noise",
            "caption": "α=0.2, β=0.5. σ_β = 0 vs 0.02 (same seed family).",
        },
        {
            "file": "figs/spec_03.png",
            "title": "Spectrogram of 03",
            "caption": "Tension-noise sweep: roughness and dropouts toward the end.",
        },
    ]
    if (FIGS / "spec_06_vs_real.png").is_file():
        figures.append(
            {
                "file": "figs/spec_06_vs_real.png",
                "title": "06 vs real alarm",
                "caption": "60 Hz AM+FM attempt vs zebra finch alarm (23.48–23.98 s).",
            }
        )

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B4",
        "title": "Respiration and noise — the voice breathes",
        "figures": figures,
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("wrote", OUT / "manifest.json", "copied", copied)


if __name__ == "__main__":
    main()
