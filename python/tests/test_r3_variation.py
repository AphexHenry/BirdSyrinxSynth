"""WP-R3: the automatic syllable marker, 48 kHz analysis, and the variation model."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

from cuicui.bank.build import read_mono
from cuicui.bank.segment import SegmentSettings, annotate, find_syllables, frame_levels
from cuicui.variation import Model, VariationSettings

SR = 48000


def _chirp(f0: float, f1: float, ms: float, sr: int = SR, level: float = 0.5) -> np.ndarray:
    n = int(ms / 1000 * sr)
    f = np.geomspace(f0, f1, n)
    env = np.sin(np.linspace(0, np.pi, n)) ** 0.5
    return level * env * np.sin(2 * np.pi * np.cumsum(f) / sr)


def _song(sr: int = SR, seed: int = 1) -> tuple[np.ndarray, list[tuple[float, float]]]:
    """Two phrases of loud chirps (onsets known), a faint far phrase, a click, background noise."""
    rng = np.random.default_rng(seed)
    x = 1e-4 * rng.standard_normal(int(6.0 * sr))
    truth = []

    def put(t: float, y: np.ndarray) -> None:
        i = int(t * sr)
        x[i: i + len(y)] += y

    t = 0.5
    for k in range(4):  # phrase 1: 4 syllables, 60 ms gaps
        y = _chirp(3000 + 200 * k, 4200 - 300 * k, 80, sr)
        put(t, y)
        truth.append((t, t + 0.08))
        t += 0.14
    t = 2.0
    for _ in range(3):  # phrase 2, 1.2 s later
        put(t, _chirp(4500, 2800, 90, sr))
        truth.append((t, t + 0.09))
        t += 0.15
    for k in range(3):  # a far bird: 35 dB quieter, its own block
        put(4.0 + 0.15 * k, _chirp(3500, 3900, 70, sr, level=0.5 * 10 ** (-35 / 20)))
    put(5.2, _chirp(3000, 3000, 8, sr))  # an 8 ms click-like tone
    return x, truth


def test_marker_finds_the_syllables_and_drops_far_birds_and_clicks(tmp_path: Path):
    x, truth = _song()
    path = tmp_path / "song.wav"
    sf.write(path, x, SR)
    ann = annotate(path)
    birds = [s for s in ann["segments"] if s["category"] == "BIRD_SOUND"]
    assert len(birds) == len(truth)
    for s, (a, b) in zip(birds, truth, strict=True):
        assert s["start"] == pytest.approx(a, abs=0.02) and s["end"] == pytest.approx(b, abs=0.02)
    # segments tile the recording in time order
    ends = [s["end"] for s in ann["segments"]]
    starts = [s["start"] for s in ann["segments"]]
    assert starts[0] == 0.0 and all(
        e == pytest.approx(s2) for e, s2 in zip(ends, starts[1:], strict=False))


def test_marker_timing_is_exact_at_44k1(tmp_path: Path):
    """At 44.1 kHz a 44-sample hop is 0.998 ms; read_mono resamples, so late syllables stay put."""
    x, truth = _song(sr=44100)
    x = np.concatenate([np.zeros(int(60 * 44100)), x])  # one minute later: 2.3 ms/s would be 140 ms
    path = tmp_path / "late.wav"
    sf.write(path, x, 44100)
    y, sr, file_sr = read_mono(path)
    assert sr == 48000 and file_sr == 44100
    birds = [s for s in annotate(path)["segments"] if s["category"] == "BIRD_SOUND"]
    assert birds[0]["start"] == pytest.approx(60 + truth[0][0], abs=0.02)
    assert birds[-1]["end"] == pytest.approx(60 + truth[-1][1], abs=0.02)


def test_read_mono_leaves_48k_untouched(tmp_path: Path):
    x = np.random.default_rng(0).standard_normal((4800, 2)) * 0.1
    sf.write(tmp_path / "a.wav", x, 48000, subtype="FLOAT")
    y, sr, file_sr = read_mono(tmp_path / "a.wav")
    assert sr == file_sr == 48000
    np.testing.assert_allclose(y, x.astype(np.float32).astype(float).mean(axis=1), atol=1e-7)


def test_keep_preserves_the_human_segments(tmp_path: Path):
    x, _ = _song()
    path = tmp_path / "song.wav"
    sf.write(path, x, SR)
    human = {"recording_id": "t", "source_sha256": "abc", "segments": [
        {"category": "SILENCE", "start": 0.0, "end": 0.45},
        {"category": "BIRD_SOUND", "start": 0.45, "end": 1.2,
         "elements": [{"type_label": "swipe"}]},
        {"category": "SILENCE", "start": 1.2, "end": 6.0}]}
    ann = annotate(path, keep=human, settings=SegmentSettings(keep_min_silence_s=3.0))
    assert ann["segments"][1] == human["segments"][1]
    assert ann["segments"][2]["start"] == 1.2 and ann["segments"][2]["category"] == "SILENCE"
    new = [s for s in ann["segments"][3:] if s["category"] == "BIRD_SOUND"]
    assert len(new) == 3 and all(s["start"] >= 1.9 for s in new)
    assert ann["source_sha256"] == "abc"


def test_find_syllables_on_levels_only():
    x, truth = _song()
    found = find_syllables(frame_levels(x, SR))
    assert len(found) >= len(truth)  # before the tracker check and the faint-block filter


# ---------------------------------------------------------------------- variation model

def _bank(seed: int = 3, n_rend: int = 12) -> dict:
    """A synthetic bank: phrases of 3 types (up, down, flat), each sung n_rend times with spread."""
    rng = np.random.default_rng(seed)
    proto = {"up": (9800, 10600, 80), "down": (10800, 9900, 90), "flat": (10300, 10350, 120)}
    phrases = []
    for p in range(n_rend):
        t, syls = 0, []
        for name in ("up", "down", "flat") if p % 2 == 0 else ("up", "flat", "down"):
            c0, c1, ms = proto[name]
            n = int(round(ms * np.exp(0.025 * rng.standard_normal())))
            off = 26.0 * rng.standard_normal()
            bow = 25.0 * rng.standard_normal()
            c = np.linspace(c0, c1, n) + off + bow * np.sin(np.linspace(0, np.pi, n))
            a = np.sin(np.linspace(0, np.pi, n)) * 10 ** (1.5 * rng.standard_normal() / 20)
            f = 440.0 * 2 ** ((c - 6900.0) / 1200.0)
            syls.append({"segment": len(syls), "label": name, "t_ms": t,
                         "f_hz": [float(v) for v in f], "a": [float(v) for v in a],
                         "anchors": {"rule": float(np.average(c, weights=a**2))}, "flags": []})
            t += n + 40
        phrases.append({"id": p, "start_s": 3.0 * p, "syllables": syls})
    return {"hash": "synthetic", "phrases": phrases}


def test_v0_is_the_real_syllable():
    m = Model(_bank())
    rng = np.random.default_rng(0)
    for i in (0, 5, 17):
        for mode in ("nudge", "shape"):
            c, a = m.vary(i, 0.0, rng, mode)
            np.testing.assert_array_equal(c, m.syls[i].cents)
            np.testing.assert_array_equal(a, m.syls[i].amp)


def test_types_are_found():
    m = Model(_bank())
    assert len(m.types) == 3  # one model per prototype (a far outlier may sit alone)
    for tm in m.types.values():
        labels = {m.syls[i].label for i in tm["members"]}
        assert len(labels) == 1 and len(tm["members"]) >= 10


def test_v1_gives_the_natural_spread():
    """A fresh rendition at v = 1 has the type's spread of note and length around the type mean."""
    m = Model(_bank(n_rend=20), VariationSettings(min_members=5))
    rng = np.random.default_rng(1)
    t, tm = next(iter(m.types.items()))
    i = tm["members"][0]
    notes, lens = [], []
    for _ in range(2000):
        c, _a = m.vary(i, 1.0, rng, "nudge")
        notes.append(np.mean(c) - np.mean(m.syls[i].cents) + m.syls[i].note)
        lens.append(len(c))
    assert np.mean(notes) == pytest.approx(tm["note"][0], abs=5.0)
    assert np.std(notes) == pytest.approx(tm["note"][1], rel=0.15)
    assert np.std(np.log(lens)) == pytest.approx(tm["logdur"][1], rel=0.3)


def test_nudge_keeps_the_shape_and_the_shape_model_changes_it():
    m = Model(_bank(n_rend=20))
    i = next(iter(m.types.values()))["members"][3]
    real = m.syls[i].cents
    for seed in range(5):
        c, _ = m.vary(i, 1.0, np.random.default_rng(seed), "nudge")
        rs = np.interp(np.linspace(0, len(c) - 1, len(real)), np.arange(len(c)), c)
        assert np.ptp(rs - real) < 2.0  # only a constant note shift (and a stretch)
    diffs = []
    for seed in range(5):
        c, _ = m.vary(i, 1.0, np.random.default_rng(seed), "shape")
        rs = np.interp(np.linspace(0, len(c) - 1, len(real)), np.arange(len(c)), c)
        diffs.append(np.ptp(rs - real))
    assert max(diffs) > 10.0


def test_new_phrases_follow_the_birds_transitions():
    m = Model(_bank())
    real_pairs = {(a.type, b.type) for ph in m.phrases for a, b in zip(ph, ph[1:], strict=False)}
    starts = {ph[0].type for ph in m.phrases}
    rng = np.random.default_rng(2)
    for order in (1, 2):
        for _ in range(50):
            plan = m.new_phrase(rng, order=order)
            types = [m.syls[i].type for i, _ in plan]
            assert types[0] in starts
            assert all(p in real_pairs for p in zip(types, types[1:], strict=False))
            onsets = [t for _, t in plan]
            assert onsets == sorted(onsets)


def test_model_on_the_factory_bank():
    """Smoke test on the real factory bank: types, spreads in the measured range."""
    bank = json.loads((Path(__file__).resolve().parents[2]
                       / "assets/voicebanks/zf_22938/bank.json").read_text())
    m = Model(bank)
    s = m.summary()
    assert s["syllables"] == 123 and s["types"] > 10
    assert 5.0 < s["spread_median"]["note"] < 80.0


# ------------------------------------------------------------------ the plugin's shape model (C++)
VB = Path(__file__).resolve().parents[2] / "assets/voicebanks"
HF = [str(VB / "hf_22938/bank.json"), str(VB / "hf_22937/bank.json")]


def _cc():
    return pytest.importorskip("cuicui_core")


@pytest.mark.parametrize("which", [0, 1])
def test_cpp_variant_equals_the_python_reference(which):
    """control::makeVariant = cuicui.variation.variant for the same draws (D-036)."""
    from cuicui.variation import variant

    cc = _cc()
    bank = json.loads(Path(HF[which]).read_text())
    flat = [s for p in bank["phrases"] for s in p["syllables"]]
    rng = np.random.default_rng(7 + which)
    with_model = [i for i, s in enumerate(flat) if "model" in s]
    without = [i for i, s in enumerate(flat) if "model" not in s]
    assert with_model and without
    picks = (list(rng.choice(with_model, 12, replace=False))
             + list(rng.choice(without, 6, replace=False)))
    for i in picks:
        for v, scale, rs in ((1.0, 1.0, 1.0), (0.4, 1.0, 1.0), (1.0, 1.7, 1.0), (0.0, 0.8, 1.0),
                             (0.0, 1.3, 0.6), (0.0, 0.85, 1.6), (1.0, 1.2, 1.4)):
            z = list(rng.standard_normal(7))
            c_py, a_py, d_py = variant(bank, int(i), v, z, scale, rs)
            c_cc, a_cc, d_cc = cc.variant(HF[which], int(i), v, z, scale, rs)
            assert len(c_cc) == len(c_py)
            np.testing.assert_allclose(c_cc, c_py, atol=1e-6)
            np.testing.assert_allclose(a_cc, a_py, rtol=1e-5, atol=1e-6)
            assert d_cc == pytest.approx(d_py, abs=1e-9)


def test_variant_zero_is_the_real_syllable_in_cpp():
    cc = _cc()
    bank = json.loads(Path(HF[0]).read_text())
    s = [s for p in bank["phrases"] for s in p["syllables"]][40]
    c, a, d = cc.variant(HF[0], 40, 0.0, [1.0] * 7)
    np.testing.assert_allclose(c, 1200 * np.log2(np.asarray(s["f_hz"]) / 440) + 6900, atol=1e-9)
    assert d == 0.0


def test_merged_banks_keep_both_birds():
    cc = _cc()
    one = [cc.bank_summary(p) for p in HF]
    both = cc.bank_summary(HF)
    assert both["syllables"] == sum(b["syllables"] for b in one)
    assert both["phrases"] == sum(b["phrases"] for b in one)
    assert both["birds"] == ["hf_22938", "hf_22937"]
    assert both["has_variation"] and both["models"] == sum(b["models"] for b in one)
    lo, hi = sorted(b["centres"][0] for b in one)
    assert lo <= both["centres"][0] <= hi


TUNE = [(0.1 + 0.45 * k, "on", n, 0.8) for k, n in enumerate((60, 62, 64, 65, 67, 65, 64, 62))] + \
       [(0.5 + 0.45 * k, "off", n, 0.0) for k, n in enumerate((60, 62, 64, 65, 67, 65, 64, 62))]


def test_variation_is_block_size_independent_and_changes_the_sound():
    cc = _cc()
    st = {"perf.natural_variation": 1.0, "out.room": 0}
    y1, log1 = cc.sing(HF, TUNE, st, 48000.0, 4.5, 99, 64)
    y2, log2 = cc.sing(HF, TUNE, st, 48000.0, 4.5, 99, 500)
    np.testing.assert_array_equal(y1, y2)
    assert [e["variant_seed"] for e in log1] == [e["variant_seed"] for e in log2]
    y0, log0 = cc.sing(HF, TUNE, {**st, "perf.natural_variation": 0.0}, 48000.0, 4.5, 99, 64)
    # the same choices per key press (a stretched syllable may leave room for one less before a
    # release)
    def presses(log):
        out, current = [], {}
        for e in log:
            if e["first"]:
                current[e["key"]] = len(out)
                out.append([])
            out[current[e["key"]]].append(e["syllable"])
        return out

    p0, p1 = presses(log0), presses(log1)
    assert len(p0) == len(p1) == 8
    for a, b in zip(p0, p1, strict=True):
        m = min(len(a), len(b))
        assert m > 0 and a[:m] == b[:m]
    assert not np.array_equal(y0, y1)
    assert {e["bird"] for e in log1} <= {0, 1}


def test_variation_stretches_length_and_range_within_the_spans():
    """STRETCH1: length 80–160 % (swipes up to 200 %), pitch range 60–160 %; tracker jumps only
    narrowed. The log's scales are the core's draws (stretch_draw); the curve keeps its note."""
    from cuicui.variation import variant

    cc = _cc()
    banks = [json.loads(Path(p).read_text()) for p in HF]
    flat = [s for b in banks for p in b["phrases"] for s in p["syllables"]]
    n0 = len([s for p in banks[0]["phrases"] for s in p["syllables"]])
    held = [(0.1, "on", 64, 0.8), (12.0, "off", 64, 0.0),
            (12.5, "on", 69, 0.6), (24.0, "off", 69, 0.0)]
    _, log = cc.sing(HF, held, {"perf.natural_variation": 1.0, "out.room": 0}, 48e3, 24.5, 3, 256)
    assert len(log) > 100
    for e in log:
        s = flat[e["syllable"]]
        swipe, hop = "swipe" in s["label"].lower(), "hop" in s["flags"]
        drawn = cc.stretch_draw(e["variant_seed"], 1.0, swipe, hop)
        assert (e["length_scale"], e["range_scale"]) == drawn
        assert 0.8 - 1e-9 <= e["length_scale"] <= (2.0 if swipe else 1.6) + 1e-9
        assert 0.6 - 1e-9 <= e["range_scale"] <= (1.0 if hop else 1.6) + 1e-9
        assert e["variant_note_cents"] == 0.0  # the shape model is off: the note never moves
        if "ear" in s["flags"] or hop:
            continue
        i = e["syllable"]
        b, local = (banks[0], i) if i < n0 else (banks[1], i - n0)
        c, a, _ = variant(b, local, 0.0, [0.0] * 7, e["length_scale"], e["range_scale"])
        keep = a >= a.max() * 0.1
        loud = float(np.average(c[keep], weights=a[keep] ** 2))
        assert abs(loud - s["anchors"]["rule"]) < 6.0  # the range scales around the note
    assert max(e["length_scale"] for e in log) > 1.4 and min(e["length_scale"] for e in log) < 0.9
    _, log0 = cc.sing(HF, held, {"perf.natural_variation": 0.0, "out.room": 0}, 48e3, 24.5, 3, 256)
    assert all(e["length_scale"] == 1.0 and e["range_scale"] == 1.0 for e in log0)


def test_snap_to_note_still_lands_varied_syllables_on_the_note():
    """With snap 100 % (Finch, melody), a varied syllable's note is its real note (D-036): the range
    scales around it, and the placement is unchanged."""
    cc = _cc()
    banks = [json.loads(Path(p).read_text()) for p in HF]
    flat = [s for b in banks for p in b["phrases"] for s in p["syllables"]]
    st = {"perf.natural_variation": 1.0, "perf.snap": 1.0, "perf.mapping": "follow",
          "perf.polyphony": 1, "out.room": 0}
    held = [(0.1, "on", 64, 0.8), (3.0, "off", 64, 0.0)]
    _, log = cc.sing(HF, held, st, 48000.0, 3.5, 11, 256)
    assert len(log) > 10
    for e in log:
        note = flat[e["syllable"]]["anchors"]["rule"] + e["variant_note_cents"] + e["shift_cents"]
        assert abs(note - e["target_cents"]) < 1e-6
    assert any(e["range_scale"] != 1.0 for e in log)


def test_variant_keeps_the_real_loudness_and_caps_the_draws():
    """After the R3 listening page ("1b"): no loudness variation; draws capped at ±2 sd."""
    from cuicui.variation import variant

    cc = _cc()
    bank = json.loads(Path(HF[0]).read_text())
    flat = [s for p in bank["phrases"] for s in p["syllables"]]
    i = next(k for k, s in enumerate(flat) if "model" in s)
    z = [1.3, 0.4, -2.9, 0.4, -0.7, 1.1, 0.0]  # a very quiet loudness draw: not used
    _c, a, _d = cc.variant(HF[0], i, 1.0, z)
    real = np.asarray(flat[i]["a"], float)
    stretched = np.interp(np.linspace(0, len(real) - 1, len(a)), np.arange(len(real)), real)
    np.testing.assert_allclose(a, stretched, atol=1e-6)  # the real loudness, only stretched
    far = [9.0, -7.0, 0.0, 8.0, -9.0, 6.0, 0.0]
    capped = [2.0, -2.0, 0.0, 2.0, -2.0, 2.0, 0.0]
    for f in (cc.variant, lambda *a: variant(bank, *a[1:])):
        c1, a1, d1 = f(HF[0], i, 1.0, far)
        c2, a2, d2 = f(HF[0], i, 1.0, capped)
        np.testing.assert_array_equal(np.asarray(c1), np.asarray(c2))
        assert d1 == d2
