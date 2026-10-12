"""Dominant-trace f0 / amplitude tracker and birdness metrics.

Moved here from scripts/review/track.py in WP-R1.

Tracker: short STFT (5.3 ms Hann, 1 ms hop, 4096 FFT), top-4 spectral peaks per frame in
[fmin, fmax], Viterbi path with a continuity cost (bird FM up to ~30 cents/ms is free), parabolic
peak interpolation. Voicing from band energy. The algorithm is unchanged from the R0 studies, so
banks and study results stay reproducible.
"""

from __future__ import annotations

import numpy as np
from scipy import signal

HOP_S = 0.001


def stft_mag(x: np.ndarray, sr: float, win_s: float = 0.0053, nfft: int = 4096):
    """Magnitude STFT with a 1 ms hop. Returns (frames × bins magnitudes, bin frequencies)."""
    win = int(round(win_s * sr)) // 2 * 2
    hop = int(round(HOP_S * sr))
    w = signal.windows.hann(win, sym=False)
    pad = np.concatenate([np.zeros(win // 2), x, np.zeros(win)])
    n = (len(pad) - win) // hop
    idx = np.arange(win)[None, :] + hop * np.arange(n)[:, None]
    frames = pad[idx] * w[None, :]
    mag = np.abs(np.fft.rfft(frames, nfft, axis=1))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    return mag, freqs


def track(
    x: np.ndarray,
    sr: float,
    fmin: float = 1200.0,
    fmax: float = 9000.0,
    floor_db: float = -40.0,
    mask: np.ndarray | None = None,
    k: int = 4,
) -> dict:
    """Return dict with t, f0 (Hz, nan unvoiced), amp (linear), h2_db, h3_db, voiced, level_db."""
    mag, freqs = stft_mag(x, sr)
    band = (freqs >= fmin) & (freqs <= fmax)
    bi = np.where(band)[0]
    lvl_db = 20 * np.log10(mag + 1e-12)
    n = mag.shape[0]
    t = np.arange(n) * HOP_S
    # candidates
    cand_f = np.full((n, k), np.nan)
    cand_l = np.full((n, k), -200.0)
    for i in range(n):
        row = lvl_db[i, bi]
        pk, _ = signal.find_peaks(row)
        if len(pk) == 0:
            continue
        top = pk[np.argsort(row[pk])[::-1][:k]]
        for j, p in enumerate(top):
            g = bi[p]
            a, b, c = lvl_db[i, g - 1], lvl_db[i, g], lvl_db[i, g + 1]
            den = a - 2 * b + c
            d = 0.5 * (a - c) / den if den != 0 else 0.0
            cand_f[i, j] = freqs[g] + d * (freqs[1] - freqs[0])
            cand_l[i, j] = b - 0.25 * (a - c) * d
    # Viterbi
    cents = 1200 * np.log2(np.where(np.isfinite(cand_f), cand_f, 1.0) / 440.0)
    cost = np.full((n, k), np.inf)
    back = np.zeros((n, k), dtype=int)
    cost[0] = -cand_l[0] / 6.0
    for i in range(1, n):
        dc = np.abs(cents[i][:, None] - cents[i - 1][None, :])
        tc = np.maximum(0.0, dc - 30.0) / 40.0
        tot = cost[i - 1][None, :] + tc
        back[i] = np.argmin(tot, axis=1)
        cost[i] = tot[np.arange(k), back[i]] - cand_l[i] / 6.0
        cost[i][~np.isfinite(cand_f[i])] = np.inf
        if not np.isfinite(cost[i]).any():
            cost[i] = -cand_l[i] / 6.0
            cost[i][~np.isfinite(cand_f[i])] = 1e9
    path = np.zeros(n, dtype=int)
    path[-1] = int(np.argmin(cost[-1]))
    for i in range(n - 1, 0, -1):
        path[i - 1] = back[i, path[i]]
    f0 = cand_f[np.arange(n), path]
    lvl = cand_l[np.arange(n), path]
    # voicing: peak level relative to global max, and peak prominence over band median
    med = np.median(lvl_db[:, bi], axis=1)
    gmax = np.nanmax(lvl)
    voiced = (lvl > gmax + floor_db) & (lvl - med > 18.0) & np.isfinite(f0)
    if mask is not None:
        voiced &= mask[:n]
    voiced = _clean_runs(voiced, min_on=6, min_gap=4)
    amp = np.where(voiced, 10 ** ((lvl - gmax) / 20.0), 0.0)
    # harmonic levels relative to the tracked component
    h2 = np.full(n, np.nan)
    h3 = np.full(n, np.nan)
    df = freqs[1] - freqs[0]
    for i in np.where(voiced)[0]:
        for mult, arr in ((2, h2), (3, h3)):
            fh = mult * f0[i]
            if fh > sr / 2 - 500:
                continue
            j0 = int((fh * 0.97) / df)
            j1 = int((fh * 1.03) / df) + 1
            arr[i] = lvl_db[i, j0:j1].max() - lvl[i]
    f0 = np.where(voiced, f0, np.nan)
    return {"t": t, "f0": f0, "amp": amp, "voiced": voiced, "h2_db": h2, "h3_db": h3,
            "level_db": lvl}


def _clean_runs(v: np.ndarray, min_on: int, min_gap: int) -> np.ndarray:
    v = v.copy()
    # fill short gaps
    idx = np.flatnonzero(np.diff(np.concatenate([[0], v.astype(int), [0]])))
    rs = list(zip(idx[::2], idx[1::2], strict=False))
    for (_a0, a1), (b0, _b1) in zip(rs, rs[1:], strict=False):
        if b0 - a1 <= min_gap:
            v[a1:b0] = True
    idx = np.flatnonzero(np.diff(np.concatenate([[0], v.astype(int), [0]])))
    for a0, a1 in zip(idx[::2], idx[1::2], strict=False):
        if a1 - a0 < min_on:
            v[a0:a1] = False
    return v


def runs(v: np.ndarray) -> list[tuple[int, int]]:
    """[start, end) index pairs of the True runs in a boolean array."""
    idx = np.flatnonzero(np.diff(np.concatenate([[0], v.astype(int), [0]])))
    return list(zip(idx[::2], idx[1::2], strict=False))


def metrics(tr: dict) -> dict:
    """Birdness-relevant statistics of a tracked signal."""
    v = tr["voiced"]
    f0 = tr["f0"]
    c = 1200 * np.log2(np.where(v, f0, 440.0) / 440.0) + 6900
    rs = [r for r in runs(v) if r[1] - r[0] >= 8]
    slopes, ranges, durs, gaps = [], [], [], []
    for a, b in rs:
        seg = c[a:b]
        win = min(len(seg) // 2 * 2 - 1, 7)
        seg_s = signal.savgol_filter(seg, win, 2) if len(seg) >= 7 else seg
        d = np.abs(np.diff(seg_s)) / HOP_S  # cents/s
        slopes.append(d)
        ranges.append(np.percentile(seg, 95) - np.percentile(seg, 5))
        durs.append((b - a) * HOP_S * 1000)
    for (_a0, a1), (b0, _) in zip(rs, rs[1:], strict=False):
        gaps.append((b0 - a1) * HOP_S * 1000)
    s = np.concatenate(slopes) if slopes else np.array([0.0])
    return {
        "voiced_s": float(v.sum() * HOP_S),
        "elements": len(rs),
        "median_f0_hz": float(np.nanmedian(f0)),
        "f0_p5_p95_hz": (float(np.nanpercentile(f0, 5)), float(np.nanpercentile(f0, 95))),
        "fm_median_cents_per_s": float(np.median(s)),
        "fm_p90_cents_per_s": float(np.percentile(s, 90)),
        "steady_frac": float(np.mean(s < 1500.0)),
        "range_per_element_cents": float(np.median(ranges)) if ranges else 0.0,
        "element_ms_median": float(np.median(durs)) if durs else 0.0,
        "gap_ms_median": float(np.median(gaps)) if gaps else 0.0,
        "h2_minus_h1_db": float(np.nanmedian(tr["h2_db"])),
        "h3_minus_h1_db": float(np.nanmedian(tr["h3_db"])),
    }
