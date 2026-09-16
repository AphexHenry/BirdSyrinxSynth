"""Radiated UVT responses: peak frequency / Q table and family plots.

Run: uv run python -m cuicui.analysis.uvt_response --out listening/B3/figs
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(__file__).resolve().parents[3] / "listening" / "B3" / ".mplconfig")
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from cuicui.reference.uvt import h_radiation, physical

RATE = 192000.0
DURATION_S = 0.5
F_H_LIST = (1000.0, 2000.0, 3000.0, 4500.0, 6000.0, 8000.0)
GAPE_LIST = (0.2, 0.5, 0.9)
DAMP_H = 0.1
DAMP_B = 0.3


def _cc():
    import cuicui_core as cc

    return cc


def impulse_radiation(f_h: float, gape: float) -> np.ndarray:
    n = int(round(DURATION_S * RATE))
    x = np.zeros(n, dtype=np.float64)
    x[0] = 1.0
    _flow, dflow = _cc().uvt_process(x, RATE, f_h, DAMP_H, gape, DAMP_B, 1.0)
    return np.asarray(dflow, dtype=np.float64)


def measured_mag(h: np.ndarray, freqs: np.ndarray) -> np.ndarray:
    spec = np.fft.rfft(h)
    f_bin = np.fft.rfftfreq(h.shape[0], 1.0 / RATE)
    return np.interp(freqs, f_bin, np.abs(spec))


def peak_and_q(freqs: np.ndarray, mag: np.ndarray) -> tuple[float, float]:
    i = int(np.argmax(mag))
    peak_f = float(freqs[i])
    peak = float(mag[i])
    thr = peak / np.sqrt(2.0)
    lo = i
    while lo > 0 and mag[lo] >= thr:
        lo -= 1
    hi = i
    while hi < mag.shape[0] - 1 and mag[hi] >= thr:
        hi += 1
    bw = float(freqs[hi] - freqs[lo])
    q = peak_f / bw if bw > 1.0 else float("nan")
    return peak_f, q


def main() -> None:
    parser = argparse.ArgumentParser(description="UVT radiated response family")
    parser.add_argument("--out", type=Path, default=Path("listening/B3/figs"))
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    freqs = np.linspace(50.0, 12000.0, 4000)
    rows: list[dict[str, float]] = []
    fig, axes = plt.subplots(2, 3, figsize=(11.5, 7.0), sharex=True)
    axes_f = axes.ravel()
    worst_pct = 0.0
    for ax, f_h in zip(axes_f, F_H_LIST, strict=True):
        for gape, color in zip(GAPE_LIST, ("#4c78a8", "#f58518", "#54a24b"), strict=True):
            h = impulse_radiation(f_h, gape)
            hm = measured_mag(h, freqs)
            ha = np.abs(h_radiation(1j * 2.0 * np.pi * freqs, physical(f_h, DAMP_H, gape, DAMP_B)))
            peak_f, q = peak_and_q(freqs, hm)
            err_pct = 100.0 * abs(peak_f - f_h) / f_h
            worst_pct = max(worst_pct, err_pct)
            p = physical(f_h, DAMP_H, gape, DAMP_B)
            rows.append(
                {
                    "f_H": f_h,
                    "gape": gape,
                    "peak_hz": peak_f,
                    "Q": q,
                    "err_pct": err_pct,
                    "volume_cm3": p["volume_cm3"],
                }
            )
            mag_m = 20.0 * np.log10(np.maximum(hm, 1e-18))
            mag_a = 20.0 * np.log10(np.maximum(ha, 1e-18))
            ax.plot(freqs / 1000.0, mag_m, color=color, lw=1.2, label=f"g={gape}")
            ax.plot(freqs / 1000.0, mag_a, color=color, lw=0.6, ls=":", alpha=0.7)
        ax.axvline(f_h / 1000.0, color="#888", ls="--", lw=0.8)
        ax.set_title(f"f_H = {int(f_h)} Hz")
        ax.set_xlim(0.0, 12.0)
        ax.grid(True, alpha=0.3)
        if f_h == F_H_LIST[0]:
            ax.legend(fontsize=8)
    axes[1, 0].set_xlabel("kHz")
    axes[1, 1].set_xlabel("kHz")
    axes[1, 2].set_xlabel("kHz")
    axes[0, 0].set_ylabel("dB (radiation)")
    axes[1, 0].set_ylabel("dB (radiation)")
    fig.suptitle("Radiated UVT response (solid measured, dotted analytic)")
    fig.tight_layout()
    fig.savefig(out / "uvt_radiated_family.png", dpi=140)
    plt.close(fig)

    summary = {"worst_peak_err_pct": worst_pct, "rows": rows}
    (out / "uvt_peaks.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    # Compact peak table figure
    fig2, ax2 = plt.subplots(figsize=(8.5, 3.8))
    ax2.axis("off")
    table_data = [["f_H", "gape", "peak Hz", "Q", "err %", "V cm³"]]
    for r in rows:
        table_data.append(
            [
                f"{r['f_H']:.0f}",
                f"{r['gape']:.1f}",
                f"{r['peak_hz']:.0f}",
                f"{r['Q']:.2f}",
                f"{r['err_pct']:.2f}",
                f"{r['volume_cm3']:.3f}",
            ]
        )
    tab = ax2.table(cellText=table_data, loc="center", cellLoc="center")
    tab.auto_set_font_size(False)
    tab.set_fontsize(8)
    tab.scale(1.0, 1.15)
    ax2.set_title("Radiated peak frequency and −3 dB Q (default dampings)")
    fig2.tight_layout()
    fig2.savefig(out / "uvt_peak_table.png", dpi=140, bbox_inches="tight")
    plt.close(fig2)
    print(f"worst peak error {worst_pct:.3f} %  -> {out}")


if __name__ == "__main__":
    main()
