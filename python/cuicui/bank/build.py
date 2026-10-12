"""Build a syllable bank (`cuicui.syllable_bank` v1) from an annotated recording. WP-R1.

A bank holds a bird's real syllables as 1 ms pitch (Hz) and amplitude curves, grouped into the
bird's real phrases with their real onset times, plus each syllable's "note" under four rules
(D-019). The recipe is the one the human played in the PROTO1 web prototype (R0,
`scripts/studies/proto1_make.py`):

1. Annotated segments of one category (default `BIRD_SOUND`) are grouped into phrases: a gap of
   more than `phrase_gap_s` (150 ms) starts a new phrase.
2. Each phrase (± 20 ms) is high-passed at 1 kHz and tracked (`cuicui.analysis.f0_track`, 1 ms
   hop).
3. Each syllable keeps its voiced span (first to last voiced frame inside its segment; < 15 voiced
   frames → skipped). Unvoiced frames inside are bridged by linear interpolation of f0.
4. Pitch in MIDI cents (6900 = A4 = 440 Hz), median-filtered over 5 ms. Amplitude = the tracker's
   level, smoothed over 3 ms with a 3-point half-Hann taper at both ends, normalised per phrase
   (natural loudness ratios inside a phrase are kept).
5. Anchors (a syllable's note): `rule` = mean of the longest steady part (within ± 50 cents for at
   least 20 ms), else the ending (last quarter); `steady` = stability-weighted mean; `landing` =
   last quarter; `average` = plain mean. All computed on the voiced frames only.

Nothing here is specific to one bird or species: more recordings only need their own annotation
file.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

from cuicui.analysis import f0_track

FORMAT = "cuicui.syllable_bank"
VERSION = 1
NOTE_RULES = ("rule", "steady", "landing", "average")
RULE_TEXT = {
    "rule": "longest steady part (±50 c, ≥ 20 ms), else ending",
    "steady": "stability-weighted (PITCH1/2)",
    "landing": "last quarter",
    "average": "plain mean",
}


@dataclass(frozen=True)
class BuildSettings:
    """Bank recipe constants (PROTO1 values)."""

    category: str = "BIRD_SOUND"
    phrase_gap_s: float = 0.15  # annotated segments further apart than this start a new phrase
    pad_s: float = 0.02  # analysis margin around each phrase
    highpass_hz: float = 1000.0
    min_voiced_ms: int = 15  # syllables with fewer voiced frames are skipped
    median_filter_ms: int = 5
    steady_tol_cents: float = 50.0  # "steady": pitch stays within ±50 cents
    steady_min_ms: int = 20  # shorter steady parts don't count: use the ending instead
    stability_v0: float = 2000.0  # cents/s, weight scale of the "steady" rule
    hop_threshold_cents: float = 300.0  # a 1 ms pitch step above this is flagged "hop"


def longest_steady(c: np.ndarray, tol: float) -> tuple[int, int, int]:
    """Longest run whose pitch stays within ±tol cents: (length, start, end) in frames."""
    best = (0, 0, 0)
    n = len(c)
    for i in range(n):
        lo = hi = c[i]
        for j in range(i, n):
            lo, hi = min(lo, c[j]), max(hi, c[j])
            if hi - lo > 2 * tol:
                break
            if j + 1 - i > best[0]:
                best = (j + 1 - i, i, j + 1)
    return best


def anchors(c: np.ndarray, settings: BuildSettings | None = None) -> dict:
    """A syllable's note under each rule (cents), from its voiced contour `c` (1 frame = 1 ms).

    Also returns which part the default rule used and its span [start, end) in voiced frames.
    """
    settings = settings or BuildSettings()
    q = max(1, len(c) // 4)
    landing = float(c[-q:].mean())
    n, i, j = longest_steady(c, settings.steady_tol_cents)
    use_steady = n >= settings.steady_min_ms
    slope = np.abs(np.gradient(c)) * 1000.0
    w = np.exp(-slope / settings.stability_v0)
    return {
        "rule": float(c[i:j].mean()) if use_steady else landing,
        "rule_used": "steady part" if use_steady else "ending",
        "steady_ms": int(n),
        "landing": landing,
        "steady": float(np.sum(w * c) / np.sum(w)),
        "average": float(c.mean()),
        "_span": (i, j) if use_steady else (len(c) - q, len(c)),
    }


def group_phrases(segments: list[dict], settings: BuildSettings) -> list[list[tuple[int, dict]]]:
    """Group annotated segments of the bank category into phrases (index in the file, segment)."""
    segs = [(k, s) for k, s in enumerate(segments) if s["category"] == settings.category]
    groups: list[list[tuple[int, dict]]] = []
    cur: list[tuple[int, dict]] = []
    for k, s in segs:
        if cur and s["start"] - cur[-1][1]["end"] > settings.phrase_gap_s:
            groups.append(cur)
            cur = []
        cur.append((k, s))
    if cur:
        groups.append(cur)
    return groups


def _syllable(tr: dict, k: int, s: dict, t0: float, settings: BuildSettings) -> dict | None:
    hop = f0_track.HOP_S
    i0, i1 = int((s["start"] - t0) / hop), int((s["end"] - t0) / hop)
    v = tr["voiced"][i0:i1]
    if v.sum() < settings.min_voiced_ms:
        return None
    idx = np.flatnonzero(v)
    j0, j1 = i0 + idx[0], i0 + idx[-1] + 1
    vv = tr["voiced"][j0:j1]
    f = tr["f0"][j0:j1].copy()
    f[~vv] = np.nan
    good = np.flatnonzero(~np.isnan(f))
    f = np.interp(np.arange(len(f)), good, f[good])
    c = signal.medfilt(1200 * np.log2(f / 440.0) + 6900.0, settings.median_filter_ms)
    a = np.where(vv, tr["amp"][j0:j1], 0.0)
    a = np.convolve(a, np.ones(3) / 3, mode="same")
    w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, 3))
    a[:3] *= w
    a[-3:] *= w[::-1]
    anc = anchors(c[vv], settings)
    voiced_idx = np.flatnonzero(vv)
    span_v = anc.pop("_span")
    span = [int(voiced_idx[span_v[0]]), int(voiced_idx[span_v[1] - 1]) + 1]
    h2 = tr["h2_db"][j0:j1][vv]
    h3 = tr["h3_db"][j0:j1][vv]
    return {
        "segment": k,
        "label": "+".join(e.get("type_label", "") for e in s.get("elements", [])),
        "t_ms": int(j0),
        "c": c,
        "a": a,
        "anchors": anc,
        "span": span,
        "max_step": float(np.abs(np.diff(c)).max()) if len(c) > 1 else 0.0,
        "h2": h2[np.isfinite(h2)],
        "h3": h3[np.isfinite(h3)],
    }


def _median(xs: list[float]) -> float:
    """Median as the prototype computes it (middle value, or mean of the two middle values)."""
    s = sorted(xs)
    m = len(s) // 2
    return s[m] if len(s) % 2 else 0.5 * (s[m - 1] + s[m])


def bank_hash(bank: dict) -> str:
    """12-hex content hash of a bank (the `hash` field itself excluded)."""
    body = {k: v for k, v in bank.items() if k != "hash"}
    return hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest()[:12]


def build_bank(
    wav_path: Path,
    annotations_path: Path,
    *,
    name: str,
    species: str,
    description: str = "",
    created_by: str = "WP-R1",
    settings: BuildSettings | None = None,
) -> dict:
    """Build a `cuicui.syllable_bank` v1 dict from a recording and its tseg annotation JSON."""
    settings = settings or BuildSettings()
    ann = json.loads(Path(annotations_path).read_text())
    x, sr = sf.read(str(wav_path), always_2d=True)
    x = x.mean(axis=1)
    sos = signal.butter(4, settings.highpass_hz, "highpass", fs=sr, output="sos")

    phrases: list[dict] = []
    h2_all: list[np.ndarray] = []
    h3_all: list[np.ndarray] = []
    for g in group_phrases(ann["segments"], settings):
        t0, t1 = g[0][1]["start"] - settings.pad_s, g[-1][1]["end"] + settings.pad_s
        seg = signal.sosfiltfilt(sos, x[int(t0 * sr) : int(t1 * sr)])
        tr = f0_track.track(seg, sr)
        syl = [s for s in (_syllable(tr, k, s, t0, settings) for k, s in g) if s is not None]
        if not syl:
            continue
        h2_all += [s["h2"] for s in syl]
        h3_all += [s["h3"] for s in syl]
        amax = max(s["a"].max() for s in syl)
        base = syl[0]["t_ms"]
        out = []
        for s in syl:
            flags = ["hop"] if s["max_step"] > settings.hop_threshold_cents else []
            anc = {k: (round(v, 1) if isinstance(v, float) else v) for k, v in s["anchors"].items()}
            anc["rule_span_ms"] = s["span"]
            out.append(
                {
                    "segment": s["segment"],
                    "label": s["label"],
                    "t_ms": s["t_ms"] - base,
                    "f_hz": [round(float(v), 1) for v in 440.0 * 2 ** ((s["c"] - 6900.0) / 1200.0)],
                    "a": [round(float(v), 3) for v in s["a"] / amax],
                    "anchors": anc,
                    "max_step_cents": round(s["max_step"], 1),
                    "flags": flags,
                }
            )
        phrases.append(
            {"id": len(phrases), "start_s": round(t0 + base * f0_track.HOP_S, 3), "syllables": out}
        )

    all_syl = [s for p in phrases for s in p["syllables"]]
    centres = {r: round(_median([s["anchors"][r] for s in all_syl]), 1) for r in NOTE_RULES}
    h2 = np.concatenate(h2_all) if h2_all else np.array([np.nan])
    h3 = np.concatenate(h3_all) if h3_all else np.array([np.nan])
    bank = {
        "format": FORMAT,
        "version": VERSION,
        "name": name,
        "species": species,
        "description": description,
        "source": {
            "recording": Path(wav_path).name,
            "annotations": Path(annotations_path).name,
            "recording_id": str(ann.get("recording_id", Path(wav_path).stem)),
            "source_sha256": ann.get("source_sha256"),
            "sample_rate": int(sr),
            "created_by": created_by,
            "tool": "python -m cuicui.bank",
            "tracker": "cuicui.analysis.f0_track (dominant trace, 1 ms hop)",
        },
        "contour_rate_hz": 1000,
        "cents_reference": "MIDI cents: 6900 = A4 = 440 Hz",
        "build": {
            "category": settings.category,
            "phrase_gap_s": settings.phrase_gap_s,
            "highpass_hz": settings.highpass_hz,
            "min_voiced_ms": settings.min_voiced_ms,
            "median_filter_ms": settings.median_filter_ms,
            "steady_tol_cents": settings.steady_tol_cents,
            "steady_min_ms": settings.steady_min_ms,
            "hop_threshold_cents": settings.hop_threshold_cents,
        },
        "rules": dict(RULE_TEXT),
        "centres_cents": centres,
        "bird_centre_cents": centres["rule"],
        "stats": {
            "phrases": len(phrases),
            "syllables": len(all_syl),
            "contour_ms": int(sum(len(s["f_hz"]) for s in all_syl)),
            "rule_used_steady": int(
                sum(s["anchors"]["rule_used"] == "steady part" for s in all_syl)
            ),
            "hops": int(sum("hop" in s["flags"] for s in all_syl)),
            "measured_h2_db": round(float(np.nanmedian(h2)), 1),
            "measured_h3_db": round(float(np.nanmedian(h3)), 1),
        },
        "phrases": phrases,
    }
    bank["hash"] = bank_hash(bank)
    return bank


def write_bank(bank: dict, path: Path) -> None:
    """Write a bank as compact JSON (one line; about 0.2 MB for 123 syllables)."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(bank, separators=(",", ":"), ensure_ascii=False) + "\n")
