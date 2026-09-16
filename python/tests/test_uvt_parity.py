from __future__ import annotations

import numpy as np
import pytest

from cuicui.reference.uvt import UpperVocalTract

try:
    import cuicui_core as cc
except ImportError as exc:  # pragma: no cover
    pytest.skip(f"cuicui_core is not built: {exc}", allow_module_level=True)

RATE = 192000.0
N = int(round(0.1 * RATE))


def _sets() -> list[tuple[float, float, float, float]]:
    return [
        (3000.0, 0.1, 0.5, 0.3),
        (1000.0, 0.2, 0.2, 0.5),
        (6000.0, 0.05, 0.9, 0.1),
    ]


def test_uvt_parity_three_sets() -> None:
    rng = np.random.default_rng(11)
    x = rng.standard_normal(N).astype(np.float64)
    for f_h, zh, gape, zb in _sets():
        flow_c, dflow_c = cc.uvt_process(x, RATE, f_h, zh, gape, zb, 1.0)
        flow_c = np.asarray(flow_c, dtype=np.float64)
        dflow_c = np.asarray(dflow_c, dtype=np.float64)
        ref = UpperVocalTract(RATE)
        ref.set_params(f_h, zh, gape, zb, 1.0)
        flow_p, dflow_p = ref.process_block(x)
        peak_f = float(np.max(np.abs(flow_c)))
        peak_d = float(np.max(np.abs(dflow_c)))
        err_f = float(np.max(np.abs(flow_c - flow_p)))
        err_d = float(np.max(np.abs(dflow_c - dflow_p)))
        assert peak_f > 0.0 and peak_d > 0.0
        assert err_f < 1e-9 * peak_f, f"flow fH={f_h} err={err_f} peak={peak_f}"
        assert err_d < 1e-9 * peak_d, f"dflow fH={f_h} err={err_d} peak={peak_d}"
