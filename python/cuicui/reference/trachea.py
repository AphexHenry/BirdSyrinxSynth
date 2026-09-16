"""Slow numpy reference for the tracheal delayed-feedback waveguide.

Matches `cuicui::model::Trachea` / `dsp::lagrange3Read` sample-for-sample.
See 01_research.md §4.6.
"""

from __future__ import annotations

import numpy as np

SOUND_SPEED = 350.0
MAX_LENGTH_MM = 120.0 * 4.0
MIN_DELAY = 3.0


def wrap_index(i: int, n: int) -> int:
    return i % n


def delay_read(buf: np.ndarray, write: int, delay: int) -> float:
    return float(buf[wrap_index(write - delay, int(buf.shape[0]))])


def lagrange3_read(buf: np.ndarray, write: int, delay: float) -> float:
    if delay < 1.0:
        delay = 1.0
    n = int(np.floor(delay))
    f = delay - n
    if delay >= 2.0:
        ym1 = delay_read(buf, write, n - 1)
        y0 = delay_read(buf, write, n)
        y1 = delay_read(buf, write, n + 1)
        y2 = delay_read(buf, write, n + 2)
        hm1 = -f * (f - 1.0) * (f - 2.0) / 6.0
        h0 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0
        h1 = -f * (f + 1.0) * (f - 2.0) / 2.0
        h2 = f * (f + 1.0) * (f - 1.0) / 6.0
        return hm1 * ym1 + h0 * y0 + h1 * y1 + h2 * y2
    y0 = delay_read(buf, write, n)
    y1 = delay_read(buf, write, n + 1)
    y2 = delay_read(buf, write, n + 2)
    y3 = delay_read(buf, write, n + 3)
    h0 = -(f - 1.0) * (f - 2.0) * (f - 3.0) / 6.0
    h1 = f * (f - 2.0) * (f - 3.0) / 2.0
    h2 = -f * (f - 1.0) * (f - 3.0) / 2.0
    h3 = f * (f - 1.0) * (f - 2.0) / 6.0
    return h0 * y0 + h1 * y1 + h2 * y2 + h3 * y3


def loss_cutoff_hz(loss: float) -> float:
    loss = float(np.clip(loss, 0.0, 1.0))
    if loss <= 0.0:
        return 0.0
    return 22000.0 * (3000.0 / 22000.0) ** loss


def buffer_size(internal_rate: float, max_length_mm: float = MAX_LENGTH_MM) -> int:
    rate = internal_rate if internal_rate > 0.0 else 192000.0
    l_max = max(max_length_mm, 10.0) * 0.001
    max_d = 2.0 * l_max / SOUND_SPEED * rate
    return max(int(np.ceil(max_d)) + 4, 8)


def analytic_magnitude(
    freq_hz: np.ndarray, length_mm: float, reflection: float, gain: float = 1.0
) -> np.ndarray:
    """Lossless |H_tr(f)| = |A(1−r)| / |1 + r e^{−j 2π f τ}|, τ = 2L/c."""
    tau = 2.0 * (length_mm * 0.001) / SOUND_SPEED
    f = np.asarray(freq_hz, dtype=np.float64)
    wr = 2.0 * np.pi * f * tau
    den = np.abs(1.0 + reflection * np.exp(-1j * wr))
    return np.abs(gain * (1.0 - reflection)) / np.maximum(den, 1e-18)


class Trachea:
    """Sample-by-sample numpy twin of `cuicui::model::Trachea`."""

    def __init__(self, internal_rate: float, max_length_mm: float = MAX_LENGTH_MM) -> None:
        self.rate = float(internal_rate)
        self.max_length_mm = float(max_length_mm)
        n = buffer_size(self.rate, self.max_length_mm)
        self.buf = np.zeros(n, dtype=np.float64)
        self.write = 0
        self.lp_y = 0.0
        self.lp_a = 0.0
        self.length_mm = 33.0
        self.reflection = 0.5
        self.loss = 0.3
        self.input_gain = 1.0
        self.delay = MIN_DELAY
        self.set_params(33.0, 0.5, 0.3, 1.0)

    def reset(self) -> None:
        self.buf.fill(0.0)
        self.write = 0
        self.lp_y = 0.0

    def set_params(
        self, length_mm: float, reflection: float, loss: float, input_gain: float
    ) -> None:
        self.length_mm = float(np.clip(length_mm, 1.0, self.max_length_mm))
        self.reflection = float(np.clip(reflection, -0.95, 0.95))
        self.loss = float(np.clip(loss, 0.0, 1.0))
        self.input_gain = float(np.clip(input_gain, 0.0, 4.0))
        L = self.length_mm * 0.001
        d = 2.0 * L / SOUND_SPEED * self.rate
        max_d = float(self.buf.shape[0] - 4)
        self.delay = min(max(d, MIN_DELAY), max_d)
        fc = loss_cutoff_hz(self.loss)
        if fc <= 0.0:
            self.lp_a = 0.0
        else:
            self.lp_a = float(np.exp(-2.0 * np.pi * fc / self.rate))

    def process(self, source: float) -> float:
        pi_d = lagrange3_read(self.buf, self.write, self.delay)
        pi_d2 = lagrange3_read(self.buf, self.write, 0.5 * self.delay)
        fb = (1.0 - self.lp_a) * pi_d + self.lp_a * self.lp_y
        self.lp_y = fb
        pi = self.input_gain * source - self.reflection * fb
        self.buf[self.write] = pi
        self.write = wrap_index(self.write + 1, int(self.buf.shape[0]))
        return (1.0 - self.reflection) * pi_d2

    def process_block(self, source: np.ndarray) -> np.ndarray:
        x = np.asarray(source, dtype=np.float64)
        y = np.empty_like(x)
        for i in range(x.shape[0]):
            y[i] = self.process(float(x[i]))
        return y
