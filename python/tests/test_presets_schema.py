"""WP-R4: factory presets against `schemas/preset.schema.json` (criterion 3)."""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "schemas/preset.schema.json").read_text(encoding="utf-8"))
PRESETS = sorted((ROOT / "assets/presets").glob("*.preset.json"))


def registry_ids() -> set[str]:
    text = (ROOT / "core/include/cuicui/params.def").read_text(encoding="utf-8")
    return set(re.findall(r'CUICUI_PARAM\(\w+,\s*"([^"]+)"', text))


def test_there_are_factory_presets() -> None:
    assert len(PRESETS) >= 8


@pytest.mark.parametrize("path", PRESETS, ids=lambda p: p.name)
def test_factory_preset_matches_the_schema(path: Path) -> None:
    preset = json.loads(path.read_text(encoding="utf-8"))
    Draft202012Validator(SCHEMA).validate(preset)
    assert preset["description"], "every factory preset says what it is for"
    assert set(preset["parameters"]) <= registry_ids(), "only registry parameters"


def test_names_are_unique() -> None:
    names = [json.loads(p.read_text(encoding="utf-8"))["name"] for p in PRESETS]
    assert len(names) == len(set(names))


def test_schema_rejects_a_wrong_format_and_bad_values() -> None:
    validator = Draft202012Validator(SCHEMA)
    good = {"format": "cuicui.preset", "version": 1, "name": "x", "parameters": {"perf.snap": 0.5}}
    validator.validate(good)
    for bad in (
        {**good, "format": "cuicui.other"},
        {**good, "version": 2},
        {**good, "name": ""},
        {**good, "parameters": {"perf.snap": [1, 2]}},
        {**good, "parameters": {"Not An Id": 1}},
    ):
        with pytest.raises(ValidationError):
            validator.validate(bad)
