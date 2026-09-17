"""Locking range of two weakly coupled labial oscillators.

Run: uv run python -m cuicui.analysis.locking --out listening/B5/figs
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(__file__).resolve().parents[3] / "listening" / "B5" / ".mplconfig")
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

GAMMA = 24000.0
RATE = 192000.0
ALPHA = 0.20
BETA_L = 0.30
DURATION_S = 0.40
SKIP_S = 0.18
EPS_LIST = (0.01, 0.03, 0.1)
DELTA_STEP = 0.005
DELTA_MAX = 0.10


def _cc():
    import cuicui_core as cc

    return cc


def _rest(alpha: float, beta: float) -> tuple[float, float]:
    x, y = _cc().rest_state(alpha, beta)
    return float(x), float(y)


@dataclass
class PeriodStats:
    mean_hz: float = 0.0
    jitter: float = 0.0
    n_periods: int = 0


def period_stats(x: np.ndarray, rate: float, skip: int) -> PeriodStats:
    """Mean-removed zero-crossing period stats (same idea as the C++ lock test)."""
    y = np.asarray(x, dtype=np.float64)
    n = int(y.size)
    if n - skip < 64:
        return PeriodStats()
    sl = y[skip:]
    mu = float(np.mean(sl))
    z = sl - mu
    times: list[float] = []
    for i in range(1, z.size):
        a = z[i - 1]
        b = z[i]
        if a <= 0.0 < b:
            frac = -a / (b - a)
            times.append((skip + i - 1 + frac) / rate)
    periods: list[float] = []
    for i in range(1, len(times)):
        dt = times[i] - times[i - 1]
        if 1.0e-5 < dt < 0.02:
            periods.append(dt)
    if len(periods) < 16:
        return PeriodStats()
    mean = float(np.mean(periods))
    jitter = float(np.std(periods) / mean) if mean > 0.0 else 0.0
    return PeriodStats(mean_hz=1.0 / mean, jitter=jitter, n_periods=len(periods))


def run_mix(beta_r: float, eps: float) -> np.ndarray:
    cc = _cc()
    n = int(round(DURATION_S * RATE))
    x_l0, y_l0 = _rest(-0.10, BETA_L)
    x_r0, y_r0 = _rest(-0.10, beta_r)
    _xl, _xr, mix = cc.bilateral_source_only(
        ALPHA,
        BETA_L,
        ALPHA,
        beta_r,
        GAMMA,
        RATE,
        eps,
        n,
        x_l0,
        y_l0,
        x_r0,
        y_r0 + 0.02,
    )
    return np.asarray(mix, dtype=np.float64)


def is_locked(mix: np.ndarray, sr: float, st0: PeriodStats) -> bool:
    skip = int(round(SKIP_S * sr))
    st = period_stats(mix, sr, skip)
    return (
        st.n_periods >= 24
        and st.jitter < 0.04
        and st.n_periods < int(1.35 * float(st0.n_periods))
    )


def lock_curve(eps: float, deltas: np.ndarray, st0: PeriodStats) -> np.ndarray:
    out = np.zeros(deltas.size, dtype=np.float64)
    for i, d in enumerate(deltas):
        out[i] = 1.0 if is_locked(run_mix(BETA_L + float(d), eps), RATE, st0) else 0.0
    return out


def largest_contiguous_lock(curve: np.ndarray, deltas: np.ndarray) -> float:
    last = 0.0
    for d, locked in zip(deltas, curve, strict=True):
        if locked >= 0.5:
            last = float(d)
        elif d > 0.0:
            break
    return last


def main() -> None:
    parser = argparse.ArgumentParser(description="Coupled-oscillator locking plot")
    parser.add_argument("--out", type=Path, default=Path("listening/B5/figs"))
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    deltas = np.arange(0.0, DELTA_MAX + 0.5 * DELTA_STEP, DELTA_STEP)
    rows: list[dict[str, float]] = []
    fig, ax = plt.subplots(figsize=(7.2, 4.0))
    colors = ("#4c78a8", "#f58518", "#54a24b")
    skip = int(round(SKIP_S * RATE))
    for eps, color in zip(EPS_LIST, colors, strict=True):
        mix0 = run_mix(BETA_L, eps)
        st0 = period_stats(mix0, RATE, skip)
        curve = lock_curve(eps, deltas, st0)
        d_star = largest_contiguous_lock(curve, deltas)
        rows.append(
            {
                "eps": float(eps),
                "lock_delta": d_star,
                "n_periods_ref": float(st0.n_periods),
                "f0_ref_hz": float(st0.mean_hz),
            }
        )
        ax.step(deltas, curve, where="mid", color=color, label=rf"$\varepsilon$ = {eps:g}")
        ax.axvline(d_star, color=color, ls=":", lw=0.9, alpha=0.8)
    ax.set_xlabel(r"detuning $\Delta$  ($\beta_R = 0.30 + \Delta$)")
    ax.set_ylabel("locked (single $f_0$)")
    ax.set_title("Two-side locking vs coupling")
    ax.set_ylim(-0.05, 1.15)
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(out / "locking.png", dpi=140, bbox_inches="tight")
    plt.close(fig)

    (out / "locking.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(rows))


if __name__ == "__main__":
    main()
