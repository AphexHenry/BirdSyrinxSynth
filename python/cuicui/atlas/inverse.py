"""Inverse pitch map: (ν, tone, breath, physics) tables from the forward atlas."""

from __future__ import annotations

import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import numpy as np
from scipy.interpolate import PchipInterpolator

from cuicui.atlas.source_atlas import (
    ALPHA0,
    ALPHA1,
    ALPHA_STEP,
    BETA0,
    BETA1,
    BETA_STEP,
    GAMMA0,
    RATE,
    REGIME_PERIODIC,
    grid,
)

NU_MIN = 0.03
NU_MAX = 1.10
N_NU = 256
ONSET_DELTA = 0.01
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def nu_grid() -> np.ndarray:
    return np.logspace(np.log10(NU_MIN), np.log10(NU_MAX), N_NU, dtype=np.float64)


def nu_from_hz(f0: float, gamma: float) -> float:
    return float(2.0 * np.pi * f0 / gamma)


def hz_from_nu(nu: float, gamma: float) -> float:
    return float(nu * gamma / (2.0 * np.pi))


def midi_from_hz(f0: float) -> float:
    if f0 <= 0.0 or not np.isfinite(f0):
        return float("nan")
    return 69.0 + 12.0 * np.log2(f0 / 440.0)


def midi_note_name(midi: float) -> str:
    if not np.isfinite(midi):
        return "—"
    n = int(np.round(midi))
    pc = n % 12
    octv = n // 12 - 1
    return f"{NOTE_NAMES[pc]}{octv}"


def _longest_monotone_run(nu: np.ndarray, periodic: np.ndarray) -> np.ndarray:
    """Boolean mask on the β axis: longest increasing run of periodic ν."""
    n = int(nu.size)
    keep = np.zeros(n, dtype=bool)
    idx = np.nonzero(periodic & np.isfinite(nu))[0]
    if idx.size == 0:
        return keep
    if idx.size == 1:
        keep[idx[0]] = True
        return keep

    runs: list[np.ndarray] = []
    start = 0
    for k in range(1, idx.size):
        if nu[idx[k]] <= nu[idx[k - 1]] + 1e-12:
            runs.append(idx[start:k])
            start = k
    runs.append(idx[start:])
    best = max(runs, key=lambda r: (int(r.size), int(r[-1] - r[0]) if r.size else 0))
    keep[best] = True
    return keep


def _interp_on(alpha_on: np.ndarray, betas: np.ndarray, beta: float) -> float:
    if not np.isfinite(beta):
        return float("nan")
    return float(np.interp(beta, betas, alpha_on, left=np.nan, right=np.nan))


def _nan_median_mean(
    y: np.ndarray, median_k: int = 5, mean_k: int = 3
) -> tuple[np.ndarray, dict[str, float]]:
    y = np.asarray(y, dtype=np.float64)
    out = y.copy()
    r = median_k // 2
    for i in range(y.size):
        if not np.isfinite(y[i]):
            continue
        sl = y[max(0, i - r) : min(y.size, i + r + 1)]
        sl = sl[np.isfinite(sl)]
        if sl.size:
            out[i] = float(np.median(sl))
    r2 = mean_k // 2
    sm = out.copy()
    for i in range(out.size):
        if not np.isfinite(out[i]):
            continue
        sl = out[max(0, i - r2) : min(out.size, i + r2 + 1)]
        sl = sl[np.isfinite(sl)]
        if sl.size:
            sm[i] = float(np.mean(sl))
    valid = np.isfinite(y) & np.isfinite(sm)
    delta = np.abs(sm[valid] - y[valid]) if np.count_nonzero(valid) else np.array([])
    stats = {
        "max_abs": float(np.max(delta)) if delta.size else 0.0,
        "mean_abs": float(np.mean(delta)) if delta.size else 0.0,
        "n": int(delta.size),
    }
    return sm, stats


def build_inverse(atlas: dict[str, np.ndarray]) -> dict[str, Any]:
    alphas = np.asarray(atlas["alphas"], dtype=np.float64)
    betas = np.asarray(atlas["betas"], dtype=np.float64)
    nu_map = np.asarray(atlas["nu_rest"], dtype=np.float64)
    sci_map = np.asarray(atlas["sci_rest"], dtype=np.float64)
    regime = np.asarray(atlas["regime_rest"], dtype=np.float64)
    alpha_on = np.asarray(atlas["alpha_on"], dtype=np.float64)
    n_a = int(alphas.size)
    n_b = int(betas.size)
    nus = nu_grid()
    beta_of = np.full((n_a, N_NU), np.nan, dtype=np.float64)
    sci_of = np.full((n_a, N_NU), np.nan, dtype=np.float64)

    n_periodic = 0
    n_kept = 0
    n_rows_used = 0
    for ia in range(n_a):
        periodic = regime[:, ia] == REGIME_PERIODIC
        n_periodic += int(np.count_nonzero(periodic & np.isfinite(nu_map[:, ia])))
        keep = _longest_monotone_run(nu_map[:, ia], periodic)
        n_kept += int(np.count_nonzero(keep))
        idx = np.nonzero(keep)[0]
        if idx.size < 2:
            continue
        nu_s = nu_map[idx, ia]
        b_s = betas[idx]
        sci_s = sci_map[idx, ia]
        # Strict increase for PCHIP.
        order = np.argsort(nu_s)
        nu_s = nu_s[order]
        b_s = b_s[order]
        sci_s = sci_s[order]
        uniq = np.concatenate([[True], np.diff(nu_s) > 1e-12])
        nu_s, b_s, sci_s = nu_s[uniq], b_s[uniq], sci_s[uniq]
        # Drop the SNIC-edge (lowest-ν) sample: dν/dβ is huge there and PCHIP
        # on a 0.005 grid is several cents off (01_research.md §7.4 pitfall).
        if nu_s.size > 3:
            nu_s, b_s, sci_s = nu_s[1:], b_s[1:], sci_s[1:]
        if nu_s.size < 2:
            continue
        n_rows_used += 1
        interpolator = PchipInterpolator(nu_s, b_s, extrapolate=False)
        beta_of[ia, :] = interpolator(nus)
        sci_ok = np.isfinite(sci_s)
        if int(np.count_nonzero(sci_ok)) >= 2:
            sci_i = PchipInterpolator(nu_s[sci_ok], sci_s[sci_ok], extrapolate=False)
            sci_of[ia, :] = sci_i(nus)

    # Fill interior NaNs along α so bilinear lookup is defined on [α_lo, α_hi].
    for j in range(N_NU):
        col = beta_of[:, j]
        valid = np.isfinite(col)
        if int(np.count_nonzero(valid)) < 2:
            continue
        idx = np.nonzero(valid)[0]
        i0, i1 = int(idx[0]), int(idx[-1])
        hole = ~valid
        hole[:i0] = False
        hole[i1 + 1 :] = False
        if np.any(hole):
            beta_of[hole, j] = np.interp(alphas[hole], alphas[idx], col[idx])
        col_s = sci_of[:, j]
        valid_s = np.isfinite(col_s)
        if int(np.count_nonzero(valid_s)) >= 2:
            idxs = np.nonzero(valid_s)[0]
            s0, s1 = int(idxs[0]), int(idxs[-1])
            hs = ~valid_s
            hs[:s0] = False
            hs[s1 + 1 :] = False
            if np.any(hs):
                sci_of[hs, j] = np.interp(alphas[hs], alphas[idxs], col_s[idxs])

    frac_viol = 0.0
    if n_periodic > 0:
        frac_viol = float(n_periodic - n_kept) / float(n_periodic)

    alpha_lo = np.full(N_NU, np.nan, dtype=np.float64)
    alpha_hi = np.full(N_NU, np.nan, dtype=np.float64)
    for j in range(N_NU):
        cand_lo: list[float] = []
        cand_hi: list[float] = []
        for ia in range(n_a):
            b = beta_of[ia, j]
            if not np.isfinite(b) or b > BETA1 + 1e-9:
                continue
            a = float(alphas[ia])
            on = _interp_on(alpha_on, betas, float(b))
            if np.isfinite(on) and a + 1e-12 < on + ONSET_DELTA:
                continue
            cand_lo.append(a)
            # Keep tone=1 off the SNIC-edge β grid (dν/dβ is huge there).
            if b >= BETA0 + 0.04 - 1e-12:
                cand_hi.append(a)
        if cand_lo:
            alpha_lo[j] = min(cand_lo)
            if cand_hi:
                alpha_hi[j] = min(ALPHA1, max(cand_hi))
            else:
                alpha_hi[j] = min(ALPHA1, max(cand_lo))

    lo_s, lo_stats = _nan_median_mean(alpha_lo)
    hi_s, hi_stats = _nan_median_mean(alpha_hi)
    # Keep lo <= hi.
    swap = np.isfinite(lo_s) & np.isfinite(hi_s) & (lo_s > hi_s)
    lo_s[swap], hi_s[swap] = hi_s[swap], lo_s[swap]

    reachable = np.isfinite(lo_s) & np.isfinite(hi_s) & (lo_s <= hi_s)
    # Lowest ν bins sit on the SNIC/onset floor where PCHIP on a 0.005 β-grid
    # is several–hundred cents off (01_research.md §7.4). Drop them.
    idx_r = np.nonzero(reachable)[0]
    n_drop = min(4, max(0, int(idx_r.size) - 16))
    if n_drop:
        reachable[idx_r[:n_drop]] = False
        lo_s[idx_r[:n_drop]] = np.nan
        hi_s[idx_r[:n_drop]] = np.nan
    nu_min = float(nus[reachable][0]) if np.any(reachable) else float("nan")
    nu_max = float(nus[reachable][-1]) if np.any(reachable) else float("nan")

    def _nu_max_tone(tone: float) -> float:
        last = float("nan")
        for j in range(N_NU):
            if not reachable[j]:
                continue
            a_t = float(lo_s[j] + tone * (hi_s[j] - lo_s[j]))
            ia = (a_t - ALPHA0) / ALPHA_STEP
            i0 = int(np.floor(ia))
            i1 = min(i0 + 1, n_a - 1)
            i0 = max(0, min(i0, n_a - 1))
            t = ia - i0
            b0, b1 = beta_of[i0, j], beta_of[i1, j]
            ok = (np.isfinite(b0) and t < 1e-9) or (np.isfinite(b1) and t > 1 - 1e-9) or (
                np.isfinite(b0) and np.isfinite(b1)
            )
            if ok:
                last = float(nus[j])
        return last

    return {
        "nu": nus,
        "beta_of_alpha_nu": beta_of,
        "sci_of_alpha_nu": sci_of,
        "alpha_lo_of_nu": lo_s,
        "alpha_hi_of_nu": hi_s,
        "alpha_lo_raw": alpha_lo,
        "alpha_hi_raw": alpha_hi,
        "monotonicity": {
            "periodic_cells": int(n_periodic),
            "kept_cells": int(n_kept),
            "violations": int(n_periodic - n_kept),
            "fraction": frac_viol,
            "rows_used": int(n_rows_used),
            "n_alpha": n_a,
            "n_beta": n_b,
        },
        "smoothing": {"alpha_lo": lo_stats, "alpha_hi": hi_stats},
        "nu_min": nu_min,
        "nu_max": nu_max,
        "nu_max_tone0": _nu_max_tone(0.0),
        "nu_max_tone1": _nu_max_tone(1.0),
    }


def playable_range_table(inv: dict[str, Any], gamma0: float = GAMMA0) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    nu_min = float(inv["nu_min"])
    for sigma in (0.25, 0.5, 1.0, 2.0, 4.0):
        gamma = gamma0 / sigma
        for tone, key in ((0.0, "nu_max_tone0"), (0.5, "nu_max"), (1.0, "nu_max_tone1")):
            nu_max = float(inv[key] if tone != 0.5 else inv["nu_max"])
            if tone == 0.5:
                nu_max = 0.5 * (float(inv["nu_max_tone0"]) + float(inv["nu_max_tone1"]))
            f_lo = hz_from_nu(nu_min, gamma)
            f_hi = hz_from_nu(nu_max, gamma)
            rows.append(
                {
                    "sigma": sigma,
                    "tone": tone,
                    "f_lo_hz": f_lo,
                    "f_hi_hz": f_hi,
                    "note_lo": midi_note_name(midi_from_hz(f_lo)),
                    "note_hi": midi_note_name(midi_from_hz(f_hi)),
                    "label": (
                        f"{midi_note_name(midi_from_hz(f_lo))} – "
                        f"{midi_note_name(midi_from_hz(f_hi))}"
                    ),
                }
            )
    return rows


def _git_hash(root: Path) -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(root),
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return out.strip() or "nogit"
    except (OSError, subprocess.CalledProcessError):
        return "nogit"


def write_bundle(
    atlas: dict[str, np.ndarray],
    inv: dict[str, Any],
    out_dir: Path,
    root: Path,
    elapsed_s: float,
) -> dict[str, Any]:
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    fields: list[dict[str, Any]] = [
        {"name": "nu", "shape": list(atlas["nu_rest"].shape), "dtype": "float32"},
        {"name": "amp", "shape": list(atlas["amp_rest"].shape), "dtype": "float32"},
        {"name": "sci", "shape": list(atlas["sci_rest"].shape), "dtype": "float32"},
        {"name": "regime", "shape": list(atlas["regime_rest"].shape), "dtype": "float32"},
        {"name": "nu_osc", "shape": list(atlas["nu_osc"].shape), "dtype": "float32"},
        {"name": "amp_osc", "shape": list(atlas["amp_osc"].shape), "dtype": "float32"},
        {"name": "sci_osc", "shape": list(atlas["sci_osc"].shape), "dtype": "float32"},
        {"name": "regime_osc", "shape": list(atlas["regime_osc"].shape), "dtype": "float32"},
        {"name": "alpha_on", "shape": list(atlas["alpha_on"].shape), "dtype": "float32"},
        {"name": "alpha_off", "shape": list(atlas["alpha_off"].shape), "dtype": "float32"},
        {
            "name": "beta_of_alpha_nu",
            "shape": list(inv["beta_of_alpha_nu"].shape),
            "dtype": "float32",
        },
        {
            "name": "sci_of_alpha_nu",
            "shape": list(inv["sci_of_alpha_nu"].shape),
            "dtype": "float32",
        },
        {"name": "alpha_lo_of_nu", "shape": list(inv["alpha_lo_of_nu"].shape), "dtype": "float32"},
        {"name": "alpha_hi_of_nu", "shape": list(inv["alpha_hi_of_nu"].shape), "dtype": "float32"},
        {"name": "nu_grid", "shape": list(inv["nu"].shape), "dtype": "float32"},
    ]
    arrays = {
        "nu": atlas["nu_rest"],
        "amp": atlas["amp_rest"],
        "sci": atlas["sci_rest"],
        "regime": atlas["regime_rest"],
        "nu_osc": atlas["nu_osc"],
        "amp_osc": atlas["amp_osc"],
        "sci_osc": atlas["sci_osc"],
        "regime_osc": atlas["regime_osc"],
        "alpha_on": atlas["alpha_on"],
        "alpha_off": atlas["alpha_off"],
        "beta_of_alpha_nu": inv["beta_of_alpha_nu"],
        "sci_of_alpha_nu": inv["sci_of_alpha_nu"],
        "alpha_lo_of_nu": inv["alpha_lo_of_nu"],
        "alpha_hi_of_nu": inv["alpha_hi_of_nu"],
        "nu_grid": inv["nu"],
    }
    bin_path = out_dir / "source_atlas_v1.bin"
    with bin_path.open("wb") as f:
        for spec in fields:
            arr = np.asarray(arrays[spec["name"]], dtype=np.float32)
            if list(arr.shape) != spec["shape"]:
                raise ValueError(f"shape mismatch {spec['name']}: {arr.shape} vs {spec['shape']}")
            f.write(arr.tobytes(order="C"))
    meta = {
        "format": "cuicui.source_atlas",
        "version": 1,
        "gamma0": GAMMA0,
        "internal_rate": RATE,
        "integration": "RK4 source_only, noise off",
        "alpha": {
            "min": ALPHA0,
            "max": ALPHA1,
            "step": ALPHA_STEP,
            "n": int(grid(ALPHA0, ALPHA1, ALPHA_STEP).size),
        },
        "beta": {
            "min": BETA0,
            "max": BETA1,
            "step": BETA_STEP,
            "n": int(grid(BETA0, BETA1, BETA_STEP).size),
        },
        "nu": {"min": NU_MIN, "max": NU_MAX, "n": N_NU, "spacing": "log"},
        "protocols": ["from_rest", "from_osc"],
        "fields": fields,
        "generation": {
            "date": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "git": _git_hash(root),
            "seconds": elapsed_s,
        },
        "monotonicity": inv["monotonicity"],
        "smoothing": inv["smoothing"],
        "nu_min": inv["nu_min"],
        "nu_max": inv["nu_max"],
        "nu_max_tone0": inv["nu_max_tone0"],
        "nu_max_tone1": inv["nu_max_tone1"],
        "playable_range": playable_range_table(inv),
        "bin": "source_atlas_v1.bin",
        "bin_bytes": int(bin_path.stat().st_size),
    }
    json_path = out_dir / "source_atlas_v1.json"
    json_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    return meta


def load_atlas_bundle(out_dir: Path) -> tuple[dict[str, Any], dict[str, np.ndarray]]:
    out_dir = Path(out_dir)
    meta = json.loads((out_dir / "source_atlas_v1.json").read_text(encoding="utf-8"))
    blob = (out_dir / meta["bin"]).read_bytes()
    offset = 0
    arrays: dict[str, np.ndarray] = {}
    for spec in meta["fields"]:
        n = int(np.prod(spec["shape"]))
        nbytes = n * 4
        chunk = blob[offset : offset + nbytes]
        if len(chunk) != nbytes:
            raise ValueError(f"truncated atlas field {spec['name']}")
        arr = np.frombuffer(chunk, dtype="<f4", count=n).reshape(spec["shape"]).astype(np.float64)
        arrays[spec["name"]] = arr
        offset += nbytes
    if offset != len(blob):
        raise ValueError(f"atlas bin leftover {len(blob) - offset} bytes")
    return meta, arrays


def plot_inverse(inv: dict[str, Any], out_png: Path) -> None:
    import os

    os.environ.setdefault("MPLCONFIGDIR", str(out_png.parent / ".mplconfig"))
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    betas = inv["beta_of_alpha_nu"]
    nus = inv["nu"]
    alphas = grid(ALPHA0, ALPHA1, ALPHA_STEP)
    fig, ax = plt.subplots(figsize=(7.4, 5.6), dpi=120)
    masked = np.ma.masked_invalid(betas)
    # x = log ν, y = α
    nu_edges = np.concatenate(
        [
            [nus[0] * (nus[0] / nus[1]) ** 0.5],
            np.sqrt(nus[:-1] * nus[1:]),
            [nus[-1] * (nus[-1] / nus[-2]) ** 0.5],
        ]
    )
    a_edges = np.concatenate([[alphas[0] - ALPHA_STEP * 0.5], alphas + ALPHA_STEP * 0.5])
    im = ax.pcolormesh(nu_edges, a_edges, masked, shading="auto", cmap="viridis")
    fig.colorbar(im, ax=ax, label="β")
    ax.plot(nus, inv["alpha_lo_of_nu"], color="white", lw=1.3, label=r"$\alpha_{lo}$")
    ax.plot(nus, inv["alpha_hi_of_nu"], color="yellow", lw=1.3, label=r"$\alpha_{hi}$")
    ax.set_xscale("log")
    ax.set_xlabel("ν = 2π f₀ / γ")
    ax.set_ylabel("α")
    ax.set_title("Inverse table β(α, ν)")
    ax.legend(loc="upper left", fontsize=8)
    fig.tight_layout()
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png)
    plt.close(fig)


def fmt_float(v: float) -> str:
    if not np.isfinite(v):
        return "kAtlasNaN"
    s = f"{float(v):.9g}"
    if "e" in s.lower():
        return s + "f"
    if "." not in s:
        s += ".0"
    return s + "f"


def write_header(meta: dict[str, Any], arrays: dict[str, np.ndarray], path: Path) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lo = arrays["alpha_lo_of_nu"].astype(np.float32)
    hi = arrays["alpha_hi_of_nu"].astype(np.float32)
    beta = arrays["beta_of_alpha_nu"].astype(np.float32)
    nu = arrays["nu_grid"].astype(np.float32)
    n_a, n_nu = beta.shape
    lines = [
        "#pragma once",
        "// Generated by scripts/gen_atlas_header.py — do not edit.",
        "#include <array>",
        "#include <bit>",
        "#include <cstdint>",
        "",
        "namespace cuicui::generated {",
        "",
        "inline constexpr float kAtlasNaN = std::bit_cast<float>(std::uint32_t{0x7fc00000u});",
        f"inline constexpr int kAtlasNAlpha = {n_a};",
        f"inline constexpr int kAtlasNNu = {n_nu};",
        f"inline constexpr float kAtlasAlpha0 = {fmt_float(ALPHA0)};",
        f"inline constexpr float kAtlasAlphaStep = {fmt_float(ALPHA_STEP)};",
        f"inline constexpr float kAtlasBeta0 = {fmt_float(BETA0)};",
        f"inline constexpr float kAtlasBetaStep = {fmt_float(BETA_STEP)};",
        f"inline constexpr float kAtlasGamma0 = {fmt_float(GAMMA0)};",
        f"inline constexpr float kAtlasNuMinGrid = {fmt_float(NU_MIN)};",
        f"inline constexpr float kAtlasNuMaxGrid = {fmt_float(NU_MAX)};",
        f"inline constexpr float kAtlasNuMinReachable = {fmt_float(float(meta['nu_min']))};",
        f"inline constexpr float kAtlasNuMaxReachable = {fmt_float(float(meta['nu_max']))};",
        f"inline constexpr float kAtlasNuMaxTone0 = {fmt_float(float(meta['nu_max_tone0']))};",
        f"inline constexpr float kAtlasNuMaxTone1 = {fmt_float(float(meta['nu_max_tone1']))};",
        "",
    ]

    def dump_array(name: str, data: np.ndarray, decl_n: str) -> None:
        flat = np.asarray(data, dtype=np.float32).ravel(order="C")
        lines.append(f"inline constexpr std::array<float, {decl_n}> {name}{{{{")
        row: list[str] = []
        for i, v in enumerate(flat):
            row.append(fmt_float(float(v)))
            if len(row) == 8 or i + 1 == flat.size:
                lines.append("    " + ", ".join(row) + ("," if i + 1 != flat.size else ""))
                row = []
        lines.append("}};")
        lines.append("")

    dump_array("kAtlasNu", nu, "kAtlasNNu")
    dump_array("kAtlasAlphaLo", lo, "kAtlasNNu")
    dump_array("kAtlasAlphaHi", hi, "kAtlasNNu")
    dump_array("kAtlasBetaOfAlphaNu", beta, "kAtlasNAlpha * kAtlasNNu")
    lines.append("} // namespace cuicui::generated")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
