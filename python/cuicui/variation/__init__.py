"""Variations within the bird's style (WP-R3): syllable types, nudge, shape model, new order.

Measured on the house finches 22938 and 22937 (WP-R3): a bird sings the same syllable again and
again, and its renditions are tight bundles. A **type** is such a bundle (clustered by shape and
length). Within a type, the bird's natural spread is (median over types): note ±26 c, length
±2.5 %, loudness ±1.5 dB; the shape varies in a few smooth ways (3 modes explain 91 %).

One amount `v` drives every kind of variation the same way:
  * the real rendition has values r (note, log length, loudness, shape-mode scores);
  * a **fresh rendition** of its type draws t from the type's own spread (mean ± sd · N(0, 1));
  * the result is r + v · (t − r): v = 0 is the real syllable, exactly; v = 1 is a new rendition at
    the bird's natural spread (as far from the real one as two real renditions are from each other);
    v > 1 goes beyond the bird's spread (for tests).
**nudge** moves note, length and loudness only (the shape is the real one). **shape** also moves
the type's main shape modes; the real rendition's fine detail (everything the modes do not explain)
is kept. Syllables without a fitted type (too few renditions) get a nudge with the bird's median
spread.

**New order**: a Markov chain over types (order 2, backing off to order 1), learned from the bird's
real phrases; a new phrase starts where a real one started, follows transitions the bird used, ends
where the bird ended, and uses a real rendition of each type with a real gap.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass, field

import numpy as np
from scipy.cluster.hierarchy import fcluster, linkage
from scipy.spatial.distance import squareform

FORMAT = "cuicui.variation_model"
VERSION = 1


@dataclass(frozen=True)
class VariationSettings:
    """Model constants (WP-R3)."""

    type_points: int = 32  # shape points for clustering (time-normalised)
    type_threshold: float = 100.0  # cluster cut: median |Δ cents| + 600 c per e-fold of length
    length_weight: float = 600.0
    model_points: int = 48  # shape points of a type model
    min_members: int = 5  # renditions (without tracker hops) needed to fit a type model
    modes: int = 3  # shape modes kept per type
    min_phrase_order2: int = 1  # order-2 contexts seen at least this often are used


@dataclass
class Syl:
    """One bank syllable, flattened (bank order: phrases, then syllables)."""

    index: int
    phrase: int
    k: int
    t_ms: int
    cents: np.ndarray
    amp: np.ndarray
    note: float
    hop: bool
    label: str
    type: int = -1
    scores: np.ndarray = field(default_factory=lambda: np.zeros(0))


def cents_of(f_hz) -> np.ndarray:
    return 1200.0 * np.log2(np.asarray(f_hz, float) / 440.0) + 6900.0


def resample(x: np.ndarray, n: int) -> np.ndarray:
    """Linear resampling of a curve to n points (end points kept)."""
    if len(x) == n:
        return x.astype(float).copy()
    return np.interp(np.linspace(0.0, len(x) - 1.0, n), np.arange(len(x)), x)


def peak_db(a: np.ndarray) -> float:
    return float(20.0 * np.log10(max(float(np.max(a)), 1e-6)))


def flatten(bank: dict) -> list[Syl]:
    out = []
    for p in bank["phrases"]:
        for k, s in enumerate(p["syllables"]):
            out.append(Syl(index=len(out), phrase=p["id"], k=k, t_ms=s["t_ms"],
                           cents=cents_of(s["f_hz"]), amp=np.asarray(s["a"], float),
                           note=float(s["anchors"]["rule"]), hop="hop" in s["flags"],
                           label=s["label"]))
    return out


def cluster_types(syls: list[Syl], settings: VariationSettings) -> np.ndarray:
    """Type label per syllable (1-based cluster ids, average linkage)."""
    n = len(syls)
    if n == 1:
        return np.ones(1, int)
    pts = np.array([resample(s.cents, settings.type_points) for s in syls])
    logd = np.log([len(s.cents) for s in syls])
    d = np.zeros((n, n))
    for i in range(n):
        d[i] = np.median(np.abs(pts[i][None, :] - pts), axis=1) + settings.length_weight * np.abs(
            logd[i] - logd)
    d = 0.5 * (d + d.T)
    np.fill_diagonal(d, 0.0)
    return fcluster(linkage(squareform(d, checks=False), "average"), settings.type_threshold,
                    "distance")


class Model:
    """A bird's variation model, fitted on one bank."""

    def __init__(self, bank: dict, settings: VariationSettings | None = None):
        self.settings = settings or VariationSettings()
        st = self.settings
        self.bank_hash = bank.get("hash")
        self.syls = flatten(bank)
        labels = cluster_types(self.syls, st)
        for s, lab in zip(self.syls, labels, strict=True):
            s.type = int(lab)
        self.members: dict[int, list[int]] = defaultdict(list)
        for s in self.syls:
            self.members[s.type].append(s.index)
        # type models
        self.types: dict[int, dict] = {}
        for t, idx in self.members.items():
            fit = [i for i in idx if not self.syls[i].hop]
            if len(fit) < st.min_members:
                continue
            shapes = np.array([resample(self.syls[i].cents, st.model_points) - self.syls[i].note
                               for i in fit])
            mean = shapes.mean(axis=0)
            _u, sv, vt = np.linalg.svd(shapes - mean, full_matrices=False)
            k = min(st.modes, len(fit) - 1)
            notes = np.array([self.syls[i].note for i in fit])
            logd = np.log([len(self.syls[i].cents) for i in fit])
            peaks = np.array([peak_db(self.syls[i].amp) for i in fit])
            self.types[t] = {
                "members": idx, "fit": fit, "mean": mean, "modes": vt[:k],
                "mode_sd": sv[:k] / np.sqrt(len(fit) - 1),
                "note": (float(notes.mean()), float(notes.std(ddof=1))),
                "logdur": (float(logd.mean()), float(logd.std(ddof=1))),
                "peak": (float(peaks.mean()), float(peaks.std(ddof=1))),
            }
        for s in self.syls:
            tm = self.types.get(s.type)
            if tm is not None:
                dev = resample(s.cents, st.model_points) - s.note - tm["mean"]
                s.scores = tm["modes"] @ dev
        sds = [tm[k][1] for tm in self.types.values() for k in ("note",)]
        self.global_sd = {
            "note": float(np.median(sds)) if sds else 26.0,
            "logdur": float(np.median([tm["logdur"][1] for tm in self.types.values()]))
            if self.types else 0.025,
            "peak": float(np.median([tm["peak"][1] for tm in self.types.values()]))
            if self.types else 1.5,
        }
        self._fit_order()

    # ------------------------------------------------------------------ syllable variation
    def vary(self, index: int, v: float, rng: np.random.Generator, mode: str = "shape"
             ) -> tuple[np.ndarray, np.ndarray]:
        """A varied rendition of syllable `index`: (cents per 1 ms frame, amplitude per frame).

        mode "nudge": note, length, loudness; "shape": also the type's shape modes.
        v = 0 returns the real syllable exactly.
        """
        s = self.syls[index]
        if v == 0.0:
            return s.cents.copy(), s.amp.copy()
        z = rng.standard_normal(3 + self.settings.modes)
        tm = self.types.get(s.type)
        n0 = len(s.cents)
        if tm is not None:
            note_t = tm["note"][0] + tm["note"][1] * z[0]
            logd_t = tm["logdur"][0] + tm["logdur"][1] * z[1]
            peak_t = tm["peak"][0] + tm["peak"][1] * z[2]
        else:  # no type model: a fresh rendition around itself, with the bird's median spread
            note_t = s.note + self.global_sd["note"] * z[0]
            logd_t = np.log(n0) + self.global_sd["logdur"] * z[1]
            peak_t = peak_db(s.amp) + self.global_sd["peak"] * z[2]
        dnote = v * (note_t - s.note)
        n = max(8, int(round(np.exp(np.log(n0) + v * (logd_t - np.log(n0))))))
        dpeak = v * (peak_t - peak_db(s.amp))
        c = resample(s.cents, n) + dnote
        if mode == "shape" and tm is not None and len(tm["mode_sd"]):
            k = len(tm["mode_sd"])
            target = tm["mode_sd"] * z[3:3 + k]
            delta = (v * (target - s.scores[:k])) @ tm["modes"]
            c = c + resample(delta, n)
        elif mode not in ("shape", "nudge"):
            raise ValueError(f"unknown variation mode {mode!r}")
        a = resample(s.amp, n) * 10.0 ** (dpeak / 20.0)
        return c, a

    # ------------------------------------------------------------------ order
    def _fit_order(self) -> None:
        by_phrase: dict[int, list[Syl]] = defaultdict(list)
        for s in self.syls:
            by_phrase[s.phrase].append(s)
        self.phrases = [sorted(v, key=lambda s: s.k) for _, v in sorted(by_phrase.items())]
        self.t1: dict[int, Counter] = defaultdict(Counter)
        self.t2: dict[tuple[int, int], Counter] = defaultdict(Counter)
        self.gaps: dict[tuple[int, int], list[int]] = defaultdict(list)
        self.gap_after: dict[int, list[int]] = defaultdict(list)
        self.starts: Counter = Counter()
        self.lengths = []
        for ph in self.phrases:
            seq = [s.type for s in ph]
            self.lengths.append(len(seq))
            self.starts[seq[0]] += 1
            ext = seq + [0]  # 0 = end of phrase
            for i in range(len(seq)):
                self.t1[ext[i]][ext[i + 1]] += 1
                if i > 0:
                    self.t2[(ext[i - 1], ext[i])][ext[i + 1]] += 1
            for a, b in zip(ph, ph[1:], strict=False):
                g = b.t_ms - (a.t_ms + len(a.cents))
                self.gaps[(a.type, b.type)].append(g)
                self.gap_after[a.type].append(g)
        self.all_gaps = [g for v in self.gap_after.values() for g in v] or [40]

    def new_phrase(self, rng: np.random.Generator, order: int = 2, max_tries: int = 50
                   ) -> list[tuple[int, int]]:
        """A new phrase as [(syllable index, onset ms)], following the bird's transitions."""
        lo, hi = min(self.lengths), max(self.lengths)
        starts = list(self.starts)
        p_start = np.array([self.starts[t] for t in starts], float)
        for _ in range(max_tries):
            seq = [starts[rng.choice(len(starts), p=p_start / p_start.sum())]]
            while len(seq) <= hi:
                ctx2 = (seq[-2], seq[-1]) if len(seq) >= 2 else None
                c = (self.t2[ctx2] if order >= 2 and ctx2 in self.t2 and
                     sum(self.t2[ctx2].values()) >= self.settings.min_phrase_order2
                     else self.t1[seq[-1]])
                nxt = list(c)
                p = np.array([c[t] for t in nxt], float)
                t = nxt[rng.choice(len(nxt), p=p / p.sum())]
                if t == 0:
                    break
                seq.append(t)
            if lo <= len(seq) <= hi:
                break
        out, t_ms, prev = [], 0, None
        for t in seq:
            i = self.members[t][rng.integers(len(self.members[t]))]
            if prev is not None:
                g = self.gaps.get((prev, t)) or self.gap_after.get(prev) or self.all_gaps
                t_ms += int(g[rng.integers(len(g))])
            out.append((i, t_ms))
            t_ms += len(self.syls[i].cents)
            prev = t
        return out

    def real_phrase(self, phrase_pos: int) -> list[tuple[int, int]]:
        """A real phrase as [(syllable index, onset ms)] (bank onsets)."""
        ph = self.phrases[phrase_pos]
        return [(s.index, s.t_ms - ph[0].t_ms) for s in ph]

    def summary(self) -> dict:
        n_fit = sum(len(tm["members"]) for tm in self.types.values())
        det = [c.most_common(1)[0][1] / sum(c.values()) for t, c in self.t1.items()
               if t and sum(c.values()) >= 4]
        return {
            "syllables": len(self.syls), "types": len(self.members),
            "modelled_types": len(self.types), "syllables_in_modelled_types": n_fit,
            "spread_median": {k: round(v, 4) for k, v in self.global_sd.items()},
            "phrases": len(self.phrases), "phrase_lengths": [min(self.lengths), max(self.lengths)],
            "next_type_determinism_median": round(float(np.median(det)), 2) if det else None,
        }


# ------------------------------------------------------------------ the bank's variation section
SECTION_FORMAT = "cuicui.variation_model"
Z_CAP = 2.0  # draws capped at ±2 sd (the shape model; rare extremes sound like mistakes)


def _r(x, nd: int = 4):
    return [round(float(v), nd) for v in x]


def attach_section(bank: dict, settings: VariationSettings | None = None) -> dict:
    """Fit the shape model on `bank` and write it into the bank (in place; returns the section).

    Adds a top-level "variation" section (types with a model: note / log length / loudness mean and
    sd, mean shape, shape modes and their sd; the bird's median spread) and, per syllable, "type"
    (its cluster id) and, for syllables of a modelled type, "model" (index into the section's types)
    and "scores" (its shape-mode scores). The plugin reads these (D-036).
    """
    m = Model(bank, settings)
    st = m.settings
    order = sorted(m.types)
    index = {t: k for k, t in enumerate(order)}
    types = []
    for t in order:
        tm = m.types[t]
        types.append({
            "id": int(t), "renditions": len(tm["members"]), "fitted_on": len(tm["fit"]),
            "note": _r(tm["note"], 3), "logdur": _r(tm["logdur"], 5), "peak_db": _r(tm["peak"], 3),
            "mean": _r(tm["mean"], 3), "modes": [_r(v, 6) for v in tm["modes"]],
            "mode_sd": _r(tm["mode_sd"], 4),
        })
    section = {
        "format": SECTION_FORMAT, "version": VERSION, "points": st.model_points,
        "settings": {k: v for k, v in st.__dict__.items()},
        "spread_median": {"note": round(m.global_sd["note"], 3),
                          "logdur": round(m.global_sd["logdur"], 5),
                          "peak_db": round(m.global_sd["peak"], 3)},
        "types": types,
    }
    flat = [s for p in bank["phrases"] for s in p["syllables"]]
    for s, syl in zip(flat, m.syls, strict=True):
        s["type"] = int(syl.type)
        s.pop("model", None)
        s.pop("scores", None)
        if syl.type in index:
            s["model"] = index[syl.type]
            s["scores"] = _r(syl.scores, 4)
    bank["variation"] = section
    return section


def _resample_exact(x: np.ndarray, n: int) -> np.ndarray:
    """np.interp on linspace(0, len - 1, n): the plugin's resampling (C++ `variation.hpp`)."""
    return np.interp(np.linspace(0.0, len(x) - 1.0, n), np.arange(len(x)), x)


def variant(bank: dict, index: int, v: float, z, length_scale: float = 1.0, range_scale: float = 1.0
            ) -> tuple[np.ndarray, np.ndarray, float]:
    """A varied rendition of flat syllable `index`, from the bank's stored model (the reference for
    the plugin): (cents per 1 ms frame, amplitude per frame, note change in cents).

    z: standard normals [note, length, loudness, mode 1, mode 2, …], each capped at ±`Z_CAP` (no
    rare extremes). The loudness draw is not used: the syllable keeps its real loudness (the human,
    after the R3 listening page: "1b"). v = 0, or a bank without a variation section, returns the
    real syllable. `length_scale` stretches the result in time; `range_scale` scales its pitch
    movement around its note (the plugin's variation after STRETCH1; the plugin keeps v = 0).
    """
    flat = [s for p in bank["phrases"] for s in p["syllables"]]
    s = flat[index]
    c0 = cents_of(s["f_hz"])
    a0 = np.asarray(s["a"], float)
    z = np.clip(np.asarray(z, float), -Z_CAP, Z_CAP)
    sec = bank.get("variation")
    if sec is None:  # no shape model: only the stretch
        v = 0.0
    if v == 0.0 and length_scale == 1.0 and range_scale == 1.0:
        return c0.copy(), a0.copy(), 0.0
    n0 = len(c0)
    note_s = float(s["anchors"]["rule"])
    peak_s = peak_db(a0)
    model = sec["types"][s["model"]] if (sec is not None and "model" in s) else None
    if v == 0.0:
        note_t, logd_t, peak_t = note_s, np.log(n0), peak_s
    elif model is not None:
        note_t = model["note"][0] + model["note"][1] * z[0]
        logd_t = model["logdur"][0] + model["logdur"][1] * z[1]
        peak_t = model["peak_db"][0] + model["peak_db"][1] * z[2]
    else:  # no type model: the bird's median spread
        sp = sec["spread_median"]
        note_t = note_s + sp["note"] * z[0]
        logd_t = np.log(n0) + sp["logdur"] * z[1]
        peak_t = peak_s + sp["peak_db"] * z[2]
    dnote = v * (note_t - note_s)
    if v == 0.0:
        n_real = n0 * length_scale
    else:
        n_real = np.exp(np.log(n0) + v * (logd_t - np.log(n0))) * length_scale
    n = max(8, int(np.floor(n_real + 0.5)))
    c = _resample_exact(c0, n) + dnote
    if model is not None and v != 0.0:
        k = len(model["mode_sd"])
        coef = [v * (model["mode_sd"][j] * z[3 + j] - s["scores"][j]) for j in range(k)]
        delta = np.zeros(sec["points"])
        for j in range(k):
            delta = delta + coef[j] * np.asarray(model["modes"][j])
        d = _resample_exact(delta, n)
        # the shape change keeps the note: centre it on the loud part (rule E: loudness² weights
        # over the frames within 20 dB of the peak), so only `dnote` moves the note (D-036)
        ar = _resample_exact(a0, n)
        w = np.where(ar >= ar.max() * 0.1, ar * ar, 0.0)
        c = c + d - (float(np.sum(w * d) / np.sum(w)) if np.sum(w) > 0 else 0.0)
    if range_scale != 1.0:  # the pitch movement around the (moved) note, narrower or wider
        c = (note_s + dnote) + range_scale * (c - (note_s + dnote))
    a = _resample_exact(a0, n)  # the real loudness (no loudness variation)
    del peak_t, peak_s
    return c, a, float(dnote)
