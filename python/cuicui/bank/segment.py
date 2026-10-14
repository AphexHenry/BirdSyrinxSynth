"""Mark a recording's syllables automatically (WP-R3): a tseg-style annotation for the bank builder.

1. A 1 ms frame is "tonal" when its strongest spectral peak in 1.2–9 kHz (after a 1 kHz high-pass)
   is within `rel_db` of the recording's loud level (99.9th percentile of that peak level) and
   stands `prominence_db` above the band's median.
2. Tonal runs closer than `join_ms` are joined; runs shorter than `min_ms` are dropped.
3. Each run is checked with the bank's pitch tracker (run per phrase, as the bank builder does): it
   is cut where the tracker hears a gap of `split_gap_ms` or more (a reverberant gap can look tonal
   to step 1), trimmed to its voiced frames, and dropped if it has fewer than `min_voiced_ms` of
   them or lasts less than `min_syllable_ms`.
4. Blocks of syllables (gaps under `block_gap_ms`) whose loudest frame is more than `block_min_db`
   below the loud level are dropped: far-away birds (the human, on 22937).

Recordings are analysed at 48 kHz (`read_mono`), so the 1 ms frames are exact.

Checked on 22938 against the human's annotation (WP-R3): inside the annotated 7.6–53.3 s, all 123
annotated syllables are found, one marked syllable each (3 marked ones span two annotated ones; 4
marked sounds are not annotated).

Output: the tseg JSON shape the bank builder reads (schema 1.2 subset). Each syllable segment hugs
its run (`edge_ms` of margin, at most half the gap to a neighbour), so the builder's voiced span
cannot reach into a neighbour's sound; SILENCE segments fill the rest. Every syllable gets the label
`unlabelled_label` until it is labelled. `keep` merges a human annotation: its segments are kept as
they are (same indices, so note corrections keyed by segment still apply), and automatic syllables
are added only where the human annotation has one SILENCE segment longer than `keep_min_silence_s`
(an unannotated stretch).
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy import signal

from cuicui.analysis import f0_track
from cuicui.bank.build import read_mono

TOOL_VERSION = "cuicui.bank.segment 1 (WP-R3)"


@dataclass(frozen=True)
class SegmentSettings:
    """Marker constants (checked on 22938, WP-R3)."""

    highpass_hz: float = 1000.0
    fmin_hz: float = 1200.0
    fmax_hz: float = 9000.0
    rel_db: float = 40.0  # tonal frames are within this of the loud level (99.9th percentile)
    prominence_db: float = 18.0  # peak above the band median (the tracker's voicing test)
    join_ms: int = 8  # tonal runs closer than this are one syllable
    min_ms: int = 15  # shorter runs are dropped (clicks)
    split_gap_ms: int = 12  # a tracker gap this long cuts a run in two
    min_voiced_ms: int = 15  # = BuildSettings.min_voiced_ms
    min_syllable_ms: int = 40  # shorter pieces are dropped (the human's shortest syllable: 51 ms)
    # the human (22937): "phrases are long and clear blocks separated by 500ms or more"; faint
    # blocks are far-away birds. Syllables closer than `block_gap_ms` form a block; a block whose
    # loudest frame is more than `block_min_db` below the loud level is dropped.
    block_gap_ms: int = 500
    block_min_db: float = 20.0
    phrase_gap_ms: int = 150  # = BuildSettings.phrase_gap_s
    edge_ms: int = 5  # margin around a syllable (at most half the gap to a neighbour)
    chunk_s: float = 10.0  # analysis chunk (memory)
    unlabelled_label: str = "?"
    keep_min_silence_s: float = 5.0


def frame_levels(x: np.ndarray, sr: float, settings: SegmentSettings | None = None) -> dict:
    """Per 1 ms frame: band peak level (dB), band median (dB), peak frequency (Hz)."""
    settings = settings or SegmentSettings()
    sos = signal.butter(4, settings.highpass_hz, "highpass", fs=sr, output="sos")
    y = signal.sosfiltfilt(sos, x)
    hop = int(round(f0_track.HOP_S * sr))
    n_tot = len(y) // hop
    pk = np.full(n_tot, -240.0)
    med = np.zeros(n_tot)
    fpk = np.zeros(n_tot)
    chunk = int(settings.chunk_s * sr) // hop * hop
    for s0 in range(0, len(y), chunk):
        mag, freqs = f0_track.stft_mag(y[s0 : s0 + chunk], sr)
        band = (freqs >= settings.fmin_hz) & (freqs <= settings.fmax_hz)
        lv = 20.0 * np.log10(mag[:, band] + 1e-12)
        i0 = s0 // hop
        m = min(lv.shape[0], n_tot - i0, chunk // hop)
        pk[i0 : i0 + m] = lv[:m].max(axis=1)
        med[i0 : i0 + m] = np.median(lv[:m], axis=1)
        fpk[i0 : i0 + m] = freqs[band][lv[:m].argmax(axis=1)]
    return {"peak_db": pk, "median_db": med, "peak_hz": fpk}


def find_syllables(levels: dict, settings: SegmentSettings | None = None) -> list[tuple[int, int]]:
    """Syllables as [start, end) frame (ms) pairs, from `frame_levels`."""
    settings = settings or SegmentSettings()
    pk, med = levels["peak_db"], levels["median_db"]
    loud = float(np.percentile(pk, 99.9))
    tonal = (pk > loud - settings.rel_db) & (pk - med > settings.prominence_db)
    out: list[tuple[int, int]] = []
    for a, b in f0_track.runs(tonal):
        if out and a - out[-1][1] <= settings.join_ms:
            out[-1] = (out[-1][0], int(b))
        else:
            out.append((int(a), int(b)))
    return [(a, b) for a, b in out if b - a >= settings.min_ms]


def refine(x: np.ndarray, sr: float, sylls: list[tuple[int, int]],
           settings: SegmentSettings | None = None) -> list[tuple[int, int]]:
    """Cut runs at the tracker's gaps and trim them to its voiced frames (module doc, step 3)."""
    settings = settings or SegmentSettings()
    sos = signal.butter(4, settings.highpass_hz, "highpass", fs=sr, output="sos")
    pad = 20  # = BuildSettings.pad_s
    out: list[tuple[int, int]] = []
    for ph in group(sylls, settings.phrase_gap_ms):
        t0 = max(0, ph[0][0] - pad)
        t1 = ph[-1][1] + pad
        seg = x[int(t0 * sr / 1000): int(t1 * sr / 1000)]
        tr = f0_track.track(signal.sosfiltfilt(sos, seg), sr)
        v = tr["voiced"]
        for a, b in ph:
            pieces: list[list[int]] = []
            for r0, r1 in f0_track.runs(v[a - t0: b - t0]):
                if pieces and r0 - pieces[-1][1] < settings.split_gap_ms:
                    pieces[-1][1] = int(r1)
                else:
                    pieces.append([int(r0), int(r1)])
            out += [(a + r0, a + r1) for r0, r1 in pieces
                    if int(v[a - t0 + r0: a - t0 + r1].sum()) >= settings.min_voiced_ms
                    and r1 - r0 >= settings.min_syllable_ms]
    return out


def drop_faint_blocks(sylls: list[tuple[int, int]], levels: dict,
                      settings: SegmentSettings | None = None) -> list[tuple[int, int]]:
    """Drop blocks (syllables closer than `block_gap_ms`) far below the loud level (far birds)."""
    settings = settings or SegmentSettings()
    pk = levels["peak_db"]
    loud = float(np.percentile(pk, 99.9))
    out: list[tuple[int, int]] = []
    for blk in group(sylls, settings.block_gap_ms):
        if max(float(pk[a:b].max()) for a, b in blk) >= loud - settings.block_min_db:
            out += blk
    return out


def group(sylls: list[tuple[int, int]], gap_ms: int) -> list[list[tuple[int, int]]]:
    """Syllables closer than `gap_ms` form one phrase."""
    phrases: list[list[tuple[int, int]]] = []
    for a, b in sylls:
        if phrases and a - phrases[-1][-1][1] <= gap_ms:
            phrases[-1].append((a, b))
        else:
            phrases.append([(a, b)])
    return phrases


def _segments(phrases: list[list[tuple[int, int]]], t0_ms: int, t1_ms: int,
              settings: SegmentSettings) -> list[dict]:
    """Tile [t0, t1) ms with SILENCE and BIRD_SOUND segments (seconds)."""
    segs: list[dict] = []

    def add(cat: str, a: int, b: int, label: str | None = None) -> None:
        if b <= a:
            return
        s = {"category": cat, "start": round(a / 1000.0, 6), "end": round(b / 1000.0, 6)}
        if label is not None:
            s["elements"] = [{"type_label": label}]
        segs.append(s)

    flat = [x for ph in phrases for x in ph]
    cur = t0_ms
    for i, (a, b) in enumerate(flat):
        lo = a - settings.edge_ms
        if i > 0:
            lo = max(lo, (flat[i - 1][1] + a) // 2)
        hi = b + settings.edge_ms if i == len(flat) - 1 else min(b + settings.edge_ms,
                                                                  (b + flat[i + 1][0]) // 2)
        lo, hi = max(lo, cur, t0_ms), min(hi, t1_ms)
        add("SILENCE", cur, lo)
        add("BIRD_SOUND", lo, hi, settings.unlabelled_label)
        cur = max(cur, hi)
    add("SILENCE", cur, t1_ms)
    return segs


def annotate(wav_path: Path, *, keep: dict | None = None,
             settings: SegmentSettings | None = None) -> dict:
    """An automatic annotation of a recording (tseg JSON shape), optionally around a human one."""
    settings = settings or SegmentSettings()
    x, sr, file_sr = read_mono(Path(wav_path))
    raw = Path(wav_path).read_bytes()
    n_ms = int(len(x) / sr * 1000)
    levels = frame_levels(x, sr, settings)
    sylls = refine(x, sr, find_syllables(levels, settings), settings)
    sylls = drop_faint_blocks(sylls, levels, settings)
    if keep is None:
        segments = _segments(group(sylls, settings.phrase_gap_ms), 0, n_ms, settings)
        rid = Path(wav_path).stem
    else:
        segments = []
        for s in keep["segments"]:
            a, b = int(round(s["start"] * 1000)), int(round(s["end"] * 1000))
            if s["category"] == "SILENCE" and s["end"] - s["start"] >= settings.keep_min_silence_s:
                inside = [(p, q) for p, q in sylls if p >= a + settings.phrase_gap_ms
                          and q <= b - settings.phrase_gap_ms]
                if inside:  # the human's segment shrinks to its start; new ones are appended
                    new = _segments(group(inside, settings.phrase_gap_ms), a, b, settings)
                    segments.append({**new[0], "category": "SILENCE"})
                    segments.append(None)  # placeholder: appended segments go to the end
                    segments.extend([{"_append": True, **t} for t in new[1:]])
                    continue
            segments.append(dict(s))
        # keep the human's indices: their segments first (in order), then the new ones
        kept = [s for s in segments if s is not None and not s.get("_append")]
        appended = [{k: v for k, v in s.items() if k != "_append"}
                    for s in segments if s is not None and s.get("_append")]
        segments = kept + appended
        rid = str(keep.get("recording_id", Path(wav_path).stem))
    return {
        "recording_id": rid,
        "sample_rate": float(file_sr),
        "schema_version": "1.2",
        "segments": segments,
        "source_sha256": (keep or {}).get("source_sha256") or hashlib.sha256(raw).hexdigest(),
        "tool_version": TOOL_VERSION,
        "auto": {
            "settings": settings.__dict__,
            "syllables_found": len(sylls),
            "kept_human_segments": len(keep["segments"]) if keep else 0,
        },
    }


def write_annotation(ann: dict, path: Path) -> None:
    """Write an annotation as indented JSON."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(ann, indent=1) + "\n")
