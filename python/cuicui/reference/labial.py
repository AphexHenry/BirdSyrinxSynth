"""Slow numpy RK4 reference for the Takens–Bogdanov labial oscillator."""

from __future__ import annotations

import numpy as np


def rhs(x: float, y: float, alpha: float, beta: float, gamma: float) -> tuple[float, float]:
    """ẋ, ẏ — 01_research.md §4.3, this project's sign convention."""
    g2 = gamma * gamma
    x2 = x * x
    dx = y
    dy = (
        -alpha * g2
        - beta * g2 * x
        - g2 * x2 * x
        - gamma * x2 * y
        + g2 * x2
        - gamma * x * y
    )
    return dx, dy


def rk4_step(
    x: float, y: float, alpha: float, beta: float, gamma: float, h: float
) -> tuple[float, float]:
    k1x, k1y = rhs(x, y, alpha, beta, gamma)
    k2x, k2y = rhs(x + 0.5 * h * k1x, y + 0.5 * h * k1y, alpha, beta, gamma)
    k3x, k3y = rhs(x + 0.5 * h * k2x, y + 0.5 * h * k2y, alpha, beta, gamma)
    k4x, k4y = rhs(x + h * k3x, y + h * k3y, alpha, beta, gamma)
    x1 = x + h * (k1x + 2.0 * k2x + 2.0 * k3x + k4x) / 6.0
    y1 = y + h * (k1y + 2.0 * k2y + 2.0 * k3y + k4y) / 6.0
    return x1, y1


def rest_displacement(alpha: float, beta: float) -> float:
    """Largest stable real root of x³ − x² + βx + α = 0, else 0."""
    roots = np.roots([1.0, -1.0, beta, alpha])
    best: float | None = None
    for r in roots:
        if abs(r.imag) > 1e-10:
            continue
        x = float(r.real)
        # Newton polish
        for _ in range(8):
            f = ((x - 1.0) * x + beta) * x + alpha
            fp = 3.0 * x * x - 2.0 * x + beta
            if abs(fp) < 1e-18:
                break
            x -= f / fp
        tr_neg = x * (1.0 + x) > 0.0
        det_pos = (beta + 3.0 * x * x - 2.0 * x) > 0.0
        if tr_neg and det_pos and (best is None or x > best):
            best = x
    return 0.0 if best is None else best


def integrate(
    alpha: np.ndarray,
    beta: np.ndarray,
    gamma: float,
    internal_rate: float,
    x0: float,
    y0: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Sample-and-hold RK4, one step per internal sample."""
    n = int(alpha.shape[0])
    if beta.shape[0] != n:
        raise ValueError("alpha and beta must have the same length")
    h = 1.0 / float(internal_rate)
    xs = np.empty(n, dtype=np.float64)
    ys = np.empty(n, dtype=np.float64)
    x = float(x0)
    y = float(y0)
    a = np.asarray(alpha, dtype=np.float64)
    b = np.asarray(beta, dtype=np.float64)
    for i in range(n):
        x, y = rk4_step(x, y, float(a[i]), float(b[i]), float(gamma), h)
        xs[i] = x
        ys[i] = y
    return xs, ys
