from __future__ import annotations

import numpy as np
import pytest

from cuicui.reference.trachea import Trachea

try:
    import cuicui_core as cc
except ImportError as exc:  # pragma: no cover
    pytest.skip(f"cuicui_core is not built: {exc}", allow_module_level=True)

RATE = 192000.0
N = int(round(0.1 * RATE))


def _sets() -> list[tuple[float, float, float, float]]:
    # length_mm, reflection, loss, input_gain
    return [
        (33.0, 0.5, 0.0, 1.0),
        (20.0, -0.5, 0.3, 1.0),
        (80.0, 0.9, 1.0, 0.8),
    ]


def test_trachea_parity_three_sets() -> None:
    rng = np.random.default_rng(3)
    x = rng.standard_normal(N).astype(np.float64)
    for length_mm, r, loss, gain in _sets():
        y_cpp = np.asarray(
            cc.trachea_process(x, RATE, length_mm, r, loss, gain), dtype=np.float64
        )
        ref = Trachea(RATE)
        ref.set_params(length_mm, r, loss, gain)
        y_py = ref.process_block(x)
        peak = float(np.max(np.abs(y_cpp)))
        err = float(np.max(np.abs(y_cpp - y_py)))
        assert peak > 0.0
        assert err < 1e-9 * peak, f"L={length_mm} r={r} loss={loss} err={err} peak={peak}"
