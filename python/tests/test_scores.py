from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "schemas" / "gesture_score.schema.json").read_text(encoding="utf-8"))


def _import_core():
    try:
        import cuicui_core as cc
    except ImportError as exc:  # pragma: no cover
        pytest.skip(f"cuicui_core is not built: {exc}")
    return cc


def _f0_near(x: np.ndarray, sr: float, f_ref: float) -> float:
    y = np.asarray(x, dtype=np.float64)
    y = y - np.mean(y)
    if y.size < 64 or not (f_ref > 1.0):
        return 0.0
    nfft = 1 << int(np.ceil(np.log2(max(int(y.size), 4096))))
    nfft = min(nfft, 16384)
    w = np.hanning(y.size)
    spec = np.abs(np.fft.rfft(y * w, n=nfft))
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sr)
    mask = (freqs >= f_ref * 0.82) & (freqs <= f_ref * 1.22)
    if not np.any(mask):
        return 0.0
    idx = np.nonzero(mask)[0]
    k = int(idx[int(np.argmax(spec[mask]))])
    if 0 < k < spec.size - 1:
        a, b, c = float(spec[k - 1]), float(spec[k]), float(spec[k + 1])
        denom = a - 2.0 * b + c
        delta = 0.5 * (a - c) / denom if abs(denom) > 1e-18 else 0.0
        delta = float(np.clip(delta, -0.5, 0.5))
        return (k + delta) * sr / nfft
    return float(freqs[k])


def _two_voice(x: np.ndarray, sr: float, f_a: float, f_b: float) -> bool:
    y = np.asarray(x, dtype=np.float64)
    y = y - np.mean(y)
    nfft = 1 << int(np.ceil(np.log2(max(int(y.size), 8192))))
    nfft = min(nfft, 32768)
    spec = np.abs(np.fft.rfft(y * np.hanning(y.size), n=nfft))
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sr)

    def peak_near(f0: float) -> float:
        mask = (freqs >= f0 * 0.92) & (freqs <= f0 * 1.08)
        if not np.any(mask):
            return 0.0
        return float(np.max(spec[mask]))

    floor = float(np.median(spec)) + 1e-12
    return peak_near(f_a) > 8.0 * floor and peak_near(f_b) > 8.0 * floor


def test_archetype_names_and_params() -> None:
    cc = _import_core()
    names = list(cc.archetype_names())
    assert "whistle" in names
    assert "switch_sweep" in names
    assert len(names) == 11
    specs = list(cc.archetype_params("whistle"))
    ids = [s["name"] for s in specs]
    assert "dur" in ids and "arch" in ids


def test_generate_archetype_deterministic() -> None:
    cc = _import_core()
    a = cc.generate_archetype("buzz", {"wobble": 30.0}, 3)
    b = cc.generate_archetype("buzz", {"wobble": 30.0}, 3)
    c = cc.generate_archetype("buzz", {"wobble": 30.0}, 4)
    assert list(a["curves"]["pitch_cents"]) == list(b["curves"]["pitch_cents"])
    assert list(a["curves"]["pitch_cents"]) != list(c["curves"]["pitch_cents"])
    assert abs(max(a["curves"]["amp_db"])) < 0.01


def test_score_tools_sequence_validates() -> None:
    cc = _import_core()
    from cuicui.gestures.score_tools import sequence

    score = sequence(
        [
            ("intro", None, 1760.0),
            ("downsweep", {"depth": 600.0}, 2093.0),
            ("chip", None, 2093.0),
        ],
        name="unit",
    )
    Draft202012Validator(SCHEMA).validate(score)
    y = np.asarray(cc.render_score(score, 48000.0), dtype=np.float32)
    assert y.size > 1000
    assert np.isfinite(y).all()


def test_example_scores_validate() -> None:
    examples = ROOT / "examples" / "scores"
    if not examples.is_dir():
        pytest.skip("example scores not generated yet")
    files = list(examples.glob("*.gscore.json"))
    if not files:
        pytest.skip("example scores not generated yet")
    validator = Draft202012Validator(SCHEMA)
    for path in files:
        data = json.loads(path.read_text(encoding="utf-8"))
        validator.validate(data)


def _target_hz(event: dict, t: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    t0 = float(event["t_s"])
    dur = float(event["duration_ms"]) * 0.001
    u = np.clip((t - t0) / max(dur, 1e-9), 0.0, 1.0)
    grid = np.linspace(0.0, 1.0, 64)
    cents = np.interp(u, grid, np.asarray(event["curves"]["pitch_cents"], dtype=np.float64))
    amp = np.interp(u, grid, np.asarray(event["curves"]["amp_db"], dtype=np.float64))
    ahz = float(event["anchor_hz"])
    hz = ahz * (2.0 ** (cents / 1200.0))
    return hz, amp


def _median_cents_error(y: np.ndarray, sr: float, event: dict, t0: float, t1: float) -> float:
    hop = int(0.010 * sr)
    win = int(0.030 * sr)
    i0 = int(t0 * sr)
    i1 = int(t1 * sr)
    errs: list[float] = []
    t_axis = np.arange(y.size) / sr
    target, amp = _target_hz(event, t_axis)
    for i in range(i0, max(i0, i1 - win), hop):
        mid = i + win // 2
        if mid >= y.size or amp[mid] < -18.0:
            continue
        sl = y[i : i + win]
        f_ref = float(target[mid])
        f0 = _f0_near(sl, sr, f_ref)
        if f0 > 1.0 and f_ref > 1.0:
            errs.append(abs(1200.0 * float(np.log2(f0 / f_ref))))
    if not errs:
        return 1e9
    return float(np.median(errs))


def test_whistle_upsweep_downsweep_arch_tuning() -> None:
    cc = _import_core()
    from cuicui.gestures.score_tools import archetype_event

    names = ("whistle", "upsweep", "downsweep", "arch")
    sr = 48000.0
    for name in names:
        ahz = 2093.0
        ev = archetype_event(name, 0.02, anchor_hz=ahz)
        score = {
            "format": "cuicui.gesture_score",
            "version": 1,
            "settings": {"body.size": 1.0, "perf.breath": 0.7},
            "gestures": [ev],
        }
        y = np.asarray(cc.render_score(score, sr), dtype=np.float32)
        dur = float(ev["duration_ms"]) * 0.001
        err = _median_cents_error(y, sr, ev, 0.02 + 0.15 * dur, 0.02 + 0.85 * dur)
        assert err <= 15.0, f"{name} median {err:.1f} cents"


def test_two_voice_has_both_pitches() -> None:
    cc = _import_core()
    from cuicui.gestures.score_tools import archetype_event

    ahz = 1800.0
    interval = 380.0
    f2 = ahz * (2.0 ** (interval / 1200.0))
    score = {
        "format": "cuicui.gesture_score",
        "version": 1,
        "settings": {"body.size": 1.0, "perf.breath": 0.8},
        "gestures": [
            archetype_event("two_voice", 0.02, params={"interval": interval}, anchor_hz=ahz)
        ],
    }
    y = np.asarray(cc.render_score(score, 48000.0), dtype=np.float32)
    sl = y[int(0.06 * 48000) : int(0.18 * 48000)]
    assert _two_voice(sl, 48000.0, ahz, f2)


def test_invalid_score_errors() -> None:
    cc = _import_core()
    with pytest.raises(Exception, match="gesture_score"):
        cc.render_score({"format": "nope", "version": 1}, 48000.0)
