"""WP-R1: syllable bank, Python reference of the prototype, and the C++ whistle path against it."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

import cuicui_core as cc
import numpy as np
import pytest
from scipy import signal

from cuicui.bank import anchors, bank_hash, build_bank, load_bank, load_corrections, validate_bank
from cuicui.reference import singing as ref

ROOT = Path(__file__).resolve().parents[2]
BANK_PATH = ROOT / "assets/voicebanks/zf_22938/bank.json"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
PLUGIN_RENDER = ROOT / "build/release/bin/plugin_render"
SR = 48000


@pytest.fixture(scope="module")
def bank_json() -> dict:
    return load_bank(BANK_PATH)


@pytest.fixture(scope="module")
def bank(bank_json) -> ref.Bank:
    return ref.Bank.from_json(bank_json)


# ------------------------------------------------------------------ bank
def test_bank_asset_is_valid_and_small(bank_json):
    assert validate_bank(bank_json) == []
    assert bank_hash(bank_json) == bank_json["hash"]
    assert bank_json["stats"]["phrases"] == 12
    assert bank_json["stats"]["syllables"] == 123
    assert BANK_PATH.stat().st_size < 300_000


@pytest.mark.skipif(not REC.exists(), reason="recording 22938 not on this machine")
def test_bank_rebuild_is_reproducible(bank_json):
    # WP-R5: rule E (the default) + the notes set by ear next to the bank (D-031, D-032)
    rebuilt = build_bank(REC, REC.with_suffix(".json"), name="zf_22938", species="zebra finch",
                         description=bank_json["description"],
                         created_by=bank_json["source"]["created_by"],
                         corrections=load_corrections(BANK_PATH.parent / "note_corrections.json"))
    assert rebuilt["hash"] == bank_json["hash"]


def test_anchor_rules_on_synthetic_contours():
    two_steps = np.r_[np.full(30, 9000.0), np.full(22, 9500.0)]  # the longer steady part wins
    a = anchors(two_steps)
    assert a["rule_used"] == "steady part"
    assert a["rule"] == pytest.approx(9000.0, abs=1e-9)
    assert a["_span"] == (0, 30)
    assert a["landing"] == pytest.approx(9500.0)
    sweep = np.linspace(9000, 10000, 40)  # 25 c/ms: no steady part of 20 ms
    b = anchors(sweep)
    assert b["rule_used"] == "ending"
    assert b["rule"] == pytest.approx(sweep[-10:].mean())
    assert b["landing"] == b["rule"]


def test_validator_reports_problems(bank_json):
    bad = json.loads(json.dumps(bank_json))
    bad["phrases"][0]["syllables"][1]["t_ms"] = 3  # overlaps syllable 0
    bad["phrases"][1]["syllables"][0]["a"] = bad["phrases"][1]["syllables"][0]["a"][:-1]
    problems = validate_bank(bad)
    assert any("starts before" in p for p in problems)
    assert any("differ in length" in p for p in problems)


# ------------------------------------------------------------------ reference vs C++ core
SCENARIOS = {
    "taps_random": (
        [(0.05 + 0.31 * k, "on", 60 + (k * 5) % 24, 0.3 + 0.07 * k) for k in range(10)]
        + [(0.08 + 0.31 * k, "off", 60 + (k * 5) % 24, 0.0) for k in range(10)],
        {}, 4.0),
    "holds_phrase_start": (
        [(0.1, "on", 64, 0.9), (3.0, "off", 64, 0.0), (3.2, "on", 71, 0.5), (6.5, "off", 71, 0.0)],
        {"perf.order": "phrase", "perf.pause_s": 0.3}, 8.0),
    "chord_shuffled_follow": (
        [(0.1, "on", n, 0.8) for n in (60, 64, 67, 72)]
        + [(4.0, "off", n, 0.0) for n in (60, 64, 67, 72)],
        {"perf.order": "shuffled", "perf.mapping": "follow", "perf.size_st": -2.0}, 6.0),
    "poly2_sustain_wheel_rules": (
        [(0.1, "sustain", 0, 1.0), (0.2, "on", 62, 0.7), (0.4, "off", 62, 0.0),
         (0.6, "on", 65, 0.6), (0.8, "on", 69, 0.5), (1.0, "wheel", 0, 0.6),
         (2.5, "sustain", 0, 0.0), (2.6, "off", 65, 0.0), (3.5, "on", 62, 1.0),
         (5.0, "all_notes_off", 0, 0.0)],
        {"perf.polyphony": 2, "perf.note_rule": "landing", "perf.first_on_note": 0,
         "perf.snap": 0.25},
        6.0),
}


def _settings(d: dict) -> ref.Settings:
    rules = {"rule": "rule", "steady": "steady", "landing": "landing", "average": "average"}
    return ref.Settings(
        rule=rules[d.get("perf.note_rule", "rule")],
        mapping="follow" if d.get("perf.mapping") == "follow" else "own_height",
        size_st=float(d.get("perf.size_st", 0.0)),
        first_on_note=bool(d.get("perf.first_on_note", 1)),
        snap=float(d.get("perf.snap", 0.0)),
        order=d.get("perf.order", "random"),
        pause_s=float(d.get("perf.pause_s", 0.5)),
        polyphony=int(d.get("perf.polyphony", 4)),
    )


@pytest.mark.parametrize("name", list(SCENARIOS))
def test_cpp_core_plays_exactly_the_reference_syllables(bank, name):
    events, settings, dur = SCENARIOS[name]
    # the reference is the PROTO1 prototype: play as it did (no release alarm, D-030)
    prototype = {**settings, "out.room": 0, "release_alarm": 0, "perf.natural_variation": 0.0}
    _, log = cc.sing(str(BANK_PATH), events, prototype, SR, dur, 20261011, 256)
    expect = ref.perform(bank, events, _settings(settings), SR, dur, 20261011)
    assert len(log) == len(expect) > 0
    for got, exp in zip(log, expect, strict=True):
        assert got["syllable"] == exp["syllable"]
        assert got["sample"] == exp["sample"]
        assert got["first"] == exp["first"]
        assert got["shift_cents"] == pytest.approx(exp["shift_cents"], abs=1e-9)
        assert got["gain"] == pytest.approx(exp["gain"], abs=1e-6)
        assert got["pan"] == pytest.approx(exp["pan"], abs=1e-6)


@pytest.mark.parametrize("name", ["taps_random", "chord_shuffled_follow"])
def test_cpp_core_audio_matches_the_reference_voice(bank, name):
    events, settings, dur = SCENARIOS[name]
    prototype = {**settings, "out.room": 0, "release_alarm": 0,  # as PROTO1 played (D-030 off,
                 "perf.natural_variation": 0.0}                    # no variation, WP-R3)
    audio, log = cc.sing(str(BANK_PATH), events, prototype, SR, dur, 7, 256)
    expect = ref.render(bank, log, SR, len(audio))
    err = np.max(np.abs(audio - expect))
    assert np.max(np.abs(expect)) > 0.05
    assert err < 2e-6, err  # float32 engine vs float64 reference


def test_cpp_core_results_do_not_depend_on_the_block_size():
    events, settings, dur = SCENARIOS["poly2_sustain_wheel_rules"]
    outs = [cc.sing(str(BANK_PATH), events, settings, SR, dur, 3, b) for b in (1, 100, 4096)]
    for audio, log in outs[1:]:
        assert log == outs[0][1]
        assert np.array_equal(audio, outs[0][0])


def test_room_is_quiet_and_late(bank):
    events = [(0.1, "on", 69, 1.0), (0.11, "off", 69, 0.0)]
    dry, log = cc.sing(str(BANK_PATH), events, {"out.room": 0}, SR, 1.5, 5, 256)
    wet, _ = cc.sing(str(BANK_PATH), events, {"out.room": 1}, SR, 1.5, 5, 256)
    room = wet - dry
    first = np.flatnonzero(np.abs(room[:, 0]) > 1e-9)[0]
    # nothing before 4 ms after the syllable starts (the room's pre-delay)
    assert first >= log[0]["sample"] + int(0.004 * SR) - 1
    level_db = 10 * np.log10(np.sum(room**2) / np.sum(dry**2))
    assert -45.0 < level_db < -25.0


# ------------------------------------------------- criterion 6: plugin vs prototype logic
def _per_ms_cents(x: np.ndarray, start: int, n_ms: int) -> np.ndarray:
    """Mean frequency of each 1 ms frame from the analytic signal's phase, in cents."""
    ph = np.unwrap(np.angle(signal.hilbert(x)))
    hop = SR // 1000
    idx = start + hop * np.arange(n_ms + 1)
    idx = idx[idx < len(ph)]
    f = np.diff(ph[idx]) / (2 * np.pi) * 1000.0
    return 1200 * np.log2(np.maximum(f, 1.0) / 440.0) + 6900


@pytest.mark.skipif(not PLUGIN_RENDER.exists(), reason="plugin_render not built")
def test_plugin_pitch_curve_matches_the_prototype_algorithm(bank, tmp_path):
    # Monophonic melody: taps and holds, far enough apart that syllables never overlap.
    notes = [(60, 0.20, 0.03), (67, 1.00, 0.03), (64, 1.80, 1.20), (72, 4.20, 0.03),
             (55, 5.00, 2.00)]
    vels = (100, 70, 110, 50, 90)
    spec = ",".join(f"{n}:{t}:{d}:{v}" for (n, t, d), v in zip(notes, vels, strict=True))
    params = {"out.room": 0, "out.master_db": 0, "voice.harmonics": 0, "perf.polyphony": 1,
              "perf.natural_variation": 0}  # the prototype had no variation (WP-R3)
    (tmp_path / "params.json").write_text(json.dumps(params))
    wav, logp = tmp_path / "out.wav", tmp_path / "log.json"
    subprocess.run([str(PLUGIN_RENDER), "--notes", spec, "--params", str(tmp_path / "params.json"),
                    "--seed", "20261012", "--duration", "8.5", "--syllable-log", str(logp),
                    "--no-release-alarm",
                    "--bank", str(BANK_PATH),  # the R1 bank (WP-R3: no longer the default)
                    "-o", str(wav)], check=True, capture_output=True)
    import soundfile as sf

    y, sr = sf.read(str(wav), always_2d=True)
    assert sr == SR
    log = json.loads(logp.read_text())
    lat = log["latency_samples"]
    events = []
    for (n, t, d), v in zip(notes, vels, strict=True):
        events += [(t, "on", n, np.float32(v / 127)), (t + d, "off", n, 0.0)]
    expect = ref.perform(bank, events, ref.Settings(polyphony=1), SR, 8.5, 20261012)
    got = log["starts"]
    assert [(s["syllable"], s["sample"]) for s in got] == [
        (e["syllable"], e["sample"]) for e in expect]

    # Pitch curve of every syllable: plugin audio vs the prototype voice, 1 ms frames in the voice.
    centre = np.cos(np.pi / 4)  # mono at the centre of the equal-power pan
    mono = y[:, 0] / centre
    ref_audio = ref.render(bank, expect, SR, len(mono) + lat, harmonics=False)[:, 0] / centre
    worst, frames = 0.0, 0
    for e in expect:
        syl = bank.syllables[e["syllable"]]
        a = syl.a
        keep = np.flatnonzero(a[2:-3] > 0.1 * a.max()) + 2  # skip edges and near-silent ms
        c_plug = _per_ms_cents(mono, e["sample"] + lat, len(a))
        c_ref = _per_ms_cents(ref_audio, e["sample"], len(a))
        f_inst = ref.instantaneous_hz(bank, e["syllable"], e["shift_cents"], SR)
        f_ms = f_inst[: len(f_inst) // 48 * 48].reshape(-1, 48).mean(axis=1)  # mean over each ms
        target = 1200 * np.log2(f_ms / 440.0) + 6900
        keep = keep[(keep < len(c_plug)) & (keep < len(c_ref)) & (keep < len(target))]
        worst = max(worst, float(np.max(np.abs(c_plug[keep] - c_ref[keep]))))
        # and the plugin follows the bank's contour (+ shift) itself, within the 1 ms averaging
        assert np.median(np.abs(c_plug[keep] - target[keep])) < 5.0
        frames += len(keep)
    assert frames > 1000
    assert worst < 5.0, worst
    # sample level: the plugin is the prototype voice, delayed by its latency
    seg = slice(lat, len(mono))
    assert np.max(np.abs(mono[seg] - ref_audio[: len(mono) - lat])) < 1e-5


def test_release_alarm_ends_long_presses_only(bank):
    """D-030: the long rising call ends presses of 0.6 s or more; never short ones or phrases."""
    events = [(0.10, "on", 64, 0.8), (0.40, "off", 64, 0.0),   # 0.3 s: no call
              (1.00, "on", 67, 0.8), (1.59, "off", 67, 0.0),   # 0.59 s: no call
              (2.50, "on", 69, 0.8), (4.50, "off", 69, 0.0)]   # 2 s: a call after the release
    _, log = cc.sing(str(BANK_PATH), events, {"out.room": 0}, SR, 6.0, 4, 256)
    calls = [e for e in log if e["release_alarm"]]
    assert len(calls) == 1
    assert calls[0]["key"] == 69 and calls[0]["sample"] >= int(4.5 * SR)
    assert bank.syllables[calls[0]["syllable"]].label.startswith("alarm")
    assert all(not bank.syllables[e["syllable"]].label.startswith("alarm")
               for e in log if not e["release_alarm"])
