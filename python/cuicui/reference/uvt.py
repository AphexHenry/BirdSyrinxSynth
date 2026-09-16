"""Slow numpy twin of `cuicui::model::UpperVocalTract`.

Physical circuit + trapezoidal 3×3 update, matching the C++ closed-form inverse.
See 01_research.md §4.7.
"""

from __future__ import annotations

import numpy as np

RHO = 1.15
SOUND_SPEED = 350.0
GLOTTIS_LENGTH_M = 0.002
GLOTTIS_RADIUS_M = 0.0004
BEAK_LENGTH_M = 0.008
BEAK_A0 = 1.0e-6
BEAK_A1 = 20.0e-6
UPDATE_PERIOD = 8
PARAM_EPS = 1.0e-12
FLOW_CAL = 25649535.0
RAD_CAL = 1534.636


def physical(
    oec_freq_hz: float,
    oec_damping: float,
    beak_gape: float,
    beak_damping: float,
    size_scale: float = 1.0,
) -> dict[str, float]:
    sigma = float(np.clip(size_scale, 0.25, 4.0))
    f_ctrl = float(np.clip(oec_freq_hz, 300.0, 10000.0))
    zh = float(np.clip(oec_damping, 0.01, 1.0))
    zb = float(np.clip(beak_damping, 0.01, 1.0))
    g = float(np.clip(beak_gape, 0.0, 1.0))
    lg_len = GLOTTIS_LENGTH_M * sigma
    lg_r = GLOTTIS_RADIUS_M * sigma
    ag = np.pi * lg_r * lg_r
    lg = RHO * lg_len / ag
    ab = (BEAK_A0 + g * (BEAK_A1 - BEAK_A0)) * sigma * sigma
    lb = RHO * (BEAK_LENGTH_M * sigma) / ab
    leq = (lg * lb) / (lg + lb)
    f_h = f_ctrl / sigma
    w_h = 2.0 * np.pi * f_h
    ch = 1.0 / (w_h * w_h * leq)
    z0 = float(np.sqrt(leq / ch))
    rh = zh * z0
    rb = zb * z0
    volume = ch * RHO * SOUND_SPEED * SOUND_SPEED
    return {
        "Lg": float(lg),
        "Lb": float(lb),
        "Leq": float(leq),
        "Ch": float(ch),
        "Rh": float(rh),
        "Rb": float(rb),
        "fH": float(f_h),
        "volume_m3": float(volume),
        "volume_cm3": float(volume * 1.0e6),
    }


def invert3(a: np.ndarray) -> np.ndarray | None:
    a00, a01, a02 = float(a[0, 0]), float(a[0, 1]), float(a[0, 2])
    a10, a11, a12 = float(a[1, 0]), float(a[1, 1]), float(a[1, 2])
    a20, a21, a22 = float(a[2, 0]), float(a[2, 1]), float(a[2, 2])
    det = (
        a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20)
    )
    if not np.isfinite(det) or abs(det) <= 1.0e-30:
        return None
    inv = 1.0 / det
    out = np.empty((3, 3), dtype=np.float64)
    out[0, 0] = (a11 * a22 - a12 * a21) * inv
    out[0, 1] = (a02 * a21 - a01 * a22) * inv
    out[0, 2] = (a01 * a12 - a02 * a11) * inv
    out[1, 0] = (a12 * a20 - a10 * a22) * inv
    out[1, 1] = (a00 * a22 - a02 * a20) * inv
    out[1, 2] = (a02 * a10 - a00 * a12) * inv
    out[2, 0] = (a10 * a21 - a11 * a20) * inv
    out[2, 1] = (a01 * a20 - a00 * a21) * inv
    out[2, 2] = (a00 * a11 - a01 * a10) * inv
    return out


def state_matrix(p: dict[str, float]) -> tuple[np.ndarray, np.ndarray]:
    lg, lb, ch, rh, rb = p["Lg"], p["Lb"], p["Ch"], p["Rh"], p["Rb"]
    a = np.array(
        [
            [-rh / lg, -1.0 / lg, rh / lg],
            [1.0 / ch, 0.0, -1.0 / ch],
            [rh / lb, 1.0 / lb, -(rh + rb) / lb],
        ],
        dtype=np.float64,
    )
    b = np.array([1.0 / lg, 0.0, 0.0], dtype=np.float64)
    return a, b


def circuit_h_uvt(s: np.ndarray, p: dict[str, float]) -> np.ndarray:
    """Published circuit transfer H_uvt(s) = p_out / p_t = R_b i_b / p_t."""
    s = np.asarray(s, dtype=np.complex128)
    lg, lb, ch, rh, rb = p["Lg"], p["Lb"], p["Ch"], p["Rh"], p["Rb"]
    zc = rh + 1.0 / (s * ch)
    zb = s * lb + rb
    zp = (zc * zb) / (zc + zb)
    return (zp / (s * lg + zp)) * (rb / (s * lb + rb))


def published_h_uvt(s: np.ndarray, p: dict[str, float]) -> np.ndarray:
    """Transfer function of the published 3rd-order state equations (Arneodo / Mindlin)."""
    s = np.atleast_1d(np.asarray(s, dtype=np.complex128))
    lg, lb, ch, rh, rb = p["Lg"], p["Lb"], p["Ch"], p["Rh"], p["Rb"]
    out = np.empty(s.shape, dtype=np.complex128)
    for i, si in enumerate(s.ravel()):
        m = np.array(
            [
                [si, -1.0, 0.0],
                [
                    1.0 / (lg * ch),
                    si + rh * (1.0 / lb + 1.0 / lg),
                    -(1.0 / (lg * ch) - rb * rh / (lb * lg)),
                ],
                [0.0, lg / lb, si + rb / lb],
            ],
            dtype=np.complex128,
        )
        rhs = np.array([0.0, si / lg + rh / (lg * lb), 1.0 / lb], dtype=np.complex128)
        x = np.linalg.solve(m, rhs)
        out.ravel()[i] = rb * x[2]
    return out.reshape(np.asarray(s).shape)


def h_flow(s: np.ndarray, p: dict[str, float]) -> np.ndarray:
    """Beak flow / tracheal pressure: i_b / p_t = H_uvt / R_b."""
    return circuit_h_uvt(s, p) / p["Rb"]


def h_radiation(s: np.ndarray, p: dict[str, float]) -> np.ndarray:
    """Analytic radiated transfer: (di_b/dt) / p_t from C (sI − A)^{-1} b."""
    s = np.atleast_1d(np.asarray(s, dtype=np.complex128))
    a, b = state_matrix(p)
    c = np.array(
        [p["Rh"] / p["Lb"], 1.0 / p["Lb"], -(p["Rh"] + p["Rb"]) / p["Lb"]],
        dtype=np.float64,
    )
    eye = np.eye(3, dtype=np.complex128)
    out = np.empty(s.shape, dtype=np.complex128)
    for i, si in enumerate(s.ravel()):
        x = np.linalg.solve(si * eye - a.astype(np.complex128), b.astype(np.complex128))
        out.ravel()[i] = c @ x
    return out.reshape(np.asarray(s).shape)


def mix(flow: float | np.ndarray, dflow: float | np.ndarray, radiation: float) -> np.ndarray:
    rho = float(np.clip(radiation, 0.0, 1.0))
    return (1.0 - rho) * FLOW_CAL * np.asarray(flow) + rho * RAD_CAL * np.asarray(dflow)


class UpperVocalTract:
    """Sample-by-sample numpy twin of the C++ class."""

    def __init__(self, internal_rate: float) -> None:
        self.rate = float(internal_rate) if internal_rate > 0.0 else 192000.0
        self.h = 1.0 / self.rate
        self.oec_freq = 3000.0
        self.oec_damp = 0.1
        self.gape = 0.5
        self.beak_damp = 0.3
        self.size = 1.0
        self.phys = physical(self.oec_freq, self.oec_damp, self.gape, self.beak_damp, self.size)
        self.minv = np.eye(3)
        self.n = np.eye(3)
        self.b0 = 0.0
        self.q = np.zeros(3, dtype=np.float64)
        self.last_u = 0.0
        self.flow = 0.0
        self.dflow = 0.0
        self.since_update = UPDATE_PERIOD
        self.dirty = True
        self._rebuild()
        self.reset()

    def reset(self) -> None:
        self.q[:] = 0.0
        self.last_u = 0.0
        self.flow = 0.0
        self.dflow = 0.0
        self.since_update = UPDATE_PERIOD

    def prime(self, pt_steady: float) -> None:
        self._rebuild()
        ib = pt_steady / self.phys["Rb"] if abs(self.phys["Rb"]) > 1.0e-18 else 0.0
        self.q[0] = ib
        self.q[1] = pt_steady
        self.q[2] = ib
        self.last_u = pt_steady
        self.flow = ib
        self.dflow = 0.0
        self.since_update = UPDATE_PERIOD

    def set_params(
        self,
        oec_freq_hz: float,
        oec_damping: float,
        beak_gape: float,
        beak_damping: float,
        size_scale: float = 1.0,
    ) -> None:
        changed = (
            abs(oec_freq_hz - self.oec_freq) > PARAM_EPS
            or abs(oec_damping - self.oec_damp) > PARAM_EPS
            or abs(beak_gape - self.gape) > PARAM_EPS
            or abs(beak_damping - self.beak_damp) > PARAM_EPS
            or abs(size_scale - self.size) > PARAM_EPS
        )
        self.oec_freq = float(oec_freq_hz)
        self.oec_damp = float(oec_damping)
        self.gape = float(beak_gape)
        self.beak_damp = float(beak_damping)
        self.size = float(size_scale)
        if not changed:
            return
        self.dirty = True
        if self.since_update >= UPDATE_PERIOD:
            self._rebuild()
            self.dirty = False
            self.since_update = 0

    def process(self, pt: float) -> tuple[float, float]:
        if self.dirty and self.since_update >= UPDATE_PERIOD:
            self._rebuild()
            self.dirty = False
            self.since_update = 0
        u_sum = self.last_u + pt
        self.last_u = pt
        inj = 0.5 * self.h * self.b0 * u_sum
        q = self.q
        n = self.n
        tmp0 = n[0, 0] * q[0] + n[0, 1] * q[1] + n[0, 2] * q[2] + inj
        tmp1 = n[1, 0] * q[0] + n[1, 1] * q[1] + n[1, 2] * q[2]
        tmp2 = n[2, 0] * q[0] + n[2, 1] * q[1] + n[2, 2] * q[2]
        m = self.minv
        nq0 = m[0, 0] * tmp0 + m[0, 1] * tmp1 + m[0, 2] * tmp2
        nq1 = m[1, 0] * tmp0 + m[1, 1] * tmp1 + m[1, 2] * tmp2
        nq2 = m[2, 0] * tmp0 + m[2, 1] * tmp1 + m[2, 2] * tmp2
        self.q[0], self.q[1], self.q[2] = nq0, nq1, nq2
        rh, rb, lb = self.phys["Rh"], self.phys["Rb"], self.phys["Lb"]
        self.flow = nq2
        self.dflow = (nq1 + rh * nq0 - (rh + rb) * nq2) / lb
        self.since_update += 1
        if self.since_update > UPDATE_PERIOD:
            self.since_update = UPDATE_PERIOD
        return self.flow, self.dflow

    def process_block(self, x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        flow = np.empty(x.shape[0], dtype=np.float64)
        dflow = np.empty(x.shape[0], dtype=np.float64)
        for i, u in enumerate(x):
            f, d = self.process(float(u))
            flow[i] = f
            dflow[i] = d
        return flow, dflow

    def _rebuild(self) -> None:
        self.phys = physical(self.oec_freq, self.oec_damp, self.gape, self.beak_damp, self.size)
        a, b = state_matrix(self.phys)
        self.b0 = float(b[0])
        hh = 0.5 * self.h
        ident = np.eye(3, dtype=np.float64)
        m = ident - hh * a
        self.n = ident + hh * a
        inv = invert3(m)
        if inv is not None:
            self.minv = inv
