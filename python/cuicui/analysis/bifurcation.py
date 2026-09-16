"""Regime maps, analytic overlays, onset/offset ramps for the labial oscillator.

Run: uv run python -m cuicui.analysis.bifurcation --out listening/B1/figs
"""

from __future__ import annotations

import argparse
import json
import os
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

# Writable cache inside the repo (sandbox / CI may not allow ~/.matplotlib).
os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(__file__).resolve().parents[3] / "listening" / "B1" / ".mplconfig")
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

GAMMA = 24000.0
RATE = 192000.0
DURATION_S = 0.15
ANALYSE_S = 0.10


def _cc():
    import cuicui_core as cc

    return cc


def saddle_node_curve(xs: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(α, β) = (2x³ − x², 2x − 3x²). 01_research.md §4.3.1."""
    alpha = 2.0 * xs**3 - xs**2
    beta = 2.0 * xs - 3.0 * xs**2
    return alpha, beta


def onset_from_rest_alpha(beta: float) -> float:
    if beta >= 0.25:
        return 0.0
    xu = (1.0 + np.sqrt(max(0.0, 1.0 - 3.0 * beta))) / 3.0
    return float(2.0 * xu**3 - xu**2)


def _f0_sci(x: np.ndarray, sr: float) -> tuple[float, float, float]:
    """f0 (Hz), peak-to-peak, SCI = centroid/f0 on the last ANALYSE_S."""
    n = x.size
    n_an = min(n, int(round(ANALYSE_S * sr)))
    sl = np.asarray(x[-n_an:], dtype=np.float64)
    mu = float(sl.mean())
    ac = sl - mu
    p2p = float(ac.max() - ac.min()) if sl.size else 0.0
    if p2p < 0.05:
        return 0.0, p2p, float("nan")
    # interpolated rising zero crossings
    y0 = ac[:-1]
    y1 = ac[1:]
    mask = (y0 <= 0.0) & (y1 > 0.0)
    idx = np.nonzero(mask)[0]
    if idx.size < 2:
        return 0.0, p2p, float("nan")
    frac = y0[idx] / (y0[idx] - y1[idx])
    times = (idx.astype(np.float64) + frac) / sr
    f0 = float((idx.size - 1) / (times[-1] - times[0]))
    spec = np.fft.rfft(ac * np.hanning(ac.size))
    mag = np.abs(spec)
    freqs = np.fft.rfftfreq(ac.size, 1.0 / sr)
    wsum = float(np.sum(mag))
    centroid = float(np.sum(freqs * mag) / wsum) if wsum > 0 else 0.0
    sci = centroid / f0 if f0 > 1.0 else float("nan")
    return f0, p2p, sci


def _eval_chunk(payload: tuple) -> list[tuple[int, int, float, float, float]]:
    """Worker: list of (ia, ib, alpha, beta) → (ia, ib, f0, p2p, sci)."""
    cc = _cc()
    points, gamma, rate, duration = payload
    n = int(round(duration * rate))
    out: list[tuple[int, int, float, float, float]] = []
    for ia, ib, alpha, beta in points:
        x0, y0 = cc.rest_state(-0.1, float(beta))
        a = np.full(n, float(alpha), dtype=np.float64)
        b = np.full(n, float(beta), dtype=np.float64)
        xs, _ys = cc.source_only(a, b, float(gamma), float(rate), float(x0), float(y0))
        f0, p2p, sci = _f0_sci(np.asarray(xs), rate)
        out.append((int(ia), int(ib), f0, p2p, sci))
    return out


def _onset_offset_one(beta: float) -> dict[str, float]:
    cc = _cc()
    ramp = 0.2
    a0, a1 = -0.10, 0.40
    n_up = int(round(((a1 - a0) / ramp) * RATE))
    n_dn = n_up
    alpha_up = a0 + ramp * np.arange(n_up, dtype=np.float64) / RATE
    alpha_dn = a1 - ramp * np.arange(n_dn, dtype=np.float64) / RATE
    alpha = np.concatenate([alpha_up, alpha_dn])
    b = np.full(alpha.shape[0], beta, dtype=np.float64)
    x0, y0 = cc.rest_state(a0, beta)
    xs, _ys = cc.source_only(alpha, b, GAMMA, RATE, float(x0), float(y0))
    x = np.asarray(xs)
    win = int(round(0.010 * RATE))
    hop = int(round(0.001 * RATE))

    def first_onset(i0: int, i1: int, alphas: np.ndarray, xx: np.ndarray) -> float:
        prev = False
        onset = float("nan")
        for i in range(i0 + win, i1 + 1, hop):
            sl = xx[i - win : i]
            mu = float(sl.mean())
            ac = sl - mu
            p2p = float(ac.max() - ac.min())
            zc = int(np.sum((ac[:-1] <= 0.0) & (ac[1:] > 0.0)))
            ok = zc >= 3 and p2p > 0.05
            if ok and prev:
                onset = float(alphas[i - hop - 1])
                break
            prev = ok
        return onset

    onset = first_onset(0, n_up, alpha, x)
    # offset: last time the condition holds on the way down, then first miss
    prev = True
    offset = float("nan")
    for i in range(n_up + win, n_up + n_dn + 1, hop):
        sl = x[i - win : i]
        mu = float(sl.mean())
        ac = sl - mu
        p2p = float(ac.max() - ac.min())
        zc = int(np.sum((ac[:-1] <= 0.0) & (ac[1:] > 0.0)))
        ok = zc >= 3 and p2p > 0.05
        if prev and not ok:
            offset = float(alpha[i - 1])
            break
        prev = ok
    return {
        "beta": beta,
        "onset_alpha": onset,
        "offset_alpha": offset,
        "analytic_onset": onset_from_rest_alpha(beta),
    }


def regime_maps(
    out_dir: Path, n_alpha: int = 61, n_beta: int = 66, workers: int = 8
) -> dict[str, np.ndarray]:
    alphas = np.linspace(-0.1, 0.5, n_alpha)
    betas = np.linspace(-0.3, 1.0, n_beta)
    points = [
        (ia, ib, float(alphas[ia]), float(betas[ib]))
        for ib in range(n_beta)
        for ia in range(n_alpha)
    ]
    chunk = max(8, len(points) // (workers * 4))
    chunks = [points[i : i + chunk] for i in range(0, len(points), chunk)]
    payloads = [(ch, GAMMA, RATE, DURATION_S) for ch in chunks]
    f0 = np.full((n_beta, n_alpha), np.nan)
    p2p = np.full((n_beta, n_alpha), np.nan)
    sci = np.full((n_beta, n_alpha), np.nan)

    def _consume(result: list[tuple[int, int, float, float, float]]) -> None:
        for ia, ib, fv, pv, sv in result:
            f0[ib, ia] = fv if fv > 0 else np.nan
            p2p[ib, ia] = pv
            sci[ib, ia] = sv

    try:
        with ProcessPoolExecutor(max_workers=workers) as ex:
            for result in ex.map(_eval_chunk, payloads):
                _consume(result)
    except (PermissionError, OSError):
        for payload in payloads:
            _consume(_eval_chunk(payload))
    xs = np.linspace(-0.5, 1.2, 400)
    sn_a, sn_b = saddle_node_curve(xs)
    hopf_b = np.linspace(0.0, 1.0, 50)
    hopf_a = np.zeros_like(hopf_b)

    def _heatmap(z: np.ndarray, title: str, fname: str, cmap: str, vmin=None, vmax=None) -> None:
        fig, ax = plt.subplots(figsize=(7.2, 5.4), dpi=120)
        masked = np.ma.masked_invalid(z)
        im = ax.pcolormesh(alphas, betas, masked, shading="auto", cmap=cmap, vmin=vmin, vmax=vmax)
        fig.colorbar(im, ax=ax, label=title)
        ax.plot(hopf_a, hopf_b, color="white", lw=1.2, label="Hopf α=0")
        ax.plot(sn_a, sn_b, color="cyan", lw=1.2, label="saddle-node")
        b_rest = np.linspace(-0.3, 0.249, 80)
        ax.plot(
            [onset_from_rest_alpha(b) for b in b_rest],
            b_rest,
            color="magenta",
            lw=1.2,
            ls="--",
            label="onset from rest",
        )
        ax.set_xlim(-0.1, 0.5)
        ax.set_ylim(-0.3, 1.0)
        ax.set_xlabel("α (pressure unfolding)")
        ax.set_ylabel("β (tension)")
        ax.set_title(title + f"  γ={GAMMA:.0f}, {RATE/1000:.0f} kHz")
        ax.legend(loc="upper right", fontsize=8, facecolor="#222", labelcolor="white")
        fig.tight_layout()
        fig.savefig(out_dir / fname)
        plt.close(fig)

    _heatmap(f0, "f₀ (Hz)", "regime_f0.png", "magma", vmin=0, vmax=4500)
    _heatmap(p2p, "peak-to-peak", "regime_p2p.png", "viridis")
    _heatmap(sci, "SCI = centroid / f₀", "regime_sci.png", "plasma", vmin=1.0, vmax=2.5)
    np.savez(out_dir / "regime_maps.npz", alphas=alphas, betas=betas, f0=f0, p2p=p2p, sci=sci)
    return {"alphas": alphas, "betas": betas, "f0": f0, "p2p": p2p, "sci": sci}


def ramp_table(out_dir: Path) -> list[dict[str, float]]:
    betas = [-0.10, -0.05, 0.05, 0.15, 0.20, 0.30, 0.50]
    rows = [_onset_offset_one(b) for b in betas]
    (out_dir / "onset_offset.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")

    fig, ax = plt.subplots(figsize=(7.0, 4.2), dpi=120)
    b = [r["beta"] for r in rows]
    ax.plot(b, [r["analytic_onset"] for r in rows], "k--", lw=1.2, label="analytic onset from rest")
    ax.plot(b, [r["onset_alpha"] for r in rows], "o-", color="#e8a54b", label="simulated onset")
    ax.plot(b, [r["offset_alpha"] for r in rows], "s-", color="#6cb6ff", label="simulated offset")
    ax.set_xlabel("β")
    ax.set_ylabel("α")
    ax.set_title("Hysteresis: onset vs offset (ramp 0.2 s⁻¹)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "hysteresis.png")
    plt.close(fig)
    return rows


def f0_vs_beta(out_dir: Path) -> None:
    cc = _cc()
    betas = np.linspace(-0.05, 0.8, 40)
    alphas = (0.05, 0.2, 0.4)
    n = int(round(0.25 * RATE))
    fig, ax = plt.subplots(figsize=(7.0, 4.2), dpi=120)
    for alpha in alphas:
        f0s = []
        for beta in betas:
            x0, y0 = cc.rest_state(-0.1, float(beta))
            a = np.full(n, alpha)
            b = np.full(n, beta)
            xs, _ = cc.source_only(a, b, GAMMA, RATE, float(x0), float(y0))
            f0, p2p, _sci = _f0_sci(np.asarray(xs), RATE)
            f0s.append(f0 if p2p > 0.05 else np.nan)
        ax.plot(betas, f0s, lw=1.4, label=f"α = {alpha}")
    hopf = GAMMA * np.sqrt(np.clip(betas, 0, None)) / (2.0 * np.pi)
    hopf[betas < 0] = np.nan
    ax.plot(betas, hopf, "k--", lw=1.0, label=r"$\gamma\sqrt{\beta}/2\pi$")
    ax.set_xlabel("β")
    ax.set_ylabel("f₀ (Hz)")
    ax.set_title("Pitch vs tension (from rest, then step α)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "f0_vs_beta.png")
    plt.close(fig)


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path, default=Path("listening/B1/figs"))
    p.add_argument("--workers", type=int, default=8)
    args = p.parse_args(argv)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.perf_counter()
    print(f"regime maps → {out}", flush=True)
    regime_maps(out, workers=args.workers)
    print("onset/offset ramps", flush=True)
    ramp_table(out)
    print("f0 vs beta", flush=True)
    f0_vs_beta(out)
    elapsed = time.perf_counter() - t0
    print(f"BIFURCATION OK in {elapsed:.1f}s → {out}")


if __name__ == "__main__":
    # Re-import under the package name so ProcessPoolExecutor (spawn) can pickle workers.
    from cuicui.analysis.bifurcation import main as _main

    _main()
