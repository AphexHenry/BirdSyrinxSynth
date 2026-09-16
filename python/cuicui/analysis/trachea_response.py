"""Measured vs analytic magnitude responses of the tracheal waveguide.

Run: uv run python -m cuicui.analysis.trachea_response --out listening/B2/figs
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(__file__).resolve().parents[3] / "listening" / "B2" / ".mplconfig")
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from cuicui.reference.trachea import SOUND_SPEED, analytic_magnitude

RATE = 192000.0
DURATION_S = 1.0
CASES = (
    (0.5, "r = +0.5 (quarter-wave)"),
    (-0.5, "r = −0.5 (half-wave)"),
    (0.9, "r = +0.9"),
    (-0.9, "r = −0.9"),
)
LENGTH_MM = 33.0


def _cc():
    import cuicui_core as cc

    return cc


def impulse_response(length_mm: float, reflection: float, loss: float = 0.0) -> np.ndarray:
    n = int(round(DURATION_S * RATE))
    x = np.zeros(n, dtype=np.float64)
    x[0] = 1.0
    cc = _cc()
    return np.asarray(
        cc.trachea_process(x, RATE, length_mm, reflection, loss, 1.0), dtype=np.float64
    )


def measured_magnitude(h: np.ndarray, freqs: np.ndarray) -> np.ndarray:
    spec = np.fft.rfft(h)
    f_bin = np.fft.rfftfreq(h.shape[0], 1.0 / RATE)
    mag = np.abs(spec)
    return np.interp(freqs, f_bin, mag)


def main() -> None:
    parser = argparse.ArgumentParser(description="Trachea measured vs analytic responses")
    parser.add_argument("--out", type=Path, default=Path("listening/B2/figs"))
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    freqs = np.linspace(50.0, 12000.0, 2000)
    fig, axes = plt.subplots(2, 2, figsize=(10.5, 7.2), sharex=True, sharey=True)
    summary: dict[str, dict[str, float]] = {}
    for ax, (r, title) in zip(axes.ravel(), CASES, strict=True):
        h = impulse_response(LENGTH_MM, r, 0.0)
        hm = measured_magnitude(h, freqs)
        ha = analytic_magnitude(freqs, LENGTH_MM, r, 1.0)
        mask = ha > 1e-3
        err = np.abs(20.0 * np.log10(np.maximum(hm[mask], 1e-18) / ha[mask]))
        worst = float(np.max(err)) if err.size else 0.0
        ax.plot(
            freqs / 1000.0,
            20.0 * np.log10(np.maximum(ha, 1e-12)),
            color="#333",
            lw=1.2,
            label="analytic",
        )
        ax.plot(
            freqs / 1000.0,
            20.0 * np.log10(np.maximum(hm, 1e-12)),
            color="#c45c26",
            lw=1.0,
            alpha=0.85,
            label="measured",
        )
        ax.set_title(title)
        ax.set_xlim(0.05, 12.0)
        ax.set_ylim(-25, 30)
        ax.grid(True, alpha=0.3)
        summary[f"r={r}"] = {"worst_err_db": worst, "length_mm": LENGTH_MM}
        if r == 0.5:
            ax.legend(loc="upper right", fontsize=8)

    for ax in axes[1, :]:
        ax.set_xlabel("Frequency (kHz)")
    for ax in axes[:, 0]:
        ax.set_ylabel("Magnitude (dB)")
    fig.suptitle(f"Trachea, L = {LENGTH_MM:.0f} mm, c = {SOUND_SPEED:.0f} m/s, ℓ = 0")
    fig.tight_layout()
    fig.savefig(out / "trachea_response.png", dpi=120)
    plt.close(fig)

    # Loss comparison at the ~8 kHz quarter-wave peak.
    h0 = impulse_response(LENGTH_MM, 0.5, 0.0)
    h1 = impulse_response(LENGTH_MM, 0.5, 1.0)
    f_hi = np.linspace(5000.0, 11000.0, 800)
    m0 = measured_magnitude(h0, f_hi)
    m1 = measured_magnitude(h1, f_hi)
    fig2, ax2 = plt.subplots(figsize=(7.2, 4.0))
    ax2.plot(f_hi / 1000.0, 20.0 * np.log10(np.maximum(m0, 1e-12)), label="ℓ = 0", color="#333")
    ax2.plot(f_hi / 1000.0, 20.0 * np.log10(np.maximum(m1, 1e-12)), label="ℓ = 1", color="#c45c26")
    ax2.set_xlabel("Frequency (kHz)")
    ax2.set_ylabel("Magnitude (dB)")
    ax2.set_title("Loss: 8 kHz peak, r = +0.5, L = 33 mm")
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    fig2.tight_layout()
    fig2.savefig(out / "trachea_loss.png", dpi=120)
    plt.close(fig2)

    (out / "trachea_response.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(f"wrote {out / 'trachea_response.png'} worst_err={summary}")


if __name__ == "__main__":
    main()
