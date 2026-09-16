from __future__ import annotations

import numpy as np

from cuicui.reference.uvt import circuit_h_uvt, physical, published_h_uvt


def test_published_vs_circuit_analytic() -> None:
    rng = np.random.default_rng(7)
    freqs = np.array(
        [
            50.0,
            80.0,
            120.0,
            200.0,
            350.0,
            500.0,
            800.0,
            1200.0,
            1800.0,
            2500.0,
            3000.0,
            4000.0,
            5000.0,
            6500.0,
            8000.0,
            9000.0,
            10000.0,
            11000.0,
            11500.0,
            12000.0,
        ]
    )
    for _ in range(5):
        f_h = float(rng.uniform(800.0, 8000.0))
        gape = float(rng.uniform(0.05, 0.95))
        zh = float(rng.uniform(0.03, 0.6))
        zb = float(rng.uniform(0.05, 0.8))
        p = physical(f_h, zh, gape, zb, 1.0)
        s = 1j * 2.0 * np.pi * freqs
        hc = circuit_h_uvt(s, p)
        hp = published_h_uvt(s, p)
        mag_c = np.abs(hc)
        mag_p = np.abs(hp)
        rel = np.abs(mag_p - mag_c) / np.maximum(mag_c, 1e-30)
        phase = np.angle(hp / hc)
        assert float(np.max(rel)) < 1e-9, f"mag rel {np.max(rel)} fH={f_h} g={gape}"
        assert float(np.max(np.abs(phase))) < 1e-9, f"phase {np.max(np.abs(phase))}"
