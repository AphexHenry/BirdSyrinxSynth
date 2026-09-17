from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from cuicui.atlas.inverse import load_atlas_bundle, midi_from_hz, midi_note_name, nu_from_hz
from cuicui.atlas.source_atlas import (
    ALPHA0,
    ALPHA_STEP,
    BETA0,
    BETA_STEP,
    GAMMA0,
    analytic_onset_from_rest,
)

ROOT = Path(__file__).resolve().parents[2]
ATLAS = ROOT / "assets" / "atlas"


def _import_core():
    try:
        import cuicui_core as cc
    except ImportError as exc:  # pragma: no cover
        pytest.skip(f"cuicui_core is not built: {exc}")
    return cc


def test_atlas_files_exist_and_small() -> None:
    j = ATLAS / "source_atlas_v1.json"
    b = ATLAS / "source_atlas_v1.bin"
    h = ROOT / "core" / "include" / "cuicui" / "generated" / "atlas_tables.hpp"
    assert j.is_file()
    assert b.is_file()
    assert h.is_file()
    assert b.stat().st_size <= 5 * 1024 * 1024
    assert h.stat().st_size <= 5 * 1024 * 1024
    meta = json.loads(j.read_text(encoding="utf-8"))
    assert meta["bin_bytes"] == b.stat().st_size
    assert meta["monotonicity"]["fraction"] < 0.02


def test_b1_spot_points_within_0_5_percent() -> None:
    meta, arrays = load_atlas_bundle(ATLAS)
    del meta
    nu = arrays["nu"]
    spots = [
        (0.10, 0.50, 3098.0),
        (0.20, 0.20, 2658.0),
        (0.30, 0.10, 2817.0),
        (0.15, 0.05, 1380.0),
        (0.40, 0.35, 3797.0),
        (0.25, -0.05, 1740.0),
    ]
    for alpha, beta, f0 in spots:
        ia = int(round((alpha - ALPHA0) / ALPHA_STEP))
        ib = int(round((beta - BETA0) / BETA_STEP))
        nu_a = float(nu[ib, ia])
        f_a = nu_a * GAMMA0 / (2.0 * np.pi)
        assert abs(f_a - f0) / f0 < 0.005


def test_onset_matches_analytic() -> None:
    _meta, arrays = load_atlas_bundle(ATLAS)
    on = arrays["alpha_on"]
    n_beta = on.size
    betas = BETA0 + np.arange(n_beta) * BETA_STEP
    mask = betas < 0.25
    analytic = np.array([analytic_onset_from_rest(float(b)) for b in betas], dtype=np.float64)
    err = np.abs(on[mask] - analytic[mask])
    err = err[np.isfinite(err)]
    assert err.size > 50
    assert float(np.max(err)) <= 0.01 + 1e-9


def test_inverse_solve_folding() -> None:
    cc = _import_core()
    _nmin, nmax = cc.inverse_nu_range(0.6)
    r = cc.inverse_solve(2.0 * nmax, 0.6, 0.2, 0.0, True)
    assert r["folded"] is True
    assert r["clamped"] is False
    assert r["nu_used"] == pytest.approx(nmax, rel=1e-9)


def test_range_fold_param_registered() -> None:
    cc = _import_core()
    ids = {p["id"] for p in cc.param_specs()}
    assert "perf.range_fold" in ids
    spec = next(p for p in cc.param_specs() if p["id"] == "perf.range_fold")
    assert spec["default"] == 1.0
    assert spec["layer"] == "Performance"


def test_playable_range_note_names() -> None:
    meta = json.loads((ATLAS / "source_atlas_v1.json").read_text(encoding="utf-8"))
    rows = meta["playable_range"]
    assert len(rows) == 15
    sigma1 = next(r for r in rows if r["sigma"] == 1.0 and r["tone"] == 0.5)
    assert sigma1["note_lo"]
    assert sigma1["note_hi"]
    # Sanity: names match the frequencies.
    assert midi_note_name(midi_from_hz(sigma1["f_lo_hz"])) == sigma1["note_lo"]


def test_closed_loop_python_spot() -> None:
    cc = _import_core()
    gamma = 24000.0
    f_ref = 2000.0
    nu = nu_from_hz(f_ref, gamma)
    sol = cc.inverse_solve(nu, 0.6, 0.2, 0.0, False)
    assert sol["reachable"] is True
    n = 48000 // 4
    p = float((sol["alpha"] + 0.10) / 0.55) if sol["alpha"] <= 0.45 else 1.0
    off = 0.0 if sol["alpha"] <= 0.45 else float(sol["alpha"] - 0.45)
    settings = {"body.size": 1.0, "noise.aspiration": 0.0}
    controls = {
        "resp.pressure": np.full(n, p, dtype=np.float32),
        "src.L.beta": np.full(n, sol["beta"], dtype=np.float32),
        "src.L.gate": np.ones(n, dtype=np.float32),
        "src.R.gate": np.zeros(n, dtype=np.float32),
        "src.L.alpha_offset": np.full(n, off, dtype=np.float32),
        "noise.beta_sigma": np.zeros(n, dtype=np.float32),
    }
    y, _state = cc.render_controls(settings, controls, 48000.0, 48000.0, 1, 0)
    y = np.asarray(y, dtype=np.float64)
    skip = int(0.08 * 48000)
    sl = y[skip:] - np.mean(y[skip:])
    nfft = 1 << int(np.ceil(np.log2(max(int(sl.size), 4096))))
    nfft = min(nfft, 16384)
    w = np.hanning(sl.size)
    spec = np.abs(np.fft.rfft(sl * w, n=nfft))
    freqs = np.fft.rfftfreq(nfft, d=1.0 / 48000.0)
    mask = (freqs >= f_ref * 0.82) & (freqs <= f_ref * 1.22)
    k = int(np.nonzero(mask)[0][int(np.argmax(spec[mask]))])
    a, b, c = float(spec[k - 1]), float(spec[k]), float(spec[k + 1])
    denom = a - 2.0 * b + c
    delta = 0.5 * (a - c) / denom if abs(denom) > 1e-18 else 0.0
    f0 = (k + float(np.clip(delta, -0.5, 0.5))) * 48000.0 / nfft
    cents = 1200.0 * np.log2(f0 / f_ref)
    assert abs(cents) <= 5.0
