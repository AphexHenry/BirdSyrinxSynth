from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import ValidationError

from cuicui.listen.manifest import load_manifest

ROOT = Path(__file__).resolve().parents[2]


def test_schema_accepts_architecture_example(tmp_path: Path) -> None:
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B1",
        "title": "Labial oscillator — raw source",
        "items": [
            {
                "file": "01_hopf_onset.wav",
                "title": "Soft (Hopf) onset, β = 0.5",
                "listen_for": "Smooth fade-in of a nearly pure tone; no click.",
                "params": {"src.L.beta": 0.5},
                "metrics": {"f0_hz": 2701, "onset_ms": 12.0},
                "group": "onsets",
                "reference": None,
            }
        ],
    }
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    loaded = load_manifest(path)
    assert loaded["wp"] == "B1"


def test_schema_rejects_wrong_format(tmp_path: Path) -> None:
    path = tmp_path / "manifest.json"
    path.write_text(
        '{"format": "nope", "version": 1, "wp": "A1", "title": "x", "items": []}',
        encoding="utf-8",
    )
    with pytest.raises(ValidationError):
        load_manifest(path)


def test_listening_schema_file_exists() -> None:
    assert (ROOT / "schemas" / "listening_manifest.schema.json").is_file()
    assert (ROOT / "schemas" / "render_spec.schema.json").is_file()
