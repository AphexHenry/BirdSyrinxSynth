"""WP-R3 study helpers: the whistle voice on syllable lists, recordings at 48 kHz, loudness matching.

The voice is the plugin's whistle (PROTO1 recipe): a sine on the 1 ms pitch curve, the bird's 2nd/3rd
harmonics at −30 / −33 dB, the amplitude curve, 2.5 ms fades at both ends, then a light room (−22 dB
wet, the COLOUR1 room).
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts/studies"))
import colour1_make as C1  # noqa: E402

SR = 48000
HARM = ((2, -30.0), (3, -33.0))
ANCHOR_HARM = ((2, -5.0), (3, -10.0), (4, -15.0), (5, -20.0))
FADE_S = 0.0025
REC = {
    "22938": Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav"),
    "22937": ROOT / "recordings/22937 - House Finch.wav",
}
HP = signal.butter(4, 1000, "highpass", fs=SR, output="sos")


def recording(rec: str) -> np.ndarray:
    """The whole recording, mono, 48 kHz (not filtered)."""
    x, sr = sf.read(str(REC[rec]), always_2d=True)
    x = x.mean(axis=1)
    if sr != SR:
        x = signal.resample_poly(x, SR, int(sr))
    return x


def clip(x: np.ndarray, t0: float, t1: float) -> np.ndarray:
    """A passage, high-passed at 1 kHz (as COLOUR1 / R1CHECK references)."""
    return signal.sosfiltfilt(HP, x[int(t0 * SR): int(t1 * SR)])


def noise_bed(x: np.ndarray, busy: list[tuple[float, float]], dur: float = 1.5) -> np.ndarray:
    """The quietest `dur` s of background (not digital silence, away from any syllable), high-passed."""
    y = signal.sosfiltfilt(HP, x)
    win, hop = int(dur * SR), int(0.25 * SR)
    best, best_rms = None, np.inf
    for i in range(0, len(y) - win, hop):
        t0, t1 = i / SR, (i + win) / SR
        if any(a < t1 + 0.1 and b > t0 - 0.1 for a, b in busy):
            continue
        seg = y[i: i + win]
        r = float(np.sqrt(np.mean(seg**2)))
        if 1e-6 < r < best_rms:
            best, best_rms = seg, r
    if best is None:
        return np.zeros(win)
    f = np.minimum(1.0, np.minimum(np.arange(win), np.arange(win)[::-1]) / (0.05 * SR))
    return best * f


def whistle(events: list[tuple[float, np.ndarray, np.ndarray]], n: int, harmonics=HARM,
            room: bool = True) -> tuple[np.ndarray, np.ndarray]:
    """Render [(onset s, cents per ms, amp per ms)] into n samples; returns (audio, voiced mask)."""
    f = np.zeros(n)
    a = np.zeros(n)
    mask = np.zeros(n, bool)
    for t0, c, amp in events:
        i0 = int(round(t0 * SR))
        m = len(c)
        ns = int(round(m / 1000 * SR))
        tt = np.arange(ns) / SR * 1000.0
        fi = 440.0 * 2 ** ((np.interp(tt, np.arange(m), c) - 6900.0) / 1200.0)
        ai = np.interp(tt, np.arange(m), amp)
        k = int(FADE_S * SR)
        env = np.minimum(1.0, np.minimum(np.arange(ns) + 1, np.arange(ns)[::-1] + 1) / k)
        lo, hi = max(0, i0), min(n, i0 + ns)
        if hi <= lo:
            continue
        f[lo:hi] = fi[lo - i0: hi - i0]
        a[lo:hi] = (ai * env)[lo - i0: hi - i0]
        mask[lo:hi] = True
    f[~mask] = 1000.0
    y = C1.tone(f, a, harmonics)
    if room:
        y = C1.room(y)
    return y, mask


def match(y: np.ndarray, mask: np.ndarray, target_rms: float, bed: np.ndarray) -> np.ndarray:
    """Scale y so its voiced RMS equals target_rms, then add the background bed (looped)."""
    r = float(np.sqrt(np.mean(y[mask] ** 2))) if mask.any() else 1.0
    return y * target_rms / (r + 1e-12) + np.resize(bed, len(y))
