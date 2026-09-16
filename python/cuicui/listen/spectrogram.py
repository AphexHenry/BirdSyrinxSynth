"""Spectrogram and waveform stills for listening pages (offline, matplotlib Agg)."""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np
import soundfile as sf
from scipy import signal


def _read_mono(wav_path: Path) -> tuple[np.ndarray, int]:
    audio, sr = sf.read(wav_path, always_2d=True)
    mono = np.mean(audio, axis=1)
    return np.asarray(mono, dtype=np.float64), int(sr)


def save_spectrogram(
    wav_path: Path | str,
    png_path: Path | str,
    fmax: float = 12000,
    n_fft: int = 1024,
    hop: int = 128,
    db_range: float = 80,
) -> None:
    """Write a 0–fmax Hz spectrogram PNG (Hann, 80 dB range by default)."""
    wav_path = Path(wav_path)
    png_path = Path(png_path)
    audio, sr = _read_mono(wav_path)
    _f, _t, spec = signal.spectrogram(
        audio,
        fs=sr,
        window="hann",
        nperseg=n_fft,
        noverlap=max(n_fft - hop, 0),
        scaling="spectrum",
        mode="magnitude",
    )
    spec_db = 20.0 * np.log10(np.maximum(spec, 1e-20))
    spec_db -= float(np.max(spec_db))
    spec_db = np.clip(spec_db, -db_range, 0.0)

    mask = _f <= fmax
    fig, ax = plt.subplots(figsize=(10, 3.2), dpi=120)
    im = ax.pcolormesh(
        _t,
        _f[mask],
        spec_db[mask, :],
        shading="gouraud",
        cmap="magma",
        vmin=-db_range,
        vmax=0.0,
    )
    ax.set_ylim(0.0, fmax)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Hz")
    ax.set_title(wav_path.name)
    fig.colorbar(im, ax=ax, label="dB")
    fig.tight_layout()
    png_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path)
    plt.close(fig)


def save_waveform(wav_path: Path | str, png_path: Path | str) -> None:
    """Write a simple waveform thumbnail (for clips shorter than 0.5 s)."""
    wav_path = Path(wav_path)
    png_path = Path(png_path)
    audio, sr = _read_mono(wav_path)
    t = np.arange(audio.size, dtype=np.float64) / sr
    fig, ax = plt.subplots(figsize=(10, 1.8), dpi=120)
    ax.plot(t, audio, color="#222", linewidth=0.8)
    ax.set_xlim(0.0, t[-1] if t.size else 0.0)
    ax.set_ylim(-1.05, 1.05)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("amp")
    ax.set_title(wav_path.name)
    fig.tight_layout()
    png_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(png_path)
    plt.close(fig)
