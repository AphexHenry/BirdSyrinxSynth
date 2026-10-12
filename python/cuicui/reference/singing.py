"""Python reference of the PROTO1 prototype's playing logic and whistle voice (WP-R1, criterion 6).

A slow, readable port of the prototype JavaScript (`scripts/studies/proto1_page.html`:
`Placement`, `Orders`, `Singer`, `Performer`, `Voice`) used to check the plugin, written
independently of the C++. Sample-accurate scheduling replaces the prototype's 20 ms JavaScript
timer: a key's first syllable starts on its note-on sample, the next one at note-on + its start time
(rounded to a sample).

Randomness: the prototype used `Math.random()`; here every key press draws a 64-bit seed from one
master xoshiro128+ stream and then uses its own stream (pan, start syllable, order), exactly as
`core/include/cuicui/control/mode_ab.hpp` documents, so a seed gives the same syllables in both.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RULES = ("rule", "steady", "landing", "average")
ORDERS = ("random", "phrase", "shuffled")


# ---------------------------------------------------------------- randomness (dsp/rng.hpp)
def splitmix64(state: int) -> tuple[int, int]:
    """Advance a SplitMix64 state; returns (new state, output)."""
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


class Rng:
    """xoshiro128+ seeded through SplitMix64, as `cuicui::dsp::Rng`."""

    def __init__(self, seed: int = 1) -> None:
        x = seed if seed != 0 else 1
        x, a = splitmix64(x)
        x, b = splitmix64(x)
        self.s = [a & MASK32, a >> 32, b & MASK32, b >> 32]
        if not any(self.s):
            self.s[0] = 1

    def next(self) -> int:
        s = self.s
        result = (s[0] + s[3]) & MASK32
        t = (s[1] << 9) & MASK32
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = ((s[3] << 11) | (s[3] >> 21)) & MASK32
        return result


def uniform_index(rng: Rng, n: int) -> int:
    """floor(u·n) for u = (next >> 8)·2⁻²⁴, exactly."""
    return ((rng.next() >> 8) * max(n, 1)) >> 24


def uniform01(rng: Rng) -> float:
    return (rng.next() >> 8) * 2.0**-24


# ---------------------------------------------------------------- the bank
@dataclass
class Syllable:
    phrase: int
    index: int
    t_ms: int
    f_hz: np.ndarray
    a: np.ndarray
    anchors: dict[str, float]
    label: str


@dataclass
class Bank:
    syllables: list[Syllable]
    phrases: list[tuple[int, int]]  # (first syllable, count)
    centres: dict[str, float]

    @classmethod
    def from_json(cls, bank: dict) -> Bank:
        syl: list[Syllable] = []
        phrases: list[tuple[int, int]] = []
        for p in bank["phrases"]:
            phrases.append((len(syl), len(p["syllables"])))
            for i, s in enumerate(p["syllables"]):
                syl.append(Syllable(p["id"], i, s["t_ms"], np.asarray(s["f_hz"], float),
                                    np.asarray(s["a"], float), {r: s["anchors"][r] for r in RULES},
                                    s.get("label", "")))
        return cls(syl, phrases, {r: bank["centres_cents"][r] for r in RULES})

    def syllable_of(self, phrase: int, index: int) -> int:
        return self.phrases[phrase][0] + index


# ---------------------------------------------------------------- Placement (prototype)
def target(midi: int, centre: float, mapping: str, size_st: float) -> float:
    """`Placement.target`: own height = the note name at the octave nearest the bird; follow = the
    key three octaves up."""
    if mapping == "follow":
        return (midi + 36 + size_st) * 100.0
    pc = midi % 12
    c0 = centre + 100.0 * size_st
    best = None
    for m in range(pc, 140, 12):
        c = m * 100.0
        if best is None or abs(c - c0) < abs(best - c0):
            best = c
    return float(best)


def shift(anchor: float, tgt: float, centre: float, k: float) -> float:
    """`Placement.shift`: k = 1 puts the anchor on the target, k = 0 keeps the bird's melody."""
    natural = anchor + (tgt - centre)
    return (1 - k) * natural + k * tgt - anchor


def velocity_gain(v: float) -> float:
    return 0.12 + 0.28 * min(max(v, 0.0), 1.0) ** 1.5


# ---------------------------------------------------------------- Orders (prototype)
class Order:
    """`Orders.real` (random / phrase start) and `Orders.shuffled`, one per key press."""

    def __init__(self, bank: Bank, mode: str, rng: Rng, pointer: list[int]) -> None:
        self.bank, self.mode, self.rng, self.pointer = bank, mode, rng, pointer
        if mode == "shuffled":
            self.last = uniform_index(rng, len(bank.syllables))
            self.left = 4 + uniform_index(rng, 6)
            self.first = self.last
            return
        if mode == "random":
            pick = bank.syllables[uniform_index(rng, len(bank.syllables))]
            self.p, self.i = pick.phrase, pick.index
        else:
            self.p, self.i = self._next_phrase(), 0
        self.first = bank.syllable_of(self.p, self.i)

    def _next_phrase(self) -> int:
        n = len(self.bank.phrases)
        if self.mode == "random":
            return uniform_index(self.rng, n)
        p = self.pointer[0] % n
        self.pointer[0] = (p + 1) % n
        return p

    def next(self, prev_start: float, pause_s: float) -> tuple[int, float]:
        b = self.bank
        if self.mode == "shuffled":
            s = uniform_index(self.rng, len(b.syllables))
            end = prev_start + len(b.syllables[self.last].f_hz) / 1000.0
            gap = 0.02 + uniform01(self.rng) * 0.05
            self.left -= 1
            if self.left <= 0:
                self.left = 4 + uniform_index(self.rng, 6)
                gap = pause_s
            self.last = s
            return s, end + gap
        prev = b.syllable_of(self.p, self.i)
        if self.i + 1 < b.phrases[self.p][1]:
            self.i += 1
            s = b.syllable_of(self.p, self.i)
            return s, prev_start + (b.syllables[s].t_ms - b.syllables[prev].t_ms) / 1000.0
        end = prev_start + len(b.syllables[prev].f_hz) / 1000.0
        self.p, self.i = self._next_phrase(), 0
        return b.syllable_of(self.p, 0), end + pause_s


# ---------------------------------------------------------------- Singer / Performer (prototype)
@dataclass
class Settings:
    rule: str = "rule"
    mapping: str = "own_height"
    size_st: float = 0.0
    first_on_note: bool = True
    snap: float = 0.0
    order: str = "random"
    pause_s: float = 0.5
    polyphony: int = 4
    spread: float = 0.3


@dataclass
class _Singer:
    note: int
    age: int
    t0: int
    target: float
    gain: float
    pan: float
    rng: Rng
    order: Order
    next_syl: int
    next_start_s: float = 0.0
    next_sample: int = 0
    sustained: bool = False
    starts: list = field(default_factory=list)


def perform(bank: Bank, events: list[tuple[float, str, int, float]], settings: Settings, sr: float,
            duration_s: float, seed: int) -> list[dict]:
    """Syllable starts for timed events (time_s, type, note, value); types as `cuicui_core.sing`."""
    s = settings
    master = Rng(seed if seed != 0 else 1)
    pointer = [0]

    def to_sample(t: float) -> int:
        return int(math.floor(t * sr + 0.5))

    # event values travel as 32-bit floats in the plugin (velocity, controllers)
    timed = sorted(
        ((to_sample(t), k, typ, note, float(np.float32(val)))
         for k, (t, typ, note, val) in enumerate(events)),
        key=lambda e: (e[0], e[1]),
    )
    end_sample = to_sample(duration_s)
    # a key takes the first free slot; syllables due on the same sample go lowest slot first
    slots: list[_Singer | None] = [None] * 16
    sustain = False
    wheel = 0.0
    age = 1
    out: list[dict] = []

    def emit(sg: _Singer, at: int, first: bool) -> None:
        k = 1.0 if (first and s.first_on_note) else min(max(s.snap + wheel, 0.0), 1.0)
        syl = sg.next_syl
        anchor = bank.syllables[syl].anchors[s.rule]
        out.append({"sample": at, "syllable": syl,
                    "shift_cents": shift(anchor, sg.target, bank.centres[s.rule], k),
                    "gain": sg.gain, "pan": sg.pan, "first": first, "note": sg.note})
        nxt, start = sg.order.next(sg.next_start_s, s.pause_s)
        sg.next_syl, sg.next_start_s = nxt, start
        sg.next_sample = max(sg.t0 + int(math.floor(start * sr + 0.5)), at + 1)

    def release(sg: _Singer) -> None:
        slots[slots.index(sg)] = None

    def active() -> list[_Singer]:
        return [g for g in slots if g is not None]

    ei = 0
    while True:
        due = None
        for g in slots:
            if g is not None and (due is None or g.next_sample < due.next_sample):
                due = g
        te = timed[ei][0] if ei < len(timed) else None
        if te is not None and te < end_sample and (due is None or te <= due.next_sample):
            t, _, typ, note, val = timed[ei]
            ei += 1
            if typ == "on":
                for g in [g for g in active() if g.note == note]:
                    release(g)
                poly = min(max(s.polyphony, 1), 16)
                while len(active()) >= poly:
                    release(min(active(), key=lambda g: g.age))
                hi, lo = master.next(), master.next()
                rng = Rng((hi << 32) | lo)
                u = uniform01(rng)
                pan = (2.0 * u - 1.0) * min(max(s.spread, 0.0), 1.0) if poly > 1 else 0.0
                tgt = target(note, bank.centres[s.rule], s.mapping, s.size_st)
                order = Order(bank, s.order, rng, pointer)
                sg = _Singer(note, age, t, tgt, velocity_gain(val), pan, rng, order, order.first)
                age += 1
                slots[slots.index(None)] = sg
                emit(sg, t, True)
            elif typ == "off":
                for g in [g for g in active() if g.note == note and not g.sustained]:
                    if sustain:
                        g.sustained = True
                    else:
                        release(g)
            elif typ == "sustain":
                sustain = val >= 0.5
                if not sustain:
                    for g in [g for g in active() if g.sustained]:
                        release(g)
            elif typ == "wheel":
                wheel = min(max(val, 0.0), 1.0)
            elif typ in ("all_notes_off", "all_sound_off"):
                slots = [None] * 16
                sustain = False
            continue
        if due is None or due.next_sample >= end_sample:
            break
        emit(due, due.next_sample, False)
    return out


# ---------------------------------------------------------------- Voice (prototype `Voice.render`)
def render_syllable(bank: Bank, syl: int, shift_cents: float, gain: float, sr: float,
                    harmonics: bool = True, h2_db: float = -30.0, h3_db: float = -33.0,
                    edge_fade_ms: float = 2.5) -> np.ndarray:
    """One syllable as in the prototype (log-frequency / linear-amplitude interpolation between 1 ms
    points, phase advanced before the sine, harmonics at phases 0.6 / 0.9), plus the R1 edge
    fades."""
    sy = bank.syllables[syl]
    f_pts, a_pts = sy.f_hz, sy.a
    m = len(f_pts)
    n = max(1, int(math.floor(m / 1000 * sr + 0.5)))
    i = np.arange(n)
    tms = i / sr * 1000
    j = np.floor(tms).astype(int)
    fr = tms - j
    last = j >= m - 1
    j[last], fr[last] = m - 2, 1.0
    lf = np.log(f_pts[j]) * (1 - fr) + np.log(f_pts[j + 1]) * fr
    f = np.exp(lf) * 2 ** (shift_cents / 1200)
    a = a_pts[j] * (1 - fr) + a_pts[j + 1] * fr
    ph = np.cumsum(2 * np.pi * f / sr)
    s = np.sin(ph)
    if harmonics:
        s = s + 10 ** (h2_db / 20) * np.sin(2 * ph + 0.6)
        s = s + 10 ** (h3_db / 20) * np.sin(3 * ph + 0.9)
    w = np.ones(n)
    nf = min(int(math.floor(edge_fade_ms * 0.001 * sr + 0.5)), n // 2)
    if nf > 0:
        w[:nf] = 0.5 - 0.5 * np.cos(np.pi * (np.arange(nf) + 0.5) / nf)
        w[n - nf:] = 0.5 - 0.5 * np.cos(np.pi * (np.arange(nf, 0, -1) - 0.5) / nf)
    return gain * a * s * w


def instantaneous_hz(bank: Bank, syl: int, shift_cents: float, sr: float) -> np.ndarray:
    """The frequency the voice follows, per sample (for pitch-curve comparisons)."""
    sy = bank.syllables[syl]
    m = len(sy.f_hz)
    n = max(1, int(math.floor(m / 1000 * sr + 0.5)))
    tms = np.arange(n) / sr * 1000
    j = np.floor(tms).astype(int)
    fr = tms - j
    last = j >= m - 1
    j[last], fr[last] = m - 2, 1.0
    lf = np.log(sy.f_hz[j]) * (1 - fr) + np.log(sy.f_hz[j + 1]) * fr
    return np.exp(lf) * 2 ** (shift_cents / 1200)


def render(bank: Bank, starts: list[dict], sr: float, n: int, harmonics: bool = True,
           edge_fade_ms: float = 2.5) -> np.ndarray:
    """Dry stereo mix of syllable starts (equal-power pan, as the prototype's StereoPannerNode)."""
    out = np.zeros((n, 2))
    for st in starts:
        y = render_syllable(bank, st["syllable"], st["shift_cents"], st["gain"], sr, harmonics,
                            edge_fade_ms=edge_fade_ms)
        x = 0.5 * (st["pan"] + 1.0)
        a0 = st["sample"]
        seg = y[: max(0, min(len(y), n - a0))]
        out[a0:a0 + len(seg), 0] += seg * math.cos(x * math.pi / 2)
        out[a0:a0 + len(seg), 1] += seg * math.sin(x * math.pi / 2)
    return out
