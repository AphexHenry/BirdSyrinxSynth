"""Forward source atlas of the labial oscillator (WP-C2).

Grid, two initial-condition protocols, onset/offset boundaries.
Uses ``cuicui_core.source_only`` (GIL released). Cache one file per β row.
"""

from __future__ import annotations

import json
import os
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np

GAMMA0 = 24000.0
RATE = 352800.0
ALPHA0 = -0.10
ALPHA1 = 0.60
ALPHA_STEP = 0.005
BETA0 = -0.30
BETA1 = 1.20
BETA_STEP = 0.005
RAMP_S = 0.020
HOLD_S = 0.150
ANALYSE_S = 0.100
SEED_ALPHA = 0.45
A_REST = -0.10
BOUNDARY_RAMP = 0.2  # s^-1, 01_research.md §4.3.1
P2P_SILENT = 0.05
AC_PERIODIC = 0.99
AC_DOUBLE_MARGIN = 0.05
HALF_F0_DB = -20.0

REGIME_SILENT = 0
REGIME_PERIODIC = 1
REGIME_DOUBLED = 2
REGIME_IRREGULAR = 3


def grid(start: float, stop: float, step: float) -> np.ndarray:
    n = int(round((stop - start) / step)) + 1
    return (start + np.arange(n, dtype=np.float64) * step).astype(np.float64)


def analytic_onset_from_rest(beta: float) -> float:
    """01_research.md §4.3.1: SNIC for β < 0.25, else Hopf at α = 0."""
    if beta >= 0.25:
        return 0.0
    disc = max(0.0, 1.0 - 3.0 * beta)
    xu = (1.0 + np.sqrt(disc)) / 3.0
    return float(2.0 * xu**3 - xu**2)


def _cc():
    import cuicui_core as cc

    return cc


def _acf_norm(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    x = x - float(x.mean())
    n = int(x.size)
    if n < 8:
        return np.zeros(0, dtype=np.float64)
    nfft = 1 << (2 * n - 1).bit_length()
    spec = np.fft.rfft(x, n=nfft)
    ac = np.fft.irfft(spec * np.conjugate(spec), n=nfft)[:n]
    if ac[0] <= 1e-18:
        return ac
    return ac / ac[0]


def _f0_zc(x: np.ndarray, sr: float) -> float:
    ac = np.asarray(x, dtype=np.float64)
    ac = ac - float(ac.mean())
    y0 = ac[:-1]
    y1 = ac[1:]
    idx = np.nonzero((y0 <= 0.0) & (y1 > 0.0))[0]
    if idx.size < 4:
        return 0.0
    frac = y0[idx] / (y0[idx] - y1[idx])
    times = (idx.astype(np.float64) + frac) / sr
    span = float(times[-1] - times[0])
    if span <= 0.0:
        return 0.0
    return float((idx.size - 1) / span)


def _energy_at(mag: np.ndarray, freqs: np.ndarray, f: float) -> float:
    if f <= 0.0 or mag.size == 0:
        return 0.0
    return float(np.interp(f, freqs, mag, left=0.0, right=0.0))


def analyse_window(x: np.ndarray, sr: float, gamma: float) -> tuple[float, float, float, int]:
    """Return (nu, amp_p2p, sci, regime) for one analysis window."""
    sl = np.asarray(x, dtype=np.float64)
    if sl.size < 32:
        return float("nan"), 0.0, float("nan"), REGIME_SILENT
    mu = float(sl.mean())
    ac = sl - mu
    amp = float(ac.max() - ac.min()) if ac.size else 0.0
    if amp < P2P_SILENT:
        return float("nan"), amp, float("nan"), REGIME_SILENT

    f0 = _f0_zc(ac, sr)
    spec = np.fft.rfft(ac * np.hanning(ac.size))
    mag = np.abs(spec)
    power = mag * mag
    freqs = np.fft.rfftfreq(ac.size, 1.0 / sr)
    wsum = float(np.sum(mag))
    centroid = float(np.sum(freqs * mag) / wsum) if wsum > 0.0 else 0.0

    acf = _acf_norm(ac)
    t_samp = (sr / f0) if f0 > 1.0 else 0.0
    r_t = 0.0
    r_2t = 0.0
    if t_samp >= 1.0 and acf.size > int(t_samp) + 1:
        r_t = float(np.interp(t_samp, np.arange(acf.size, dtype=np.float64), acf))
        if 2.0 * t_samp < acf.size - 1:
            r_2t = float(np.interp(2.0 * t_samp, np.arange(acf.size, dtype=np.float64), acf))

    sci = float(centroid / f0) if f0 > 1.0 else float("nan")
    nu = float(2.0 * np.pi * f0 / gamma) if f0 > 1.0 else float("nan")

    e_f0 = _energy_at(power, freqs, f0)
    e_half = _energy_at(power, freqs, 0.5 * f0)
    half_db = -120.0
    if e_f0 > 1e-24 and e_half > 0.0:
        half_db = 10.0 * np.log10(e_half / e_f0)

    if f0 <= 1.0:
        return float("nan"), amp, float("nan"), REGIME_IRREGULAR
    if r_t > AC_PERIODIC:
        return nu, amp, sci, REGIME_PERIODIC
    if r_2t > r_t + AC_DOUBLE_MARGIN and half_db > HALF_F0_DB:
        return nu, amp, sci, REGIME_DOUBLED
    return nu, amp, sci, REGIME_IRREGULAR


def _run_protocol(
    cc: Any,
    alpha_start: float,
    alpha_end: float,
    beta: float,
    x0: float,
    y0: float,
) -> tuple[np.ndarray, np.ndarray]:
    n_ramp = int(round(RAMP_S * RATE))
    n_hold = int(round(HOLD_S * RATE))
    n = n_ramp + n_hold
    a = np.empty(n, dtype=np.float64)
    if n_ramp > 0:
        a[:n_ramp] = np.linspace(alpha_start, alpha_end, n_ramp, dtype=np.float64)
    a[n_ramp:] = alpha_end
    b = np.full(n, beta, dtype=np.float64)
    xs, ys = cc.source_only(a, b, GAMMA0, RATE, float(x0), float(y0))
    return np.asarray(xs), np.asarray(ys)


def _onset_offset(cc: Any, beta: float) -> tuple[float, float]:
    a0, a1 = A_REST, ALPHA1
    n_up = int(round(((a1 - a0) / BOUNDARY_RAMP) * RATE))
    n_dn = int(round(((SEED_ALPHA - a0) / BOUNDARY_RAMP) * RATE))
    alpha_up = a0 + BOUNDARY_RAMP * np.arange(n_up, dtype=np.float64) / RATE
    # From oscillation at SEED_ALPHA down to rest.
    n_seed = int(round(HOLD_S * RATE))
    a_seed = np.full(n_seed, SEED_ALPHA, dtype=np.float64)
    alpha_dn = SEED_ALPHA - BOUNDARY_RAMP * np.arange(n_dn, dtype=np.float64) / RATE
    alpha = np.concatenate([alpha_up, a_seed, alpha_dn])
    b = np.full(alpha.shape[0], beta, dtype=np.float64)
    x0, y0 = cc.rest_state(a0, beta)
    xs, _ys = cc.source_only(alpha, b, GAMMA0, RATE, float(x0), float(y0))
    x = np.asarray(xs)
    win = int(round(0.010 * RATE))
    hop = int(round(0.001 * RATE))

    def window_ok(i: int) -> bool:
        sl = x[i - win : i]
        mu = float(sl.mean())
        ac = sl - mu
        p2p = float(ac.max() - ac.min())
        zc = int(np.sum((ac[:-1] <= 0.0) & (ac[1:] > 0.0)))
        return zc >= 3 and p2p > P2P_SILENT

    onset = float("nan")
    prev = False
    for i in range(win, n_up + 1, hop):
        ok = window_ok(i)
        if ok and prev:
            onset = float(alpha[i - hop])
            break
        prev = ok

    offset = float("nan")
    i0 = n_up + n_seed
    prev = True
    for i in range(i0 + win, i0 + n_dn + 1, hop):
        ok = window_ok(i)
        if prev and not ok:
            offset = float(alpha[i - 1])
            break
        prev = ok
    return onset, offset


def process_beta_row(payload: tuple[int, float, np.ndarray, str, bool]) -> dict[str, Any]:
    """Worker: one β, all α, both protocols, plus onset/offset."""
    ib, beta, alphas, cache_dir, force = payload
    cache = Path(cache_dir)
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"beta_{ib:04d}.npz"
    if path.is_file() and not force:
        data = np.load(path)
        if float(data["beta"]) == float(beta) and int(data["ib"]) == int(ib):
            return {k: data[k] for k in data.files}

    cc = _cc()
    n_a = int(alphas.size)
    n_an = int(round(ANALYSE_S * RATE))
    nu_rest = np.full(n_a, np.nan, dtype=np.float64)
    amp_rest = np.full(n_a, np.nan, dtype=np.float64)
    sci_rest = np.full(n_a, np.nan, dtype=np.float64)
    regime_rest = np.zeros(n_a, dtype=np.float64)
    nu_osc = np.full(n_a, np.nan, dtype=np.float64)
    amp_osc = np.full(n_a, np.nan, dtype=np.float64)
    sci_osc = np.full(n_a, np.nan, dtype=np.float64)
    regime_osc = np.zeros(n_a, dtype=np.float64)

    seed_x = 0.0
    seed_y = 0.0
    have_seed = False
    ia_seed = int(round((SEED_ALPHA - ALPHA0) / ALPHA_STEP))

    for ia in range(n_a):
        alpha = float(alphas[ia])
        x0, y0 = cc.rest_state(A_REST, float(beta))
        xs, ys = _run_protocol(cc, A_REST, alpha, float(beta), float(x0), float(y0))
        nu, amp, sci, reg = analyse_window(xs[-n_an:], RATE, GAMMA0)
        nu_rest[ia] = nu
        amp_rest[ia] = amp
        sci_rest[ia] = sci
        regime_rest[ia] = float(reg)
        if ia == ia_seed:
            seed_x = float(np.asarray(xs)[-1])
            seed_y = float(np.asarray(ys)[-1])
            have_seed = True

    if not have_seed:
        x0, y0 = cc.rest_state(A_REST, float(beta))
        xs, ys = _run_protocol(cc, A_REST, SEED_ALPHA, float(beta), float(x0), float(y0))
        seed_x = float(np.asarray(xs)[-1])
        seed_y = float(np.asarray(ys)[-1])

    for ia in range(n_a):
        alpha = float(alphas[ia])
        xs, _ys = _run_protocol(cc, SEED_ALPHA, alpha, float(beta), seed_x, seed_y)
        nu, amp, sci, reg = analyse_window(xs[-n_an:], RATE, GAMMA0)
        nu_osc[ia] = nu
        amp_osc[ia] = amp
        sci_osc[ia] = sci
        regime_osc[ia] = float(reg)

    alpha_on, alpha_off = _onset_offset(cc, float(beta))
    out = {
        "ib": np.int32(ib),
        "beta": np.float64(beta),
        "nu_rest": nu_rest,
        "amp_rest": amp_rest,
        "sci_rest": sci_rest,
        "regime_rest": regime_rest,
        "nu_osc": nu_osc,
        "amp_osc": amp_osc,
        "sci_osc": sci_osc,
        "regime_osc": regime_osc,
        "alpha_on": np.float64(alpha_on),
        "alpha_off": np.float64(alpha_off),
    }
    np.savez(path, **out)
    return out


def assemble_atlas(
    rows: list[dict[str, Any]], alphas: np.ndarray, betas: np.ndarray
) -> dict[str, np.ndarray]:
    n_b = int(betas.size)
    n_a = int(alphas.size)
    atlas: dict[str, np.ndarray] = {
        "alphas": np.asarray(alphas, dtype=np.float64),
        "betas": np.asarray(betas, dtype=np.float64),
    }
    keys = (
        "nu_rest",
        "amp_rest",
        "sci_rest",
        "regime_rest",
        "nu_osc",
        "amp_osc",
        "sci_osc",
        "regime_osc",
    )
    for k in keys:
        atlas[k] = np.full((n_b, n_a), np.nan, dtype=np.float64)
    atlas["alpha_on"] = np.full(n_b, np.nan, dtype=np.float64)
    atlas["alpha_off"] = np.full(n_b, np.nan, dtype=np.float64)
    for row in rows:
        ib = int(row["ib"])
        for k in keys:
            atlas[k][ib, :] = np.asarray(row[k], dtype=np.float64)
        atlas["alpha_on"][ib] = float(row["alpha_on"])
        atlas["alpha_off"][ib] = float(row["alpha_off"])
    return atlas


def generate_atlas(
    cache_dir: Path,
    workers: int = 0,
    force: bool = False,
    log=print,
) -> dict[str, np.ndarray]:
    alphas = grid(ALPHA0, ALPHA1, ALPHA_STEP)
    betas = grid(BETA0, BETA1, BETA_STEP)
    if workers <= 0:
        workers = max(1, os.cpu_count() or 4)
    cache_dir = Path(cache_dir)
    cache_dir.mkdir(parents=True, exist_ok=True)
    payloads = [
        (ib, float(betas[ib]), alphas, str(cache_dir), bool(force)) for ib in range(int(betas.size))
    ]
    t0 = time.perf_counter()
    log(f"atlas grid {alphas.size} × {betas.size}, {workers} workers, cache {cache_dir}")
    rows: list[dict[str, Any]] = [None] * len(payloads)  # type: ignore[list-item]
    try:
        with ProcessPoolExecutor(max_workers=workers) as ex:
            for i, row in enumerate(ex.map(process_beta_row, payloads), 1):
                rows[int(row["ib"])] = row
                if i % 10 == 0 or i == len(payloads):
                    dt = time.perf_counter() - t0
                    log(f"  {i}/{len(payloads)} β rows  {dt:.1f}s")
    except (PermissionError, OSError) as exc:
        log(f"multiprocessing unavailable ({exc}); running serially")
        for i, payload in enumerate(payloads, 1):
            row = process_beta_row(payload)
            rows[int(row["ib"])] = row
            if i % 10 == 0 or i == len(payloads):
                log(f"  {i}/{len(payloads)} β rows  {time.perf_counter() - t0:.1f}s")
    elapsed = time.perf_counter() - t0
    log(f"atlas generation {elapsed:.1f}s")
    atlas = assemble_atlas(rows, alphas, betas)
    atlas["elapsed_s"] = np.array([elapsed])
    return atlas


def save_forward_npz(atlas: dict[str, np.ndarray], path: Path) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(path, **{k: v for k, v in atlas.items() if isinstance(v, np.ndarray)})


def plot_regime_map(atlas: dict[str, np.ndarray], out_png: Path) -> None:
    os.environ.setdefault("MPLCONFIGDIR", str(out_png.parent / ".mplconfig"))
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    alphas = atlas["alphas"]
    betas = atlas["betas"]
    nu = atlas["nu_rest"]
    f0 = nu * GAMMA0 / (2.0 * np.pi)
    fig, ax = plt.subplots(figsize=(7.4, 5.6), dpi=120)
    masked = np.ma.masked_invalid(f0)
    im = ax.pcolormesh(alphas, betas, masked, shading="auto", cmap="magma", vmin=0, vmax=5000)
    fig.colorbar(im, ax=ax, label="f₀ (Hz), from rest")
    ax.plot(atlas["alpha_on"], betas, color="cyan", lw=1.4, label=r"$\alpha_{on}$")
    ax.plot(atlas["alpha_off"], betas, color="lime", lw=1.2, ls="--", label=r"$\alpha_{off}$")
    b_an = betas[betas < 0.25]
    ax.plot(
        [analytic_onset_from_rest(float(b)) for b in b_an],
        b_an,
        color="white",
        lw=1.0,
        ls=":",
        label="analytic onset",
    )
    ax.set_xlabel("α")
    ax.set_ylabel("β")
    ax.set_title("Source atlas (from rest) with onset/offset")
    ax.legend(loc="upper right", fontsize=8, facecolor="#222", labelcolor="white")
    fig.tight_layout()
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png)
    plt.close(fig)


def write_onset_report(atlas: dict[str, np.ndarray], path: Path) -> dict[str, Any]:
    betas = atlas["betas"]
    on = atlas["alpha_on"]
    mask = betas < 0.25
    analytic = np.array([analytic_onset_from_rest(float(b)) for b in betas], dtype=np.float64)
    err = on[mask] - analytic[mask]
    err = err[np.isfinite(err)]
    report = {
        "n_beta_lt_0_25": int(np.count_nonzero(mask)),
        "max_abs_error": float(np.max(np.abs(err))) if err.size else float("nan"),
        "median_abs_error": float(np.median(np.abs(err))) if err.size else float("nan"),
        "pass_pm_0_01": bool(err.size > 0 and float(np.max(np.abs(err))) <= 0.01 + 1e-9),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report
