"""Load and validate listening-page manifests."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import jsonschema


def _schema_path() -> Path:
    # python/cuicui/listen/manifest.py → repo root / schemas
    return Path(__file__).resolve().parents[3] / "schemas" / "listening_manifest.schema.json"


def load_schema() -> dict[str, Any]:
    with _schema_path().open(encoding="utf-8") as f:
        return json.load(f)


def load_manifest(path: Path | str) -> dict[str, Any]:
    path = Path(path)
    with path.open(encoding="utf-8") as f:
        data = json.load(f)
    jsonschema.validate(data, load_schema())
    if data.get("format") != "cuicui.listening_manifest":
        raise ValueError(f"unexpected format: {data.get('format')!r}")
    if data.get("version") != 1:
        raise ValueError(f"unsupported manifest version: {data.get('version')!r}")
    return data
