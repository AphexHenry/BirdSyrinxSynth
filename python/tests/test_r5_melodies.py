"""WP-R5: note rules (MELODY2), notes set by ear (TUNE1) and the rebuilt bank."""

from __future__ import annotations

import copy
import json
from pathlib import Path

import numpy as np
import pytest

from cuicui.bank import (
    NOTE_CANDIDATES,
    NOTE_RULES,
    BuildSettings,
    anchors,
    apply_corrections,
    load_corrections,
    note_candidates,
)

ROOT = Path(__file__).resolve().parents[2]


def _value(cand: dict, rule: str) -> float:
    return cand[rule][0]


def test_steady_tone_has_the_same_note_under_every_rule():
    c = np.full(60, 9000.0)
    cand = note_candidates(c, np.ones(60))
    for r in NOTE_CANDIDATES:
        assert _value(cand, r) == pytest.approx(9000.0)
    assert cand["B"][1] == cand["C"][1] == "steady part"


def test_rising_glide_at_even_loudness():
    c = np.linspace(9000.0, 9600.0, 60)  # 10 c/ms: no steady part of 20 ms
    cand = note_candidates(c, np.ones(60))
    ending = c[-15:].mean()
    assert _value(cand, "A") == pytest.approx(ending)  # D-019: the ending (last quarter)
    assert _value(cand, "B") == pytest.approx(ending)  # every frame is loud: the same ending
    assert cand["B"][1] == "loud ending"
    for r in "CDE":  # flat loudness and constant speed: the plain mean
        assert _value(cand, r) == pytest.approx(9300.0)


def test_falling_glide_loud_at_the_top_is_noted_above_its_quiet_bottom():
    c = np.linspace(10000.0, 9400.0, 80)
    a = np.linspace(1.0, 0.2, 80)  # −14 dB at the end: nothing dropped by the −20 dB floor
    cand = note_candidates(c, a)
    plain_ending = c[-20:].mean()
    assert _value(cand, "A") == pytest.approx(plain_ending)
    # B: the last quarter of the frames within 10 dB of the peak, weighted by loudness
    loud = np.flatnonzero(a >= 10 ** (-10 / 20))
    tail = loud[-(len(loud) // 4):]
    assert _value(cand, "B") == pytest.approx(np.sum(a[tail] ** 2 * c[tail]) / np.sum(a[tail] ** 2))
    assert _value(cand, "B") > _value(cand, "A") + 50.0
    # C = E here (no steady part): the loudness-weighted mean sits above the plain mean
    assert _value(cand, "C") == pytest.approx(_value(cand, "E"))
    assert _value(cand, "E") == pytest.approx(np.sum(a**2 * c) / np.sum(a**2))
    assert _value(cand, "E") > c.mean() + 50.0
    assert _value(cand, "D") == pytest.approx(c.mean())  # constant speed: equal stability weights


def test_quiet_misread_tail_is_ignored_by_the_candidates():
    glide = np.linspace(10000.0, 9500.0, 60)
    c = np.r_[glide, np.full(6, 8500.0)]  # last 6 ms misread far below (as #95's 1.76 kHz)
    a = np.r_[np.ones(60), np.full(6, 0.05)]  # −26 dB: below the −20 dB floor
    with_tail = note_candidates(c, a)
    without = note_candidates(glide, np.ones(60))
    assert with_tail["kept_frames"] == 60
    for r in "BCDE":
        assert _value(with_tail, r) == pytest.approx(_value(without, r))
    assert _value(with_tail, "A") < _value(without, "A") - 100.0  # the control still uses them
    assert with_tail["B"][2] == (45, 60)  # the span, in frames of c, ends before the quiet tail


def test_steady_part_is_found_on_the_loud_frames_only():
    # a 25 ms steady part, then a glide; the steady part wins for A, B and C
    c = np.r_[np.full(25, 9800.0), np.linspace(9800.0, 9000.0, 40)]
    a = np.ones(len(c))
    cand = note_candidates(c, a)
    for r in "ABC":
        assert _value(cand, r) == pytest.approx(9800.0, abs=10.0)  # the run reaches into the glide
        assert cand[r][1] == "steady part"
    # the same steady part, now 30 dB down: B and C ignore it and use the loud glide instead
    a[:25] = 10 ** (-30 / 20)
    cand = note_candidates(c, a)
    assert _value(cand, "A") == pytest.approx(9800.0, abs=10.0)
    assert cand["C"][1] == "whole"
    assert _value(cand, "C") == pytest.approx(np.linspace(9800.0, 9000.0, 40).mean())


def test_candidate_a_is_the_d019_rule_unchanged():
    rng = np.random.default_rng(3)
    for _ in range(20):
        c = 9000.0 + np.cumsum(rng.normal(0.0, 15.0, 70))
        a = rng.uniform(0.01, 1.0, 70)
        assert _value(note_candidates(c, a), "A") == anchors(c)["rule"]


def test_build_settings_default_is_rule_e():
    assert BuildSettings().note_rule == "E"  # MELODY2 + the human's choice (D-031)
    with pytest.raises(ValueError):
        note_candidates(np.ones(3), np.ones(4))


# ------------------------------------------------------------------ notes set by ear (TUNE1)
def _phrases() -> list[dict]:
    def syl(seg: int, label: str, n: int, note: float) -> dict:
        anc = {r: note for r in NOTE_RULES}
        anc |= {"rule_used": "whole", "steady_ms": 0, "rule_span_ms": [2, n]}
        return {"segment": seg, "label": label, "t_ms": 0, "f_hz": [3000.0] * n, "a": [1.0] * n,
                "anchors": anc, "flags": []}
    return [{"id": 0, "syllables": [syl(4, "swipe", 50, 10500.0), syl(7, "peep", 40, 10300.0)]},
            {"id": 1, "syllables": [syl(9, "swipe", 60, 10600.0)]}]


def _corrections(*items: dict) -> dict:
    return {"format": "cuicui.note_corrections", "version": 1, "bank": "test",
            "syllables": list(items)}


def test_a_note_set_by_ear_dominates_every_rule():
    phrases = _phrases()
    out = apply_corrections(phrases, _corrections(
        {"segment": 7, "label": "peep", "note_cents": 10550.0, "move_cents": -250.0},
        {"segment": 9, "label": "swipe", "no_clear_note": True}))
    assert out == {"ear_notes": 1, "no_note": 1}
    peep = phrases[0]["syllables"][1]
    assert all(peep["anchors"][r] == 10550.0 for r in NOTE_RULES)
    assert peep["anchors"]["rule_used"] == "ear" and peep["anchors"]["rule_span_ms"] == [0, 40]
    assert peep["flags"] == ["ear"]
    last = phrases[1]["syllables"][0]
    assert last["flags"] == ["no_note"] and last["anchors"]["rule"] == 10600.0  # kept, only flagged
    first = phrases[0]["syllables"][0]
    assert first["flags"] == [] and first["anchors"]["rule"] == 10500.0


@pytest.mark.parametrize("bad, message", [
    ({"segment": 5, "label": "swipe", "note_cents": 1.0}, "no syllable from segment 5"),
    ({"segment": 7, "label": "swipe", "note_cents": 1.0}, "is a 'peep', not a 'swipe'"),
])
def test_corrections_that_do_not_match_the_bank_are_refused(bad, message):
    with pytest.raises(ValueError, match=message):
        apply_corrections(_phrases(), _corrections(bad))
    twice = _corrections({"segment": 4, "label": "swipe", "note_cents": 1.0},
                         {"segment": 4, "label": "swipe", "no_clear_note": True})
    with pytest.raises(ValueError, match="listed twice"):
        apply_corrections(_phrases(), twice)


def test_corrections_file_is_checked_against_its_schema(tmp_path):
    good = _corrections({"segment": 7, "label": "peep", "note_cents": 10550.0},
                        {"segment": 9, "label": "swipe", "no_clear_note": True})
    (tmp_path / "ok.json").write_text(json.dumps(good))
    assert load_corrections(tmp_path / "ok.json") == good
    for bad in ({"segment": 7, "label": "peep"},  # neither a note nor "no clear note"
                {"segment": 7, "label": "peep", "note_cents": 1.0, "no_clear_note": True}):
        doc = copy.deepcopy(good)
        doc["syllables"][0] = bad
        (tmp_path / "bad.json").write_text(json.dumps(doc))
        with pytest.raises(ValueError, match="invalid note corrections"):
            load_corrections(tmp_path / "bad.json")
